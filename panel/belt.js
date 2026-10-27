// belt.js
// Lifecast - the belt and the small panel (Fase 7, E4: the LB structure).
//
// The screen is the character's. Along the bottom runs a belt: the title,
// the categories, search and the panel's buttons. Choosing a category opens a
// small panel anchored over the belt's left end, showing ONE region (one
// section) of it, with the sister regions as chips at its head. All shows
// every section, laid out as one running list (name, slider, value, one line
// each); search and Changed show every section that matches. The
// categories are groups.js's groups - this file knows none by name.
//
// Where things live:
//   - the row counter sits at the head of the small panel, shown whenever
//     more than one region is on screen (All, search, Changed); in a single
//     region the belt's per-category numbers say it. The head is OUTSIDE
//     #sections on purpose: search.js watches the whole subtree of #sections
//     and rewrites the counter on every mutation, so a counter inside it feeds
//     its own observer forever (measured in the E4 probe: the page hung);
//   - the version line is in Settings > Debug (D132); the name and Finish are
//     on the belt, search and Changed over the upper left corner (D129),
//     undo and redo in the Character Control bar (D130).
//
// Narrow (D136): when the belt does not fit the screen in one row, or leaves
// the name field too small to type in, <html> gets data-belt-narrow and the
// stylesheet folds it - the counts go, and the name and Finish go up to a
// row of their own over the belt. The categories are a strip that scrolls
// (D146), so the quick buttons are never folded (D126 whole again). The test
// is always made on the wide row, so the two layouts cannot chase each other.
//
// Opening: the page outlives the menu (presets.js receives "menu-closed"),
// so the belt re-opens as the player left it in this run of the game, and
// each category re-opens on the region last shown in it. The first open shows
// no category, on purpose. Keeping it across games needs a settings key the
// DLL does not accept today.
"use strict";

