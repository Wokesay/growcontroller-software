// SPDX-License-Identifier: AGPL-3.0-or-later
// Shell: navigation, header with the Stop, page choice, sign-in, setup.
import type { ComponentChildren } from "preact";
import { useEffect, useRef, useState } from "preact/hooks";
import {
  Beaker, Compass, Cpu, Droplets, Eye, FlaskConical, Gauge, History, LayoutDashboard, MoreHorizontal, OctagonX, Settings, SlidersHorizontal, Sprout, Sun, Thermometer, Waves, Wrench,
} from "lucide-preact";
import { ApiError, post } from "./api";
import { msg, t, type TextKey } from "./i18n";
import { authed, config, hasPlugs, info, lastUpdate, platformLost, readOnly, refreshState, simulated, stale, state, toast, toastError } from "./store";
import { Banner, ErrorBoundary, Modal, Pill, Toasts, navigate, route } from "./ui";
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
import { Welcome } from "./pages/welcome";
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

// Home Assistant's trial switches nothing, so a stop there would stop
// nothing; it says so instead (SD-040).
// compact: only the icon on a phone, where it shares the bar with "Clear stop"
function ReadOnlyTag(p: { compact?: boolean }) {
  const [open, setOpen] = useState(false);
  return (
    <>
      <button
        class="btn sm ro-tag"
        onClick={() => setOpen(true)}
        title={t("shell.readOnlyTitle")}
        aria-label={p.compact ? t("shell.readOnly") : undefined}
        aria-haspopup="dialog"
        data-testid="read-only-tag"
      >
        <Eye size={15} aria-hidden="true" /> {p.compact ? <span class="hide-sm">{t("shell.readOnly")}</span> : t("shell.readOnly")}
      </button>
      {open && (
        <Modal
          title={t("shell.readOnlyTitle")}
          onClose={() => setOpen(false)}
          footer={
            <button class="btn" onClick={() => setOpen(false)}>
              {t("common.gotIt")}
            </button>
          }
        >
          <p>{t("shell.readOnlyWhat")}</p>
          <p class="muted small">{t("shell.readOnlyWhy")}</p>
        </Modal>
      )}
    </>
  );
}

// No answer within this time counts as not confirmed: the dialog says so and offers
// to try again, instead of waiting without a word (fetch has no time limit of its own)
const STOP_TIMEOUT_MS = 5000;

function StopButton() {
  const [ask, setAsk] = useState(false);
  const [sending, setSending] = useState(false);
  const [failed, setFailed] = useState<string | null>(null);
  const retry = useRef<HTMLButtonElement>(null);
  const open = useRef(false);  // the dialog, for a Stop that fails after it was closed
  const st = state.value;
  // A Stop that arrived after all ends the dialog, so it does not come back after Resume
  useEffect(() => {
    if (st?.stopped) {
      open.current = false;
      setAsk(false);
      setFailed(null);
    }
  }, [st?.stopped]);
  // The confirm button was disabled while sending: give it the focus back to try again
  useEffect(() => {
    if (failed && !sending) retry.current?.focus();
  }, [failed, sending]);
  const resume = async (done: TextKey) => {
    try {
      await post("/resume");
      await refreshState();
      toast(t(done));
    } catch (e) {
      toastError(e);
    }
  };
  if (readOnly.value) {
    if (!st?.stopped) return <ReadOnlyTag />;
    // A stop from an earlier version stays clearable; nothing starts running
    return (
      <>
        <ReadOnlyTag compact />
        <button class="btn sm" onClick={() => resume("shell.stopCleared")}>
          {t("shell.clearStop")}
        </button>
      </>
    );
  }
  if (st?.stopped)
    return (
      <button class="btn sm" onClick={() => resume("shell.resumed")}>
        {t("shell.resume")}
      </button>
    );
  const close = () => {
    open.current = false;
    setAsk(false);
    setFailed(null);
  };
  const hint = () => t(hasPlugs.value ? "shell.stopHintPlugs" : "shell.stopHint");
  const stop = async () => {
    setSending(true);
    setFailed(null);
    try {
      await post("/stop", {}, STOP_TIMEOUT_MS);
    } catch (e) {
      // Never silent: the dialog stays open to try again, or, closed while sending, a
      // message says so. It may have arrived late: a new state with the Stop closes the dialog.
      const said = e instanceof ApiError ? t("shell.stopRefused", { reason: e.message }) : t("shell.stopFailed");
      const text = /[.!?…]$/.test(said) ? said : `${said}.`;  // the emergency hint follows as its own sentence
      if (open.current) setFailed(text);
      else toast(`${text} ${hint()}`, "error");
      void refreshState();
      return;
    } finally {
      setSending(false);
    }
    close();
    toast(t("shell.stopped"), "info");
    await refreshState();
  };
  return (
    <>
      <button
        class="stop-btn"
        onClick={() => {
          setFailed(null);  // a failure from a Stop cancelled while sending is not this one's
          open.current = true;
          setAsk(true);
        }}
        title={t("shell.stopTitle")}
      >
        <OctagonX size={18} /> <span class="hide-sm">{t("common.stop")}</span>
      </button>
      {ask && (
        <Modal
          title={t("shell.stopAsk")}
          onClose={close}
          footer={
            <>
              <button class="btn" onClick={close}>
                {t("common.cancel")}
              </button>
              <button ref={retry} class="btn danger" disabled={sending} onClick={stop} data-testid="stop-confirm">
                {sending ? t("shell.stopSending") : failed ? t("shell.stopRetry") : t("shell.stopAll")}
              </button>
            </>
          }
        >
          <p>{t("shell.stopText")}</p>
          <p class="muted small">{t("shell.stopNote")}</p>
          <div role="alert" data-testid="stop-failed">
            {failed && (
              <Banner tone="bad">
                {failed} {hint()}
              </Banner>
            )}
          </div>
        </Modal>
      )}
    </>
  );
}

