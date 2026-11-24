// Passkey sign-in, sign-up and "add a passkey". A button with data-passkey="signin" | "signup" | "add" starts it;
// status goes to #passkey-status. The page root carries data-csrf. The browser's own JSON helpers
// (parse…FromJSON, toJSON) do the base64url work, so this needs Chrome 129, Safari 18.4 or Firefox 135.
(() => {
  const say = (msg) => { const el = document.getElementById("passkey-status"); if (el) el.textContent = msg; };

  const post = async (url, body) => {
    const res = await fetch(url, {
      method: "POST",
      headers: { "content-type": "application/json", "x-csrf-token": document.querySelector("[data-csrf]").dataset.csrf },
      body: JSON.stringify(body),
    });
    const json = await res.json().catch(() => ({}));
    if (!res.ok) throw Object.assign(new Error(json.error || "Could not complete that."), { status: res.status });
    return json;
  };

  const register = async (label) => {
    const start = await post("/passkey/register/options", { display_name: label });
    const { response: r } = (await navigator.credentials.create({
      publicKey: PublicKeyCredential.parseCreationOptionsFromJSON(start.options),
    })).toJSON();
    await post("/passkey/register/finish", {
      challenge_id: start.challenge_id,
      client_data_json: r.clientDataJSON,
      attestation_object: r.attestationObject,
      label,
    });
  };

  const signIn = async () => {
    const start = await post("/passkey/login/options", {});
    const c = (await navigator.credentials.get({
      publicKey: PublicKeyCredential.parseRequestOptionsFromJSON(start.options),
    })).toJSON();
    await post("/passkey/login/finish", {
      challenge_id: start.challenge_id,
      credential_id: c.rawId,
      client_data_json: c.response.clientDataJSON,
      authenticator_data: c.response.authenticatorData,
      signature: c.response.signature,
      user_handle: c.response.userHandle || "",
    });
  };

  const messages = {
    NotAllowedError: "Cancelled.",
    InvalidStateError: "This device already has a passkey for this account.",
    credential_taken: "That passkey is already registered.",
  };

  document.addEventListener("click", async (ev) => {
    const btn = ev.target.closest("[data-passkey]");
    if (!btn) return;
    ev.preventDefault();
    if (!window.PublicKeyCredential?.parseCreationOptionsFromJSON) return say("This browser does not support passkeys.");
    const kind = btn.dataset.passkey;
    const label = document.getElementById("passkey-name")?.value.trim() || "This device";
    say("Waiting for your device…");
    try {
      await (kind === "signin" ? signIn() : register(label));
      if (kind !== "add") return location.assign("/");
      say("Passkey added.");
      window.htmx?.ajax("GET", "/account/passkeys/list", { target: "#passkey-list", swap: "outerHTML" });
    } catch (e) {
      say(messages[e.name] || messages[e.message] || "Something went wrong. Try again.");
    }
  });

  // The server sends HX-Trigger {"passkey-note": "..."} when it declines a removal.
  document.addEventListener("passkey-note", (ev) => say(ev.detail?.value));
})();
