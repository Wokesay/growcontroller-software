// SPDX-License-Identifier: AGPL-3.0-or-later
import { render } from "preact";
import "./styles.css";
import { App } from "./app";
import { boot, errorText } from "./store";
import { loadTheme } from "./theme";

loadTheme();

render(<App />, document.getElementById("app")!);
boot().catch((e) => {
  // As text, not markup: no translation can ever become HTML.
  const box = document.createElement("div");
  box.className = "login";
  box.appendChild(document.createElement("p")).textContent = errorText(e);
  document.getElementById("app")!.replaceChildren(box);
});
