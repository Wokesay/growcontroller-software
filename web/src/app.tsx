// Gerüst: Navigation, Kopfzeile mit Not-Halt, Seitenwahl, Anmeldung, Einrichtung.
import type { ComponentChildren } from "preact";
import { useState } from "preact/hooks";
import {
  Beaker, Cpu, Droplets, FlaskConical, Gauge, History, LayoutDashboard, MoreHorizontal, OctagonX, Settings, SlidersHorizontal, Sprout, Wrench,
} from "lucide-preact";
import { post } from "./api";
import { authed, config, info, live, refreshState, simulated, state, toast } from "./store";
import { Modal, Pill, Toasts, navigate, route } from "./ui";
import { Login } from "./pages/login";
import { Overview } from "./pages/overview";
import { MixPage } from "./pages/mix";
import { TankPage } from "./pages/tank";
import { HistoryPage } from "./pages/history";
import { RecipesPage } from "./pages/recipes";
import { DevicesPage } from "./pages/devices";
import { FunctionsPage } from "./pages/functions";
import { SettingsPage } from "./pages/settings";
import { SetupWizard } from "./pages/setup";
import { SimPanel } from "./pages/sim";

type NavItem = { path: string; label: string; icon: ComponentChildren; mobile?: boolean };
const NAV: NavItem[] = [
  { path: "/", label: "Übersicht", icon: <LayoutDashboard size={19} />, mobile: true },
  { path: "/mischen", label: "Mischen", icon: <Beaker size={19} />, mobile: true },
  { path: "/tank", label: "Tank & Regelung", icon: <Droplets size={19} />, mobile: true },
  { path: "/verlauf", label: "Verlauf", icon: <History size={19} />, mobile: true },
  { path: "/rezepte", label: "Rezepte & Kanister", icon: <FlaskConical size={19} /> },
  { path: "/geraete", label: "Geräte", icon: <Cpu size={19} /> },
  { path: "/funktionen", label: "Funktionen", icon: <SlidersHorizontal size={19} /> },
  { path: "/einstellungen", label: "Einstellungen", icon: <Settings size={19} /> },
];

const PAGES: Record<string, { title: string; el: () => ComponentChildren }> = {
  "/": { title: "Übersicht", el: () => <Overview /> },
  "/mischen": { title: "Mischen", el: () => <MixPage /> },
  "/tank": { title: "Tank & Regelung", el: () => <TankPage /> },
  "/verlauf": { title: "Verlauf", el: () => <HistoryPage /> },
  "/rezepte": { title: "Rezepte & Kanister", el: () => <RecipesPage /> },
  "/geraete": { title: "Geräte", el: () => <DevicesPage /> },
  "/funktionen": { title: "Funktionen", el: () => <FunctionsPage /> },
  "/einstellungen": { title: "Einstellungen", el: () => <SettingsPage /> },
};

function StopButton() {
  const [ask, setAsk] = useState(false);
  const st = state.value;
  if (st?.stopped)
    return (
      <button
        class="btn sm"
        onClick={async () => {
          await post("/resume");
          await refreshState();
          toast("Automatik fortgesetzt");
        }}
      >
        Fortsetzen
      </button>
    );
  return (
    <>
      <button class="stop-btn" onClick={() => setAsk(true)} title="Not-Halt: alle Pumpen und Ausgänge aus">
        <OctagonX size={18} /> <span class="hide-sm">STOPP</span>
      </button>
      {ask && (
        <Modal
          title="Not-Halt auslösen?"
          onClose={() => setAsk(false)}
          footer={
            <>
              <button class="btn" onClick={() => setAsk(false)}>
                Abbrechen
              </button>
              <button
                class="btn danger"
                onClick={async () => {
                  await post("/stop");
                  setAsk(false);
                  await refreshState();
                  toast("Not-Halt: alles aus", "info");
                }}
              >
                Alles stoppen
              </button>
            </>
          }
        >
          <p>Alle Pumpen und Ausgänge gehen sofort aus. Laufende Aufträge werden abgebrochen, die Automatik ruht, bis du „Fortsetzen“ wählst.</p>
          <p class="muted small">Messen und Sperren laufen weiter.</p>
        </Modal>
      )}
    </>
  );
}

