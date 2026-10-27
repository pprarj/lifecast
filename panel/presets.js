// presets.js
// Lifecast - the presets section: the presets on disk, searched and sorted
// here, a preset to load, and a name to save the current character under.
//
// The DLL sends the whole list once (and again when the panel comes back on
// screen, or after a save); search and sort run over that copy, so typing
// sends nothing across the bridge. Whether each file reads as a preset
// arrives later, as a second message, and marks the rows that do not - a
// bad file never costs the list.
//
// Loading and saving are RaceMenu's, asked for from here: each row has its own
// Load, which applies that preset whole in one click (D128), and Save writes
// the character under the field's name. Each row also has an "i" that opens,
// under the row, what the preset holds - its hair, eyes, brows and scar, how
// many sliders and layers, and the plugins it needs, a missing one marked.
// That description is the DLL's, sent with the check ("presets-checked",
// "info"): the page shows what arrived and says nothing it was not told. A
// DLL that sends no description leaves the date and the size.
// A load the DLL gave up on can still answer later ("late"), and that later
// answer is the one that describes the character.
// Saving over an existing preset can be refused by the DLL when the overwrite
// would destroy parts of that file the live character was not given here;
// the page then lists them and offers a new name or RaceMenu's own save.
// An overwrite the DLL allows is previewed first: when the file would gain or
// lose entries (a skin override left by an earlier load, say), the page shows
// both, with the same weight, and the file is written only on "Save anyway".
//
// ONE text field in this section: it searches the list, and its text is also
// the name the Save button uses. The page as a whole no longer has only one -
// the character's name field (finish.js) is the second - and keyboard.js,
// which covers every text field on the page, is shared by both: its window
// for a doubled key press is the page's, not the field's.
//
// The sort order is the one preference the panel keeps across games: it is
// read from the DLL's settings (settings.js) and written back when the player
// changes it.
"use strict";

