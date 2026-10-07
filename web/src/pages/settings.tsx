// Einstellungen: System, Zugang, Darstellung, Updates mit „Was ist neu“,
// Daten (Export/Import), Problem melden mit Diagnosepaket, Über.
import { useEffect, useState } from "preact/hooks";
import { Bug, Download, FileJson, Info, KeyRound, Languages, Moon, RefreshCcw, Server, Upload } from "lucide-preact";
import { lang, setLang, t, type Lang } from "../i18n";
import { get, post, put } from "../api";
import { dateTime, num } from "../format";
import { config, info, logoutLocal, refreshConfig, simulated, toast } from "../store";
import { Banner, Button, Card, Field, Modal, NumberInput, Pill, Seg, Toggle } from "../ui";

// Ziel für öffentliche Fehlermeldungen. Zieht mit, wenn die Software ein eigenes Repo bekommt.
const ISSUE_URL = "https://github.com/Wokesay/growcontroller/issues/new";

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
          <div class="muted small">Installiert</div>
          <strong>Version {info.value?.version}</strong>
        </div>
        <Seg
          value={cfg.system.updateChannel as "stable" | "beta"}
          onChange={async (v) => {
            await put("/system", { updateChannel: v });
            await refreshConfig();
          }}
          options={[
            ["stable", "Stabil"],
            ["beta", "Beta"],
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
          label="Automatisch nach Updates suchen"
        />
        <span>
          Nach Updates suchen <span class="muted small">(fragt die Release-Liste auf GitHub ab, sendet keine Gerätekennung)</span>
        </span>
      </label>
      <div class="row">
        <Button onClick={async () => setSt(await post("/update/check"))}>
          <RefreshCcw size={15} /> Jetzt prüfen
        </Button>
        <Button variant="ghost" onClick={async () => setLog(await (await fetch("/api/v1/changelog")).text())}>
          Changelog
        </Button>
        {st?.lastCheck ? <span class="faint small">zuletzt geprüft {dateTime(st.lastCheck)}</span> : null}
      </div>
      {av && (
        <div class="card flat stack-sm">
          <div class="row-between">
            <strong>Neu: Version {av.version}</strong>
            <Pill tone={av.channel === "beta" ? "warn" : "accent"}>{av.channel}</Pill>
          </div>
          {(["neu", "behoben", "beachten", "sicherheit"] as const).map((k) =>
            av.summary?.[k]?.length ? (
              <div>
                <div class="section-title">{{ neu: "Neu", behoben: "Behoben", beachten: "Bitte beachten", sicherheit: "Sicherheit" }[k]}</div>
                <ul style="margin:4px 0;padding-left:20px">
                  {av.summary[k].map((x: string) => (
                    <li>{x}</li>
                  ))}
                </ul>
              </div>
            ) : null,
          )}
          <p class="muted small">Installiert wird nur, wenn nichts dosiert. Rezepte, Einmesswerte und Einstellungen bleiben erhalten. Startet die neue Version nicht sauber, kehrt der Hub selbst zur alten zurück.</p>
          <div class="row">
            <Button variant="primary" onClick={async () => setMsg((await post("/update/install", { version: av.version })).message)}>
              Installieren
            </Button>
          </div>
          {msg && <Banner>{msg}</Banner>}
        </div>
      )}
      {log !== null && (
        <Modal wide title="Changelog" onClose={() => setLog(null)}>
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
    ? `${ISSUE_URL}?labels=fehler&title=${encodeURIComponent(`[${diag.reportId}] ${text.slice(0, 60) || "Problem"}`)}&body=${encodeURIComponent(
        `**Beschreibung**\n${text}\n\n**Version:** ${diag.info.version}\n**Plattform:** ${diag.info.platform.kind}\n**Vorgang:** ${diag.reportId}\n\n(Diagnosepaket bitte nur auf Nachfrage teilen – Issues sind öffentlich.)`,
      )}`
    : "";
  return (
    <div class="stack">
      <p class="muted">Beschreibe kurz, was passiert ist. Der Hub erstellt ein Diagnosepaket mit Version, Einstellungen, Zuständen und den Ereignissen der letzten 72 Stunden – ohne Passwort, Sitzungen und WLAN-Zugang. Du siehst vorher, was drin ist.</p>
      <Field label="Was ist passiert?">
        <textarea class="input" rows={3} value={text} onInput={(e) => setText((e.target as HTMLTextAreaElement).value)} placeholder="z. B. Mischlauf hielt bei Teil B an" />
      </Field>
      <div class="row">
        <Button onClick={async () => setDiag(await get("/diagnostics"))}>
          <Bug size={15} /> Diagnosepaket erstellen
        </Button>
      </div>
      {diag && (
        <div class="stack-sm">
          <div class="row">
            <Pill tone="accent">Vorgang {diag.reportId}</Pill>
            <span class="faint small">Nicht enthalten: {diag.redacted.join(", ")}</span>
          </div>
          <details>
            <summary>Inhalt ansehen</summary>
            <pre class="code">{JSON.stringify(diag, null, 2).slice(0, 20000)}</pre>
          </details>
          <div class="row">
            <Button variant="primary" onClick={download}>
              <Download size={15} /> Herunterladen
            </Button>
            <a class="btn" href={issue} target="_blank" rel="noopener noreferrer">
              Öffentlich auf GitHub melden
            </a>
          </div>
          <Banner tone="warn">GitHub-Issues sind öffentlich. Das Diagnosepaket nicht dort anhängen, nur die Vorgangsnummer nennen.</Banner>
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
  const setThemeAll = (t: "system" | "light" | "dark") => {
    setTheme(t);
    if (t === "system") delete document.documentElement.dataset.theme;
    else document.documentElement.dataset.theme = t;
    try {
      if (t === "system") localStorage.removeItem("gc-theme");
      else localStorage.setItem("gc-theme", t);
    } catch {
      /* ohne Speicher */
    }
  };
  return (
    <div class="stack">
      <div class="grid-2">
        <Card title="System" icon={<Server size={18} />}>
          <div class="stack">
            <div class="form-grid">
              <Field label="Name des Hubs">
                <input class="input" value={name} onInput={(e) => setName((e.target as HTMLInputElement).value)} />
              </Field>
              <Field label="Zeitzone">
                <input class="input" value={tz} onInput={(e) => setTz((e.target as HTMLInputElement).value)} />
              </Field>
              <Field label="Grenze je Handgabe" hint="Schutz gegen Tippfehler (Quelle: RAT-039)">
                <NumberInput value={hand} onValue={setHand} unit="ml" />
              </Field>
            </div>
            <div>
              <Button
                variant="primary"
                onClick={async () => {
                  await put("/system", { name, timezone: tz, handDoseMaxMl: hand });
                  await refreshConfig();
                  toast("Gespeichert");
                }}
              >
                Speichern
              </Button>
            </div>
          </div>
        </Card>
        <Card title="Zugang" icon={<KeyRound size={18} />}>
          <div class="stack">
            <div class="form-grid">
              <Field label="Altes Passwort">
                <input class="input" type="password" value={oldPw} onInput={(e) => setOldPw((e.target as HTMLInputElement).value)} autoComplete="current-password" />
              </Field>
              <Field label="Neues Passwort" hint="Mindestens 8 Zeichen">
                <input class="input" type="password" value={newPw} onInput={(e) => setNewPw((e.target as HTMLInputElement).value)} autoComplete="new-password" />
              </Field>
            </div>
            <div class="row">
              <Button
                disabled={!oldPw || newPw.length < 8}
                onClick={async () => {
                  await put("/auth/password", { old: oldPw, new: newPw });
                  toast("Passwort geändert – bitte neu anmelden");
                  logoutLocal();
                }}
              >
                Passwort ändern
              </Button>
              <Button
                variant="ghost"
                onClick={async () => {
                  await post("/auth/logout");
                  logoutLocal();
                }}
              >
                Abmelden
              </Button>
            </div>
            <p class="faint small">Nach 5 Fehlversuchen sperrt der Hub die Anmeldung kurz. Passwörter liegen nur als gesalzener Hash im Hub.</p>
          </div>
        </Card>
      </div>
      <div class="grid-2">
        <Card title="Updates" icon={<RefreshCcw size={18} />}>
          <Updates />
        </Card>
        <Card title="Problem melden" icon={<Bug size={18} />}>
          <Report />
        </Card>
      </div>
      <div class="grid-2">
        <Card title="Daten" icon={<FileJson size={18} />}>
          <div class="stack-sm">
            <p class="muted">Sicherung der Einstellungen (Geräte, Rezepte, Kanister, Kalibrierungen) als Datei. Der Verlauf lässt sich als CSV exportieren.</p>
            <div class="row">
              <a class="btn" href="/api/v1/config/export" download>
                <Download size={15} /> Einstellungen sichern
              </a>
              <label class="btn">
                <Upload size={15} /> Einstellungen laden
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
                      toast("Einstellungen geladen");
                    } catch (x: any) {
                      toast(x.message ?? String(x), "error");
                    }
                  }}
                />
              </label>
              <a class="btn" href="/api/v1/export.csv" download>
                <Download size={15} /> Verlauf (CSV, 7 Tage)
              </a>
            </div>
          </div>
        </Card>
        <Card title="Darstellung und Info" icon={<Info size={18} />}>
          <div class="stack-sm">
            <div class="row-between">
              <span class="row">
                <Moon size={16} /> Farbschema
              </span>
              <Seg value={theme} onChange={setThemeAll} options={[["system", "System"], ["light", "Hell"], ["dark", "Dunkel"]]} />
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
                <tr><td class="muted">Software</td><td>{info.value?.version}</td></tr>
                <tr><td class="muted">API</td><td>v{info.value?.api}</td></tr>
                <tr><td class="muted">Katalog</td><td>{info.value?.catalogVersion}</td></tr>
                <tr><td class="muted">Konfiguration</td><td>Schema {cfg.schemaVersion}, Revision {num(cfg.revision, 0)}</td></tr>
                <tr><td class="muted">Plattform</td><td>{simulated.value ? "Simulator (digitaler Zwilling)" : info.value?.platform.kind}</td></tr>
              </tbody>
            </table>
            <p class="faint small">Offene Bausteine: Preact, @preact/signals, uPlot, lucide (MIT/ISC); im Hub nlohmann/json (MIT). Lizenzliste und SBOM liegen jedem Release bei.</p>
          </div>
        </Card>
      </div>
    </div>
  );
}
