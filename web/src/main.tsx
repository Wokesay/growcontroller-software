// SPDX-License-Identifier: AGPL-3.0-or-later
import { render } from "preact";
import "./styles.css";
import { App } from "./app";
import { boot } from "./store";

try {
  const t = localStorage.getItem("gc-theme");
  if (t === "light" || t === "dark") document.documentElement.dataset.theme = t;
} catch {
  /* ohne Speicher: Systemeinstellung */
}

render(<App />, document.getElementById("app")!);
boot().catch(() => {
  document.getElementById("app")!.innerHTML =
    '<div class="login"><p>Der Hub antwortet nicht. Ist er eingeschaltet und im selben Netz?</p></div>';
});