(() => {
  // A second press within this time confirms overwriting a preset that
  // already has the name.
  const CONFIRM_MS = 3000;

  // The longest preset name the DLL accepts.
  const NAME_MAX = 64;

  let items = [];          // { name, time, size, bad: null | why }
  const infoBy = new Map(); // name (lower case) -> the DLL's description
  let opened = null;       // name of the row whose description is open
  let loadingName = null;  // the preset a load was asked for
  let sortBy = "name";     // "name" | "time"
  let selected = null;     // name of the selected row
  let confirmUntil = 0;
  let notice = "";         // the last outcome, kept across re-renders
  let loading = false;     // a load was asked and has not answered yet

  const section = LifecastControls.section("Presets");

  const field = LifecastControls.el("input", "preset-field");
  // Plain ids for a test harness that clicks by selector (id, class or tag).
  field.id = "preset-search";
  field.type = "search";
  field.placeholder = "Search, or a name to save as";
  field.spellcheck = false;
  field.autocomplete = "off";

  const bar = LifecastControls.el("div", "preset-bar");
  const byName = LifecastControls.el("button", "panel-button", "A-Z");
  const byTime = LifecastControls.el("button", "panel-button", "Newest");
  const save = LifecastControls.el("button", "panel-button preset-save", "Save");
  save.id = "preset-save";
  for (const b of [byName, byTime, save]) {
    b.type = "button";
    b.tabIndex = -1;
    bar.appendChild(b);
  }

  const status = LifecastControls.el("div", "preset-status", "--");

  // Shown only when RaceMenu did not save or load at the panel's request (an
  // older RaceMenu, or the call failing): its own Presets page does both, one
  // click away. The list, search and sort stay here either way.
  const fallback = LifecastControls.el("div", "delegate preset-fallback");
  const fallbackText = LifecastControls.el("div", "delegate-text");
  const fallbackTitle = LifecastControls.el("span", "slider-name", "");
  const fallbackNote = LifecastControls.el("span", "delegate-note", "");
  fallbackText.appendChild(fallbackTitle);
  fallbackText.appendChild(fallbackNote);
  const fallbackGo = LifecastControls.el("button", "panel-button", "Open");
  fallbackGo.type = "button";
  fallbackGo.tabIndex = -1;
  fallbackGo.title = "Opens RaceMenu (F4 comes back)";
  fallbackGo.addEventListener("click", () => LifecastBridge.send("show-racemenu", { for: "presets" }));
  fallback.appendChild(fallbackText);
  fallback.appendChild(fallbackGo);
  fallback.hidden = true;

  // Shown when the DLL refused to save over an existing preset: what the
  // overwrite would have destroyed, and the two ways on.
  const guard = LifecastControls.el("div", "delegate preset-guard");
  const guardText = LifecastControls.el("div", "delegate-text");
  const guardTitle = LifecastControls.el("span", "slider-name", "");
  const guardNote = LifecastControls.el("span", "delegate-note", "");
  guardText.appendChild(guardTitle);
  guardText.appendChild(guardNote);
  const guardRename = LifecastControls.el("button", "panel-button", "New name");
  guardRename.title = "Keeps the existing preset and saves under a free name";
  const guardRaceMenu = LifecastControls.el("button", "panel-button", "RaceMenu");
  guardRaceMenu.title = "Opens RaceMenu, whose own save replaces the file (F4 comes back)";
  for (const b of [guardRename, guardRaceMenu]) {
    b.type = "button";
    b.tabIndex = -1;
  }
  guardRaceMenu.addEventListener("click", () => LifecastBridge.send("show-racemenu", { for: "presets" }));
  guard.appendChild(guardText);
  guard.appendChild(guardRename);
  guard.appendChild(guardRaceMenu);
  guard.hidden = true;

  // Shown when an overwrite would change what the existing file holds: what it
  // would gain and what it would lose, compared entry by entry by the DLL.
  const preview = LifecastControls.el("div", "delegate preset-preview");
  const previewText = LifecastControls.el("div", "delegate-text");
  const previewTitle = LifecastControls.el("span", "slider-name", "");
  const previewGain = LifecastControls.el("span", "delegate-note", "");
  const previewLoss = LifecastControls.el("span", "delegate-note", "");
  // What the panel wrote itself in this menu session - a body chosen in the
  // Body section. Listed apart from the gains, never mixed into them: a gain
  // is what the player did not do here.
  const previewChosen = LifecastControls.el("span", "delegate-note", "");
  // What the two lists cover, said in the box: a list the player reads as
  // complete is worse than no list.
  const previewScope = LifecastControls.el("span", "delegate-note", "");
  previewText.appendChild(previewTitle);
  previewText.appendChild(previewGain);
  previewText.appendChild(previewLoss);
  previewText.appendChild(previewChosen);
  previewText.appendChild(previewScope);
  const previewSave = LifecastControls.el("button", "panel-button", "Save anyway");
  previewSave.title = "Writes the preset exactly as compared here";
  const previewCancel = LifecastControls.el("button", "panel-button", "Cancel");
  previewCancel.title = "Keeps the existing preset as it is";
  for (const b of [previewSave, previewCancel]) {
    b.type = "button";
    b.tabIndex = -1;
  }
  preview.appendChild(previewText);
  preview.appendChild(previewSave);
  preview.appendChild(previewCancel);
  preview.hidden = true;
  let previewName = null;  // the name the shown preview is for
  let reveal = null;       // a box just shown, brought into view by the next render

  const list = LifecastControls.el("ul", "preset-list");

  // The two tabs of this section. "RaceMenu Portraits" is another mod's
  // window, drawn in front of the scene while the panel steps back to its
  // card; it exists only when the DLL says that mod and its menu framework
  // are loaded (never a dependency). F4 comes back here, on "Presets".
  const tabs = LifecastControls.el("div", "preset-tabs");
  const tabPresets = LifecastControls.el("button", "panel-button is-on", "Presets");
  const tabPortraits = LifecastControls.el("button", "panel-button", "RaceMenu Portraits");
  tabPortraits.title = "Opens RaceMenu Portraits' own window over the scene (F4 comes back)";
  for (const b of [tabPresets, tabPortraits]) {
    b.type = "button";
    b.tabIndex = -1;
    tabs.appendChild(b);
  }
  tabs.hidden = true;
  tabPortraits.addEventListener("click", () => LifecastBridge.send("show-portraits"));
  LifecastBridge.on("portraits", (data) => {
    tabs.hidden = !(data && data.available === true);
  });

  section.body.appendChild(tabs);
  section.body.appendChild(field);
  section.body.appendChild(bar);
  section.body.appendChild(status);
  section.body.appendChild(fallback);
  section.body.appendChild(guard);
  section.body.appendChild(preview);
  section.body.appendChild(list);
  LifecastControls.mount(section, "presets");

  function same(a, b) {
    return a.toLowerCase() === b.toLowerCase();
  }

  function dateText(seconds) {
    if (!seconds) {
      return "";
    }
    const d = new Date(seconds * 1000);
    const pad = (n) => String(n).padStart(2, "0");
    return d.getFullYear() + "-" + pad(d.getMonth() + 1) + "-" + pad(d.getDate());
  }

  // What a refused overwrite would have destroyed, in the player's words.
  const LOSS_WORDS = {
    sculpt: () => "sculpt",
    custom: (n) => n + " extended sliders",
    bodyMorphs: (n) => n + " body morphs",
    transforms: (n) => n + " node transforms",
    overrides: (n) => n + " overlays",
    skinOverrides: (n) => n + " skin overrides",
  };

  function lossText(loss) {
    return loss
      .map((l) => (LOSS_WORDS[l.section] || ((n) => n + " " + l.section))(l.count))
      .join(", ");
  }

  // What an overwrite would gain or lose, entry by entry, in the player's words.
  const ENTRY_WORDS = {
    sculpt: ["sculpted mesh", "sculpted meshes"],
    custom: ["extended slider", "extended sliders"],
    bodyMorphs: ["body morph value", "body morph values"],
    transforms: ["node transform value", "node transform values"],
    overrides: ["overlay value", "overlay values"],
    skinOverrides: ["skin override", "skin overrides"],
    headParts: ["head part", "head parts"],
    tintInfo: ["tint layer", "tint layers"],
    faceTextures: ["face texture", "face textures"],
  };

  function entryText(diff, side) {
    return diff
      .filter((d) => d[side] > 0)
      .map((d) => {
        const w = ENTRY_WORDS[d.section] || [d.section + " entry", d.section + " entries"];
        return d[side] + " " + (d[side] === 1 ? w[0] : w[1]);
      })
      .join(", ");
  }

  function entryLabels(diff, side) {
    const key = side === "gained" ? "gainedLabels" : side === "chosen" ? "chosenLabels" : "lostLabels";
    return diff
      .filter((d) => Array.isArray(d[key]) && d[key].length)
      .map((d) => d.section + ": " + d[key].join("; "))
      .join("\n");
  }

  function hidePreview(cancel) {
    if (cancel && previewName !== null) {
      LifecastBridge.send("preset-save-cancel", { name: previewName });
    }
    previewName = null;
    preview.hidden = true;
  }

  // The first "<name> 2", "<name> 3"... that no preset uses and that stays
  // within the length the DLL accepts.
  function freeName(name) {
    for (let i = 2; i < 1000; i++) {
      const suffix = " " + i;
      const candidate = name.slice(0, NAME_MAX - suffix.length).trimEnd() + suffix;
      if (!items.some((it) => same(it.name, candidate))) {
        return candidate;
      }
    }
    return name;
  }

  function showFallback(what) {
    fallbackTitle.textContent = what === "load" ? "Load in RaceMenu" : "Save in RaceMenu";
    fallbackNote.textContent = what === "load"
      ? "RaceMenu did not load from here. Its own Presets page loads the same files."
      : "RaceMenu did not save from here. Its own Presets page saves the same files.";
    fallback.hidden = false;
    reveal = fallback;
  }

  // A box the player has to read or answer is on screen when it appears,
  // wherever the panel is scrolled: the boxes sit in this section, which
  // scrolls with the panel body. The body scrolls just enough to show the
  // whole box, or its top when it is taller than the body.
  function bringIntoView(box) {
    const body = box.closest(".panel-body");
    if (!body || box.hidden) {
      return;
    }
    const b = body.getBoundingClientRect();
    const r = box.getBoundingClientRect();
    if (r.top < b.top) {
      body.scrollTop += r.top - b.top;
    } else if (r.bottom > b.bottom) {
      body.scrollTop += Math.min(r.bottom - b.bottom, r.top - b.top);
    }
  }

  // Where bringIntoView leaves a box: all of it inside the body, or its top
  // at the body's top when it is taller. One pixel of slack for fractions.
  function inView(box) {
    const body = box.closest(".panel-body");
    if (!body || box.hidden) {
      return false;
    }
    const b = body.getBoundingClientRect();
    const r = box.getBoundingClientRect();
    return r.top >= b.top - 1 && (r.bottom <= b.bottom + 1 || r.top <= b.top + 1);
  }

  // A box brought into view stays in view while it is shown. When something
  // above it changes height - the status line wrapping to a second line, for
  // one - the box moves under a panel body that did not scroll, and its
  // buttons can end below the edge. So every height that can move it is
  // watched: this section's, and those of the sections above it. A section
  // mounted later is appended after this one and cannot move it. The box is
  // followed only while it sits where bringIntoView put it: a player who
  // scrolls it away is left where they scrolled.
  let kept = null;
  let keptInView = false;
  const keeper = typeof ResizeObserver === "function"
    ? new ResizeObserver(() => {
      if (kept === null || kept.hidden) {
        kept = null;
        return;
      }
      if (keptInView && !inView(kept)) {
        bringIntoView(kept);
      }
      keptInView = inView(kept);
    })
    : null;
  let watching = false;

  function keep(box) {
    kept = box;
    keptInView = inView(box);
    if (keeper === null || watching) {
      return;
    }
    watching = true;
    for (let s = section; s !== null; s = s.previousElementSibling) {
      keeper.observe(s);
    }
    const body = section.closest(".panel-body");
    if (body) {
      body.addEventListener("scroll", () => {
        if (kept !== null) {
          keptInView = inView(kept);
        }
      });
    }
  }

  function sizeText(bytes) {
    const n = Number(bytes) || 0;
    return n >= 1024 ? (n / 1024).toFixed(1) + " KB" : n + " bytes";
  }

  function plural(n, one, many) {
    return n + " " + (n === 1 ? one : many);
  }

  // What the preset holds, under its row: only what the DLL said.
  function detail(it) {
    const box = LifecastControls.el("li", "preset-detail");
    const line = (cls, text) => {
      const p = LifecastControls.el("div", "preset-detail-line" + (cls ? " " + cls : ""), text);
      box.appendChild(p);
      return p;
    };
    if (it.bad) {
      line("is-bad", "Unreadable: " + it.bad);
    }
    const info = infoBy.get(it.name.toLowerCase());
    if (info) {
      // Always in the same order, whatever order the file keeps them in.
      const ORDER = ["Hair", "Beard", "Eyes", "Brows", "Scar"];
      const at = (x) => (ORDER.indexOf(x.type) < 0 ? ORDER.length : ORDER.indexOf(x.type));
      const parts = (Array.isArray(info.parts) ? info.parts.slice() : []).sort((a, b) => at(a) - at(b));
      for (const p of parts.filter((x) => !x.missing)) {
        const row = line("", "");
        row.appendChild(LifecastControls.el("span", "preset-detail-key", String(p.type)));
        row.appendChild(LifecastControls.el("span", "preset-detail-value", String(p.label)));
      }
      for (const p of parts.filter((x) => x.missing)) {
        line("is-bad", "Not found in this game: " + p.label);
      }
      const c = info.counts || {};
      const held = [];
      if (typeof info.weight === "number") {
        held.push("weight " + info.weight);
      }
      if (c.sliders) {
        held.push(plural(c.sliders, "slider", "sliders"));
      }
      if (c.tints) {
        held.push(plural(c.tints, "tint layer", "tint layers"));
      }
      if (c.overlays) {
        held.push(plural(c.overlays, "overlay", "overlays"));
      }
      if (c.bodyMorphs) {
        held.push(plural(c.bodyMorphs, "body morph", "body morphs"));
      }
      if (c.sculpt) {
        held.push("sculpt");
      }
      if (c.transforms) {
        held.push("node transforms");
      }
      if (held.length) {
        line("is-dim", held.join(" \u00b7 "));
      }
      const plugins = Array.isArray(info.plugins) ? info.plugins : [];
      const missing = plugins.filter((x) => x.missing);
      if (plugins.length) {
        line("is-dim", "Needs " + plural(plugins.length, "plugin", "plugins") + ": " +
          plugins.filter((x) => !x.missing).map((x) => x.name).join(", ") +
          (missing.length ? (plugins.length > missing.length ? ", and " : "") +
            plural(missing.length, "that is", "that are") + " not active:" : ""));
      }
      for (const x of missing) {
        line("is-bad", "Not active: " + x.name);
      }
    }
    line("is-dim", (it.time ? "Saved " + dateText(it.time) + ", " : "") + sizeText(it.size));
    return box;
  }

  function loadPreset(name) {
    if (loading) {
      return;
    }
    loading = true;
    loadingName = name;
    selected = name;
    guard.hidden = true;
    fallback.hidden = true;
    notice = "loading " + name + "...";
    render();
    LifecastBridge.send("preset-load", { name: name });
  }

  function render() {
    const query = field.value.trim().toLowerCase();
    const shown = items.filter((it) => query === "" || it.name.toLowerCase().includes(query));
    shown.sort(sortBy === "time"
      ? (a, b) => b.time - a.time || a.name.localeCompare(b.name)
      : (a, b) => a.name.localeCompare(b.name, undefined, { sensitivity: "base" }));

    byName.classList.toggle("is-on", sortBy === "name");
    byTime.classList.toggle("is-on", sortBy === "time");

    list.textContent = "";
    for (const it of shown) {
      const li = LifecastControls.el("li", "preset-row");
      li.appendChild(LifecastControls.el("span", "preset-name", it.name));
      li.appendChild(LifecastControls.el("span", "preset-date", it.bad ? "unreadable" : dateText(it.time)));
      const isOpen = opened !== null && same(opened, it.name);
      const more = LifecastControls.el("button", "panel-button preset-more", "i");
      more.type = "button";
      more.tabIndex = -1;
      more.title = "What this preset holds";
      more.setAttribute("aria-expanded", isOpen ? "true" : "false");
      more.classList.toggle("is-on", isOpen);
      more.addEventListener("click", (e) => {
        e.stopPropagation();
        opened = isOpen ? null : it.name;
        render();
      });
      const go = LifecastControls.el("button", "panel-button preset-row-load",
        loading && loadingName !== null && same(loadingName, it.name) ? "Loading" : "Load");
      go.type = "button";
      go.tabIndex = -1;
      go.title = "Applies this preset";
      go.disabled = loading || it.bad !== null;
      go.addEventListener("click", (e) => {
        e.stopPropagation();
        loadPreset(it.name);
      });
      li.appendChild(more);
      li.appendChild(go);
      if (it.bad) {
        li.classList.add("preset-bad");
        li.title = it.bad;
      }
      if (selected !== null && same(selected, it.name)) {
        li.classList.add("is-selected");
      }
      li.addEventListener("click", () => {
        selected = it.name;
        render();
      });
      list.appendChild(li);
      if (isOpen) {
        list.appendChild(detail(it));
      }
    }

    const bad = items.filter((it) => it.bad).length;
    status.textContent = items.length === 0
      ? "No presets in RaceMenu's preset folder."
      : shown.length + " of " + items.length + " presets" + (bad ? ", " + bad + " unreadable" : "");
    if (notice) {
      status.textContent += " - " + notice;
    }

    save.disabled = field.value.trim() === "";
    save.textContent = Date.now() < confirmUntil ? "Overwrite?" : "Save";


    if (reveal !== null) {
      bringIntoView(reveal);
      keep(reveal);
      reveal = null;
    }
  }

  field.addEventListener("input", () => {
    confirmUntil = 0;
    notice = "";
    guard.hidden = true;
    hidePreview(true);
    render();
  });
  byName.addEventListener("click", () => {
    sortBy = "name";
    LifecastSettings.set("presetsSort", sortBy);
    render();
  });
  byTime.addEventListener("click", () => {
    sortBy = "time";
    LifecastSettings.set("presetsSort", sortBy);
    render();
  });

  // The saved order arrives from the DLL; until then the list sorts by name.
  LifecastSettings.onChange(() => {
    const saved = LifecastSettings.get("presetsSort");
    if (saved === "name" || saved === "time") {
      sortBy = saved;
      render();
    }
  });

  guardRename.addEventListener("click", () => {
    field.value = freeName(field.value.trim());
    guard.hidden = true;
    confirmUntil = 0;
    notice = "";
    render();
    field.focus();
  });

  save.addEventListener("click", () => {
    const name = field.value.trim();
    if (name === "") {
      return;
    }
    const exists = items.some((it) => same(it.name, name));
    if (exists && Date.now() >= confirmUntil) {
      confirmUntil = Date.now() + CONFIRM_MS;
      render();
      setTimeout(render, CONFIRM_MS + 50);
      return;
    }
    confirmUntil = 0;
    guard.hidden = true;
    hidePreview(false);
    fallback.hidden = true;
    notice = "saving " + name + "...";
    render();
    LifecastBridge.send("preset-save", { name: name, overwrite: exists });
  });

  previewSave.addEventListener("click", () => {
    if (previewName === null) {
      return;
    }
    const name = previewName;
    hidePreview(false);
    notice = "saving " + name + "...";
    render();
    LifecastBridge.send("preset-save", { name: name, overwrite: true, confirm: true });
  });

  previewCancel.addEventListener("click", () => {
    hidePreview(true);
    notice = "not saved";
    render();
  });

  LifecastBridge.on("preset-save-preview", (data) => {
    const diff = data && Array.isArray(data.diff) ? data.diff : [];
    const gain = entryText(diff, "gained");
    const loss = entryText(diff, "lost");
    previewName = String(data && data.name);
    previewTitle.textContent = "Save over " + previewName + "?";
    previewGain.textContent = "It would GAIN: " + (gain || "nothing") + ".";
    previewGain.title = entryLabels(diff, "gained");
    previewLoss.textContent = "It would LOSE: " + (loss || "nothing") + ".";
    previewLoss.title = entryLabels(diff, "lost");
    const chosen = entryText(diff, "chosen");
    previewChosen.textContent = chosen ? "Chosen in this panel, not counted above: " + chosen + "." : "";
    previewChosen.title = entryLabels(diff, "chosen");
    previewChosen.hidden = !chosen;
    previewScope.textContent = "Compared: " + Object.keys(ENTRY_WORDS).map((k) => ENTRY_WORDS[k][1]).join(", ") +
      ". A value that only changed is an edit, and is not listed.";
    preview.hidden = false;
    reveal = preview;
    notice = "not saved yet - see what it would change";
    render();
  });

  // The DLL ended the preview on its side: a load, the menu closing, the
  // preset changing on disk, another save. The box goes whatever name it
  // shows - the DLL holds one preview at most, and a box it does not hold
  // must not be clickable. The notice changes only when a box was showing:
  // a drop the page caused itself (Cancel, a new save) already said why.
  LifecastBridge.on("preset-save-preview-dropped", (data) => {
    const showing = previewName !== null;
    hidePreview(false);
    if (showing) {
      notice = "not saved: " + (data && data.why ? data.why : "the preview ended") +
        " - save again to see what it would change";
    }
    render();
  });

  LifecastBridge.on("presets", (data) => {
    const incoming = data && Array.isArray(data.items) ? data.items : [];
    items = incoming.map((it) => ({ name: String(it.name), time: Number(it.time) || 0, size: it.size, bad: null }));
    render();
  });

  LifecastBridge.on("presets-checked", (data) => {
    const bad = data && Array.isArray(data.bad) ? data.bad : [];
    // The description of each preset, when the DLL sends one (D128).
    infoBy.clear();
    for (const i of data && Array.isArray(data.info) ? data.info : []) {
      if (i && typeof i.name === "string") {
        infoBy.set(i.name.toLowerCase(), i);
      }
    }
    for (const b of bad) {
      const it = items.find((x) => x.name === b.name);
      if (it) {
        it.bad = String(b.why || "unreadable");
      }
    }
    render();
  });

  // The menu closed: what this section showed during that menu session ends
  // with it - the DLL has already let go of what those boxes stood for. F4
  // does not send this; the boxes stay for the player who comes back.
  LifecastBridge.on("menu-closed", () => {
    guard.hidden = true;
    fallback.hidden = true;
    hidePreview(false);
    confirmUntil = 0;
    notice = "";
    reveal = null;
    render();
  });

  // Every answer names its preset. Only an answer about the name the box
  // shows ends the box: an answer about another name says nothing about the
  // preview the DLL still holds, and hiding its box would leave that preview
  // pending with nothing on screen to agree or cancel.
  LifecastBridge.on("preset-saved", (data) => {
    if (previewName !== null && data && same(String(data.name), previewName)) {
      hidePreview(false);
    }
    if (data && data.ok) {
      selected = data.name;
      notice = "saved " + data.name;
    } else if (data && Array.isArray(data.loss)) {
      // Refused by the overwrite guard: the existing file is kept as it was.
      notice = "not saved";
      guardTitle.textContent = "Keep " + data.name + "?";
      guardNote.textContent = data.loss.length
        ? "Saving over it would lose what was not loaded here: " + lossText(data.loss) + "."
        : "Saving over it is not safe: " + (data.why || "unknown reason") + ".";
      guard.hidden = false;
      reveal = guard;
    } else {
      notice = "not saved: " + (data && data.why ? data.why : "unknown reason");
      if (data && data.fallback) {
        showFallback("save");
      }
    }
    render();
  });

  LifecastBridge.on("preset-loaded", (data) => {
    loading = false;
    loadingName = null;
    if (data && data.ok) {
      // A late answer overrides the "not loaded" shown at the timeout.
      selected = data.name;
      notice = "loaded " + data.name + (data.late ? " (late, after " + data.ms + " ms)" : "");
      fallback.hidden = true;
      // A preset replaces the whole character at once. Undoing one slider
      // back to what it held before the preset would mix two characters,
      // and nothing on screen would say so - so the history starts over.
      LifecastHistory.clear("a preset was loaded");
    } else {
      notice = "not loaded: " + (data && data.why ? data.why : "unknown reason");
      if (data && data.fallback) {
        showFallback("load");
      }
    }
    render();
  });

  // The call is still out. The DLL never gives up on it, so this is not a
  // failure: RaceMenu answers late when the game is busy, and the answer is
  // "preset-loaded" whenever it comes.
  LifecastBridge.on("preset-loading", (data) => {
    if (!loading || !data || loadingName === null || !same(loadingName, String(data.name))) {
      return;
    }
    if (data.silent) {
      // The DLL accepts a new load from here on, and an answer from this one that comes later is only logged
      // when a new load took its place - or comes as "preset-loaded" as usual when none did.
      loading = false;
      loadingName = null;
      notice = "RaceMenu has not answered after " + Math.round(Number(data.ms) / 1000) + " s - " + data.name +
        " may still apply; another preset can be loaded";
    } else {
      notice = "applying " + data.name + " - the game is busy, RaceMenu will answer when it can";
    }
    render();
  });

  LifecastBridge.whenAttached((ok) => {
    if (ok) {
      LifecastBridge.send("presets-get");
      LifecastBridge.send("portraits-get");
    }
  });

  render();
})();