// Home Assistant's page makes a new Ingress session when it loads; the app
// runs in its frame, on its origin
function reloadAll() {
  try {
    (window.top ?? window).location.reload();
  } catch {
    location.reload();
  }
}

function LiveBadge() {
  // By the age of the values, as the phone's bar: polls standing in for the stream are
  // current, an open stream that brings nothing is not
  const l = stale.value ? "offline" : lastUpdate.value > 0 ? "live" : "connecting";
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
  // A read-only hub has no dosing hardware to set up: it gets the short
  // onboarding of the Home Assistant app instead (SD-037)
  const firstRun = readOnly.value ? "/willkommen" : "/einrichtung";
  if (readOnly.value && r.path === "/einrichtung") navigate(firstRun);
  else if (!state.value.setupDone && r.path !== firstRun && !r.query.skip) navigate(firstRun);
  if (r.path === firstRun)
    return (
      <>
        {readOnly.value ? <Welcome /> : <SetupWizard />}
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
          {readOnly.value ? (  // the setup is for the hub's own dosing hardware
            <a href="#/willkommen" class="row" style="gap:8px">
              <Compass size={15} /> {t("welcome.title")}
            </a>
          ) : (
            <a href="#/einrichtung" class="row" style="gap:8px">
              <Wrench size={15} /> {t("nav.setup")}
            </a>
          )}
          <span>
            <Gauge size={13} /> {t("common.version", { v: info.value?.version })}
          </span>
        </div>
      </aside>
      <div class="main">
        <div class="top">
          <header class="topbar">
            <h1>{t(page.title)}</h1>
            <div class="spacer" />
            <LiveBadge />
            <StopButton />
          </header>
          {/* Phones hide the live badge; the live region is always there, so a reader announces the text */}
          {/* Behind Home Assistant, its lapsed session is the one cause: said at every width, instead of the offline bar */}
          <div role="alert">
            {platformLost.value && (
              <div class="lost-bar row-between" data-testid="platform-lost">
                <span>{t("shell.platformLost")}</span>
                <button class="btn sm" onClick={reloadAll} data-testid="platform-reload">
                  {t("common.reload")}
                </button>
              </div>
            )}
          </div>
          <div class="only-sm" role="status">
            {stale.value && !platformLost.value && (
              <div class="offline-bar" data-testid="offline-bar">
                {t("shell.offlineBar")}
              </div>
            )}
          </div>
        </div>
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