const LifecastBelt = (() => {
  const root = document.documentElement;
  const nav = document.getElementById("belt");
  const host = document.getElementById("sections");
  const chips = document.getElementById("regions");
  const search = document.getElementById("search");
  const changed = document.getElementById("filter-changed");

  if (!nav || !host || !chips) {
    return null;
  }

  // cat: the open category, or null. region: the section key shown.
  // last: the region last shown per category, for this run of the game.
  const ui = { cat: null, region: null, all: false, results: false, last: new Map() };

  function sections() {
    return Array.prototype.slice.call(host.querySelectorAll(":scope > .section"));
  }

  // data-group is written by controls.js's arrange(), from groups.js.
  function inCat(name) {
    return sections().filter((s) => s.dataset.group === name);
  }

  // The categories ride a strip that scrolls (D146): an arrow on each side,
  // the mouse wheel over it, and All outside it, fixed, beside the quick
  // buttons. Everything else on the belt stays where it is.
  const prev = document.createElement("button");
  prev.type = "button";
  prev.tabIndex = -1;
  prev.className = "panel-button belt-arrow";
  prev.textContent = "\u25C0";
  prev.title = "Scroll the categories left";
  const track = document.createElement("div");
  track.className = "belt-track";
  const next = document.createElement("button");
  next.type = "button";
  next.tabIndex = -1;
  next.className = "panel-button belt-arrow";
  next.textContent = "\u25B6";
  next.title = "Scroll the categories right";
  nav.appendChild(prev);
  nav.appendChild(track);
  nav.appendChild(next);

  // A clipped strip starts at its RIGHT end (2026-10-10): RACE and
  // BODY are what a new character goes to first. It stays pinned there,
  // through every relayout (fonts, scale, resize), until the player moves it
  // - the wheel or an arrow - and from then on it stays where they left it.
  // The page lives for the whole run of the game, so this is the first open
  // of each game; a strip that fits is never touched.
  let pinRight = true;

  function unpin() {
    pinRight = false;
  }

  // The arrows are on screen only while the strip is clipped: a strip that
  // fits has nothing to scroll, and the wide row is then the row it was
  // before the strip existed.
  function reach() {
    const max = track.scrollWidth - track.clientWidth;
    if (pinRight && max > 0.5 && track.scrollLeft < max - 0.5) {
      track.scrollLeft = max;
    }
    prev.hidden = next.hidden = max <= 0.5;
    prev.disabled = track.scrollLeft <= 0.5;
    next.disabled = track.scrollLeft >= max - 0.5;
  }

  function slide(dir) {
    track.scrollBy({ left: dir * Math.max(80, track.clientWidth * 0.6), behavior: "smooth" });
  }

  prev.addEventListener("click", () => { unpin(); slide(-1); });
  next.addEventListener("click", () => { unpin(); slide(1); });
  track.addEventListener("scroll", reach);
  // The wheel over the strip rolls it sideways; the camera's wheel is on
  // #stage (stage.js) and never gets here, so there is nothing to share.
  track.addEventListener("wheel", (e) => {
    const d = Math.abs(e.deltaX) > Math.abs(e.deltaY) ? e.deltaX : e.deltaY;
    if (d !== 0 && track.scrollWidth > track.clientWidth) {
      unpin();
      track.scrollLeft += d;
      reach();
      e.preventDefault();
    }
  }, { passive: false });

  const buttons = new Map();
  for (const name of LifecastGroups.names.concat(["All"])) {
    const b = document.createElement("button");
    b.type = "button";
    b.tabIndex = -1;
    b.className = "belt-cat";
    b.dataset.cat = name;
    const label = document.createElement("span");
    label.textContent = name;
    const n = document.createElement("span");
    n.className = "belt-count";
    b.appendChild(label);
    b.appendChild(n);
    b.addEventListener("click", () => choose(name));
    (name === "All" ? nav : track).appendChild(b);
    buttons.set(name, b);
  }

  // The strip's reach changes whenever a category's width does - its count
  // arrives, the fonts load, the narrow fold drops the counts - and none of
  // that resizes the window. Measured at 1280x720: pinned at 89 px, the
  // strip then grew to 153 and the pin was left short.
  if (typeof ResizeObserver !== "undefined") {
    const watch = new ResizeObserver(reach);
    watch.observe(track);
    Array.prototype.forEach.call(track.children, (b) => watch.observe(b));
  }

  // Settings (D132): a category like the others, but its button is the
  // footer's, beside RaceMenu's, and carries no count.
  for (const name of LifecastGroups.footer) {
    const b = document.querySelector('.panel-footer [data-cat="' + name + '"]');
    if (b) {
      b.addEventListener("click", () => choose(name));
      buttons.set(name, b);
    }
  }

  // The always-visible buttons (D126), after the
  // categories: Presets and OBody open their region from anywhere, Sculpt
  // hands over to RaceMenu (the one thing the Lifecast does not do - D125).
  // OBody shows only when its section exists: without the mod there is no
  // section (D007), and a button to nothing would be a lie.
  const QUICK = [
    { name: "Presets", cat: "Presets", key: "presets" },
    { name: "OBody", cat: "Body", key: "obody" },
    { name: "Sculpt", title: "Opens RaceMenu's Sculpt (F4 comes back)", send: "sculpt" },
  ];
  const quick = [];
  const sep = document.createElement("span");
  sep.className = "belt-sep";
  nav.appendChild(sep);
  // The quick buttons are one group and are never folded: they are what
  // the player reaches for from any category (D126, whole again in D146).
  const group = document.createElement("span");
  group.className = "belt-quick-group";
  nav.appendChild(group);

  for (const q of QUICK) {
    const b = document.createElement("button");
    b.type = "button";
    b.tabIndex = -1;
    b.className = "panel-button belt-quick";
    // A plain id per quick button: a test harness that clicks by selector
    // takes only an id, a class or a tag.
    b.id = "quick-" + (q.key || q.send);
    b.textContent = q.name;
    if (q.title) {
      b.title = q.title;
    }
    b.addEventListener("click", () => {
      if (q.send) {
        LifecastBridge.send("show-racemenu", { for: q.send });
      } else {
        openRegion(q.cat, q.key);
      }
    });
    group.appendChild(b);
    quick.push({ q: q, el: b });
  }

  // A second click on the open category closes the panel.
  function choose(name) {
    if (name === "All") {
      ui.all = !ui.all;
      ui.cat = null;
    } else {
      ui.all = false;
      ui.cat = ui.cat === name ? null : name;
      if (ui.cat) {
        const keys = inCat(ui.cat).map((s) => s.dataset.key);
        const kept = ui.last.get(ui.cat);
        ui.region = keys.indexOf(kept) >= 0 ? kept : (keys[0] || null);
      }
    }
    apply();
    host.scrollTop = 0;
  }

  // One region of one category, from a quick button; pressed again, closes.
  function openRegion(cat, key) {
    const shown = !ui.all && !ui.results && ui.cat === cat && ui.region === key;
    ui.all = false;
    ui.cat = shown ? null : cat;
    ui.region = shown ? ui.region : key;
    apply();
    host.scrollTop = 0;
  }

  function chooseRegion(key) {
    ui.region = key;
    apply();
    host.scrollTop = 0;
  }

  // search.js does the filtering and the counting; this only decides that
  // the panel is open and shows every region while a filter is on.
  function filtering() {
    return (search && search.value.trim() !== "") ||
      (changed && changed.getAttribute("aria-pressed") === "true");
  }

  // ----- the form of All (F22, D211) --------------------------------------
  // RaceMenu's ALL, as a form: one line per slider (name, slider, value), a
  // colour as ONE line with a diamond, every face slider under its full name.
  // The first 46 lines follow RaceMenu's order and names; what is only ours
  // (body morphs, scales, overlays...) comes after them, in the order of the
  // sections, under its section's head. The parity is of FORM, not of SET
  // (D211): the native-only $ rows are out of v1 (D196) and are not here.
  //
  // Nothing is moved: the sections and their wrappers become display:contents
  // so that every control is a flex item of #sections, and `order` sorts
  // them. A control is found by its section key and its (first-text) name.
  // Leaving All takes every class, order and renamed text back.
  const NATIVE = [
    ["sex"],
    ["face:RacePresets", "Preset", "Presets"],
    ["head", "Skin Tone"],
    ["look:Body", "Weight"],
    ["look:Features", "Complexion"],
    ["pick", "Dirt", "Dirt"],
    ["head", "Dirt", "Dirt Color"],
    ["look:Features", "Scar", "Scars"],
    ["pick", "War Paint", "War Paint"],
    ["head", "War Paint", "War Paint Color"],
    ["look:Hair", "Hair"],
    ["look:Hair", "Hair Color"],
    ["face:Eyes", "Eye Shape"],
    ["face:Eyes", "Eye Color"],
    ["face:Eyes", "Height", "Eye Height"],
    ["face:Eyes", "Width", "Eye Width"],
    ["face:Eyes", "Depth", "Eye Depth"],
    ["head", "Eyeliner", "Eyeliner Color"],
    ["head", "Upper Eye Socket", "Eye Shadow"],
    ["head", "Lower Eye Socket", "Eye Tint"],
    ["face:Brows", "Brow Type"],
    ["face:Brows", "Height", "Brow Height"],
    ["face:Brows", "Width", "Brow Width"],
    ["face:Brows", "Forward", "Brow Forward"],
    ["face:Nose", "Nose Type"],
    ["face:Nose", "Height", "Nose Height"],
    ["face:Nose", "Length", "Nose Length"],
    ["face:Jaw", "Width", "Jaw Width"],
    ["face:Jaw", "Height", "Jaw Height"],
    ["face:Jaw", "Forward", "Jaw Forward"],
    ["face:Cheekbones", "Height", "Cheekbone Height"],
    ["face:Cheekbones", "Width", "Cheekbone Width"],
    ["head", "Cheeks", "Cheek Color"],
    ["head", "Laugh Lines"],
    ["head", "Lower Cheeks", "Cheek Color Lower"],
    ["head", "Nose", "Nose Color"],
    ["head", "Chin", "Chin Color"],
    ["head", "Neck", "Neck Color"],
    ["head", "Forehead", "Forehead Color"],
    ["face:Mouth", "Mouth Shape"],
    ["face:Mouth", "Height", "Mouth Height"],
    ["face:Mouth", "Forward", "Mouth Forward"],
    ["face:Chin", "Width", "Chin Width"],
    ["face:Chin", "Length", "Chin Length"],
    ["face:Chin", "Forward", "Chin Forward"],
    ["head", "Lips", "Lip Color"],
  ];
  const TAIL = 1000;
  const shaped = { renamed: [], els: [] };

  // The name a row or head goes by: its own first text, never the "list"
  // chip or the colour chip that sit beside it in the same span.
  function nameNode(el) {
    const n = el.querySelector(".slider-name");
    const t = n && n.firstChild;
    return t && t.nodeType === 3 ? t : null;
  }

  function nameOf(el) {
    const t = nameNode(el);
    return t ? t.data.trim() : "";
  }

  function rename(el, to) {
    const t = nameNode(el);
    if (t && t.data !== to) {
      shaped.renamed.push([t, t.data]);
      t.data = to;
    }
  }

  function unshape() {
    for (const [t, old] of shaped.renamed) {
      t.data = old;
    }
    for (const el of shaped.els) {
      el.classList.remove("all-wrap", "all-item", "all-hide", "all-line-head", "all-line-slider");
      el.style.removeProperty("order");
    }
    shaped.renamed = [];
    shaped.els = [];
  }

  function mark(el, ...classes) {
    el.classList.add(...classes);
    if (shaped.els.indexOf(el) < 0) {
      shaped.els.push(el);
    }
  }

  // The controls of a section, through the bare wrapper divs that tints.js
  // and sliders.js keep in a section body.
  function itemsOf(section) {
    const out = [];
    const walk = (parent) => {
      for (const c of parent.children) {
        if (c.tagName === "DIV" && c.className === "") {
          mark(c, "all-wrap");
          walk(c);
        } else {
          out.push(c);
        }
      }
    };
    const body = section.querySelector(":scope > .section-body");
    if (body) {
      mark(body, "all-wrap");
      walk(body);
    }
    return out;
  }

  function shape() {
    unshape();
    const taken = new Set();
    const flat = sections().filter((s) => s.classList.contains("is-flat"));
    // Everything that can be named, per section, in document order.
    const lines = []; // { sec, el, name, head, block }
    for (const s of flat) {
      mark(s, "all-wrap");
      const items = itemsOf(s);
      let block = null;
      for (const el of items) {
        if (el.classList.contains("tint-head")) {
          block = { head: el, label: nameOf(el), members: [] };
        } else if (block && !el.classList.contains("slider") &&
                   !el.classList.contains("picker") && !el.classList.contains("swatch-grid")) {
          block = null;
        }
        lines.push({ sec: s, el: el, name: el.classList.contains("slider") ? nameOf(el) : "",
          block: block });
        if (block && el !== block.head) {
          block.members.push(el);
        }
      }
    }
    const slot = (n) => (n + 1) * 10;
    const blockOf = (label) => {
      const l = lines.find((x) => x.block && x.el === x.block.head && x.block.label === label);
      return l ? l.block : null;
    };
    NATIVE.forEach((spec, i) => {
      const [kind, key, as] = spec;
      const at = slot(i);
      if (kind === "sex") {
        const s = host.querySelector(':scope > .section[data-key="sex"]');
        if (s) {
          s.style.order = String(at);
          mark(s, "all-item");
          taken.add(s);
        }
      } else if (kind === "head" || kind === "pick") {
        const block = blockOf(key);
        if (!block) {
          return;
        }
        if (kind === "pick") {
          const row = block.members.find((m) => m.classList.contains("slider") && nameOf(m) === "Texture");
          if (row) {
            rename(row, as);
            row.style.order = String(at);
            mark(row, "all-item");
            taken.add(row);
          }
          return;
        }
        // The colour line: the head (name and diamond) and the Intensity
        // row side by side; the picker opens under them.
        rename(block.head, as || key);
        block.head.style.order = String(at);
        mark(block.head, "all-item", "all-line-head");
        taken.add(block.head);
        for (const m of block.members) {
          const n = m.classList.contains("slider") ? nameOf(m) : "";
          if (n === "Intensity") {
            m.style.order = String(at + 1);
            mark(m, "all-item", "all-line-slider");
          } else if (m.classList.contains("picker")) {
            m.style.order = String(at + 2);
            mark(m, "all-item");
          } else if (n === "Red" || n === "Green" || n === "Blue" ||
                     m.classList.contains("swatch-grid")) {
            mark(m, "all-hide");
          } else {
            continue; // Catalog: a line of ours, goes with the tail
          }
          taken.add(m);
        }
      } else {
        const l = lines.find((x) => x.sec.dataset.key === kind && x.name === key && !taken.has(x.el));
        if (l) {
          if (as) {
            rename(l.el, as);
          }
          l.el.style.order = String(at);
          mark(l.el, "all-item");
          taken.add(l.el);
        }
      }
    });
    // The rest, after the 46, in the order of the sections; a section's head
    // stands before its first control, and is gone when it has none left.
    let n = 0;
    for (const s of flat) {
      const rest = lines.filter((x) => x.sec === s && !taken.has(x.el));
      const head = s.querySelector(":scope > .section-head");
      if (head) {
        mark(head, "all-item");
        if (rest.length === 0) {
          mark(head, "all-hide");
        } else {
          head.style.order = String(TAIL + n * 10);
          n += 1;
        }
      }
      for (const x of rest) {
        x.el.style.order = String(TAIL + n * 10);
        mark(x.el, "all-item");
        n += 1;
      }
    }
    for (const s of sections()) {
      if (!s.classList.contains("is-flat") && !s.classList.contains("is-off") && !taken.has(s)) {
        s.style.order = String(TAIL + n * 10);
        mark(s, "all-item");
        n += 1;
      }
    }
  }

  function apply() {
    ui.results = filtering();
    unshape();
    const wide = ui.all || ui.results;
    const region = !wide && !!ui.cat;
    if (region && ui.region) {
      ui.last.set(ui.cat, ui.region);
    }
    for (const s of sections()) {
      const on = region && s.dataset.key === ui.region;
      s.classList.toggle("is-region", on);
      s.classList.toggle("is-off", region ? !on : !wide);
      // All reads as one running list, as RaceMenu's ALL does (D184): the
      // sections that hold sliders are laid open, one line per slider. A
      // section with no slider (Presets, Body...) keeps its own look.
      s.classList.toggle("is-flat", ui.all && !!s.querySelector(".slider"));
    }
    for (const [name, b] of buttons) {
      const n = name === "All" ? host.querySelectorAll(".slider").length
        : inCat(name).reduce((t, s) => t + s.querySelectorAll(".slider").length, 0);
      const count = b.querySelector(".belt-count");
      if (count) {
        count.textContent = n > 0 ? String(n) : "";
      }
      const on = name === "All" ? ui.all && !ui.results : name === ui.cat && !wide;
      b.classList.toggle("is-on", on);
      b.setAttribute("aria-pressed", on ? "true" : "false");
    }
    for (const { q, el } of quick) {
      if (q.key) {
        el.hidden = !host.querySelector(':scope > .section[data-key="' + q.key + '"]');
        el.classList.toggle("is-on", region && ui.cat === q.cat && ui.region === q.key);
      }
    }
    if (ui.all) {
      shape();
    }
    fit();
    root.toggleAttribute("data-belt-open", wide || region);
    root.toggleAttribute("data-belt-wide", wide);
    root.toggleAttribute("data-belt-all", ui.all);
    root.toggleAttribute("data-belt-region", region);
    chips.textContent = "";
    if (region) {
      for (const s of inCat(ui.cat)) {
        const r = document.createElement("button");
        r.type = "button";
        r.tabIndex = -1;
        r.className = "region-chip" + (s.dataset.key === ui.region ? " is-on" : "");
        r.dataset.key = s.dataset.key;
        r.textContent = (s.querySelector(".section-title") || {}).textContent || s.dataset.key;
        r.addEventListener("click", () => chooseRegion(s.dataset.key));
        chips.appendChild(r);
      }
    }
  }

  // ----- narrow ----------------------------------------------------------
  // Measured in the E5 probe at 1280x720: the wide row needs 1456 px at 80%
  // and leaves the name field 18 px; folded, it fits up to 120%. A name
  // field under NAME_MIN counts as not fitting even when nothing overflows.
  //
  // The strip counts for its DECLARED minimum (the arrows plus the CSS
  // min-width of .belt-track), never for its content: with the strip
  // scrolling, what does not fit is clipped inside it and the belt cell's
  // scrollWidth stops growing, so a sum of contents would call every row
  // wide and the belt would run off the screen without ever folding (D154).
  const NAME_MIN = 80;
  const cells = [".panel-header", ".panel-toolbar", ".panel-footer"]
    .map((sel) => document.querySelector(sel))
    .filter((el) => el);
  const name = document.getElementById("finish-name");

  // The width the belt cell needs at the least: padding, every fixed item,
  // the two arrows (on screen or not - they come the moment the strip is
  // clipped) and the strip's declared min-width.
  function beltMin() {
    const cs = getComputedStyle(nav);
    const gap = parseFloat(cs.columnGap) || 0;
    const pad = (parseFloat(cs.paddingLeft) || 0) + (parseFloat(cs.paddingRight) || 0);
    const arrow = parseFloat(getComputedStyle(prev).width) || 0;
    const kids = Array.prototype.filter.call(nav.children, (k) =>
      k !== track && k !== prev && k !== next && k.offsetParent !== null);
    const fixed = kids.reduce((t, k) => t + k.getBoundingClientRect().width +
      (parseFloat(getComputedStyle(k).marginLeft) || 0), 0);
    return pad + fixed + 2 * arrow + (parseFloat(getComputedStyle(track).minWidth) || 0) +
      gap * (kids.length + 2);
  }

  // The belt's grid column is told this minimum, so that the strip is what
  // gives way when the screen is short, and never the buttons beside it.
  const panel = document.getElementById("panel");

  function fit() {
    prev.hidden = next.hidden = true;
    root.removeAttribute("data-belt-narrow");
    const min = beltMin();
    if (panel) {
      panel.style.setProperty("--belt-min", min + "px");
    }
    const need = cells.reduce((t, el) => t + el.scrollWidth, 0) + min;
    const narrow = need > window.innerWidth ||
      (!!name && name.getBoundingClientRect().width < NAME_MIN);
    root.toggleAttribute("data-belt-narrow", narrow);
    reach();
  }

  window.addEventListener("resize", fit);
  // The panel scale is a style on <html>; fit() itself only touches
  // data-belt-narrow, which this does not watch.
  new MutationObserver(fit).observe(root, { attributes: true, attributeFilter: ["style"] });
  if (document.fonts && document.fonts.ready) {
    document.fonts.ready.then(fit);
  }

  // Sections mount as direct children of #sections, some on the DLL's own
  // time, and arrange() re-appends them: childList is enough, and one apply
  // per frame is plenty.
  let queued = false;
  new MutationObserver(() => {
    if (queued) {
      return;
    }
    queued = true;
    requestAnimationFrame(() => {
      queued = false;
      apply();
    });
  }).observe(host, { childList: true });

  // All is shaped from what is there: a control that arrives later (the
  // tint layers and lists come on the DLL's own time) must be shaped too.
  // Only elements coming or going count; a value's text changing on every
  // drag frame does not.
  let reshape = false;
  new MutationObserver((records) => {
    if (!ui.all || reshape || !records.some((r) =>
      Array.prototype.some.call(r.addedNodes, (n) => n.nodeType === 1) ||
      Array.prototype.some.call(r.removedNodes, (n) => n.nodeType === 1))) {
      return;
    }
    reshape = true;
    requestAnimationFrame(() => {
      reshape = false;
      if (ui.all) {
        shape();
      }
    });
  }).observe(host, { childList: true, subtree: true });

  if (search) {
    search.addEventListener("input", apply);
  }
  if (changed) {
    changed.addEventListener("click", apply);
  }

  apply();

  return {
    choose: choose,
    chooseRegion: chooseRegion,
    openRegion: openRegion,
    state: () => ({ cat: ui.cat, region: ui.region, all: ui.all, results: ui.results }),
  };
})();
