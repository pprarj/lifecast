// pad.js
// Lifecast - the controller, while the panel has the focus.
//
// The DLL sends every controller event as a "pad" message (Panel/Pad.h); this
// file makes the controller do what the mouse does, over a focus ring:
//
//   D-pad, left stick   the ring moves to the nearest visible control in that
//                       direction, by where things are on the screen
//   left / right        on a slider, one press of its < or > button
//   A                   the click (on a text field: the click that opens typing)
//   B                   back: leaves a text field, closes an open list or
//                       colour picker, clears the search
//   LB / RB             the previous / next category of the belt, wrapping
//   right stick         turns the character, as the drag on the stage does
//   LT                  the stage's zoom, as its Zoom button (+ in the free camera)
//   RT                  the stage's zoom out: its - in the free camera, the same
//                       RaceMenu zoom as LT in standard
//
// In a list of rows that are not buttons (the OBody list) up and down walk the
// rows one by one and A is the row's click.
//
// Every action goes through the path the mouse already uses - the button's
// own click, the slider's own < >, the stage's drag - so nothing here writes
// a value of its own. The ring is a class (.pad-focus), not the browser's
// focus: a button that took the real focus would keep it after the mouse
// came back, and a text field that took it would start typing. The ring goes
// away at the first mouse press or key, and comes back where it was at the
// next controller press.
"use strict";

