// SPDX-License-Identifier: AGPL-3.0-or-later
// Gerüst: Navigation, Kopfzeile mit Not-Halt, Seitenwahl, Anmeldung, Einrichtung.
import type { ComponentChildren } from "preact";
import { useState } from "preact/hooks";
import {
  Beaker, Cpu, Droplets, FlaskConical, Gauge, History, LayoutDashboard, MoreHorizontal, OctagonX, Settings, SlidersHorizontal, Sprout, Sun, Thermometer, Waves, Wrench,
} from "lucide-preact";
import { post } from "./api";
import { msg, t, type TextKey } from "./i18n";
import { authed, config, info, live, refreshState, simulated, state, toast } from "./store";
import { ErrorBoundary, Modal, Pill, Toasts, navigate, route } from "./ui";
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
import { ClimatePage, IrrigationPage, LightPage } from "./pages/areas";

type NavItem = { path: string; label: TextKey; icon: ComponentChildren; mobile?: boolean };
const NAV: NavItem[] = [
  { path: "/", label: "nav.overview", icon: <LayoutDashboard size={19} />, mobile: true },
  { path: "/mischen", label: "nav.mix", icon: <Beaker size={19} />, mobile: true },
  { path: "/tank", label: "nav.tank", icon: <Droplets size={19} />, mobile: true },
  { path: "/klima", label: "nav.climate", icon: <Thermometer size={19} /> },
  { path: "/licht", label: "nav.light", icon: <Sun size={19} /> },
  { path: "/bewaesserung", label: "nav.irrigation", icon: <Waves size={19} /> },
  { path: "/verlauf", label: "nav.history", icon: <History size={19} />, mobile: true },
  { path: "/rezepte", label: "nav.recipes", icon: <FlaskConical size={19} /> },
  { path: "/geraete", label: "nav.devices", icon: <Cpu size={19} /> },
  { path: "/funktionen", label: "nav.functions", icon: <SlidersHorizontal size={19} /> },
  { path: "/einstellungen", label: "nav.settings", icon: <Settings size={19} /> },
];

const PAGES: Record<string, { title: TextKey; el: () => ComponentChildren }> = {
  "/": { title: "nav.overview", el: () => <Overview /> },
  "/mischen": { title: "nav.mix", el: () => <MixPage /> },
  "/tank": { title: "nav.tank", el: () => <TankPage /> },
  "/klima": { title: "nav.climate", el: () => <ClimatePage /> },
  "/licht": { title: "nav.light", el: () => <LightPage /> },
  "/bewaesserung": { title: "nav.irrigation", el: () => <IrrigationPage /> },
  "/verlauf": { title: "nav.history", el: () => <HistoryPage /> },
  "/rezepte": { title: "nav.recipes", el: () => <RecipesPage /> },
  "/geraete": { title: "nav.devices", el: () => <DevicesPage /> },
  "/funktionen": { title: "nav.functions", el: () => <FunctionsPage /> },
  "/einstellungen": { title: "nav.settings", el: () => <SettingsPage /> },
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
          toast(t("shell.resumed"));
        }}
      >
        {t("shell.resume")}
      </button>
    );
  return (
    <>
      <button class="stop-btn" onClick={() => setAsk(true)} title={t("shell.stopTitle")}>
        <OctagonX size={18} /> <span class="hide-sm">{t("common.stop")}</span>
      </button>
      {ask && (
        <Modal
          title={t("shell.stopAsk")}
          onClose={() => setAsk(false)}
          footer={
            <>
              <button class="btn" onClick={() => setAsk(false)}>
                {t("common.cancel")}
              </button>
              <button
                class="btn danger"
                onClick={async () => {
                  await post("/stop");
                  setAsk(false);
                  await refreshState();
                  toast(t("shell.stopped"), "info");
                }}
              >
                {t("shell.stopAll")}
              </button>
            </>
          }
        >
          <p>{t("shell.stopText")}</p>
          <p class="muted small">{t("shell.stopNote")}</p>
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
      {simulated.value && <span class="sim-tag">{t("common.simulator")}</span>}
      {l === "live" ? (
        <Pill tone="ok" dot pulse title={t("shell.liveTitle")}>
          {t("common.live")}
        </Pill>
      ) : l === "connecting" ? (
        <Pill tone="neutral" dot>
          {t("common.connecting")}
        </Pill>
      ) : (
        <Pill tone="bad" dot title={t("shell.offlineTitle")}>
          {t("common.offline")}
        </Pill>
      )}
      {wd && (
        <a href="#/tank" style="text-decoration:none">
          <Pill tone={wd.stale ? "bad" : wd.overall === "problem" ? "bad" : wd.overall === "ok" ? "ok" : "neutral"}>
            {wd.stale ? t("shell.watchdogStale") : msg(wd.headline)}
          </Pill>
        </a>
      )}
    </div>
  );
}

function MoreSheet(p: { onClose: () => void }) {
  return (
    <Modal title={t("common.more")} onClose={p.onClose}>
      <nav class="nav stack-sm">
        {NAV.filter((n) => !n.mobile).map((n) => (
          <a href={`#${n.path}`} onClick={p.onClose}>
            {n.icon}
            {t(n.label)}
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
        <p class="muted">{t("shell.connecting")}</p>
      </div>
    );
  if (!authed.value) return <Login />;
  if (!state.value || !config.value)
    return (
      <div class="login">
        <p class="muted">{t("shell.loading")}</p>
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
            <small>{config.value.system.name !== "growcontroller" ? config.value.system.name : t("app.tagline")}</small>
          </div>
        </div>
        <nav class="nav">
          {NAV.slice(0, 6).map((n) => (
            <a href={`#${n.path}`} class={r.path === n.path ? "active" : ""}>
              {n.icon}
              {t(n.label)}
            </a>
          ))}
          <div class="sep" />
          {NAV.slice(6).map((n) => (
            <a href={`#${n.path}`} class={r.path === n.path ? "active" : ""}>
              {n.icon}
              {t(n.label)}
            </a>
          ))}
        </nav>
        <div class="side-foot">
          <a href="#/einrichtung" class="row" style="gap:8px">
            <Wrench size={15} /> {t("nav.setup")}
          </a>
          <span>
            <Gauge size={13} /> {t("common.version", { v: info.value?.version })}
          </span>
        </div>
      </aside>
      <div class="main">
        <header class="topbar">
          <h1>{t(page.title)}</h1>
          <div class="spacer" />
          <LiveBadge />
          <StopButton />
        </header>
        <main class="content">
          <ErrorBoundary key={r.path}>{page.el()}</ErrorBoundary>
        </main>
      </div>
      <nav class="bottom-nav">
        {NAV.filter((n) => n.mobile).map((n) => (
          <a href={`#${n.path}`} class={r.path === n.path ? "active" : ""}>
            {n.icon}
            {t(n.label).split(" ")[0]}
          </a>
        ))}
        <button onClick={() => setMore(true)}>
          <MoreHorizontal size={19} />
          {t("common.more")}
        </button>
      </nav>
      {more && <MoreSheet onClose={() => setMore(false)} />}
      {simulated.value && <SimPanel />}
      <Toasts />
    </div>
  );
}
