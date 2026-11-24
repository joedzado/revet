// End-to-end magic-link test in a real Chromium, with a real SMTP exchange: the app mails through
// libcurl to a small SMTP server in this script, and the script opens the link it receives.
// Covers: request a link, a mail scanner fetching the link does not spend it, sign in, replay refused,
// the account page, adding an email to a signed-in account, getting back in after losing every passkey,
// per-address limits, and bad input.
//
//   export REVET_SMTP_URL=smtp://127.0.0.1:2526 REVET_MAIL_FROM="App <login@app.example.com>" \
//          REVET_BASE_URL=http://localhost:18100 REVET_APP_NAME=Example \
//          REVET_RP_ID=localhost REVET_WEBAUTHN_ORIGINS=http://localhost:18100
//   ./build/revet --port 18100 &       # empty database
//   node tests/e2e/magic_browser.mjs 18100 2526 "/path/to/Chromium"
import { spawn } from "node:child_process";
import { mkdtempSync } from "node:fs";
import { createServer } from "node:net";
import { tmpdir } from "node:os";
import { join } from "node:path";

const [, , APP_PORT, SMTP_PORT, CHROME] = process.argv;
const DEBUG_PORT = 9335;
const BASE = `http://localhost:${APP_PORT}`;
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
let failures = 0;
const check = (ok, what, extra = "") => { console.log(`${ok ? "PASS" : "FAIL"}  ${what}${extra ? "  " + extra : ""}`); if (!ok) failures++; };

// ── SMTP server that keeps what it is sent ──
const mails = [];
const smtp = createServer((sock) => {
  let buf = "", inData = false, cur = { rcpt: [], data: "" };
  sock.write("220 sink\r\n");
  sock.on("data", (chunk) => {
    buf += chunk.toString("binary");
    for (;;) {
      if (inData) {
        const end = buf.indexOf("\r\n.\r\n");
        if (end < 0) return;
        cur.data = buf.slice(0, end + 2); buf = buf.slice(end + 5); inData = false;
        mails.push(cur); cur = { rcpt: [], data: "" }; sock.write("250 queued\r\n");
        continue;
      }
      const eol = buf.indexOf("\r\n"); if (eol < 0) return;
      const line = buf.slice(0, eol); buf = buf.slice(eol + 2);
      if (/^(EHLO|HELO)/i.test(line)) sock.write("250-sink\r\n250 8BITMIME\r\n");
      else if (/^MAIL FROM:/i.test(line)) sock.write("250 ok\r\n");
      else if (/^RCPT TO:/i.test(line)) { cur.rcpt.push(line.slice(8)); sock.write("250 ok\r\n"); }
      else if (/^DATA/i.test(line)) { inData = true; sock.write("354 go\r\n"); }
      else if (/^QUIT/i.test(line)) { sock.write("221 bye\r\n"); sock.end(); return; }
      else sock.write("250 ok\r\n");
    }
  });
}).listen(Number(SMTP_PORT), "127.0.0.1");

const header = (m, name) => (m.data.match(new RegExp(`^${name}: (.*)\\r$`, "mi")) || [])[1];
const textOf = (m) => {
  const part = m.data.split(/\r\n--revet-[0-9a-f]+/)[1] || "";
  const b64 = part.split("\r\n\r\n")[1] || "";
  return Buffer.from(b64.replace(/\s+/g, ""), "base64").toString("utf8");
};
const linkOf = (m) => (textOf(m).match(/https?:\/\/[^\s]+\/auth\/magic\?token=[A-Za-z0-9_-]+/) || [])[0];
const waitMail = async (n, ms = 10000) => { const t = Date.now(); while (Date.now() - t < ms) { if (mails.length >= n) return mails[n - 1]; await sleep(100); } return null; };

// ── Chromium over the DevTools protocol ──
const chrome = spawn(CHROME, ["--headless=new", `--remote-debugging-port=${DEBUG_PORT}`, `--user-data-dir=${mkdtempSync(join(tmpdir(), "chrome-magic-"))}`,
  "--no-first-run", "--no-default-browser-check", "--disable-gpu", "about:blank"], { stdio: "ignore" });