const LifecastPad = (() => {
  const STICK_ON = 0.5;           // a left stick past this points somewhere
  const STICK_FIRST_MS = 380;     // a held left stick moves again after this,
  const STICK_EVERY_MS = 160;     // and then every this
  const TURN_PX_PER_S = 500;      // the right stick at full tilt, in the drag's own pixels a second

  // .pad-row: a list's row that is clickable but is not a button (the OBody list).
  const TARGETS = "button, input, select, textarea, .slider, .pad-row";
  const TEXT = "input:not([type]), input[type=text], input[type=search], textarea";

  let cur = null;
  let shown = false;
  let presses = 0;
  let last = "";
  const stick = { dir: null, timer: null };
  const turn = { x: 0, frame: null, at: 0 };

  function hidden(a_el) {
    if (a_el.disabled || a_el.closest("[hidden]")) {
      return true;
    }
    const r = a_el.getBoundingClientRect();
    if (r.width < 2 || r.height < 2) {
      return true;
    }
    const st = getComputedStyle(a_el);
    return st.visibility === "hidden" || st.pointerEvents === "none";
  }

  // What the ring can stop on: every visible control, a slider as one stop
  // (its < > and its range are the slider's, not stops of their own).
  function targets() {
    const out = [];
    for (const el of document.querySelectorAll(TARGETS)) {
      if (el.matches("input[type=range]")) {
        continue;
      }
      // A list's entries live inside the row that owns it, but they are what
      // the controller walks while the list is open.
      if (!el.classList.contains("slider") && el.closest(".slider") && !el.matches(TEXT) && !el.closest(".listpop")) {
        continue;
      }
      // A row scrolled out of its list is not a stop for a move across the
      // page: the rows of a list are walked one by one (go()), and a clipped
      // one would be "nearest" to a control below the list.
      if (el.classList.contains("pad-row") && isRowClipped(el)) {
        continue;
      }
      if (!hidden(el)) {
        out.push(el);
      }
    }
    return out;
  }

  function isRowClipped(a_el) {
    const list = a_el.parentElement;
    if (!list) {
      return false;
    }
    const r = a_el.getBoundingClientRect();
    const l = list.getBoundingClientRect();
    const mid = r.top + r.height / 2;
    return mid < l.top || mid > l.bottom;
  }

  function show() {
    if (cur) {
      cur.classList.add("pad-focus");
      cur.scrollIntoView({ block: "nearest", inline: "nearest" });
    }
    shown = true;
  }

  function hide() {
    if (cur) {
      cur.classList.remove("pad-focus");
    }
    shown = false;
  }

  function setCur(a_el) {
    if (cur && cur !== a_el) {
      cur.classList.remove("pad-focus");
    }
    cur = a_el;
    show();
  }

  function valid() {
    // A list that was drawn again (OBody answers every choice with its whole
    // list) leaves the ring on a detached row: it moves to the row that has
    // the same data-pad-key.
    if (cur && !document.contains(cur) && cur.dataset && cur.dataset.padKey) {
      const key = cur.dataset.padKey;
      const again = Array.from(document.querySelectorAll("[data-pad-key]")).find((el) => el.dataset.padKey === key);
      if (again) {
        cur = again;
        if (shown) {
          show();
        }
      }
    }
    return cur && document.contains(cur) && !hidden(cur);
  }

  // The first stop when there is none: the top one of the open section, else
  // the top-left one of the page.
  function first(a_all) {
    const host = document.getElementById("sections");
    const inHost = a_all.filter((el) => host && host.contains(el));
    const pool = inHost.length ? inHost : a_all;
    let best = null;
    let bestKey = Infinity;
    for (const el of pool) {
      const r = el.getBoundingClientRect();
      const key = r.top * 4 + r.left;
      if (key < bestKey) {
        bestKey = key;
        best = el;
      }
    }
    return best;
  }

  // Where a stop lives: the panel's column (search, regions, sections), the
  // belt, the bars under the stage. A move looks inside the current zone
  // first and leaves it only when the zone has nothing more that way: the
  // Character bar sits beside the column's lower rows, and a plain nearest
  // stop crosses between the two halfway (D253).
  const ZONES = [
    ["column", "#finder, #regions, #sections"],
    ["belt", "#belt"],
    ["toolbar", "#toolbar"],
    ["footer", ".panel-footer"],
    ["control", "#control-bar"],
    ["stage", "#stage"],
  ];

  // Up out of a bar under the panel lands in the column, its last rows -
  // measured without this, up from the belt went to Undo and from Undo to
  // the column's top, skipping every row between (D253).
  const EXITS = {
    belt: { up: "column" },
    toolbar: { up: "column" },
    footer: { up: "column" },
    control: { up: "column", left: "column" },
  };

  function zone(a_el) {
    for (const [name, sel] of ZONES) {
      if (a_el.closest(sel)) {
        return name;
      }
    }
    return "page";
  }

  // The two bars over the belt: the Character bar and the camera bar. With
  // the column open every up out of the belt went to the column and the
  // zone rule below kept the plain nearest stop away, so nothing ever
  // reached them (measured: no stop outside the camera bar had a move into
  // it).
  const BARS = ["control", "stage"];

  // The nearest stop in a direction: it has to be past the current one in
  // that direction, and the score is the distance along the direction plus
  // twice the distance across it, edge to edge - a stop straight below beats
  // a closer one off to the side.
  function nearest(a_dir) {
    const all = targets();
    if (!valid()) {
      return first(all);
    }
    const here = zone(cur);
    const inZone = (name) => all.filter((el) => zone(el) === name);
    const a = cur.getBoundingClientRect();
    const horizontal = a_dir === "left" || a_dir === "right";
    // The gap across the move, edge to edge: 0 is a stop in the same row (or
    // column).
    const across = (el) => {
      const b = el.getBoundingClientRect();
      return horizontal ? Math.max(0, Math.max(a.top, b.top) - Math.min(a.bottom, b.bottom))
        : Math.max(0, Math.max(a.left, b.left) - Math.min(a.right, b.right));
    };
    const inside = nearestIn(a_dir, inZone(here), 1);
    // Left and right: a stop in the row's own other zone beats one of another
    // row of this zone - the column's last row is beside the Character bar,
    // and the plain zone rule sent it to a chip of a row above.
    if (horizontal && (!inside || across(inside) > 0)) {
      const row = nearestIn(a_dir, all.filter((el) => zone(el) !== here && across(el) === 0), 1);
      if (row) {
        return row;
      }
    }
    if (a_dir === "up") {
      // Up out of the belt, the toolbar, the footer or the camera bar: the
      // bar straight above, else the column below. The belt's first buttons
      // sit left of both bars, and up from them goes to the nearest bar stop:
      // a slider row cannot go right (its < >), so the column's last row had
      // no way in that cost fewer than five presses.
      if (here === "belt" || here === "toolbar" || here === "footer" || here === "stage") {
        const above = all.filter((el) => BARS.includes(zone(el)) && zone(el) !== here);
        const bar = nearestIn("up", above.filter((el) => across(el) === 0), ROW) || (here === "belt" ? nearestIn("up", above, ROW) : null);
        if (bar) {
          return bar;
        }
      }
      // The Character bar and the camera bar share a row: up from one is the
      // other (its first button). Lifted above the camera bar (D135) there is
      // none in its row and up is the column as before.
      if (here === "control") {
        let first_ = null;
        for (const el of inZone("stage")) {
          const b = el.getBoundingClientRect();
          if (Math.max(a.top, b.top) < Math.min(a.bottom, b.bottom) && (!first_ || b.left < first_.getBoundingClientRect().left)) {
            first_ = el;
          }
        }
        if (first_) {
          return first_;
        }
      }
    }
    const exit = EXITS[here] && EXITS[here][a_dir];
    return inside || (exit && nearestIn(a_dir, inZone(exit), ROW)) || nearestIn(a_dir, all, 1);
  }

  // Into another zone the nearest ROW wins first: the belt starts right of
  // the column, and with the plain score up from it picked the column's top
  // (Changed) over its bottom row (Male, Female) - measured, D253.
  const ROW = 50;

  function nearestIn(a_dir, a_all, a_along) {
    const a = cur.getBoundingClientRect();
    let best = null;
    let bestScore = Infinity;
    for (const el of a_all) {
      if (el === cur || el.contains(cur) || cur.contains(el)) {
        continue;
      }
      const b = el.getBoundingClientRect();
      let along;
      let across;
      if (a_dir === "up" || a_dir === "down") {
        along = a_dir === "down" ? b.top - a.bottom : a.top - b.bottom;
        if ((a_dir === "down" ? b.top + b.height / 2 <= a.top + a.height / 2 : b.top + b.height / 2 >= a.top + a.height / 2)) {
          continue;
        }
        across = Math.max(0, Math.max(a.left, b.left) - Math.min(a.right, b.right));
      } else {
        along = a_dir === "right" ? b.left - a.right : a.left - b.right;
        if ((a_dir === "right" ? b.left + b.width / 2 <= a.left + a.width / 2 : b.left + b.width / 2 >= a.left + a.width / 2)) {
          continue;
        }
        across = Math.max(0, Math.max(a.top, b.top) - Math.min(a.bottom, b.bottom));
      }
      const score = a_along * Math.max(0, along) + 2 * across;
      if (score < bestScore) {
        bestScore = score;
        best = el;
      }
    }
    return best;
  }

  function go(a_dir) {
    if (!shown && valid()) {
      show();
      last = "show";
      return;
    }
    // Left and right on a slider are its < and >: the same click the mouse makes.
    if (valid() && cur.classList.contains("slider") && (a_dir === "left" || a_dir === "right")) {
      const steps = cur.querySelectorAll(".step-button");
      const b = steps[a_dir === "left" ? 0 : steps.length - 1];
      if (b && !b.disabled) {
        b.click();
        last = "step " + a_dir;
        return;
      }
    }
    // Up and down in a list of rows: the neighbouring row, one per press,
    // scrolled into view by the ring (show()); past either end the move
    // goes on across the page as from any other stop.
    if (valid() && cur.classList.contains("pad-row") && (a_dir === "up" || a_dir === "down")) {
      const row = a_dir === "down" ? cur.nextElementSibling : cur.previousElementSibling;
      if (row && row.classList.contains("pad-row")) {
        setCur(row);
        last = "row " + a_dir;
        return;
      }
    }
    const next = nearest(a_dir);
    if (next) {
      setCur(next);
      last = "move " + a_dir;
    } else {
      show();
      last = "edge " + a_dir;
    }
  }

  function activate() {
    if (!valid()) {
      setCur(first(targets()));
      last = "first";
      return;
    }
    // A ring that was hidden by the mouse is shown first, never clicked unseen.
    if (!shown) {
      show();
      last = "show";
      return;
    }
    if (cur.classList.contains("slider")) {
      // A row that offers a list or a colour opens it; a plain slider has
      // nothing to click (left and right are its < and >).
      const opener = cur.querySelector(".list-chip, .picker-chip");
      if (opener && !hidden(opener)) {
        opener.click();
        const pop = cur.querySelector(".listpop");
        if (pop && !pop.hidden) {
          // Into the list: on the entry in use, else on the first one shown.
          const items = Array.from(pop.querySelectorAll(".listpop-item")).filter((el) => !hidden(el));
          const at = items.find((el) => el.classList.contains("selected")) || items[0];
          if (at) {
            setCur(at);
          }
        }
        last = "open";
        return;
      }
      last = "slider";
      return;
    }
    if (cur.matches(TEXT)) {
      cur.focus();
    }
    const row = cur.closest(".slider");
    cur.click();
    // A list now stays open after a choice, so the ring stays on the entry;
    // if the click did hide it (a picker, a button that closes), back to the
    // row, as the close does.
    if (row && !valid()) {
      setCur(row);
    }
    last = "click";
  }

  function back() {
    const active = document.activeElement;
    if (active && active.matches && active.matches(TEXT)) {
      active.blur();
      setCur(active);
      last = "leave field";
      return;
    }
    const open = Array.from(document.querySelectorAll(".listpop, .picker")).filter((w) => !w.hidden);
    if (open.length) {
      for (const w of open) {
        // The same as their own close: the list's close() and the picker's
        // toggle only flip hidden.
        w.hidden = true;
        if (cur && w.contains(cur)) {
          const owner = w.closest(".slider") ||
            (w.closest(".section") && w.closest(".section").querySelector(".list-chip, .swatch, .slider"));
          setCur(owner && !hidden(owner) ? owner : first(targets()));
        }
      }
      last = "close";
      return;
    }
    const search = document.getElementById("search");
    if (search && search.value) {
      search.value = "";
      search.dispatchEvent(new Event("input", { bubbles: true }));
      last = "clear search";
      return;
    }
    last = "nothing to close";
  }

  // LB / RB: the belt's categories in their order on the screen, wrapping.
  function category(a_step) {
    const cats = Array.from(document.querySelectorAll(".belt-cat")).filter((b) => !b.hidden && !b.disabled);
    if (!cats.length) {
      return;
    }
    const at = cats.findIndex((b) => b.classList.contains("is-on"));
    const next = at < 0 ? (a_step > 0 ? cats[0] : cats[cats.length - 1])
      : cats[(at + a_step + cats.length) % cats.length];
    next.click();
    const stop = first(targets());
    setCur(stop || next);
    last = "category " + (next.dataset.cat || "");
  }

  // The right stick turns the character through the stage's drag (stage.js),
  // one frame at a time while it is off centre.
  function turnFrame(a_now) {
    const dt = turn.at ? Math.min(0.1, (a_now - turn.at) / 1000) : 0;
    turn.at = a_now;
    if (dt && turn.x) {
      document.dispatchEvent(new CustomEvent("lifecast-stage-drag", { detail: { dx: turn.x * TURN_PX_PER_S * dt } }));
    }
    turn.frame = turn.x ? requestAnimationFrame(turnFrame) : null;
    if (!turn.x) {
      turn.at = 0;
    }
  }

  function onRight(a_x) {
    turn.x = a_x;
    if (turn.x && turn.frame === null) {
      turn.at = 0;
      turn.frame = requestAnimationFrame(turnFrame);
    }
  }

  function stickDir(a_x, a_y) {
    if (Math.max(Math.abs(a_x), Math.abs(a_y)) < STICK_ON) {
      return null;
    }
    if (Math.abs(a_x) > Math.abs(a_y)) {
      return a_x > 0 ? "right" : "left";
    }
    return a_y > 0 ? "up" : "down";
  }

  function onLeft(a_x, a_y) {
    const dir = stickDir(a_x, a_y);
    if (dir === stick.dir) {
      return;
    }
    clearTimeout(stick.timer);
    stick.timer = null;
    stick.dir = dir;
    if (!dir) {
      return;
    }
    presses += 1;
    go(dir);
    const again = () => {
      go(stick.dir);
      stick.timer = setTimeout(again, STICK_EVERY_MS);
    };
    stick.timer = setTimeout(again, STICK_FIRST_MS);
  }

  function reset() {
    clearTimeout(stick.timer);
    stick.timer = null;
    stick.dir = null;
    turn.x = 0;
  }

  const DPAD = { DUp: "up", DDown: "down", DLeft: "left", DRight: "right" };

  function onPad(a_data) {
    if (!a_data) {
      return;
    }
    if (a_data.reset) {
      reset();
      return;
    }
    if (a_data.b === "LStick") {
      onLeft(Number(a_data.x) || 0, Number(a_data.y) || 0);
      return;
    }
    if (a_data.b === "RStick") {
      onRight(Number(a_data.x) || 0);
      return;
    }
    const down = a_data.phase === "down";
    if (!down && a_data.phase !== "repeat") {
      return;
    }
    if (DPAD[a_data.b]) {
      presses += 1;
      go(DPAD[a_data.b]);
    } else if (!down) {
      // Only the directions repeat; A, B and the bumpers act once per press.
    } else if (a_data.b === "A") {
      presses += 1;
      activate();
    } else if (a_data.b === "B") {
      presses += 1;
      back();
    } else if (a_data.b === "LB" || a_data.b === "RB") {
      presses += 1;
      category(a_data.b === "RB" ? 1 : -1);
    } else if (a_data.b === "LT") {
      // The zoom of the stage's bar, whatever the ring is on: stage.js picks
      // RaceMenu's far/near in standard and one step closer in free.
      presses += 1;
      document.dispatchEvent(new CustomEvent("lifecast-stage-zoom"));
      last = "zoom";
    } else if (a_data.b === "RT") {
      // The other way: Farther in the free camera, the same RaceMenu zoom as
      // LT in standard (it only has the two positions).
      presses += 1;
      document.dispatchEvent(new CustomEvent("lifecast-stage-farther"));
      last = "farther";
    }
  }

  // A list drew its rows again: the ring finds its row now, not at the next press.
  document.addEventListener("lifecast-pad-refresh", () => {
    valid();
  });

  // The mouse or the keyboard back in the player's hand: the ring steps aside.
  document.addEventListener("pointerdown", hide, true);
  document.addEventListener("keydown", hide, true);

  LifecastBridge.on("pad", onPad);

  function describe(a_el) {
    if (!a_el) {
      return null;
    }
    const nameEl = a_el.classList.contains("slider") ? a_el.querySelector(".slider-name") : null;
    const range = a_el.classList.contains("slider") ? a_el.querySelector("input[type=range]") : null;
    const text = (nameEl ? nameEl.textContent : (a_el.getAttribute("aria-label") || a_el.title || a_el.textContent || a_el.placeholder || ""))
      .replace(/\s+/g, " ").trim().slice(0, 40);
    return {
      tag: a_el.tagName.toLowerCase(),
      id: a_el.id || "",
      cls: String(a_el.className).replace("pad-focus", "").trim(),
      text: text,
      value: range ? Number(range.value) : (a_el.matches(TEXT) ? a_el.value : null),
    };
  }

  // What the ring is on, for whoever is checking the page from outside.
  function state() {
    const on = document.querySelector(".belt-cat.is-on");
    const active = document.activeElement;
    return {
      presses: presses,
      last: last,
      shown: shown,
      focus: describe(valid() ? cur : null),
      category: on ? on.dataset.cat : null,
      typing: !!(active && active.matches && active.matches(TEXT)),
      open: Array.from(document.querySelectorAll(".listpop, .picker")).filter((w) => !w.hidden).length,
      stops: targets().length,
    };
  }

  return { state: state };
})();