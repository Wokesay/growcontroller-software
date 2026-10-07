// Anmeldung und Erstpasswort. Kein Standardpasswort, kein Überspringen
// (EN 18031-1 AUM-5-1).
import { useState } from "preact/hooks";
import { KeyRound, Sprout } from "lucide-preact";
import { ApiError, post } from "../api";
import { afterLogin, info } from "../store";
import { Banner, Field } from "../ui";

export function Login() {
  const first = !info.value?.hasPassword;
  const [pw, setPw] = useState("");
  const [pw2, setPw2] = useState("");
  const [err, setErr] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);

  async function submit(e: Event) {
    e.preventDefault();
    setErr(null);
    if (first && pw !== pw2) return setErr("Die Passwörter stimmen nicht überein.");
    setBusy(true);
    try {
      await post(first ? "/auth/setup" : "/auth/login", { password: pw });
      await afterLogin();
    } catch (x) {
      setErr(x instanceof ApiError ? x.message : String(x));
    } finally {
      setBusy(false);
    }
  }

  return (
    <div class="login">
      <form class="card" onSubmit={submit}>
        <div class="row">
          <div class="brand-mark" style="width:40px;height:40px;border-radius:12px">
            <Sprout size={22} />
          </div>
          <div>
            <h1 style="font-size:1.3rem">growcontroller</h1>
            <p class="muted small">{info.value?.name && info.value.name !== "growcontroller" ? info.value.name : "Fertigations-Hub"}</p>
          </div>
        </div>
        {first ? (
          <>
            <div>
              <h2>Willkommen</h2>
              <p class="muted">Lege zuerst ein eigenes Passwort fest. Es schützt Pumpen und Einstellungen in deinem Netz.</p>
            </div>
            <Field label="Neues Passwort" hint="Mindestens 8 Zeichen">
              <input class="input" type="password" autoComplete="new-password" value={pw} onInput={(e) => setPw((e.target as HTMLInputElement).value)} required minLength={8} name="password" />
            </Field>
            <Field label="Passwort wiederholen">
              <input class="input" type="password" autoComplete="new-password" value={pw2} onInput={(e) => setPw2((e.target as HTMLInputElement).value)} required name="password2" />
            </Field>
          </>
        ) : (
          <Field label="Passwort">
            <input class="input" type="password" autoComplete="current-password" value={pw} onInput={(e) => setPw((e.target as HTMLInputElement).value)} autoFocus required name="password" />
          </Field>
        )}
        {err && <Banner tone="bad">{err}</Banner>}
        <button class="btn primary lg block" type="submit" disabled={busy}>
          <KeyRound size={18} /> {first ? "Passwort festlegen" : "Anmelden"}
        </button>
        <p class="faint small">
          Die Web-App läuft auf dem Hub in deinem Heimnetz – ohne Cloud und ohne Konto. Die Steuerung läuft weiter, auch wenn diese Seite zu ist.
        </p>
      </form>
    </div>
  );
}
