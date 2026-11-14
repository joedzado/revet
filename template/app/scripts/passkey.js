// Passkey sign-in, sign-up and "add a passkey" for the browser. No libraries.
// A button with data-passkey="signin" | "signup" | "add" starts the flow; the status line is #passkey-status.
// The page root carries data-csrf; requests go to the cookie-session routes under /passkey/.
(function () {
  "use strict";

  const enc = (buf) =>
    btoa(String.fromCharCode(...new Uint8Array(buf))).replace(/\+/g, "-").replace(/\//g, "_").replace(/=+$/, "");
  const dec = (s) => {
    s = s.replace(/-/g, "+").replace(/_/g, "/");
    const bin = atob(s + "=".repeat((4 - (s.length % 4)) % 4));
    return Uint8Array.from(bin, (c) => c.charCodeAt(0)).buffer;
  };

  const root = () => document.querySelector("[data-csrf]");
  const say = (msg) => {
    const el = document.getElementById("passkey-status");
    if (el) el.textContent = msg;
  };

  async function post(url, body) {
    const res = await fetch(url, {
      method: "POST",
      credentials: "same-origin",
      headers: { "content-type": "application/json", "x-csrf-token": root().dataset.csrf },
      body: JSON.stringify(body),
    });
    let json = {};
    try { json = await res.json(); } catch (_) {}
    return { ok: res.ok, status: res.status, json };
  }

  function creationOptions(o) {
    return {
      ...o,
      challenge: dec(o.challenge),
      user: { ...o.user, id: dec(o.user.id) },
      excludeCredentials: (o.excludeCredentials || []).map((c) => ({ ...c, id: dec(c.id) })),
    };
  }

  async function register(label) {
    const start = await post("/passkey/register/options", { display_name: label });
    if (!start.ok) throw new Error("Could not start. Try again.");
    const cred = await navigator.credentials.create({ publicKey: creationOptions(start.json.options) });
    const done = await post("/passkey/register/finish", {
      challenge_id: start.json.challenge_id,
      client_data_json: enc(cred.response.clientDataJSON),
      attestation_object: enc(cred.response.attestationObject),
      label: label,
    });
    if (!done.ok) throw new Error(done.json.error === "credential_taken" ? "That passkey is already registered." : "Could not save the passkey.");
    return done.json;
  }

  async function signIn() {
    const start = await post("/passkey/login/options", {});
    if (!start.ok) throw new Error("Could not start. Try again.");
    const o = start.json.options;
    const cred = await navigator.credentials.get({ publicKey: { ...o, challenge: dec(o.challenge) } });
    const r = cred.response;
    const done = await post("/passkey/login/finish", {
      challenge_id: start.json.challenge_id,
      credential_id: enc(cred.rawId),
      client_data_json: enc(r.clientDataJSON),
      authenticator_data: enc(r.authenticatorData),
      signature: enc(r.signature),
      user_handle: r.userHandle ? enc(r.userHandle) : "",
    });
    if (!done.ok) throw new Error("Sign-in failed.");
    return done.json;
  }

  async function run(kind) {
    if (!window.PublicKeyCredential) return say("This browser does not support passkeys.");
    const name = document.getElementById("passkey-name");
    const label = (name && name.value.trim()) || "This device";
    say("Waiting for your device…");
    try {
      if (kind === "signin") {
        await signIn();
        location.assign("/");
      } else if (kind === "signup") {
        await register(label);
        location.assign("/");
      } else {
        await register(label);
        say("Passkey added.");
        if (window.htmx) htmx.ajax("GET", "/account/passkeys/list", { target: "#passkey-list", swap: "outerHTML" });
      }
    } catch (e) {
      say(e && e.name === "NotAllowedError" ? "Cancelled." : (e && e.message) || "Something went wrong.");
    }
  }

  // The server sends HX-Trigger {"passkey-note": "..."} when it declines a removal.
  document.addEventListener("passkey-note", (ev) => say(ev.detail && ev.detail.value));

  document.addEventListener("click", (ev) => {
    const btn = ev.target.closest("[data-passkey]");
    if (btn) {
      ev.preventDefault();
      run(btn.dataset.passkey);
    }
  });
})();
