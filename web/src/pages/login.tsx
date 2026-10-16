// SPDX-License-Identifier: AGPL-3.0-or-later
// Anmeldung und Erstpasswort. Kein Standardpasswort, kein Überspringen
// (EN 18031-1 AUM-5-1).
import { useState } from "preact/hooks";
import { KeyRound, Languages, Sprout } from "lucide-preact";
import { post } from "../api";
import { afterLogin, errorText, hasPlugs, info, sessionExpired } from "../store";
import { lang, setLang, t, tIn, type Lang } from "../i18n";
import { sourceUrl } from "../source";
import { Banner, Field, LANGS, Seg } from "../ui";

export function Login() {
  const first = !info.value?.hasPassword;
  const needsCode = first && !!info.value?.setupCode;  // only who can read the hub's log sets the first password (#79)
  const [code, setCode] = useState("");
  const [pw, setPw] = useState("");
  const [pw2, setPw2] = useState("");
  const [err, setErr] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);

  async function submit(e: Event) {
    e.preventDefault();
    setErr(null);
    if (first && pw !== pw2) return setErr(t("login.mismatch"));
    setBusy(true);
    try {
      await post(first ? "/auth/setup" : "/auth/login", needsCode ? { password: pw, code } : { password: pw });
      await afterLogin();
    } catch (x) {
      setErr(errorText(x));
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
            <p class="muted small">{info.value?.name && info.value.name !== "growcontroller" ? info.value.name : t("login.tagline")}</p>
          </div>
        </div>
        {/* What was clicked when the session ran out was not carried out: never silent */}
        {sessionExpired.value && !first && (
          <Banner tone="warn">
            <span data-testid="session-expired">
              {t("login.expired")} {t(hasPlugs.value ? "shell.stopHintPlugs" : "shell.stopHint")}
            </span>
          </Banner>
        )}
        {first ? (
          <>
            {/* The language comes first, English chosen until the user picks another (SD-014) */}
            <div class="field" data-testid="first-lang">
              {/* The same label in both languages, so it does not move when the language does */}
              <span id="first-lang-label" class="row" style="gap:6px">
                <Languages size={16} aria-hidden="true" />
                <span lang="en">{tIn("en", "common.language")}</span> · <span lang="de">{tIn("de", "common.language")}</span>
              </span>
              <Seg<Lang> value={lang.value} onChange={(l) => setLang(l)} options={LANGS} labelledBy="first-lang-label" large />
            </div>
            <div>
              <h2>{t("setup.start.h")}</h2>
              <p class="muted">{t("login.firstText")}</p>
            </div>
            {needsCode && (
              <Field label={t("login.code")} hint={info.value?.platform.kind === "home-assistant" && <span class="muted">{t("login.codeHintHa")}</span>}>
                <input class="input" style="font-family:var(--mono)" type="text" autoComplete="one-time-code" autoCapitalize="characters" autoCorrect="off" spellcheck={false} value={code} onInput={(e) => setCode((e.target as HTMLInputElement).value)} required name="code" />
              </Field>
            )}
            <Field label={t("login.newPassword")} hint={t("login.minLength")}>
              <input class="input" type="password" autoComplete="new-password" value={pw} onInput={(e) => setPw((e.target as HTMLInputElement).value)} required minLength={8} name="password" />
            </Field>
            <Field label={t("login.repeatPassword")}>
              <input class="input" type="password" autoComplete="new-password" value={pw2} onInput={(e) => setPw2((e.target as HTMLInputElement).value)} required name="password2" />
            </Field>
          </>
        ) : (
          <Field label={t("login.password")}>
            <input class="input" type="password" autoComplete="current-password" value={pw} onInput={(e) => setPw((e.target as HTMLInputElement).value)} autoFocus required name="password" />
          </Field>
        )}
        {err && <Banner tone="bad">{err}</Banner>}
        <button class="btn primary lg block" type="submit" disabled={busy}>
          <KeyRound size={18} /> {first ? t("login.setPassword") : t("login.signIn")}
        </button>
        <p class="faint small">
          {t("login.localNote")}
        </p>
        <p class="faint small">
          <a href={sourceUrl} target="_blank" rel="noopener noreferrer" data-testid="source-link">
            {t("login.source")}
          </a>
        </p>
      </form>
    </div>
  );
}
