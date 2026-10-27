// panel.js
// Lifecast - the panel frame: says hello to the DLL and shows whether the
// bridge is up.
"use strict";

(() => {
  // Bumped for the Fase 6.5 round: the DLL logs this string on "hello", and
  // the whole of 6.5 changed the page without changing the number - a log
  // that says 0.9.0 could not tell which page had run.
  const PAGE_VERSION = "0.9.4";

  // How long the page waits for the DLL's answer to "hello" before saying
  // so. Only the status line depends on it; nothing waits on it.
  const HELLO_TIMEOUT_MS = 3000;

  // How big the panel is drawn. The bounds are a guard, not a taste: below
  // kMin the step buttons stop being hittable, and above kMax the panel eats
  // the character it exists to show (2.5 is the ceiling the player asked for on
  // a 4K screen, where a 1.6 panel stayed small). Where inside the range the player lands
  // is theirs, and nothing here picks a default other than "as authored".
  const SCALE_MIN = 0.8;
  const SCALE_MAX = 2.5;
  const SCALE_STEP = 0.1;

  const statusEl = document.getElementById("bridge-status");
  const versionsEl = document.getElementById("versions");
  const toRaceMenuEl = document.getElementById("to-racemenu");
  const settingsEl = document.getElementById("settings-button");

  let answered = false;

  // The status lives in Settings > Debug (D133). A working bridge is the
  // normal case and tells the player nothing; a broken one (a version
  // mismatch, no bridge) also marks the Settings button, so it is not hidden
  // behind a click nobody makes.
  function setStatus(text, kind) {
    statusEl.textContent = text;
    statusEl.className = "status status-" + kind;
    if (settingsEl) {
      settingsEl.classList.toggle("has-trouble", kind === "bad");
      settingsEl.title = kind === "bad" ? "Bridge: " + text : "";
    }
  }

  LifecastBridge.on("hello", (data) => {
    answered = true;
    versionsEl.textContent = "page " + PAGE_VERSION + " / dll " + data.dll + " / protocol " + data.protocol;
    if (data.compatible) {
      setStatus("connected", "ok");
    } else {
      setStatus("version mismatch", "bad");
    }
  });

  LifecastBridge.on("error", (data) => {
    console.warn("[panel] the DLL reported an error", data);
  });

  // Back to RaceMenu from inside the page. The DLL hides this page, which
  // also releases the backend's focus, and shows RaceMenu's film again.
  function toRaceMenu() {
    if (!LifecastBridge.send("show-racemenu")) {
      setStatus("no bridge", "bad");
    }
  }

  // The button is the page's only way back. Esc is deliberately NOT one: it
  // only ever went one way (nothing brings the Lifecast back on Esc), and a
  // key that means "leave" here would fight any text field the page grows.
  // F4 is the key, and it works in both directions.
  toRaceMenuEl.addEventListener("click", toRaceMenu);

  // ----- the panel's size ---------------------------------------------------
  const scaleValueEl = document.getElementById("scale-value");
  let scale = 1;

  function showScale() {
    document.documentElement.style.setProperty("--panel-scale", String(scale));
    scaleValueEl.textContent = Math.round(scale * 100) + "%";
    document.getElementById("scale-down").disabled = scale <= SCALE_MIN + 0.001;
    document.getElementById("scale-up").disabled = scale >= SCALE_MAX - 0.001;
  }

  function stepScale(a_dir) {
    // Rounded to the step so repeated presses do not drift, the same way a
    // slider row steps.
    const next = Math.round((scale + a_dir * SCALE_STEP) / SCALE_STEP) * SCALE_STEP;
    scale = Math.min(SCALE_MAX, Math.max(SCALE_MIN, next));
    showScale();
  }

  document.getElementById("scale-down").addEventListener("click", () => {
    stepScale(-1);
    LifecastSettings.set("panelScale", scale);
  });
  document.getElementById("scale-up").addEventListener("click", () => {
    stepScale(1);
    LifecastSettings.set("panelScale", scale);
  });
  showScale();

  // The saved scale arrives after this page has already drawn at the
  // default (1) - settings.js's own doc explains why nothing here can wait
  // for it. Only a number inside the authored bounds is taken; anything
  // else leaves the default in place, same as presets.js's sort order.
  LifecastSettings.onChange(() => {
    const saved = LifecastSettings.get("panelScale");
    if (typeof saved === "number" && saved >= SCALE_MIN && saved <= SCALE_MAX) {
      scale = saved;
      showScale();
    }
  });

  LifecastBridge.whenAttached((ok) => {
    if (!ok) {
      setStatus("no bridge", "bad");
      return;
    }
    LifecastBridge.send("hello", { page: PAGE_VERSION, protocol: LifecastBridge.PROTOCOL });
    setTimeout(() => {
      if (!answered) {
        setStatus("no answer", "warn");
      }
    }, HELLO_TIMEOUT_MS);
  });
})();
