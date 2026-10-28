// undokeys.js
// Lifecast - Ctrl+Z and Ctrl+Y over the history (history.js).
//
// Ctrl+Z undoes, Ctrl+Y and Ctrl+Shift+Z redo: the same undo()/redo() the
// buttons call, so the busy lock (a race or sex change still settling) and
// the empty history answer the same way. Ctrl+Alt and Ctrl+Meta are not ours.
//
// ONE STEP PER PHYSICAL PRESS, on every backend. Meridian delivers some key
// presses twice, ~15 ms apart, as two whole presses (keyboard.js), and an
// undo that fires twice undoes a step nobody asked about (D130). Measured on
// Prisma (1.5 E4): one keydown per chord in 20 of 20, Ctrl reported, no
// doubled press. The doubled press is dropped here the way keyboard.js drops
// it, by key state and not by a time window alone: a keydown for a key that
// has not come up since its last keydown is the same press (a doubled press
// or auto-repeat that does not say so), however late. If a keyup is ever lost
// the key is forgotten after FORGET_MS without keydowns, so the shortcut
// cannot stay locked.
//
// Keys are read by `key`, never `code`: the Prisma page reports `code` empty
// (measured). A backend may hand the letter over as the control character
// Ctrl makes of it (U+001A for Z, U+0019 for Y) or only by keyCode, so those
// count too (1.5 E5, D283).
//
// Every Ctrl+Z / Ctrl+Y that reaches the page, and every Z / Y outside a text
// field, goes to the Lifecast.log as one "undo-key" line - what the page
// received and what it did with it - so a round on a real keyboard says
// where a press that did nothing stopped.
//
// In a text field the shortcut belongs to the field (name, search, a typed
// value): the history is not touched and the key goes on to the field.
"use strict";

(() => {
  // A key that has not come up and has not been pressed again for this long
  // is taken as up (a keyup the backend never delivered).
  const FORGET_MS = 1000;

  // letter -> time of the last keydown of a press that has not come up.
  const down = new Map();

  function isTextField(el) {
    if (!el) {
      return false;
    }
    if (el.isContentEditable || el.tagName === "TEXTAREA") {
      return true;
    }
    return el.tagName === "INPUT" && /^(text|search|number|)$/i.test(el.type || "");
  }

  function letterOf(event) {
    const key = typeof event.key === "string" ? event.key.toLowerCase() : "";
    if (key === "z" || key === "y") {
      return key;
    }
    if (key === "\u001a") {
      return "z";
    }
    if (key === "\u0019") {
      return "y";
    }
    if (key === "" || key === "unidentified") {
      return event.keyCode === 90 ? "z" : event.keyCode === 89 ? "y" : null;
    }
    return null;
  }

  function report(event, letter, what) {
    LifecastBridge.send("undo-key", {
      l: letter, what: what, key: event.key, code: event.code, kc: event.keyCode,
      ctrl: event.ctrlKey, sh: event.shiftKey, alt: event.altKey, rp: event.repeat,
    });
  }

  document.addEventListener("keydown", (event) => {
    const letter = letterOf(event);
    if (!letter) {
      return;
    }
    const field = isTextField(document.activeElement);
    if (!event.ctrlKey) {
      if (!field) {
        report(event, letter, "no-ctrl");
      }
      return;
    }
    if (event.altKey || event.metaKey) {
      report(event, letter, "alt-or-meta");
      return;
    }
    if (field) {
      report(event, letter, "text-field");
      return;
    }
    // Ours from here: the page's default (and the game's) never sees it.
    event.preventDefault();

    const now = performance.now();
    const last = down.get(letter);
    down.set(letter, now);
    if (event.repeat || (last !== undefined && now - last < FORGET_MS)) {
      report(event, letter, "same-press");
      return; // the same physical press again
    }
    const undo = letter === "z" && !event.shiftKey;
    report(event, letter, undo ? "undo" : "redo");
    if (undo) {
      LifecastHistory.undo();
    } else {
      LifecastHistory.redo();
    }
  }, true);

  document.addEventListener("keyup", (event) => {
    const letter = letterOf(event);
    if (letter) {
      down.delete(letter);
    }
  }, true);

  // Focus left the page (Alt+Tab, the other backend's window): no keyup will
  // come for what was down.
  window.addEventListener("blur", () => down.clear());
})();