function LiveBadge() {
  const l = live.value;
  const wd = state.value?.watchdog;
  return (
    <div class="row hide-sm">
      {simulated.value && <span class="sim-tag">Simulator</span>}
      {l === "live" ? (
        <Pill tone="ok" dot pulse title="Live-Verbindung zum Hub">
          live
        </Pill>
      ) : l === "connecting" ? (
        <Pill tone="neutral" dot>
          verbinde …
        </Pill>
      ) : (
        <Pill tone="bad" dot title="Keine Verbindung zum Hub – die Steuerung läuft auf dem Hub weiter">
          getrennt
        </Pill>
      )}
      {wd && (
        <a href="#/tank" style="text-decoration:none">
          <Pill tone={wd.stale ? "bad" : wd.overall === "problem" ? "bad" : wd.overall === "ok" ? "ok" : "neutral"}>
            {wd.stale ? "Überwachung ohne Bewertung" : wd.headline}
          </Pill>
        </a>
      )}
    </div>
  );
}

function MoreSheet(p: { onClose: () => void }) {
  return (
    <Modal title="Mehr" onClose={p.onClose}>
      <nav class="nav stack-sm">
        {NAV.filter((n) => !n.mobile).map((n) => (
          <a href={`#${n.path}`} onClick={p.onClose}>
            {n.icon}
            {n.label}
          </a>
        ))}
      </nav>
    </Modal>
  );
}

export function App() {
  const [more, setMore] = useState(false);
  if (authed.value === null)
    return (
      <div class="login">
        <p class="muted">Verbinde mit dem Hub …</p>
      </div>
    );
  if (!authed.value) return <Login />;
  if (!state.value || !config.value)
    return (
      <div class="login">
        <p class="muted">Lade …</p>
      </div>
    );
  const r = route.value;
  if (!state.value.setupDone && r.path !== "/einrichtung" && !r.query.skip) {
    navigate("/einrichtung");
  }
  if (r.path === "/einrichtung")
    return (
      <>
        <SetupWizard />
        {simulated.value && <SimPanel />}
        <Toasts />
      </>
    );
  const page = PAGES[r.path] ?? PAGES["/"];
  return (
    <div class="shell">
      <aside class="side">
        <div class="brand">
          <div class="brand-mark">
            <Sprout size={18} />
          </div>
          <div>
            growcontroller
            <small>{config.value.system.name !== "growcontroller" ? config.value.system.name : "Fertigation"}</small>
          </div>
        </div>
        <nav class="nav">
          {NAV.slice(0, 4).map((n) => (
            <a href={`#${n.path}`} class={r.path === n.path ? "active" : ""}>
              {n.icon}
              {n.label}
            </a>
          ))}
          <div class="sep" />
          {NAV.slice(4).map((n) => (
            <a href={`#${n.path}`} class={r.path === n.path ? "active" : ""}>
              {n.icon}
              {n.label}
            </a>
          ))}
        </nav>
        <div class="side-foot">
          <a href="#/einrichtung" class="row" style="gap:8px">
            <Wrench size={15} /> Einrichtung
          </a>
          <span>
            <Gauge size={13} /> Version {info.value?.version}
          </span>
        </div>
      </aside>
      <div class="main">
        <header class="topbar">
          <h1>{page.title}</h1>
          <div class="spacer" />
          <LiveBadge />
          <StopButton />
        </header>
        <main class="content">{page.el()}</main>
      </div>
      <nav class="bottom-nav">
        {NAV.filter((n) => n.mobile).map((n) => (
          <a href={`#${n.path}`} class={r.path === n.path ? "active" : ""}>
            {n.icon}
            {n.label.split(" ")[0]}
          </a>
        ))}
        <button onClick={() => setMore(true)}>
          <MoreHorizontal size={19} />
          Mehr
        </button>
      </nav>
      {more && <MoreSheet onClose={() => setMore(false)} />}
      {simulated.value && <SimPanel />}
      <Toasts />
    </div>
  );
}