let wsUrl; for (let i = 0; i < 50 && !wsUrl; i++) { try { wsUrl = (await (await fetch(`http://127.0.0.1:${DEBUG_PORT}/json/version`)).json()).webSocketDebuggerUrl; } catch { await sleep(200); } }
const ws = new WebSocket(wsUrl); await new Promise((r) => (ws.onopen = r));
let nextId = 1; const pending = new Map();
ws.onmessage = (m) => { const msg = JSON.parse(m.data); if (msg.id && pending.has(msg.id)) { const { res, rej } = pending.get(msg.id); pending.delete(msg.id); msg.error ? rej(new Error(JSON.stringify(msg.error))) : res(msg.result); } };
const send = (method, params = {}, sessionId) => new Promise((res, rej) => { const id = nextId++; pending.set(id, { res, rej }); ws.send(JSON.stringify({ id, method, params, ...(sessionId ? { sessionId } : {}) })); });
const { targetId } = await send("Target.createTarget", { url: "about:blank" });
const { sessionId } = await send("Target.attachToTarget", { targetId, flatten: true });
const cdp = (m, p) => send(m, p, sessionId);
await cdp("Page.enable"); await cdp("Runtime.enable"); await cdp("Network.enable"); await cdp("WebAuthn.enable");
const deviceOptions = { protocol: "ctap2", transport: "internal", hasResidentKey: true, hasUserVerification: true, isUserVerified: true, automaticPresenceSimulation: true };
let { authenticatorId } = await cdp("WebAuthn.addVirtualAuthenticator", { options: deviceOptions });
const ev = async (expr) => { const r = await cdp("Runtime.evaluate", { expression: expr, awaitPromise: true, returnByValue: true }); if (r.exceptionDetails) throw new Error(JSON.stringify(r.exceptionDetails.exception?.description ?? r.exceptionDetails.text)); return r.result.value; };
const path = () => ev("location.pathname");
const goto = async (url) => { await cdp("Page.navigate", { url: url.startsWith("http") ? url : BASE + url }); await sleep(150); for (let i = 0; i < 100; i++) { if (await ev("document.readyState") === "complete") return; await sleep(100); } };
const until = async (expr, what, ms = 8000) => { const t = Date.now(); while (Date.now() - t < ms) { if (await ev(expr)) return true; await sleep(100); } console.log("  timed out waiting for:", what); return false; };
const text = () => ev("document.body.innerText");
const creds = async () => (await cdp("WebAuthn.getCredentials", { authenticatorId })).credentials;
const requestLink = async (email) => { await ev(`(() => { const f = document.querySelector("form[hx-post='/auth/magic/request']"); f.querySelector("input[name=email]").value = ${JSON.stringify(email)}; f.requestSubmit(); })()`); };

