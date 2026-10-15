// sliders.js
// Lifecast - every slider of RaceMenu's list (D243).
//
// Which sliders exist is RaceMenu's answer, read off its film by the DLL
// (Edit/FilmSliders.h): the game's face morphs, RaceMenu's own, and every
// slider a mod registered - body morphs and body scales among them. Nothing
// here names a mod or keeps a list of sliders; a slider is a key, a label,
// a range and the tab RaceMenu files it under.
//
// Where a row goes follows that tab. The game's 18 face morphs keep the
// sections they always had (the table below is where they sit, not which
// exist). The rest goes to the section of the part it changes when the page
// has one (Eyes, Brows, Mouth, Hair, Body), to a section of its own per tab
// otherwise, and a mod's tab is a section of its own, empty when the mod put
// nothing in it this time - RaceMenu shows it empty too.
//
// A value goes back by key; the DLL writes it the way RaceMenu's own slider
// does, and sends back what the film holds.
"use strict";

(() => {
  // The game's 18, by the label RaceMenu gives them: [face section, row label].
  // The order is the order of the rows.
  const ENGINE = [
    ["Nose Length", "Nose", "Length"], ["Nose Height", "Nose", "Height"],
    ["Jaw Height", "Jaw", "Height"], ["Jaw Width", "Jaw", "Width"], ["Jaw Forward", "Jaw", "Forward"],
    ["Cheekbone Height", "Cheekbones", "Height"], ["Cheekbone Width", "Cheekbones", "Width"],
    ["Eye Height", "Eyes", "Height"], ["Eye Width", "Eyes", "Width"], ["Eye Depth", "Eyes", "Depth"],
    ["Brow Height", "Brows", "Height"], ["Brow Width", "Brows", "Width"], ["Brow Forward", "Brows", "Forward"],
    ["Mouth Height", "Mouth", "Height"], ["Mouth Forward", "Mouth", "Forward"],
    ["Chin Width", "Chin", "Width"], ["Chin Length", "Chin", "Length"], ["Chin Forward", "Chin", "Forward"],
  ];
  const FACE = ["Nose", "Jaw", "Cheekbones", "Eyes", "Brows", "Mouth", "Chin"];

  // RaceMenu's tabs by bit that have a section here already: [flag, section key].
  const HOMES = [[32, "face:Eyes"], [64, "face:Brows"], [128, "face:Mouth"], [256, "look:Hair"], [4, "look:Body"]];
  // The body scale tab has its own section, under Body.
  const SCALES = "rsm_body_scale";

  // The text for an install with no mod tab at all (F49). A mod registers its
  // tab a moment AFTER the film is read - measured at about a second, five in
  // an earlier round - so zero is only believed after it has stayed zero.
  const NONE_AFTER_MS = 6000;

  // section key -> { head, tail }: the two places rows go in a section that
  // other files fill too. head sits before everything (where the face sliders
  // always were), tail after it.
  const places = new Map();

  function sectionEl(key) {
    return document.querySelector('#sections > [data-key="' + key + '"]');
  }

  function place(key) {
    let p = places.get(key);
    if (p) {
      return p;
    }
    const section = sectionEl(key);
    if (!section || !section.body) {
      return null;
    }
    p = { head: LifecastControls.el("div"), tail: LifecastControls.el("div"), section: section };
    section.body.prepend(p.head);
    section.body.appendChild(p.tail);
    places.set(key, p);
    return p;
  }

  // The fixed sections exist before the first list arrives (look.js and
  // tints.js made them), so their places are taken now, in load order: the
  // lists and tint layers other files put there stay between head and tail.
  for (const title of FACE) {
    LifecastControls.slot(title, "face:" + title);
    place("face:" + title);
  }
  place("look:Hair");
  place("look:Body");

  function titleOf(label) {
    if (typeof label !== "string" || label === "") {
      return "Sliders";
    }
    return label === label.toUpperCase()
      ? label.toLowerCase().replace(/(^|\s)\S/g, (c) => c.toUpperCase())
      : label;
  }

  // Sections made from the list, by key. They are never unmounted - a section
  // mounted again sorts after its old place - only emptied and hidden.
  const own = new Map(); // key -> { section, body }

  function ownSection(key, title) {
    let s = own.get(key);
    if (!s) {
      const section = LifecastControls.slot(title, key);
      const body = LifecastControls.el("div");
      section.body.appendChild(body);
      s = { section: section, body: body };
      own.set(key, s);
    }
    s.section.hidden = false;
    return s;
  }

  function decimals(step) {
    if (step >= 1) {
      return 0;
    }
    return step >= 0.1 ? 1 : 2;
  }

  function makeRow(s, label) {
    // The game's morphs move by a tenth on RaceMenu's own slider; the page
    // always gave them a hundredth, and the game takes any value.
    const engine = s.kind === "engine";
    const step = engine ? 0.01 : s.step;
    const dp = decimals(step);
    const signed = s.min < 0;
    // Five intervals to an arrow press. A row of ten intervals or fewer is
    // not really a slider (a mod registers 0 to 2 by 1 to get a button, and
    // one of those is a Reset that clears every body morph): there the arrow
    // moves one interval, so no single press can cross the range.
    const steps = (s.max - s.min) / step;
    return LifecastControls.row({
      label: label,
      min: s.min,
      max: s.max,
      step: step,
      button: steps > 10 ? step * 5 : step,
      format: (v) => (signed && v >= 0 ? "+" : "") + v.toFixed(dp),
      send: (v, final) => LifecastBridge.send("slider-set", { k: s.k, v: v, final: final }),
    });
  }

  let rows = new Map(); // key -> row
  let signature = "";
  let noneTimer = null;

  function clearNone() {
    if (noneTimer !== null) {
      clearTimeout(noneTimer);
      noneTimer = null;
    }
    const s = own.get("morph:none");
    if (s) {
      s.section.hidden = true;
    }
  }

  function showNone() {
    noneTimer = null;
    const s = ownSection("morph:none", "Body morphs");
    s.body.replaceChildren(
      LifecastControls.el(
        "div",
        "delegate-note",
        "There are no body morph sliders in this installation: no mod has added any to RaceMenu. A mod that adds them for one sex only shows nothing here for the other."
      )
    );
  }

  // Where one slider goes: { key, at: "head" | "tail" | "own", title }.
  function homeOf(s, tabs, engine) {
    if (engine) {
      return { key: "face:" + engine[1], at: "head" };
    }
    const tab = tabs.get(s.tab);
    if (!tab) {
      return { key: "morph:-", at: "own", title: "Other Sliders" };
    }
    if (tab.key) {
      return tab.key === SCALES
        ? { key: "scales", at: "own", title: "Body Scales" }
        : { key: "morph:" + tab.key, at: "own", title: tab.label };
    }
    const home = HOMES.find((h) => h[0] === tab.flag);
    if (home && places.has(home[1])) {
      return { key: home[1], at: "tail" };
    }
    return { key: "face:tab:" + tab.id, at: "own", title: titleOf(tab.label) };
  }

  function build(data) {
    for (const p of places.values()) {
      p.head.replaceChildren();
      p.tail.replaceChildren();
    }
    for (const [key, s] of own) {
      if (key !== "morph:none") {
        s.body.replaceChildren();
        s.section.hidden = true;
      }
    }
    rows = new Map();

    const tabs = new Map(data.tabs.map((t) => [t.id, t]));
    const engineOf = new Map(ENGINE.map((e, i) => [e[0], [i, e[1], e[2]]]));
    // The Russian game labels the same 18 in Russian: the same rows, by alias.
    const RU = {
      "\u0414\u043b\u0438\u043d\u0430 \u043d\u043e\u0441\u0430": "Nose Length", "\u0412\u044b\u0441\u043e\u0442\u0430 \u043d\u043e\u0441\u0430": "Nose Height",
      "\u0412\u044b\u0441\u043e\u0442\u0430 \u0447\u0435\u043b\u044e\u0441\u0442\u0438": "Jaw Height", "\u0428\u0438\u0440\u0438\u043d\u0430 \u0447\u0435\u043b\u044e\u0441\u0442\u0438": "Jaw Width", "\u0427\u0435\u043b\u044e\u0441\u0442\u044c \u0432\u043f\u0435\u0440\u0435\u0434": "Jaw Forward",
      "\u0412\u044b\u0441\u043e\u0442\u0430 \u0441\u043a\u0443\u043b": "Cheekbone Height", "\u0428\u0438\u0440\u0438\u043d\u0430 \u0441\u043a\u0443\u043b": "Cheekbone Width",
      "\u0412\u044b\u0441\u043e\u0442\u0430 \u0433\u043b\u0430\u0437": "Eye Height", "\u0420\u0430\u0441\u0441\u0442\u043e\u044f\u043d\u0438\u0435 \u043c\u0435\u0436\u0434\u0443 \u0433\u043b\u0430\u0437\u0430\u043c\u0438": "Eye Width", "\u0413\u043b\u0443\u0431\u0438\u043d\u0430 \u0433\u043b\u0430\u0437\u043d\u0438\u0446": "Eye Depth",
      "\u0412\u044b\u0441\u043e\u0442\u0430 \u0431\u0440\u043e\u0432\u0435\u0439": "Brow Height", "\u0428\u0438\u0440\u0438\u043d\u0430 \u0431\u0440\u043e\u0432\u0435\u0439": "Brow Width", "\u0411\u0440\u043e\u0432\u0438 \u0432\u043f\u0435\u0440\u0435\u0434": "Brow Forward",
      "\u0412\u044b\u0441\u043e\u0442\u0430 \u0433\u0443\u0431": "Mouth Height", "\u0413\u0443\u0431\u044b \u0432\u043f\u0435\u0440\u0435\u0434": "Mouth Forward",
      "\u0428\u0438\u0440\u0438\u043d\u0430 \u043f\u043e\u0434\u0431\u043e\u0440\u043e\u0434\u043a\u0430": "Chin Width", "\u0414\u043b\u0438\u043d\u0430 \u043f\u043e\u0434\u0431\u043e\u0440\u043e\u0434\u043a\u0430": "Chin Length", "\u041f\u043e\u0434\u0431\u043e\u0440\u043e\u0434\u043e\u043a \u0432\u043f\u0435\u0440\u0435\u0434": "Chin Forward",
    };
    for (const ru in RU) {
      if (engineOf.has(RU[ru])) {
        engineOf.set(ru, engineOf.get(RU[ru]));
      }
    }
    const counts = {};
    const notDrawn = [];

    // A mod's tab is a section even when it holds nothing.
    let modTabs = 0;
    for (const t of data.tabs) {
      if (t.key && t.key !== SCALES) {
        modTabs += 1;
        ownSection("morph:" + t.key, t.label);
      }
    }

    // The game's 18 in the table's order; everything else in the film's.
    const ordered = data.sliders.slice().sort((a, b) => {
      const ea = a.kind === "engine" && engineOf.has(a.text) ? engineOf.get(a.text)[0] : 1e9;
      const eb = b.kind === "engine" && engineOf.has(b.text) ? engineOf.get(b.text)[0] : 1e9;
      return ea - eb;
    });
    for (const s of ordered) {
      const engine = s.kind === "engine" ? engineOf.get(s.text) : undefined;
      const home = homeOf(s, tabs, engine);
      let host = null;
      if (home.at === "own") {
        host = ownSection(home.key, home.title).body;
      } else {
        const p = places.get(home.key);
        host = p ? p[home.at] : null;
      }
      if (!host) {
        notDrawn.push(s.k);
        continue;
      }
      const row = makeRow(s, engine ? engine[2] : s.label);
      // The film's key, on the row: what a value of this row is called on both sides.
      row.el.dataset.slider = s.k;
      row.set(s.value);
      rows.set(s.k, row);
      host.appendChild(row.el);
      counts[home.key] = (counts[home.key] || 0) + 1;
    }

    for (const [key, s] of own) {
      if (key.indexOf("morph:") === 0 && key !== "morph:none" && !s.section.hidden && s.body.childElementCount === 0) {
        s.body.appendChild(
          LifecastControls.el(
            "div",
            "delegate-note",
            "RaceMenu has this tab and it holds nothing right now. What goes in it is the mod's own decision - some add theirs for one sex only."
          )
        );
      }
    }

    if (modTabs === 0) {
      if (noneTimer === null && !(own.get("morph:none") && !own.get("morph:none").section.hidden)) {
        noneTimer = setTimeout(showNone, NONE_AFTER_MS);
      }
    } else {
      clearNone();
    }

    LifecastBridge.send("sliders-drawn", { rows: rows.size, sliders: data.sliders.length, sections: counts, notDrawn: notDrawn });
  }

  function setValue(k, v) {
    const row = rows.get(k);
    if (row && !row.dragging()) {
      row.set(v === undefined ? null : v);
    }
  }

  LifecastBridge.on("sliders", (data) => {
    // ready says the film was read. A false one is a bad read, not an empty
    // list: what is on screen is better than nothing, so leave it.
    if (!data || !data.ready || !Array.isArray(data.sliders) || !Array.isArray(data.tabs)) {
      return;
    }
    // Every key, label, range and tab: when it moves, all rows are rebuilt,
    // so they stay in the order RaceMenu lists them.
    const next = JSON.stringify([
      data.tabs.map((t) => [t.id, t.label]),
      data.sliders.map((s) => [s.k, s.label, s.tab, s.min, s.max, s.step]),
    ]);
    if (next !== signature) {
      signature = next;
      build(data);
      return;
    }
    for (const s of data.sliders) {
      setValue(s.k, s.value);
    }
  });

  LifecastBridge.on("slider-values", (data) => {
    if (!data || typeof data.values !== "object" || data.values === null) {
      return;
    }
    for (const k of Object.keys(data.values)) {
      setValue(k, data.values[k]);
    }
  });

  LifecastBridge.whenAttached((ok) => {
    if (ok) {
      LifecastBridge.send("sliders-get");
    }
  });
})();
