// controls.js
// Lifecast - the slider row every section builds on.
//
// A row is a label, the current value, a range input and a pair of step
// buttons. The row does not know what it edits: it calls a_opts.send(value,
// final) and the section turns that into a message.
//
// While the range is dragged the row sends at most one value per
// SEND_GAP_MS, always the latest; when the drag ends it sends the final
// value marked as such, which is what makes the DLL redraw at once. A step
// button sends one final value.
"use strict";

const LifecastControls = (() => {
  const SEND_GAP_MS = 50;

  // The portrait (F27): what every edit row holds right now, so a step that
  // rewrites the whole face (a race preset) can be undone by putting the
  // values back instead of by replaying the older step. The set is the rows
  // THEMSELVES, not a list of fields: a control added later is in the
  // portrait the day it is built, with no line here to forget. A part is a
  // value that is not one row (a free hair colour): { get(), restore(v) }.
  const portraitRows = new Set();
  const portraitParts = new Map();

  function el(tag, cls, text) {
    const e = document.createElement(tag);
    if (cls) {
      e.className = cls;
    }
    if (text !== undefined) {
      e.textContent = text;
    }
    return e;
  }

  // A section is a header that collapses it and a body that holds the rows.
  // The caller appends into section.body, never into the section itself:
  // hidden, remove() and closest() stay meaningful on the whole section.
  //
  // Collapsed is the default (D064). a_opts.open opts one out, for a section
  // whose content is the panel's primary action rather than a list to browse.
  function section(title, a_opts) {
    const opts = a_opts || {};
    const s = el("section", "section");
    const head = el("button", "section-head");
    head.type = "button";
    head.tabIndex = -1;
    head.appendChild(el("span", "section-caret"));
    head.appendChild(el("h2", "section-title", title));
    const body = el("div", "section-body");
    s.appendChild(head);
    s.appendChild(body);
    s.body = body;
    s.dataset.open = opts.open ? "1" : "0";
    head.setAttribute("aria-expanded", opts.open ? "true" : "false");
    head.addEventListener("click", () => {
      const open = s.dataset.open !== "1";
      toggle(s, body, open);
      head.setAttribute("aria-expanded", open ? "true" : "false");
      if (s.dataset.key) {
        saveOpen(s.dataset.key, open);
      }
    });
    return s;
  }

  // ----- opening and closing a section, animated only while it animates -----
  //
  // D115 (2026-10-05): a section eases open and shut, and it is in the
  // layout ONLY during the animation. A closed section is display:none, which
  // cannot animate, so closing keeps the body displayed (data-closing) until the
  // height reaches 0 and then lets the closed rule hide it again - between
  // gestures the panel is exactly what it was before this existed.
  //
  // The off switch is ONE declaration, no code: --section-anim: 0ms in :root.
  // A duration of zero takes the first branch below, which is the old toggle.
  //
  // Each animation measures itself (frame intervals between its start and end)
  // and tells the DLL, which logs every page message whether or not it has a
  // handler - "anim-measure" has none on purpose, so the number lands in
  // Lifecast.log with no DLL change. The "no handler" warning that follows is
  // that, and is expected.
  function sectionAnimMs() {
    const raw = getComputedStyle(document.documentElement).getPropertyValue("--section-anim").trim();
    const n = parseFloat(raw);
    if (!isFinite(n) || n <= 0) {
      return 0;
    }
    return /ms$/.test(raw) ? n : (/s$/.test(raw) ? n * 1000 : n);
  }

  function toggle(s, body, open) {
    if (s._animEnd) {
      s._animEnd();
    }
    const ms = sectionAnimMs();
    if (ms === 0) {
      s.dataset.open = open ? "1" : "0";
      return;
    }
    if (open) {
      s.dataset.open = "1";
    } else {
      s.dataset.closing = "1";
      s.dataset.open = "0";
    }
    const target = open ? body.scrollHeight : 0;
    // Where it starts: opening grows from 0, closing shrinks from where it is.
    const from = open ? 0 : body.scrollHeight;
    body.style.height = from + "px";
    if (open) {
      body.style.paddingTop = "0px";
    }
    body.classList.add("is-animating");
    void body.offsetHeight;
    body.style.height = target + "px";
    body.style.paddingTop = open ? "" : "0px";

    const t0 = performance.now();
    const frames = [];
    let last = t0;
    let raf = requestAnimationFrame(function tick(now) {
      frames.push(now - last);
      last = now;
      raf = requestAnimationFrame(tick);
    });
    let finished = false;
    const timer = setTimeout(end, ms + 80);
    function end() {
      if (finished) {
        return;
      }
      finished = true;
      clearTimeout(timer);
      cancelAnimationFrame(raf);
      body.removeEventListener("transitionend", onEnd);
      body.classList.remove("is-animating");
      body.style.height = "";
      body.style.paddingTop = "";
      delete s.dataset.closing;
      s._animEnd = null;
      const took = performance.now() - t0;
      const worst = frames.length ? Math.max.apply(null, frames) : 0;
      LifecastBridge.send("anim-measure", {
        what: open ? "section-open" : "section-close",
        key: s.dataset.key || "",
        wantedMs: ms,
        tookMs: Math.round(took),
        frames: frames.length,
        worstFrameMs: Math.round(worst),
      });
    }
    function onEnd(e) {
      if (e.target === body && e.propertyName === "height") {
        end();
      }
    }
    body.addEventListener("transitionend", onEnd);
    s._animEnd = end;
  }

  // Which sections the player left open, kept by the DLL (2026-09-28).
  // This file stays ignorant of which key means what, same as
  // order and group - it only reads and writes a key -> bool object under
  // the "sectionsOpen" setting.
  //
  // The saved object always arrives AFTER every section below has already
  // mounted at its own default (settings.js's answer is async; the scripts
  // that build sections are not), so applying it means walking what is
  // already in the DOM, not something mount() can do for a section that
  // does not exist yet at that instant.
  function saveOpen(a_key, a_open) {
    const current = LifecastSettings.get("sectionsOpen");
    const next = current && typeof current === "object" ? Object.assign({}, current) : {};
    next[a_key] = a_open;
    LifecastSettings.set("sectionsOpen", next);
  }

  // The DLL echoes "settings" on every settings-set, from wherever it came
  // (E3, Fase 6.5 - a single scale drag produced ten echoes in ten seconds,
  // measured in the phase document). Reapplying the whole saved map on every
  // one of them fights reveal(), which opens a section in the DOM without
  // saving it: the very next echo used to put it right back where the saved
  // map still said it was. lastSeen is the map as of the last echo this
  // listener actually saw - only a key whose SAVED value changed since then
  // gets pushed onto the DOM; a key that has not changed is left alone, so a
  // section opened by any path that does not call saveOpen (reveal today,
  // whatever else tomorrow) survives an echo that was never about it.
  let lastSeen = null;

  LifecastSettings.onChange(() => {
    const saved = LifecastSettings.get("sectionsOpen");
    if (!saved || typeof saved !== "object") {
      return;
    }
    const previous = lastSeen;
    lastSeen = saved;
    const host = document.getElementById("sections");
    host.querySelectorAll(":scope > .section").forEach((s) => {
      const key = s.dataset.key;
      if (!key || !Object.prototype.hasOwnProperty.call(saved, key)) {
        return;
      }
      if (previous && Object.prototype.hasOwnProperty.call(previous, key) && previous[key] === saved[key]) {
        return;
      }
      const open = !!saved[key];
      s.dataset.open = open ? "1" : "0";
      const head = s.querySelector(".section-head");
      if (head) {
        head.setAttribute("aria-expanded", open ? "true" : "false");
      }
    });
  });

  // The order the sections appear in. Until something sets it, it is the order
  // they mount in - which is the order of the <script> tags, and that is the
  // accident this exists to end. A key the order does not name falls in after
  // every key it does name, by MOUNTING order.
  //
  // The tie-break is the mounting index and not the current position on
  // purpose: sorting by where things sit now makes an unnamed section drift a
  // little further every time the order is set again, so the same list applied
  // twice would not give the same page.
  let order = [];
  let mounted = 0;

  // An entry ending in "*" names every key that starts with what comes before
  // it ("morph:*"): sections a mod produces on the DLL's own time cannot be
  // named one by one, but their place can. An exact entry wins over a prefix
  // one, so "face:paint" can sit apart from "face:*".
  function rank(a_el) {
    const key = a_el.dataset.key;
    const at = order.indexOf(key);
    if (at >= 0) {
      return at;
    }
    const pre = order.findIndex((o) => o.slice(-1) === "*" && typeof key === "string" && key.indexOf(o.slice(0, -1)) === 0);
    return pre < 0 ? order.length : pre;
  }

  // What group a section's key belongs to (E2 item 1). null means no group -
  // the section stays a heading of one. The function itself is domain
  // knowledge this file does not have; it comes from outside, same as order.
  let groupOf = () => null;

  function arrange() {
    const host = document.getElementById("sections");
    // Headings are rebuilt every call, not patched - cheap at panel size, and
    // it means a heading can never survive its group becoming empty.
    host.querySelectorAll(":scope > .group-heading").forEach((h) => h.remove());
    const sorted = Array.prototype.slice.call(host.querySelectorAll(":scope > .section"))
      .sort((a, b) => rank(a) - rank(b) || Number(a.dataset.mountedAt) - Number(b.dataset.mountedAt));

    // A group shows up ONCE, where its first section falls; every later
    // section of that group joins it there instead of keeping its own place
    // in the sort. Sorting alone is not enough, because a section mounted on
    // the DLL's own time has no place in the declared order and lands past
    // every group - and a heading is written whenever the group changes, so
    // that section would open a second heading for its own group at the
    // bottom of the page. Measured on the bench, 2026-09-30: two mod tabs and
    // the face paint section each did exactly that.
    //
    // A section in no group keeps its own slot: there is nothing to join.
    const buckets = [];
    const byGroup = new Map();
    sorted.forEach((s) => {
      const g = groupOf(s.dataset.key);
      if (!g) {
        buckets.push([s]);
        return;
      }
      let bucket = byGroup.get(g);
      if (!bucket) {
        bucket = [];
        byGroup.set(g, bucket);
        buckets.push(bucket);
      }
      bucket.push(s);
    });

    // A group of ONE section gets no heading: the heading would only repeat
    // the section's own title above it (Presets over Presets).
    buckets.forEach((bucket) => {
      const g = groupOf(bucket[0].dataset.key);
      if (g && bucket.length > 1) {
        host.appendChild(el("div", "group-heading", g));
      }
      bucket.forEach((s) => {
        // Read by search.js to tell which heading a hidden section belongs
        // to without depending on DOM adjacency (an ungrouped section can
        // sit right after a group, and must not be read as still part of
        // it), and by belt.js to tell which category a section is in.
        s.dataset.group = g || "";
        host.appendChild(s);
      });
    });
  }

  // Replaces the order and rearranges what is already mounted. The list comes
  // from outside this file on purpose: no section order is written here.
  function setOrder(a_keys) {
    order = Array.isArray(a_keys) ? a_keys.slice() : [];
    arrange();
  }

  // Replaces the grouping function and rearranges to draw the headings it
  // implies. Same shape as setOrder: this file stays ignorant of what a
  // "Body" or a "Face" is.
  function setGroups(a_fn) {
    groupOf = typeof a_fn === "function" ? a_fn : () => null;
    arrange();
  }

  // Removes the "No sections yet." line the first time a section is added.
  function mount(a_section, a_key) {
    const host = document.getElementById("sections");
    const placeholder = host.querySelector(".placeholder");
    if (placeholder) {
      placeholder.remove();
    }
    a_section.dataset.key = a_key === undefined ? "" : a_key;
    a_section.dataset.mountedAt = String(mounted);
    mounted += 1;
    host.appendChild(a_section);
    arrange();
  }

  // The section mounted under a_key, made and mounted the first time anyone
  // asks for it. Several files add rows to one section (E19: Eye Color from
  // look.js and a tint layer from tints.js both land in the Eyes section that
  // sliders.js puts the sliders in), and section() above makes a NEW one on every
  // call. The join is by KEY, never by title: titles are not unique (Hair is a
  // section and a control) and some come from a mod (sliders.js).
  const slots = new Map();

  function slot(a_title, a_key, a_opts) {
    let s = slots.get(a_key);
    if (!s) {
      s = section(a_title, a_opts);
      slots.set(a_key, s);
      mount(s, a_key);
    }
    return s;
  }

  // a_opts: { label, min, max, step, button, format(v), send(v, final) }
  // button is how far one press of < or > moves the value.
  // Optional: portrait:false keeps the row out of the portrait; entry(before,
  // after, put) builds the history entry in place of the default one (or
  // returns null for none) - for a row whose step is not a value to put back.
  // typed:true lets the player click the number and type the value. Only for
  // a row whose shown number IS the value in the same unit - never a list or
  // index row, nor one whose value text is replaced by something else.
  function row(a_opts) {
    const wrap = el("div", "slider");
    const name = el("span", "slider-name", a_opts.label);
    const value = el("span", "slider-value", "--");
    const input = el("input");
    input.type = "range";
    input.min = String(a_opts.min);
    input.max = String(a_opts.max);
    input.step = String(a_opts.step);
    input.value = String(a_opts.min);
    input.disabled = true;

    const down = el("button", "step-button", "<");
    const up = el("button", "step-button", ">");
    down.type = "button";
    up.type = "button";
    down.tabIndex = -1;
    up.tabIndex = -1;

    const track = el("div", "slider-track");
    track.appendChild(down);
    track.appendChild(input);
    track.appendChild(up);

    wrap.appendChild(name);
    wrap.appendChild(value);
    wrap.appendChild(track);

    const state = { dragging: false, timer: null, lastSent: 0, hasInitial: false, initial: null, last: null, fake: false,
      editing: null };
    wrap.dataset.changed = "0";

    function show(v) {
      value.textContent = a_opts.format(v);
    }

    // a_id is who the value is (a head part: name and plugin), for a row whose
    // index means a different part in another build of the game's list.
    function send(final, a_id) {
      if (state.timer !== null) {
        clearTimeout(state.timer);
        state.timer = null;
      }
      state.lastSent = performance.now();
      a_opts.send(Number(input.value), final, a_id);
    }

    // One finished gesture, offered to the history. state.last is the value
    // the row held before it - and the only OTHER writer of state.last is
    // set(), which is where a value from the DLL arrives. So a value the
    // player did not move never becomes the start of an entry, and a drag
    // keeps the value from before the hand touched it: while dragging()
    // is true the sections stop calling set(), which is what makes the
    // "before" survive the echoes that arrive mid-drag.
    // The same write the player's own gesture would do, with another value:
    // set the input, show it, and send it as final.
    function put(v, a_id) {
      if (input.disabled) {
        return false;
      }
      input.value = String(v);
      show(Number(input.value));
      state.last = Number(input.value);
      send(true, a_id);
      return true;
    }

    function commit(a_v) {
      const before = state.last;
      state.last = a_v;
      if (typeof before !== "number") {
        return;
      }
      const entry =
        typeof a_opts.entry === "function"
          ? a_opts.entry(before, a_v, put)
          : { label: a_opts.label, before: before, after: a_v, apply: put };
      if (entry) {
        LifecastHistory.record(entry);
      }
    }

    input.addEventListener("input", () => {
      state.dragging = true;
      show(Number(input.value));
      const wait = SEND_GAP_MS - (performance.now() - state.lastSent);
      if (wait <= 0) {
        send(false);
      } else if (state.timer === null) {
        state.timer = setTimeout(() => send(false), wait);
      }
    });

    input.addEventListener("change", () => {
      state.dragging = false;
      send(true);
      commit(Number(input.value));
    });

    function stepBy(a_dir) {
      if (input.disabled) {
        return;
      }
      const min = Number(input.min);
      const max = Number(input.max);
      // Rounded to the input's step, so repeated presses do not drift.
      const step = Number(input.step);
      let v = Number(input.value) + a_dir * a_opts.button;
      v = Math.round(v / step) * step;
      v = Math.min(max, Math.max(min, v));
      input.value = String(v);
      show(Number(input.value));
      send(true);
      commit(Number(input.value));
    }

    // The typed number, or null when the text is not one. A sign, a dot or a
    // comma decimal and a trailing % are accepted.
    function parseTyped(a_text) {
      const t = String(a_text).trim().replace(/%$/, "").trim().replace(",", ".");
      if (!/^[+-]?(\d+\.?\d*|\.\d+)$/.test(t)) {
        return null;
      }
      const n = Number(t);
      return isFinite(n) ? n : null;
    }

    // Closes the editor. a_text is what to apply, or null to cancel. An
    // applied number goes the stepBy way: held to the range, rounded to the
    // step, sent once as final, offered to the history.
    function endEdit(a_text) {
      const field = state.editing;
      if (!field) {
        return;
      }
      state.editing = null;
      field.remove();
      const n = a_text === null ? null : parseTyped(a_text);
      if (n === null || input.disabled) {
        show(Number(input.value));
        return;
      }
      const min = Number(input.min);
      const max = Number(input.max);
      const step = Number(input.step);
      let v = Math.round(n / step) * step;
      v = Math.min(max, Math.max(min, v));
      // The step multiplication leaves float dust (0.30000000000000004).
      v = Number(v.toFixed(Math.min(8, (String(step).split(".")[1] || "").length)));
      v = Math.min(max, Math.max(min, v));
      input.value = String(v);
      show(Number(input.value));
      send(true);
      commit(Number(input.value));
    }

    function beginEdit() {
      if (input.disabled || state.editing) {
        return;
      }
      const field = el("input", "slider-edit");
      field.type = "text";
      field.value = String(Number(input.value));
      field.setAttribute("aria-label", a_opts.label);
      state.editing = field;
      value.textContent = "";
      value.appendChild(field);
      field.addEventListener("keydown", (e) => {
        if (e.key === "Enter") {
          e.preventDefault();
          endEdit(field.value);
        } else if (e.key === "Escape") {
          e.preventDefault();
          e.stopPropagation();
          endEdit(null);
        }
      });
      field.addEventListener("blur", () => {
        if (state.editing === field) {
          endEdit(field.value);
        }
      });
      field.focus();
      field.select();
    }

    if (a_opts.typed) {
      value.classList.add("is-typed");
      value.addEventListener("click", beginEdit);
    }

    down.addEventListener("click", () => stepBy(-1));
    up.addEventListener("click", () => stepBy(1));

    const handle = {
      el: wrap,
      input: input,
      // The player's hand wins over a value from before it moved.
      dragging: () => state.dragging || state.editing !== null,
      // null disables the row and shows "--".
      //
      // The FIRST number a row ever receives is its baseline (V079: nothing
      // else in the page keeps one). data-changed on the row's own element
      // is the public form of that comparison - search.js reads it, so a
      // "changed" filter never needs a map back to this closure.
      set(a_v) {
        const off = a_v === null || typeof a_v !== "number" || !isFinite(a_v);
        input.disabled = off;
        down.disabled = off;
        up.disabled = off;
        state.fake = false;
        if (off) {
          if (state.editing) {
            const field = state.editing;
            state.editing = null;
            field.remove();
          }
          value.textContent = "--";
          state.last = null;
          return;
        }
        input.value = String(a_v);
        // An echo from the DLL does not wipe what is being typed.
        if (!state.editing) {
          show(a_v);
        }
        state.last = a_v;
        if (!state.hasInitial) {
          state.hasInitial = true;
          state.initial = a_v;
        }
        wrap.dataset.changed = a_v !== state.initial ? "1" : "0";
      },
      // The same as pressing a step button, but to a value the caller picked
      // (listpick.js): set it, show it, send it as final, offer it to the
      // undo history. A disabled row takes nothing.
      choose(a_v) {
        if (input.disabled || typeof a_v !== "number" || !isFinite(a_v)) {
          return;
        }
        const v = Math.min(Number(input.max), Math.max(Number(input.min), a_v));
        input.value = String(v);
        show(Number(input.value));
        send(true);
        commit(Number(input.value));
      },
      setRange(a_min, a_max) {
        input.min = String(a_min);
        input.max = String(a_max);
      },
      // The value just set() stands in for one the row cannot hold (a list
      // that shows "--" and rests on 0): the portrait must not keep it.
      mark(a_fake) {
        state.fake = !!a_fake;
      },
      // Replaces the value text with arbitrary content (a colour chip).
      valueEl: value,
      changed: () => wrap.dataset.changed === "1",
    };
    if (a_opts.portrait !== false) {
      portraitRows.add({
        el: wrap,
        snapshot: () => (input.disabled || state.fake ? null : Number(input.value)),
        // Who the value is, when the row can say (a_opts.identity: a head part,
        // whose index is only good inside the list it came from, E31).
        ident: () => (typeof a_opts.identity === "function" ? a_opts.identity() : null),
        // false when there is nothing to do: the row already holds it. With an
        // identity the question is "is it this part", not "is it this number".
        restore: (v, a_id) => {
          if (typeof v !== "number" || input.disabled) {
            return false;
          }
          if (a_id) {
            const now = typeof a_opts.identity === "function" ? a_opts.identity() : null;
            if (now && now.n === a_id.n && now.s === a_id.s) {
              return false;
            }
            return put(v, a_id);
          }
          return Number(input.value) === v ? false : put(v);
        },
      });
    }
    return handle;
  }

  function addPortraitPart(a_key, a_part) {
    portraitParts.set(a_key, a_part);
  }

  // The rows that are on the page now, each with the key that finds it again
  // after a section rebuilds its rows (tints, scales, overlays do): the
  // section it sits in, its label, and which of that label it is, in document
  // order. Rows that left the page are dropped here.
  function liveRows() {
    const live = [];
    for (const r of portraitRows) {
      if (!r.el.isConnected) {
        portraitRows.delete(r);
        continue;
      }
      live.push(r);
    }
    live.sort((a, b) => (a.el.compareDocumentPosition(b.el) & Node.DOCUMENT_POSITION_FOLLOWING ? -1 : 1));
    const seen = new Map();
    return live.map((r) => {
      const sec = r.el.closest("[data-key]");
      const nm = r.el.querySelector(".slider-name");
      const base = (sec ? sec.dataset.key : "") + "|" + (nm ? nm.textContent.trim() : "");
      const n = (seen.get(base) || 0) + 1;
      seen.set(base, n);
      return [r, base + "|" + String(n)];
    });
  }

  // How many rows the portrait would hold right now. A race or sex change
  // rebuilds some sections a moment after the game answered (E31); this is
  // what the history waits on before it puts values back.
  function portraitSize() {
    return liveRows().length;
  }

  function portrait() {
    const rows = [];
    for (const [r, key] of liveRows()) {
      rows.push([r, r.snapshot(), key, r.ident()]);
    }
    const parts = {};
    for (const [key, part] of portraitParts) {
      parts[key] = part.get();
    }
    return { rows: rows, parts: parts };
  }

  // Puts back only what differs from the portrait. Called after every step
  // newer than the portrait was undone, so what differs is what the step
  // changed. A row that left the page since the portrait (its section
  // rebuilt) is found again by its key. Returns how many values were written.
  function restorePortrait(a_portrait) {
    let written = 0;
    let byKey = null;
    for (const [r, v, key, id] of a_portrait.rows) {
      if (v === null) {
        continue;
      }
      let row = r;
      if (!r.el.isConnected) {
        if (byKey === null) {
          byKey = new Map(liveRows().map(([lr, lk]) => [lk, lr]));
        }
        row = byKey.get(key);
      }
      if (row && row.restore(v, id)) {
        written += 1;
      }
    }
    for (const [key, part] of portraitParts) {
      const want = a_portrait.parts[key];
      if (want !== undefined && JSON.stringify(want) !== JSON.stringify(part.get())) {
        part.restore(want);
        written += 1;
      }
    }
    return written;
  }

  // Opens the section named a_key if it is collapsed, and scrolls it into
  // view. Returns false when the key names no mounted section - the caller
  // (a shortcut button, e.g.) decides what that means; this function does
  // not, so it stays correct whether the section is conditional or not.
  function reveal(a_key) {
    const host = document.getElementById("sections");
    const s = host.querySelector('.section[data-key="' + a_key + '"]');
    if (!s) {
      return false;
    }
    if (s.dataset.open !== "1") {
      s.dataset.open = "1";
      const head = s.querySelector(".section-head");
      if (head) {
        head.setAttribute("aria-expanded", "true");
      }
    }
    s.scrollIntoView({ block: "start", behavior: "smooth" });
    return true;
  }

  return { el: el, section: section, slot: slot, mount: mount, setOrder: setOrder, setGroups: setGroups, row: row, reveal: reveal,
    portrait: portrait, restorePortrait: restorePortrait, portraitSize: portraitSize, addPortraitPart: addPortraitPart };
})();