try {
  // 1. Ask for a link from the sign-in page.
  await goto("/login");
  check(await ev(`!!document.querySelector("form[hx-post='/auth/magic/request']")`), "login page has the email form");
  await requestLink("Ann@Example.COM ");
  check(await until(`document.getElementById("magic-status")?.innerText.includes("on its way")`, "confirmation"), "page says a link is on its way");
  const m1 = await waitMail(1);
  check(!!m1, "a mail arrived over SMTP");
  check(header(m1, "To") === "ann@example.com", "addressed to the normalised address", header(m1, "To"));
  check(/^My ?App|Example|App/.test(header(m1, "From") || "") && (header(m1, "From") || "").includes("login@app.example.com"), "from the configured sender", header(m1, "From"));
  check(header(m1, "Subject") === "Your Example sign-in link", "subject", header(m1, "Subject"));
  const link1 = linkOf(m1);
  check(!!link1 && link1.startsWith(`${BASE}/auth/magic?token=`), "link points at the configured base URL", link1?.slice(0, 50));
  check(textOf(m1).includes("15 minutes"), "mail says how long the link lasts");

  // 2. A mail scanner opening the link does not use it up.
  for (let i = 0; i < 3; i++) { const r = await fetch(link1); check(r.status === 200 && (await r.text()).includes("Sign in as"), `scanner fetch ${i + 1}: link page shown, nothing spent`); }

  // 3. The person opens it and presses the button.
  await goto(link1);
  check((await text()).includes("ann@example.com"), "link page shows which address will be signed in");
  check(await ev(`!!document.querySelector("form[action='/auth/magic/confirm'] button, form[action='/auth/magic/confirm'] input[type=submit]")`), "with a button");
  await ev(`document.querySelector("form[action='/auth/magic/confirm']").requestSubmit()`);
  check(await until(`location.pathname === "/"`, "redirect home"), "pressing it signs in");
  const cookies = (await cdp("Network.getCookies", { urls: [BASE] })).cookies.map((c) => c.name);
  check(cookies.includes("_cw_Session"), "session cookie set", cookies.join(","));

  // 4. The link is spent.
  await goto(link1);
  check((await text()).includes("expired"), "replaying the link shows it has expired");
  check(!(await text()).includes("Sign in as"), "and offers no button");

  // 5. Account page shows the address; add a passkey.
  await goto("/account/passkeys");
  check((await text()).includes("ann@example.com"), "account page lists the email address");
  await ev(`document.querySelector("[data-passkey=add]").click()`);
  check(await until(`document.querySelectorAll(".passkey-row").length >= 1`, "passkey row"), "adding a passkey works for an email account");
  await sleep(400);

  // 6. Add a second address while signed in.
  await ev(`(() => { const f = document.querySelector("form[hx-post='/auth/magic/request']"); f.querySelector("input[name=email]").value = "ann.work@example.com"; f.requestSubmit(); })()`);
  const m2 = await waitMail(2);
  check(!!m2 && header(m2, "Subject") === "Confirm your email address for Example", "confirm mail for a second address", header(m2 ?? { data: "" }, "Subject"));
  await goto(linkOf(m2));
  check((await text()).includes("Add ") && (await text()).includes("ann.work@example.com"), "link page asks to add the address");
  await ev(`document.querySelector("form[action='/auth/magic/confirm']").requestSubmit()`);
  check(await until(`location.pathname === "/account/passkeys"`, "back to the account page"), "confirming returns to the account page");
  check((await text()).includes("ann.work@example.com") && (await text()).includes("ann@example.com"), "both addresses are listed");

  // 7. Lose every passkey and the session, then get back in with a link.
  await cdp("Network.clearBrowserCookies");
  await cdp("WebAuthn.removeVirtualAuthenticator", { authenticatorId });
  ({ authenticatorId } = await cdp("WebAuthn.addVirtualAuthenticator", { options: deviceOptions })); // a new phone with no passkeys
  await goto("/login");
  await ev(`document.querySelector("[data-passkey=signin]").click()`);
  await sleep(1500);
  check((await path()) === "/login", "with no passkey on the device, passkey sign-in fails");
  await requestLink("ann.work@example.com");
  const m3 = await waitMail(3);
  check(!!m3, "a link for the second address arrives");
  await goto(linkOf(m3));
  await ev(`document.querySelector("form[action='/auth/magic/confirm']").requestSubmit()`);
  check(await until(`location.pathname === "/"`, "signed in"), "the link signs in the same account from a new device");
  await goto("/account/passkeys");
  check(await ev(`document.querySelectorAll(".passkey-row").length`) >= 2, "it is the same account: the old passkey and both addresses are there");
  await ev(`document.querySelector("[data-passkey=add]").click()`);
  check(await until(`document.querySelectorAll(".passkey-row").length >= 3`, "new passkey"), "and a passkey can be added for the new phone");

  // 8. Limits and bad input.
  await cdp("Network.clearBrowserCookies");
  await goto("/login");
  // The browser's own validation stops most bad input before it is sent; skip it to reach the server's check.
  await ev(`document.querySelector("form[hx-post='/auth/magic/request']").noValidate = true`);
  await requestLink("not an address");
  check(await until(`document.getElementById("magic-status")?.innerText.includes("does not look like")`, "message"), "a bad address gets a clear message, and no mail");
  const before = mails.length;
  for (let i = 0; i < 6; i++) { await requestLink("flood@example.com"); await sleep(250); }
  await sleep(1500);
  const sentToFlood = mails.slice(before).filter((m) => header(m, "To") === "flood@example.com").length;
  check(sentToFlood === 3, "one address gets at most 3 links in a burst", String(sentToFlood));
  const resp = await ev(`fetch("/auth/magic/request", { method: "POST", headers: { "x-csrf-token": document.querySelector("[hx-headers]")?.getAttribute("hx-headers").match(/[0-9a-f]{64}/)?.[0] ?? "", "content-type": "application/x-www-form-urlencoded" }, body: "email=flood%40example.com" }).then(r => r.status)`);
  check(resp === 429, "the next request is a 429", String(resp));
} catch (e) {
  console.log("ERROR", e.stack || e.message); failures++;
} finally {
  ws.close(); chrome.kill(); smtp.close();
}
console.log(failures === 0 ? "ALL MAGIC-LINK BROWSER CHECKS PASSED" : `${failures} FAILED`);
process.exit(failures === 0 ? 0 : 1);
