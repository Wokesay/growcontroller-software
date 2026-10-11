// SPDX-License-Identifier: AGPL-3.0-or-later
// Onboarding of the Home Assistant app (SD-037): basics, one sensor per
// measurement, what the app is to do per area, done. It opens after the first
// sign-in, can be skipped and is opened again from Settings. The trial
// switches nothing, so the choice per area is only recorded, and the step
// says so.
import { useEffect, useRef, useState } from "preact/hooks";
import { Compass } from "lucide-preact";
import { CONTROL_AREAS, post, put, type ControlArea, type ControlMode } from "../api";
import { HaRoles, ROLES, roleLabel } from "../ha";
import { lang, setLang, t, type Lang, type TextKey } from "../i18n";
import { binding, config, refreshConfig, refreshState, state, toast } from "../store";
import { currentTheme, setTheme, type Theme } from "../theme";
import { Banner, Button, Field, Pill, Seg, navigate, route } from "../ui";
import { ControlChoice } from "./areas";
import { Page, StepNav } from "./setup";

const STEPS: TextKey[] = ["welcome.step.basics", "welcome.step.sensors", "welcome.step.control", "welcome.step.done"];
const DONE = STEPS.length - 1;
const BASE = "/willkommen";

/** Done or skipped: the app does not open this by itself again. */
async function finish(skipped = false) {
  if (!state.value?.setupDone) {
    await post("/setup/complete");
    await refreshConfig();
    await refreshState();
  }
  if (skipped) toast(t("welcome.skipped"), "info");
  navigate("/");
}

function StepBasics(p: { step: number; next: () => void }) {
  const cfg = config.value!;
  const [name, setName] = useState(cfg.system.name);
  const [theme, setThemeState] = useState<Theme>(currentTheme());
  // The browser's zone on the first run; opened again later, the one set (it may come from Settings)
  const tz = state.value!.setupDone ? cfg.system.timezone : Intl.DateTimeFormat().resolvedOptions().timeZone || "Europe/Berlin";
  return (
    <Page
      foot={{
        step: p.step,
        base: BASE,
        total: STEPS.length,
        onNext: async () => {
          await put("/system", { name: name.trim() || cfg.system.name, timezone: tz, language: lang.value });
          await refreshConfig();
          p.next();
        },
      }}
    >
      <h2 tabIndex={-1}>{t("welcome.basics.h")}</h2>
      <p class="muted">{t("welcome.basics.intro")}</p>
      <div class="form-grid">
        <Field label={t("welcome.basics.name")} hint={t("welcome.basics.nameHint")}>
          <input class="input" name="hubname" value={name} onInput={(e) => setName((e.target as HTMLInputElement).value)} />
        </Field>
        <Field label={t("setup.start.tz")} hint={state.value!.setupDone ? undefined : t("setup.start.tzHint")}>
          <input class="input" value={tz} disabled />
        </Field>
        <div class="field">
          <span>{t("common.language")}</span>
          <Seg<Lang> value={lang.value} onChange={setLang} options={[["de", "Deutsch"], ["en", "English"]]} />
        </div>
        <div class="field">
          <span>{t("settings.theme")}</span>
          <Seg<Theme>
            value={theme}
            onChange={(v) => {
              setThemeState(v);
              setTheme(v);
            }}
            options={[["system", t("settings.themeSystem")], ["light", t("settings.themeLight")], ["dark", t("settings.themeDark")]]}
          />
        </div>
      </div>
    </Page>
  );
}

function StepSensors(p: { step: number; next: () => void }) {
  return (
    <Page foot={{ step: p.step, base: BASE, total: STEPS.length, onNext: p.next }}>
      <h2 tabIndex={-1}>{t("welcome.sensors.h")}</h2>
      <HaRoles />
    </Page>
  );
}

