// SPDX-License-Identifier: AGPL-3.0-or-later
import { render } from "preact";
import "./styles.css";
import { App } from "./app";
import { boot } from "./store";
import { t } from "./i18n";

try {
  const t = localStorage.getItem("gc-theme");
  if (t === "light" || t === "dark") document.documentElement.dataset.theme = t;
} catch {
  /* ohne Speicher: Systemeinstellung */
}

render(<App />, document.getElementById("app")!);
boot().catch(() => {
  document.getElementById("app")!.innerHTML =
    `<div class="login"><p>${t("shell.noAnswer")}</p></div>`;
});
