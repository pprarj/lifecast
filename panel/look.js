// look.js
// Lifecast - the list controls (head parts, feature types, hair colour,
// complexion) and weight.
//
// Every list comes from the DLL, for the player's race and sex, in the
// "look" message: how many entries it has and which one is current. The
// page never assumes a length - it depends on the load order. A list with
// no entries hides its row. A current value that is not in the list shows
// as "--" until the player picks one.
//
// Hair colour is a list too - the race's preset colours - and also free: the
// chip opens a picker, and a picked colour goes to RaceMenu's own custom
// hair colour, which is what keeps it across saves. The list then shows
// "--", because the colour is no longer one of its entries.
"use strict";

(() => {
  const SECTIONS = [
    // The key stays "look:Body" - it is what sectionsOpen saved and what
    // groups.js orders by - but the title is Weight: "Body" is the category
    // this section sits in, beside OBody and Body Scales (E4, D123).
    {
      title: "Body",
      label: "Weight",
      controls: [["weight", "Weight"]],
    },
    {
      title: "Hair",
      controls: [["hair", "Hair"], ["hair-color", "Hair Color"], ["facial-hair", "Facial Hair"]],
    },
    {
      title: "Features",
      controls: [["scar", "Scar"], ["complexion", "Complexion"]],
    },
  ];

  // Lists that sit in the section of the part of the face they change, the
  // way RaceMenu's tabs hold them (E19, F35). [key, label, section title];
  // the section is joined by key (sliders.js puts the face sliders there).
  const FACE_LISTS = [
    ["nose-type", "Nose Type", "Nose"],
    ["eye-shape", "Eye Shape", "Eyes"],
    ["eyes", "Eye Color", "Eyes"],
    ["brows", "Brow Type", "Brows"],
    ["mouth-shape", "Mouth Shape", "Mouth"],
  ];

  const PART_KEYS = new Set(["hair", "facial-hair", "eyes", "brows", "scar"]);
  // The four head-part lists that also open as a pop-up (listpick.js); the
  // DLL sends their entries once per build of the lists, in "look-lists".
  const LIST_KEYS = ["hair", "eyes", "brows", "facial-hair"];

  const rows = new Map(); // key -> { row, kind, count, chip, picker, color, grid, swatches }

  // a_id: { n, s } - the part's name and plugin as the DLL sent them. A head
  // part's index is only good inside the list it was read from, and the DLL
  // builds that list in one order while the menu is fresh and in another once a
  // race or sex swap has settled (E31): a value put back from an older reading
  // goes by who it is, and the DLL finds that part in the list it has now.
  function send(key, v, final, a_id) {
    const message = { k: key, v: v, final: final };
    if (a_id && typeof a_id.n === "string") {
      message.n = a_id.n;
      message.s = typeof a_id.s === "string" ? a_id.s : "";
    }
    LifecastBridge.send("look-set", message);
  }

  function buildRow(key, label) {
    const entry = { kind: key === "weight" ? "range" : "list", count: 0, chip: null, grid: null, ident: null,
      pendingAfter: null };
    const isPart = PART_KEYS.has(key);
    entry.row = LifecastControls.row({
      label: label,
      min: 0,
      max: key === "weight" ? 100 : 0,
      step: 1,
      button: 1,
      typed: key === "weight",
      format: (v) => (entry.kind === "list" ? String(v) + " / " + String(entry.count - 1) : String(Math.round(v))),
      send: (v, final, id) => send(key, v, final, id),
      identity: isPart ? () => entry.ident : undefined,
      // A head part's step carries who it was and who it became, not only the
      // two numbers. The "after" is learned from the DLL's next answer.
      entry: isPart
        ? (before, after, put) => {
            if (before === after) {
              return null;
            }
            const was = entry.index === before && entry.ident ? entry.ident : null;
            const to = { i: after, id: null };
            entry.pendingAfter = to;
            return {
              label: label,
              before: { i: before, id: was },
              after: to,
              apply: (v) => put(v.i, v.id),
              describe: (v) => v.i,
            };
          }
        : undefined,
    });

    if (PART_KEYS.has(key)) {
      // The part the player wears, as RaceMenu names it: the DLL sends the
      // name and the plugin; while dragging, the index moves ahead of them.
      entry.nameEl = LifecastControls.el("div", "part-name");
      entry.row.el.appendChild(entry.nameEl);
      entry.row.input.addEventListener("input", () => {
        entry.nameEl.textContent = "...";
        entry.nameEl.title = "";
      });
    }

    if (LIST_KEYS.indexOf(key) >= 0) {
      entry.list = LifecastList.create({ row: entry.row });
      entry.index = -1;
    }

    if (key === "hair-color") {
      entry.chip = LifecastControls.el("button", "swatch-chip picker-chip");
      entry.chip.type = "button";
      entry.chip.tabIndex = -1;
      entry.chip.title = "Pick any colour";
      entry.row.el.firstChild.appendChild(entry.chip);
      entry.picker = LifecastPicker.create({
        label: "Hair Color",
        get: () => entry.color || "#000000",
        send: (hex, final) => {
          entry.color = hex;
          entry.chip.style.background = hex;
          LifecastBridge.send("look-set", { k: "hair-rgb", v: hex, final: final });
        },
      });
      entry.chip.addEventListener("click", () => entry.picker.toggle());
      entry.grid = LifecastControls.el("div", "swatch-grid");
      entry.row.el.appendChild(entry.grid);
      entry.row.el.appendChild(entry.picker.el);
      entry.row.input.addEventListener("input", () => {
        const i = Number(entry.row.input.value);
        entry.color = entry.swatches && entry.swatches[i];
        paintChip(entry, i);
      });
    }
    rows.set(key, entry);
    return entry.row.el;
  }

  function paintChip(entry, index) {
    entry.chip.style.background = entry.color || "transparent";
    entry.picker.refresh();
    for (const b of entry.grid.children) {
      b.classList.toggle("selected", Number(b.dataset.index) === index);
    }
  }

  function fillSwatches(entry, swatches) {
    const same = entry.swatches && entry.swatches.join() === swatches.join();
    entry.swatches = swatches;
    if (same) {
      return;
    }
    entry.grid.replaceChildren();
    swatches.forEach((color, i) => {
      const b = LifecastControls.el("button", "swatch");
      b.type = "button";
      b.tabIndex = -1;
      b.dataset.index = String(i);
      b.style.background = color;
      b.title = String(i);
      // A swatch does not go through the row's own send, so it records its
      // own entry: read the index BEFORE set() moves it, then pick.
      const pick = (index) => {
        if (typeof index === "string") {
          // A free colour put back (undo of a swatch click made over one, F33).
          entry.color = index;
          entry.chip.style.background = index;
          entry.picker.refresh();
          send("hair-rgb", index, true);
          return;
        }
        entry.row.set(index);
        entry.index = index; // the DLL's answer says so too; a second click before it must see it
        entry.color = entry.swatches && entry.swatches[index];
        paintChip(entry, index);
        send("hair-color", index, true);
      };
      b.addEventListener("click", () => {
        // Over a free colour the row rests on a stand-in 0: the colour is the "before".
        const before =
          entry.index < 0 && typeof entry.color === "string" ? entry.color : Number(entry.row.input.value);
        pick(i);
        LifecastHistory.record({ label: "Hair Color", before: before, after: i, apply: pick });
      });
      entry.grid.appendChild(b);
    });
  }

  // A free hair colour is not one row: the Hair Color row then rests on a
  // stand-in 0 (marked, so the portrait skips it) and the colour lives here.
  // Null while the colour is one of the race's, which the row holds.
  LifecastControls.addPortraitPart("hair-rgb", {
    get: () => {
      const e = rows.get("hair-color");
      return e && e.index < 0 && e.color ? e.color : null;
    },
    restore: (hex) => {
      const e = rows.get("hair-color");
      if (!e || typeof hex !== "string") {
        return;
      }
      e.color = hex;
      e.chip.style.background = hex;
      e.picker.refresh();
      send("hair-rgb", hex, true);
    },
  });

  for (const s of SECTIONS) {
    const section = LifecastControls.section(s.label || s.title);
    for (const [key, label] of s.controls) {
      section.body.appendChild(buildRow(key, label));
    }
    LifecastControls.mount(section, "look:" + s.title);
  }

  for (const [key, label, title] of FACE_LISTS) {
    LifecastControls.slot(title, "face:" + title).body.appendChild(buildRow(key, label));
  }

  LifecastBridge.on("look", (data) => {
    const controls = data && data.ready && data.controls ? data.controls : {};
    for (const [key, entry] of rows) {
      const c = controls[key];
      if (!c) {
        entry.row.set(null);
        entry.ident = null;
        continue;
      }
      if (c.kind === "range") {
        entry.kind = "range";
        entry.row.setRange(c.min, c.max);
        if (!entry.row.dragging()) {
          entry.row.set(c.value);
        }
        continue;
      }

      entry.kind = "list";
      entry.count = c.count;
      // The flag is what the search respects: it re-sets "hidden" on every
      // row it reprocesses, and would otherwise show an empty list again (F4).
      entry.row.el.dataset.empty = c.count === 0 ? "1" : "";
      entry.row.el.hidden = c.count === 0;
      if (c.count === 0) {
        entry.row.set(null); // no stale values from the last list behind the hidden row
        entry.ident = null;
        continue;
      }
      entry.row.setRange(0, c.count - 1);
      entry.index = c.index;
      if (PART_KEYS.has(key)) {
        entry.ident = c.index >= 0 && typeof c.name === "string" ? { n: c.name, s: typeof c.source === "string" ? c.source : "" } : null;
        if (entry.pendingAfter && entry.ident && entry.pendingAfter.i === c.index) {
          entry.pendingAfter.id = entry.ident;
          entry.pendingAfter = null;
        }
      }
      if (entry.list) {
        entry.list.setCurrent(c.index);
      }
      if (entry.grid) {
        fillSwatches(entry, Array.isArray(c.swatches) ? c.swatches : []);
      }
      if (entry.row.dragging()) {
        continue;
      }
      if (c.index < 0) {
        // Enabled, but showing that the current value is not a list entry.
        entry.row.set(0);
        entry.row.mark(true);
        // A free hair colour is no list entry, but the DLL sends it: show it (F15).
        entry.row.valueEl.textContent = typeof c.color === "string" ? c.color.toUpperCase() : "--";
      } else {
        entry.row.set(c.index);
      }
      if (entry.nameEl) {
        const name = typeof c.name === "string" ? c.name : "";
        const source = typeof c.source === "string" ? c.source : "";
        entry.nameEl.textContent = c.index < 0 ? "" : name + (source ? "  -  " + source : "");
        entry.nameEl.title = entry.nameEl.textContent;
      }
      if (entry.chip && !entry.picker.dragging()) {
        // The DLL sends the colour the player wears, preset or not.
        entry.color = typeof c.color === "string" ? c.color : entry.swatches && entry.swatches[c.index];
        paintChip(entry, c.index);
      }
    }
    // Whoever needs a reading that is up to date (racesex.js, before it takes a portrait) waits on this.
    document.dispatchEvent(new CustomEvent("lifecast-look"));
  });

  // The whole lists the pop-ups choose from. Each is checked against the
  // stamp the DLL put on it, and the answer goes back.
  LifecastBridge.on("look-lists", (data) => {
    const lists = data && data.lists ? data.lists : {};
    for (const key of LIST_KEYS) {
      const d = lists[key];
      const entry = rows.get(key);
      if (!d || !Array.isArray(d.items) || !entry || !entry.list) {
        continue;
      }
      LifecastList.verify("look:" + key, d.items, d);
      entry.list.setItems(
        d.items.map((a) => ({ name: a[0], source: a[1] })),
        entry.index
      );
    }
  });

  LifecastBridge.whenAttached((ok) => {
    if (ok) {
      LifecastBridge.send("look-get");
    }
  });
})();
