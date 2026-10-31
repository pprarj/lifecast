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
// In an open colour picker (D327) the ring is on one of its three parts:
//
//   square              D-pad and left stick move the point (right = more
//                       saturated, up = lighter); a held or repeated press
//                       takes bigger steps, the stick glides
//   hue bar             left / right move the hue; up the square, down the hex
//   hex                 A types it, as its click; up the hue bar
//   Y                   the next part: square, hue bar, hex
//   A (square, bar), B  close the picker; the ring goes back to what opened it
//
// A on a colour chip opens its picker with the ring in the square. The moves
// go through the picker's own report(), as its drag: one final colour and
// one undo entry per edit.
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
      // The Hair Color swatches live inside their row too, and stay out of
      // this list: a move across the page would find them from far off. They
      // are walked from their row (swatchGo, D327).
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

  // --- the colour picker (D327) ---

  const SQUARE_STEP = 0.02;       // a D-pad press in the square, of its width or height
  const SQUARE_STEP_MAX = 0.1;
  const HUE_STEP = 3 / 360;       // a D-pad press on the hue bar
  const HUE_STEP_MAX = 15 / 360;
  const STEP_AGAIN_MS = 350;      // a press this soon after one the same way counts as held
  const GLIDE_DEAD = 0.2;         // the left stick in the picker: past this it moves,
  const GLIDE_SQUARE = 0.7;       // at full tilt this much of the square a second,
  const GLIDE_HUE = 0.5;          // this much of the hue bar a second,
  const GLIDE_RAMP_MS = 1200;     // reached after this long held (from 35%)
  const GLIDE_TICK_MS = 16;       // a timer, not a frame: it runs the same under every backend

  const openers = new WeakMap();  // picker -> the chip or row that opened it
  const nudge = { dir: null, at: 0, streak: 0 };
  const glide = { x: 0, y: 0, frame: null, at: 0, since: 0 };

  function pickerOf(a_el) {
    const w = a_el && a_el.closest ? a_el.closest(".picker") : null;
    return w && w.lifecastPad ? w : null;
  }

  function partOf(a_el) {
    const w = pickerOf(a_el);
    if (!w) {
      return null;
    }
    const p = w.lifecastPad;
    return a_el === p.square ? "square" : a_el === p.hue ? "hue" : p.hex.contains(a_el) ? "hex" : null;
  }

  function openPickers() {
    return Array.from(document.querySelectorAll(".picker")).filter((w) => !w.hidden);
  }

  // Clicks what opens a picker; the one that is open now and was not before
  // is its picker, and the ring goes into its square.
  function openPicker(a_click, a_owner) {
    const before = new Set(openPickers());
    a_click();
    const opened = openPickers().find((w) => !before.has(w) && w.lifecastPad);
    if (!opened) {
      return false;
    }
    openers.set(opened, a_owner);
    setCur(opened.lifecastPad.square);
    return true;
  }

  // The owner of a closed list or picker, for the ring to go back to.
  function ownerOf(a_w) {
    const opener = openers.get(a_w);
    if (opener && document.contains(opener) && !hidden(opener)) {
      return opener;
    }
    const owner = a_w.closest(".slider") ||
      (a_w.closest(".section") && a_w.closest(".section").querySelector(".list-chip, .swatch, .slider"));
    return owner && !hidden(owner) ? owner : first(targets());
  }

  function closePicker(a_w) {
    a_w.lifecastPad.close();
    if (cur && a_w.contains(cur)) {
      setCur(ownerOf(a_w));
    }
  }

  // A press the same way as the last one, held or soon after it, takes a
  // bigger step: twice the step every two presses, up to the most.
  function step(a_dir, a_repeat, a_base, a_max) {
    const now = performance.now();
    if (a_dir === nudge.dir && (a_repeat || now - nudge.at < STEP_AGAIN_MS)) {
      nudge.streak += 1;
    } else {
      nudge.streak = 0;
    }
    nudge.dir = a_dir;
    nudge.at = now;
    return Math.min(a_max, a_base * Math.pow(2, Math.floor(nudge.streak / 2)));
  }

  const PARTS = ["square", "hue", "hex"];

  function toPart(a_w, a_part) {
    const p = a_w.lifecastPad;
    setCur(a_part === "square" ? p.square : a_part === "hue" ? p.hue : p.hex);
    last = "part " + a_part;
  }

  function pickerGo(a_dir, a_repeat) {
    const w = pickerOf(cur);
    const part = partOf(cur);
    const p = w.lifecastPad;
    if (part === "square") {
      const d = step(a_dir, a_repeat, SQUARE_STEP, SQUARE_STEP_MAX);
      const dx = a_dir === "right" ? d : a_dir === "left" ? -d : 0;
      const dy = a_dir === "up" ? d : a_dir === "down" ? -d : 0;
      p.nudge("square", dx, dy);
      last = "square " + a_dir;
    } else if (part === "hue") {
      if (a_dir === "left" || a_dir === "right") {
        const d = step(a_dir, a_repeat, HUE_STEP, HUE_STEP_MAX);
        p.nudge("hue", a_dir === "right" ? d : -d);
        last = "hue " + a_dir;
      } else if (!a_repeat) {
        toPart(w, a_dir === "up" ? "square" : "hex");
      }
    } else if (a_dir === "up" && !a_repeat) {
      toPart(w, "hue");
    } else {
      last = "edge " + a_dir;
    }
  }

  function glideAxis(a_v) {
    const m = Math.abs(a_v);
    return m <= GLIDE_DEAD ? 0 : Math.sign(a_v) * (m - GLIDE_DEAD) / (1 - GLIDE_DEAD);
  }

  function glideFrame() {
    const a_now = performance.now();
    const part = valid() ? partOf(cur) : null;
    if ((!glide.x && !glide.y) || (part !== "square" && part !== "hue")) {
      glide.frame = null;
      glide.at = 0;
      return;
    }
    if (!glide.at) {
      glide.since = a_now;
    }
    const dt = glide.at ? Math.min(0.1, (a_now - glide.at) / 1000) : 0;
    glide.at = a_now;
    const ramp = Math.min(1, 0.35 + 0.65 * (a_now - glide.since) / GLIDE_RAMP_MS);
    const p = pickerOf(cur).lifecastPad;
    if (dt && part === "square") {
      p.nudge("square", glideAxis(glide.x) * GLIDE_SQUARE * ramp * dt, glideAxis(glide.y) * GLIDE_SQUARE * ramp * dt);
    } else if (dt) {
      p.nudge("hue", glideAxis(glide.x) * GLIDE_HUE * ramp * dt);
    }
    glide.frame = setTimeout(glideFrame, GLIDE_TICK_MS);
  }

  // The left stick in the square or on the hue bar glides the point; true
  // when it did (elsewhere it moves the ring).
  function pickerStick(a_x, a_y) {
    const part = valid() ? partOf(cur) : null;
    if (part !== "square" && part !== "hue") {
      glide.x = 0;
      glide.y = 0;
      return false;
    }
    const was = glide.x || glide.y;
    glide.x = glideAxis(a_x) ? a_x : 0;
    glide.y = part === "square" && glideAxis(a_y) ? a_y : 0;
    if ((glide.x || glide.y) && !was) {
      presses += 1;
      if (!shown) {
        show();
      }
      last = "glide " + part;
    }
    if ((glide.x || glide.y) && glide.frame === null) {
      glide.at = 0;
      glide.frame = setTimeout(glideFrame, 0);
    }
    return true;
  }

  // Swatches inside a row (Hair Color): down on the row goes into them, on
  // the one in use or the first; inside, the ring moves among them only; up
  // from their top line is the row again, down from the bottom one leaves as
  // from the row; left and right stop at the ends.
  function rowSwatches(a_row) {
    return Array.from(a_row.querySelectorAll(".swatch")).filter((el) => !hidden(el));
  }

  function swatchGo(a_dir) {
    if (cur.classList.contains("slider") && a_dir === "down") {
      const sw = rowSwatches(cur);
      if (sw.length) {
        setCur(sw.find((el) => el.classList.contains("selected")) || first(sw));
        last = "swatch in";
        return true;
      }
      return false;
    }
    const row = cur.classList.contains("swatch") ? cur.closest(".slider") : null;
    if (!row) {
      return false;
    }
    const next = nearestIn(a_dir, rowSwatches(row), 1);
    if (next) {
      setCur(next);
      last = "swatch " + a_dir;
    } else if (a_dir === "up") {
      setCur(row);
      last = "swatch out";
    } else if (a_dir === "down") {
      return false;
    } else {
      show();
      last = "edge " + a_dir;
    }
    return true;
  }

  function go(a_dir, a_repeat) {
    if (!shown && valid()) {
      show();
      last = "show";
      return;
    }
    if (valid() && partOf(cur)) {
      pickerGo(a_dir, !!a_repeat);
      return;
    }
    if (valid() && swatchGo(a_dir)) {
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
    // In the picker: A types the hex, and closes it from the square or the bar.
    const part = partOf(cur);
    if (part === "hex") {
      pickerOf(cur).lifecastPad.typeHex();
      last = "type hex";
      return;
    }
    if (part) {
      closePicker(pickerOf(cur));
      last = "close picker";
      return;
    }
    // A colour chip of its own (a tint or Paint layer): its click, and the
    // ring into the picker that opened.
    if (cur.matches(".picker-chip")) {
      last = openPicker(() => cur.click(), cur) ? "open picker" : "click";
      return;
    }
    if (cur.classList.contains("slider")) {
      // A row that offers a list or a colour opens it; a plain slider has
      // nothing to click (left and right are its < and >).
      const opener = cur.querySelector(".list-chip, .picker-chip");
      if (opener && !hidden(opener)) {
        if (opener.matches(".picker-chip") && openPicker(() => opener.click(), cur)) {
          last = "open picker";
          return;
        }
        if (opener.matches(".picker-chip")) {
          last = "open";
          return;
        }
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
      // The picker's hex field goes away when it loses the focus (its blur
      // applies a valid hex, as a click elsewhere): the ring stays on the hex.
      const hex = pickerOf(active) ? pickerOf(active).lifecastPad.hex : null;
      active.blur();
      setCur(hex || active);
      last = "leave field";
      return;
    }
    const open = Array.from(document.querySelectorAll(".listpop, .picker")).filter((w) => !w.hidden);
    if (open.length) {
      for (const w of open) {
        // The same as their own close: the list's close() only flips hidden;
        // a picker also ends a controller edit, with its final colour (D327).
        const had = cur && w.contains(cur);
        if (w.lifecastPad) {
          w.lifecastPad.close();
        } else {
          w.hidden = true;
        }
        if (had) {
          setCur(ownerOf(w));
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
    // A picker the ring is in closes first, with its final colour (D327).
    if (valid() && pickerOf(cur)) {
      closePicker(pickerOf(cur));
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
    if (pickerStick(a_x, a_y)) {
      clearTimeout(stick.timer);
      stick.timer = null;
      stick.dir = null;
      return;
    }
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
    glide.x = 0;
    glide.y = 0;
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
      go(DPAD[a_data.b], !down);
    } else if (!down) {
      // Only the directions repeat; A, B and the bumpers act once per press.
    } else if (a_data.b === "A") {
      presses += 1;
      activate();
    } else if (a_data.b === "B") {
      presses += 1;
      back();
    } else if (a_data.b === "Y") {
      // In a picker, its next part; nothing elsewhere.
      if (valid() && partOf(cur)) {
        presses += 1;
        if (!shown) {
          show();
          last = "show";
        } else {
          toPart(pickerOf(cur), PARTS[(PARTS.indexOf(partOf(cur)) + 1) % PARTS.length]);
        }
      }
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
      picker: valid() && pickerOf(cur) ? Object.assign({ part: partOf(cur) }, pickerOf(cur).lifecastPad.hsv()) : null,
    };
  }

  return { state: state };
})();