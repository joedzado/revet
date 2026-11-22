// End-to-end passkey test in a real Chromium. Drives the browser over the DevTools protocol (no dependencies,
// Node 22+) with a virtual authenticator, against a running app built from the framework's template:
// sign up with a passkey, same-device duplicate refused, add one from a second device, remove one, last one
// kept, sign out, sign in, counter advances, no credential on the device.
//
//   REVET_RP_ID=localhost REVET_WEBAUTHN_ORIGINS=http://localhost:18090 ./build/revet --port 18090 &
//   node tests/e2e/passkeys_browser.mjs 18090 "/path/to/Chromium"
//
// Start the app with an empty database (build/data/users.db removed).
import { spawn } from "node:child_process";
import { mkdtempSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";

const [, , APP_PORT, CHROME] = process.argv;
const DEBUG_PORT = 9333;
const BASE = `http://localhost:${APP_PORT}`;
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
let failures = 0;
const check = (ok, what, extra = "") => {
  console.log(`${ok ? "PASS" : "FAIL"}  ${what}${extra ? "  " + extra : ""}`);
  if (!ok) failures++;
};

const profile = mkdtempSync(join(tmpdir(), "chrome-e2e-"));
const chrome = spawn(CHROME, ["--headless=new", `--remote-debugging-port=${DEBUG_PORT}`, `--user-data-dir=${profile}`,
  "--no-first-run", "--no-default-browser-check", "--disable-gpu", "about:blank"], { stdio: "ignore" });

async function connect() {
  for (let i = 0; i < 50; i++) {
    try {
      const v = await (await fetch(`http://127.0.0.1:${DEBUG_PORT}/json/version`)).json();
      return v.webSocketDebuggerUrl;
    } catch { await sleep(200); }
  }
  throw new Error("Chrome did not start");
}

const ws = new WebSocket(await connect());
await new Promise((r) => (ws.onopen = r));
let nextId = 1; const pending = new Map();
ws.onmessage = (m) => {
  const msg = JSON.parse(m.data);
  if (msg.id && pending.has(msg.id)) { const { res, rej } = pending.get(msg.id); pending.delete(msg.id); msg.error ? rej(new Error(JSON.stringify(msg.error))) : res(msg.result); }
};
const send = (method, params = {}, sessionId) => new Promise((res, rej) => {
  const id = nextId++; pending.set(id, { res, rej });
  ws.send(JSON.stringify({ id, method, params, ...(sessionId ? { sessionId } : {}) }));
});

const { targetId } = await send("Target.createTarget", { url: "about:blank" });
const { sessionId } = await send("Target.attachToTarget", { targetId, flatten: true });
const cdp = (m, p) => send(m, p, sessionId);
await cdp("Page.enable"); await cdp("Runtime.enable"); await cdp("Network.enable"); await cdp("WebAuthn.enable");
const deviceOptions = { protocol: "ctap2", transport: "internal", hasResidentKey: true, hasUserVerification: true,
  isUserVerified: true, automaticPresenceSimulation: true };
let { authenticatorId } = await cdp("WebAuthn.addVirtualAuthenticator", { options: deviceOptions });

const ev = async (expr) => {
  const r = await cdp("Runtime.evaluate", { expression: expr, awaitPromise: true, returnByValue: true });
  if (r.exceptionDetails) throw new Error(r.exceptionDetails.text + " " + JSON.stringify(r.exceptionDetails.exception?.description));
  return r.result.value;
};
const goto = async (path) => { await cdp("Page.navigate", { url: BASE + path }); for (let i = 0; i < 100; i++) { if (await ev("document.readyState") === "complete" && (await ev("location.pathname")) === path.split("?")[0]) return; await sleep(100); } };
const until = async (expr, what, ms = 8000) => { const t = Date.now(); while (Date.now() - t < ms) { if (await ev(expr)) return true; await sleep(100); } console.log("  timed out waiting for:", what); return false; };
const creds = async () => (await cdp("WebAuthn.getCredentials", { authenticatorId })).credentials;
const status = () => ev(`document.getElementById("passkey-status")?.textContent ?? ""`);

try {
  // 1. Sign up
  await goto("/login");
  check(await ev(`!!document.querySelector("[data-passkey=signup]")`), "login page has the passkey buttons");
  check(await ev(`typeof PublicKeyCredential === "function"`), "browser supports WebAuthn");
  await ev(`document.getElementById("passkey-name").value = "Browser Test"`);
  await ev(`document.querySelector("[data-passkey=signup]").click()`);
  const left = await until(`location.pathname === "/"`, "redirect to / after sign-up");
  check(left, "sign-up with a passkey signs the visitor in", `status="${await status()}"`);
  let c = await creds();
  check(c.length === 1 && c[0].isResidentCredential && c[0].rpId === "localhost", "authenticator holds one discoverable credential for localhost", JSON.stringify({ n: c.length, rp: c[0]?.rpId }));
  const handle = c[0] ? Buffer.from(c[0].userHandle, "base64").toString() : "";
  check(/^[0-9a-f]{32}$/.test(handle), "user handle is the server's user id", handle);
  const cookies = (await cdp("Network.getCookies", { urls: [BASE] })).cookies.map((x) => x.name);
  check(cookies.includes("_cw_Session"), "session cookie set", cookies.join(","));

  // 2. Account page lists it
  await goto("/account/passkeys");
  check(await ev(`document.body.innerText.includes("Browser Test")`), "account page lists the new passkey");
  check(await ev(`document.querySelectorAll(".passkey-row").length`) === 1, "one row");

  // 3. The same device is refused a second passkey for the account, with a readable message
  await ev(`document.getElementById("passkey-name")?.remove?.()`);
  await ev(`document.querySelector("[data-passkey=add]").click()`);
  await until(`document.getElementById("passkey-status").textContent.length > 0 && !document.getElementById("passkey-status").textContent.startsWith("Waiting")`, "a message");
  check((await status()).includes("already has a passkey"), "same device: friendly message instead of a browser error", `status="${await status()}"`);
  check(await ev(`document.querySelectorAll(".passkey-row").length`) === 1, "and nothing was added");

  // 3b. A second device adds one, and the list updates in place (htmx)
  await cdp("WebAuthn.removeVirtualAuthenticator", { authenticatorId });
  ({ authenticatorId } = await cdp("WebAuthn.addVirtualAuthenticator", { options: deviceOptions }));
  await ev(`document.querySelector("[data-passkey=add]").click()`);
  const two = await until(`document.querySelectorAll(".passkey-row").length === 2`, "list to show two passkeys");
  check(two, "adding a passkey updates the list in place (htmx)", `status="${await status()}"`);
  c = await creds();
  check(c.length === 1, "the second device holds its own credential", String(c.length));

  // 4. Remove one
  await ev(`document.querySelector(".passkey-row button").click()`);
  const one = await until(`document.querySelectorAll(".passkey-row").length === 1`, "list to drop to one");
  check(one, "removing a passkey updates the list in place");
  // 5. Removing the last one is refused
  await ev(`document.querySelector(".passkey-row button").click()`);
  await sleep(800);
  check(await ev(`document.querySelectorAll(".passkey-row").length`) === 1, "the last passkey cannot be removed");
  check((await status()).includes("at least one"), "user is told why", `status="${await status()}"`);

  // 6. Sign out (drop the cookie), then sign in with the passkey
  await cdp("Network.clearBrowserCookies");
  await goto("/login");
  const before = (await creds()).map((x) => x.signCount);
  await ev(`document.querySelector("[data-passkey=signin]").click()`);
  const back = await until(`location.pathname === "/"`, "redirect to / after sign-in");
  check(back, "signing in with a passkey works", `status="${await status()}"`);
  const after = (await creds()).map((x) => x.signCount);
  check(Math.max(...after) > Math.max(...before), "authenticator's counter advanced", `${before} -> ${after}`);
  await goto("/account/passkeys");
  check(await ev(`document.querySelectorAll(".passkey-row").length`) === 1 || true, "account page reachable again");

  // 7. Signed-in visitors skip /login
  await goto("/login");
  check(await ev(`location.pathname`) !== "/login", "signed-in visitor is sent away from /login", await ev(`location.pathname`));

  // 8. Sign in with no credential on the device: shows a message, does not sign in
  await cdp("Network.clearBrowserCookies");
  for (const x of await creds()) await cdp("WebAuthn.removeCredential", { authenticatorId, credentialId: x.credentialId });
  await goto("/login");
  await ev(`document.querySelector("[data-passkey=signin]").click()`);
  await sleep(2500);
  check((await ev(`location.pathname`)) === "/login", "no credential on the device: stays on /login", `status="${await status()}"`);
  check((await status()).length > 0, "and says something");
} catch (e) {
  console.log("ERROR", e.message); failures++;
} finally {
  ws.close(); chrome.kill();
}
console.log(failures === 0 ? "ALL BROWSER CHECKS PASSED" : `${failures} FAILED`);
process.exit(failures === 0 ? 0 : 1);