function StepControl(p: { step: number; next: () => void }) {
  const saved = config.value!.system.control ?? {};
  // Nothing chosen yet: outside, which is how it is while the app switches nothing
  const [choice, setChoice] = useState<Record<ControlArea, ControlMode>>(
    () => Object.fromEntries(CONTROL_AREAS.map((a) => [a, saved[a] ?? "outside"])) as Record<ControlArea, ControlMode>,
  );
  return (
    <Page
      foot={{
        step: p.step,
        base: BASE,
        total: STEPS.length,
        onNext: async () => {
          await put("/system", { control: choice });
          await refreshConfig();
          p.next();
        },
      }}
    >
      <h2 tabIndex={-1}>{t("welcome.control.h")}</h2>
      <p class="muted">{t("welcome.control.intro")}</p>
      <Banner>{t("welcome.control.notYet")}</Banner>
      <div class="list">
        {CONTROL_AREAS.map((a) => (
          <div class="item ctl-choice" data-testid={`control-${a}`}>
            <span class="grow">
              <strong id={`ctl-${a}`}>{t(`control.${a}`)}</strong>
              <div class="muted small" id={`ctl-${a}-what`}>
                {t(`control.${a}.what`)}
              </div>
            </span>
            <ControlChoice area={a} value={choice[a]} onChange={(m) => setChoice({ ...choice, [a]: m })} labelledBy={`ctl-${a}`} describedBy={`ctl-${a}-what`} />
          </div>
        ))}
      </div>
    </Page>
  );
}

function StepDone(p: { step: number }) {
  const control = config.value!.system.control ?? {};
  const withSensor = ROLES.filter(([role]) => binding(role)).map(([role]) => roleLabel(role));
  return (
    <Page foot={{ step: p.step, base: BASE, total: STEPS.length, onNext: () => finish(), nextLabel: t("welcome.done.toApp") }}>
      <h2 tabIndex={-1}>{t("welcome.done.h")}</h2>
      <p data-testid="welcome-sensors">{withSensor.length ? t("welcome.done.sensors", { list: withSensor.join(", ") }) : t("welcome.done.noSensor")}</p>
      <div class="list">
        {CONTROL_AREAS.map((a) => {
          const m = control[a];
          return (
            <div class="item" data-testid={`welcome-control-${a}`}>
              <span class="grow">
                <strong>{t(`control.${a}`)}</strong>
              </span>
              <Pill tone={m ? "neutral" : "warn"}>{m ? t(`control.mode.${m}`) : t("control.mode.none")}</Pill>
            </div>
          );
        })}
      </div>
      {CONTROL_AREAS.some((a) => control[a] === "auto") && <Banner>{t("welcome.done.autoNote")}</Banner>}
      <p class="muted small">{t("welcome.done.later")}</p>
    </Page>
  );
}

export function Welcome() {
  const st = state.value!;
  const step = Math.max(0, Math.min(DONE, Number(route.value.query.s ?? 0) || 0));
  const go = (s: number) => navigate(`${BASE}?s=${s}`);
  const next = () => go(step + 1);
  // On a step change, the new step's heading takes the focus, so a screen reader says where one is
  const shown = useRef(step);
  useEffect(() => {
    if (shown.current === step) return;
    shown.current = step;
    document.querySelector<HTMLElement>(".wizard-body h2")?.focus();
  }, [step]);
  return (
    <div class="wizard-page">
      <div class="wizard">
        <div class="row-between">
          <div class="row">
            <div class="brand-mark">
              <Compass size={18} />
            </div>
            <h1 class="wizard-title">{t("welcome.title")}</h1>
          </div>
          {st.setupDone ? (
            <a class="btn sm ghost" href="#/">
              {t("setup.toApp")}
            </a>
          ) : (
            step !== DONE && (
              <Button size="sm" variant="ghost" onClick={() => finish(true)} data-testid="welcome-skip">
                {t("common.skip")}
              </Button>
            )
          )}
        </div>
        <StepNav label={t("welcome.title")} steps={STEPS} step={step} go={go} />
        {step === 0 && <StepBasics step={step} next={next} />}
        {step === 1 && <StepSensors step={step} next={next} />}
        {step === 2 && <StepControl step={step} next={next} />}
        {step === DONE && <StepDone step={step} />}
      </div>
    </div>
  );
}
