// SPDX-License-Identifier: AGPL-3.0-or-later
// Einstellungen: System, Zugang, Darstellung, Updates mit „Was ist neu“,
// Daten (Export/Import), Problem melden mit Diagnosepaket, Über.
import { useEffect, useState } from "preact/hooks";
import { Bug, Download, FileJson, Info, KeyRound, Languages, Moon, RefreshCcw, Server, Upload } from "lucide-preact";
import { lang, setLang, t, tIn, type Lang, type TextKey } from "../i18n";
import { get, post, put } from "../api";
import { dateTime, num } from "../format";
import { config, info, logoutLocal, refreshConfig, simulated, toast } from "../store";
import { Banner, Button, Card, Field, Modal, NumberInput, Pill, Seg, Toggle } from "../ui";
import { sourceKnown, sourceLabel, sourceUrl } from "../source";

// Target for public bug reports.
const ISSUE_URL = "https://github.com/Wokesay/growcontroller-software/issues/new";

// Sections of the update summary (field names of the API) and their labels.
const SUMMARY: Record<"neu" | "behoben" | "beachten" | "sicherheit", TextKey> = {
  neu: "settings.summary.new",
  behoben: "settings.summary.fixed",
  beachten: "settings.summary.notes",
  sicherheit: "settings.summary.security",
};

function Markdown(p: { text: string }) {
  // Kleiner Darsteller für den Changelog: Überschriften, Listen, Absätze.
  const blocks: preact.JSX.Element[] = [];
  let list: string[] = [];
  const flush = () => {
    if (list.length) blocks.push(<ul>{list.map((l) => <li>{l}</li>)}</ul>);
    list = [];
  };
  for (const raw of p.text.split("\n")) {
    const l = raw.trimEnd();
    if (l.startsWith("- ")) list.push(l.slice(2));
    else {
      flush();
      if (l.startsWith("### ")) blocks.push(<h3>{l.slice(4)}</h3>);
      else if (l.startsWith("## ")) blocks.push(<h2>{l.slice(3)}</h2>);
      else if (l.startsWith("# ")) blocks.push(<h2>{l.slice(2)}</h2>);
      else if (l.trim()) blocks.push(<p class="muted">{l.replace(/\[([^\]]+)\]\([^)]+\)/g, "$1")}</p>);
    }
  }
  flush();
  return <div class="md">{blocks}</div>;
}

function Updates() {
  const cfg = config.value!;
  const [st, setSt] = useState<any>(null);
  const [log, setLog] = useState<string | null>(null);
  const [msg, setMsg] = useState<string | null>(null);
  useEffect(() => {
    get("/update").then(setSt).catch(() => setSt(null));
  }, []);
  const av = st?.available;
  return (
    <div class="stack">
      <div class="row-between">
        <div>
          <div class="muted small">{t("settings.installed")}</div>
          <strong>{t("common.version", { v: info.value?.version ?? "" })}</strong>
        </div>
        <Seg
          value={cfg.system.updateChannel as "stable" | "beta"}
          onChange={async (v) => {
            await put("/system", { updateChannel: v });
            await refreshConfig();
          }}
          options={[
            ["stable", t("settings.stable")],
            ["beta", t("settings.beta")],
          ]}
        />
      </div>
      <label class="row">
        <Toggle
          checked={cfg.system.updateCheck}
          onChange={async (v) => {
            await put("/system", { updateCheck: v });
            await refreshConfig();
          }}
          label={t("settings.autoCheck")}
        />
        <span>
          {t("settings.check")} <span class="muted small">{t("settings.checkNote")}</span>
        </span>
      </label>
      <div class="row">
        <Button onClick={async () => setSt(await post("/update/check"))}>
          <RefreshCcw size={15} /> {t("settings.checkNow")}
        </Button>
        <Button variant="ghost" onClick={async () => setLog(await (await fetch("/api/v1/changelog")).text())}>
          {t("settings.changelog")}
        </Button>
        {st?.lastCheck ? <span class="faint small">{t("settings.lastChecked", { when: dateTime(st.lastCheck) })}</span> : null}
      </div>
      {av && (
        <div class="card flat stack-sm">
          <div class="row-between">
            <strong>{t("settings.newVersion", { v: av.version })}</strong>
            <Pill tone={av.channel === "beta" ? "warn" : "accent"}>{av.channel === "beta" ? t("settings.beta") : t("settings.stable")}</Pill>
          </div>
          {(["neu", "behoben", "beachten", "sicherheit"] as const).map((k) =>
            av.summary?.[k]?.length ? (
              <div>
                <div class="section-title">{t(SUMMARY[k])}</div>
                <ul style="margin:4px 0;padding-left:20px">
                  {av.summary[k].map((x: string) => (
                    <li>{x}</li>
                  ))}
                </ul>
              </div>
            ) : null,
          )}
          <p class="muted small">{t("settings.installNote")}</p>
          <div class="row">
            <Button variant="primary" onClick={async () => setMsg((await post("/update/install", { version: av.version })).message)}>
              {t("settings.install")}
            </Button>
          </div>
          {msg && <Banner>{msg}</Banner>}
        </div>
      )}
      {log !== null && (
        <Modal wide title={t("settings.changelog")} onClose={() => setLog(null)}>
          <Markdown text={log} />
        </Modal>
      )}
    </div>
  );
}

