// SPDX-License-Identifier: AGPL-3.0-or-later
import { render } from "preact";
import "./styles.css";
import { App } from "./app";
import { boot } from "./store";
import { t } from "./i18n";

try {
  const theme = localStorage.getItem("gc-theme");
  if (theme === "light" || theme === "dark") document.documentElement.dataset.theme = theme;
} catch {
  /* no storage: follow the system setting */
}

render(<App />, document.getElementById("app")!);
boot().catch(() => {
  // As text, not markup: no translation can ever become HTML.
  const box = document.createElement("div");
  box.className = "login";
  box.appendChild(document.createElement("p")).textContent = t("shell.noAnswer");
  document.getElementById("app")!.replaceChildren(box);
});
