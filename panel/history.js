// history.js
// Lifecast - undo and redo over the values the player changed.
//
// An entry is one FINISHED gesture on one control: the value it held before
// the hand touched it, the value it ended on, and how to put either one
// back. Nothing else enters - not scrolling, not a section opened or
// closed, not what was typed into the search field. Those are where the
// player is looking, and a history that undoes them undoes the wrong thing.
//
// The history lives here and not in the DLL because a gesture only exists
// here: the DLL sees a stream of *-set messages and cannot tell the drag
// that ended from the one still under way. Undo is the same *-set the
// control itself would send, with the older value - so it inherits, without
// a line of its own, everything the normal path already does (RaceMenuSync,
// the redraw, the echo back).
//
// The history does NOT cross the panel closing. It could: the browser is
// created once and only shown and hidden (Panel.h), so this page and
// everything in it survives. That is exactly why the clearing is written
// out rather than left to happen - between one opening and the next the
// player can have changed race, sex or actor, and an entry made before that
// would push a value from another character onto this one. "visibility"
// is the DLL's own message, sent from Panel::SetVisible.
"use strict";

const LifecastHistory = (() => {
  // Entries kept. Well past a single sitting at the panel; the cap is only
  // here so a long session cannot grow the list without bound.
  const LIMIT = 100;
  const LOG_LIMIT = 200;

  // entries[0 .. at-1] are done; entries[at ..] were undone and can be
  // redone. A new entry throws the second half away - the ordinary rule:
  // once you edit after undoing, there is no future to return to.
  const entries = [];
  let at = 0;

  // An apply() sends through the control's own path, and that path is what
  // records. Without this a single undo would record itself as an edit.
  let applying = false;

  const log = [];
  const listeners = [];

  function note(a_what, a_extra) {
    const line = Object.assign(
      { t: Math.round(performance.now()), what: a_what, at: at, n: entries.length },
      a_extra || {}
    );
    log.push(line);
    if (log.length > LOG_LIMIT) {
      log.shift();
    }
    console.log("[history] " + JSON.stringify(line));
    return line;
  }

  function shown(a_entry, a_value) {
    return typeof a_entry.describe === "function" ? a_entry.describe(a_value) : a_value;
  }

  function changed() {
    listeners.forEach((fn) => {
      try {
        fn();
      } catch (e) {
        console.error("[history] listener threw", e);
      }
    });
  }

  // a_entry: { label, before, after, apply(value), describe(value)? }
  // before/after may be any value apply() understands - a number for a
  // slider, a portrait of the whole face for a race preset (F27); describe()
  // is what the log prints in place of a value too big to print.
  // Returns whether it became an entry. A gesture that ended where it
  // started is not one, and neither is a gesture on a control that never
  // received a value to start from.
  function record(a_entry) {
    if (applying) {
      return false;
    }
    if (!a_entry || typeof a_entry.apply !== "function") {
      return false;
    }
    if (a_entry.before === undefined || a_entry.before === null) {
      return false;
    }
    if (a_entry.before === a_entry.after) {
      return false;
    }
    entries.length = at;
    entries.push(a_entry);
    if (entries.length > LIMIT) {
      entries.shift();
    }
    at = entries.length;
    note("record", { label: a_entry.label, from: shown(a_entry, a_entry.before), to: shown(a_entry, a_entry.after) });
    changed();
    return true;
  }

  // An entry's apply() may return a promise (E31: a race or sex change is the
  // game's own and settles a moment later). While one is out, `busy` holds
  // the history still: nothing is undone or redone on top of a character
  // that has not finished changing. A promise that resolves to false, or
  // rejects, means the step did not land.
  let busy = false;

  // Returns null for an entry that applied at once, else a promise of whether
  // it landed.
  function run(a_entry, a_value, a_what) {
    applying = true;
    let result;
    try {
      result = a_entry.apply(a_value);
    } catch (e) {
      console.error("[history] apply threw", e);
    } finally {
      applying = false;
    }
    note(a_what, { label: a_entry.label, to: shown(a_entry, a_value) });
    if (!result || typeof result.then !== "function") {
      return null;
    }
    busy = true;
    return result
      .then(
        (ok) => ok !== false,
        (e) => {
          console.error("[history] apply rejected", e);
          return false;
        }
      )
      .then((ok) => {
        busy = false;
        note(a_what + "-settled", { label: a_entry.label, ok: ok });
        changed();
        return ok;
      });
  }

  function undo() {
    if (busy) {
      note("undo-busy");
      return false;
    }
    if (at === 0) {
      note("undo-empty");
      return false;
    }
    const entry = entries[at - 1];
    at -= 1;
    run(entry, entry.before, "undo");
    changed();
    return true;
  }

  function redo() {
    if (busy) {
      note("redo-busy");
      return false;
    }
    if (at >= entries.length) {
      note("redo-empty");
      return false;
    }
    const entry = entries[at];
    at += 1;
    run(entry, entry.after, "redo");
    changed();
    return true;
  }

  // Back to where the panel opened (F28, D192): every entry the history
  // holds, newest first, each through its own undo(). Nothing is thrown away,
  // so Redo walks the same way forward and a misclick costs nothing. An entry
  // that settles later (a race or sex change, E31) is waited for before the
  // next one starts, and the walk stops at one that did not land: what lies
  // below it was recorded on a character that is not there. Entries that
  // apply at once never wait, so the loop runs through them in one turn.
  async function undoAll() {
    if (busy) {
      note("undo-all-busy");
      return false;
    }
    if (at === 0) {
      note("undo-all-empty");
      return false;
    }
    const steps = at;
    while (at > 0) {
      const entry = entries[at - 1];
      at -= 1;
      const waiting = run(entry, entry.before, "undo");
      changed();
      if (waiting !== null && (await waiting) === false) {
        note("undo-all-stopped", { label: entry.label, left: at });
        return false;
      }
    }
    note("undo-all", { steps: steps });
    return true;
  }

  function clear(a_why) {
    if (entries.length === 0) {
      return;
    }
    entries.length = 0;
    at = 0;
    note("clear", { why: a_why });
    changed();
  }

  function wire() {
    const undoButton = document.getElementById("undo");
    const redoButton = document.getElementById("redo");
    const undoAllButton = document.getElementById("undoall");
    if (!undoButton || !redoButton || !undoAllButton) {
      return;
    }
    undoButton.addEventListener("click", () => undo());
    redoButton.addEventListener("click", () => redo());
    undoAllButton.addEventListener("click", () => undoAll());
    const paint = () => {
      undoButton.disabled = busy || at === 0;
      undoAllButton.disabled = busy || at === 0;
      undoAllButton.title = at === 0 ? "Nothing to undo" : "Undo everything since the panel opened";
      redoButton.disabled = busy || at >= entries.length;
      // The shortcut rides on the title, on every backend (1.5 E5, D283).
      undoButton.title = (at === 0 ? "Nothing to undo" : "Undo " + entries[at - 1].label) + " (Ctrl+Z)";
      redoButton.title = (at >= entries.length ? "Nothing to redo" : "Redo " + entries[at].label) + " (Ctrl+Y)";
    };
    listeners.push(paint);
    paint();

    // Prisma UI draws no title tooltip (measured, 1.5 E5): there the
    // shortcut goes on the label itself.
    LifecastBridge.whenAttached((attached) => {
      if (attached && LifecastBridge.backend() === "prisma") {
        undoButton.textContent = "Undo (Ctrl+Z)";
        redoButton.textContent = "Redo (Ctrl+Y)";
      }
    });
  }

  wire();

  // The panel was hidden: the switcher went back to RaceMenu, or the menu
  // closed. See the head of this file for why this is not optional.
  LifecastBridge.on("visibility", (data) => {
    if (data && data.visible === false) {
      clear("the panel was hidden");
    }
  });

  return {
    record: record,
    undo: undo,
    redo: redo,
    undoAll: undoAll,
    clear: clear,
    // For a measurement from outside the page: how many entries can still
    // be undone, how many exist, and what happened in order.
    depth: () => at,
    size: () => entries.length,
    log: () => log.slice(),
    onChange: (fn) => listeners.push(fn),
  };
})();