function Report() {
  const [diag, setDiag] = useState<any>(null);
  const [text, setText] = useState("");
  const download = () => {
    const blob = new Blob([JSON.stringify({ ...diag, beschreibung: text }, null, 2)], { type: "application/json" });
    const a = document.createElement("a");
    a.href = URL.createObjectURL(blob);
    a.download = `growcontroller-diagnose-${diag.reportId}.json`;
    a.click();
  };
  const issue = diag
    ? `${ISSUE_URL}?labels=bug,triage&title=${encodeURIComponent(`[${diag.reportId}] ${Array.from(text).slice(0, 60).join("") || tIn("en", "settings.issueTitle")}`)}&body=${encodeURIComponent(
        tIn("en", "settings.issueBody", { text, version: diag.info.version, platform: diag.info.platform.kind, id: diag.reportId }),
      )}`
    : "";
  return (
    <div class="stack">
      <p class="muted">{t("settings.reportIntro")}</p>
      <Field label={t("settings.whatHappened")}>
        <textarea class="input" rows={3} value={text} onInput={(e) => setText((e.target as HTMLTextAreaElement).value)} placeholder={t("settings.whatHappenedHint")} />
      </Field>
      <div class="row">
        <Button onClick={async () => setDiag(await get("/diagnostics"))}>
          <Bug size={15} /> {t("settings.createDiag")}
        </Button>
      </div>
      {diag && (
        <div class="stack-sm">
          <div class="row">
            <Pill tone="accent">{t("settings.reportId", { id: diag.reportId })}</Pill>
            <span class="faint small">{t("settings.notIncluded", { list: diag.redacted.join(", ") })}</span>
          </div>
          <details>
            <summary>{t("settings.viewContents")}</summary>
            <pre class="code">{JSON.stringify(diag, null, 2).slice(0, 20000)}</pre>
          </details>
          <div class="row">
            <Button variant="primary" onClick={download}>
              <Download size={15} /> {t("settings.download")}
            </Button>
            <a class="btn" href={issue} target="_blank" rel="noopener noreferrer">
              {t("settings.reportPublic")}
            </a>
          </div>
          <Banner tone="warn">{t("settings.publicWarn")}</Banner>
        </div>
      )}
    </div>
  );
}

export function SettingsPage() {
  const cfg = config.value!;
  const [name, setName] = useState(cfg.system.name);
  const [tz, setTz] = useState(cfg.system.timezone);
  const [hand, setHand] = useState<number | null>(cfg.limits.handDoseMaxMl);
  const [oldPw, setOldPw] = useState("");
  const [newPw, setNewPw] = useState("");
  const [theme, setTheme] = useState<"system" | "light" | "dark">(((document.documentElement.dataset.theme as any) || "system"));
  const setThemeAll = (v: "system" | "light" | "dark") => {
    setTheme(v);
    if (v === "system") delete document.documentElement.dataset.theme;
    else document.documentElement.dataset.theme = v;
    try {
      if (v === "system") localStorage.removeItem("gc-theme");
      else localStorage.setItem("gc-theme", v);
    } catch {
      /* no storage */
    }
  };
  return (
    <div class="stack">
      <div class="grid-2">
        <Card title={t("settings.system")} icon={<Server size={18} />}>
          <div class="stack">
            <div class="form-grid">
              <Field label={t("setup.start.name")}>
                <input class="input" value={name} onInput={(e) => setName((e.target as HTMLInputElement).value)} />
              </Field>
              <Field label={t("setup.start.tz")}>
                <input class="input" value={tz} onInput={(e) => setTz((e.target as HTMLInputElement).value)} />
              </Field>
              <Field label={t("settings.handLimit")} hint={t("settings.handLimitHint")}>
                <NumberInput value={hand} onValue={setHand} unit="ml" />
              </Field>
            </div>
            <div>
              <Button
                variant="primary"
                onClick={async () => {
                  await put("/system", { name, timezone: tz, handDoseMaxMl: hand });
                  await refreshConfig();
                  toast(t("settings.saved"));
                }}
              >
                {t("common.save")}
              </Button>
            </div>
          </div>
        </Card>
        <Card title={t("settings.access")} icon={<KeyRound size={18} />}>
          <div class="stack">
            <div class="form-grid">
              <Field label={t("settings.oldPassword")}>
                <input class="input" type="password" value={oldPw} onInput={(e) => setOldPw((e.target as HTMLInputElement).value)} autoComplete="current-password" />
              </Field>
              <Field label={t("login.newPassword")} hint={t("login.minLength")}>
                <input class="input" type="password" value={newPw} onInput={(e) => setNewPw((e.target as HTMLInputElement).value)} autoComplete="new-password" />
              </Field>
            </div>
            <div class="row">
              <Button
                disabled={!oldPw || newPw.length < 8}
                onClick={async () => {
                  await put("/auth/password", { old: oldPw, new: newPw });
                  toast(t("settings.passwordChanged"));
                  logoutLocal();
                }}
              >
                {t("settings.changePassword")}
              </Button>
              <Button
                variant="ghost"
                onClick={async () => {
                  await post("/auth/logout");
                  logoutLocal();
                }}
              >
                {t("settings.signOut")}
              </Button>
            </div>
            <p class="faint small">{t("settings.accessNote")}</p>
          </div>
        </Card>
      </div>
      <div class="grid-2">
        <Card title={t("settings.updates")} icon={<RefreshCcw size={18} />}>
          <Updates />
        </Card>
        <Card title={t("settings.report")} icon={<Bug size={18} />}>
          <Report />
        </Card>
      </div>
      <div class="grid-2">
        <Card title={t("settings.data")} icon={<FileJson size={18} />}>
          <div class="stack-sm">
            <p class="muted">{t("settings.dataText")}</p>
            <div class="row">
              <a class="btn" href="/api/v1/config/export" download>
                <Download size={15} /> {t("settings.backup")}
              </a>
              <label class="btn">
                <Upload size={15} /> {t("settings.restore")}
                <input
                  type="file"
                  accept="application/json"
                  hidden
                  onChange={async (e) => {
                    const f = (e.target as HTMLInputElement).files?.[0];
                    if (!f) return;
                    try {
                      await post("/config/import", JSON.parse(await f.text()));
                      await refreshConfig();
                      toast(t("settings.restored"));
                    } catch (x: any) {
                      toast(x.message ?? String(x), "error");
                    }
                  }}
                />
              </label>
              <a class="btn" href="/api/v1/export.csv" download>
                <Download size={15} /> {t("settings.historyCsv")}
              </a>
            </div>
          </div>
        </Card>
        <Card title={t("settings.display")} icon={<Info size={18} />}>
          <div class="stack-sm">
            <div class="row-between">
              <span class="row">
                <Moon size={16} /> {t("settings.theme")}
              </span>
              <Seg value={theme} onChange={setThemeAll} options={[["system", t("settings.themeSystem")], ["light", t("settings.themeLight")], ["dark", t("settings.themeDark")]]} />
            </div>
            <div class="row-between">
              <span class="row">
                <Languages size={16} /> {t("common.language")}
              </span>
              <Seg<Lang>
                value={lang.value}
                onChange={async (l) => {
                  setLang(l);
                  await put("/system", { language: l });
                  await refreshConfig();
                }}
                options={[["de", "Deutsch"], ["en", "English"]]}
              />
            </div>
            <div class="divider" />
            <table class="table">
              <tbody>
                <tr><td class="muted">{t("settings.software")}</td><td>{info.value?.version}</td></tr>
                <tr><td class="muted">{t("settings.api")}</td><td>v{info.value?.api}</td></tr>
                <tr><td class="muted">{t("settings.catalog")}</td><td>{info.value?.catalogVersion}</td></tr>
                <tr><td class="muted">{t("settings.config")}</td><td>{t("settings.configValue", { schema: cfg.schemaVersion, rev: num(cfg.revision, 0) })}</td></tr>
                <tr><td class="muted">{t("settings.platform")}</td><td>{simulated.value ? t("settings.simulator") : info.value?.platform.kind}</td></tr>
                <tr>
                  <td class="muted">{t("about.source")}</td>
                  <td>
                    <a href={sourceUrl} target="_blank" rel="noopener noreferrer" data-testid="source-link">
                      {sourceKnown ? sourceLabel : t("about.sourceUnknown")}
                    </a>
                  </td>
                </tr>
              </tbody>
            </table>
            <p class="faint small">{t(sourceKnown ? "about.license" : "about.licenseUnknown")}</p>
            <p class="faint small">{t("settings.components")}</p>
          </div>
        </Card>
      </div>
    </div>
  );
}
