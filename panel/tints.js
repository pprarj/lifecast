// tints.js
// Lifecast - the tint masks: colour and intensity per layer, and which
// texture shows for war paint, dirt and markings.
//
// The layers come from the DLL, for the player's race and sex. Colour is set
// as red, green and blue, 0 to 255, or by a swatch - the race's own preset
// colours for that texture, when it has any - or freely, by the picker
// that opens from the layer's colour chip. Intensity is 0 to 100 percent.
// A picked layer adds a row that chooses its texture, "None" included.
// War paint adds one more: the catalog of every war paint installed, read
// off RaceMenu's film by the DLL. Choosing an entry puts that texture on the
// mask that is showing (D125 brought it in from RaceMenu).
"use strict";

(() => {
  const section = LifecastControls.section("Skin & Tints");
  const body = LifecastControls.el("div");
  section.body.appendChild(body);
  LifecastControls.mount(section, "tints");

  // A layer sits in the section of the thing it paints (D210): the face tints
  // in the face sections, where sliders.js puts the face sliders. What is not here
  // stays in this section - the skin itself, and the layers whose object has
  // no section of its own (D210, closed).
  const HOME = {
    "lips": "Mouth",
    "cheeks": "Cheekbones",
    "cheeks-lower": "Cheekbones",
    "eye-socket-upper": "Eyes",
    "eye-socket-lower": "Eyes",
    "eyeliner": "Eyes",
    "nose": "Nose",
    "chin": "Chin",
  };
  const homes = new Map(); // section title -> the div this file keeps in it

  function homeOf(a_layerKey) {
    const title = HOME[a_layerKey];
    if (!title) {
      return body;
    }
    let div = homes.get(title);
    if (!div) {
      div = LifecastControls.el("div");
      LifecastControls.slot(title, "face:" + title).body.appendChild(div);
      homes.set(title, div);
    }
    return div;
  }

  let layers = new Map(); // key -> { pick, catalog, r, g, b, alpha, chip, picker, grid, swatches, none }
  let keys = "";

  function hex2(n) {
    return Math.round(n).toString(16).padStart(2, "0").toUpperCase();
  }

  function colorOf(entry) {
    return "#" + hex2(entry.r.input.value) + hex2(entry.g.input.value) + hex2(entry.b.input.value);
  }

  function send(key, fields, final) {
    LifecastBridge.send("tint-set", Object.assign({ k: key, final: final }, fields));
  }

  function paint(entry) {
    entry.chip.style.background = colorOf(entry);
    entry.picker.refresh();
  }

  function setColor(entry, color) {
    const v = parseInt(color.slice(1), 16);
    if (!entry.r.dragging()) entry.r.set((v >> 16) & 255);
    if (!entry.g.dragging()) entry.g.set((v >> 8) & 255);
    if (!entry.b.dragging()) entry.b.set(v & 255);
    paint(entry);
  }

  function colorRow(key, entry, label) {
    return LifecastControls.row({
      label: label,
      min: 0,
      max: 255,
      step: 1,
      button: 1,
      format: (v) => String(Math.round(v)),
      send: (_v, final) => {
        paint(entry);
        send(key, { color: colorOf(entry) }, final);
      },
    });
  }

  function fileName(path) {
    const name = String(path).split(/[\\/]/).pop();
    return name.replace(/\.dds$/i, "");
  }

  function build(list) {
    layers = new Map();
    body.replaceChildren();
    for (const div of homes.values()) {
      div.replaceChildren();
    }
    for (const l of list) {
      const dest = homeOf(l.k);
      const entry = { pick: null, catalog: null, textures: [], texture: "", swatches: null, none: false };
      const head = LifecastControls.el("div", "tint-head");
      const name = LifecastControls.el("span", "slider-name", l.label);
      entry.chip = LifecastControls.el("button", "swatch-chip picker-chip");
      entry.chip.type = "button";
      entry.chip.tabIndex = -1;
      entry.chip.title = "Pick any colour";
      name.appendChild(entry.chip);
      head.appendChild(name);
      dest.appendChild(head);
      entry.picker = LifecastPicker.create({
        label: l.label,
        get: () => colorOf(entry),
        send: (hex, final) => {
          setColor(entry, hex);
          send(l.k, { color: hex }, final);
        },
      });
      entry.chip.addEventListener("click", () => {
        if (!entry.none) {
          entry.picker.toggle();
        }
      });
      dest.appendChild(entry.picker.el);

      if (l.kind === "pick") {
        entry.pick = LifecastControls.row({
          label: "Texture",
          min: -1,
          max: 0,
          step: 1,
          button: 1,
          format: (v) => (v < 0 ? "None" : String(v + 1) + " / " + String(entry.count)),
          send: (v, final) => send(l.k, { index: v }, final),
        });
        entry.pickList = LifecastList.create({ row: entry.pick, none: true });
        dest.appendChild(entry.pick.el);
      }
      if (Array.isArray(l.catalog)) {
        // -1 is a texture the catalog does not carry, shown by its own file
        // name; it sends nothing, since only a catalog path can be written.
        entry.catalog = LifecastControls.row({
          label: "Catalog",
          min: -1,
          max: 0,
          step: 1,
          button: 1,
          format: (v) => {
            if (v >= 0 && entry.textures[v]) {
              return entry.textures[v].name;
            }
            return entry.texture ? fileName(entry.texture) : "None";
          },
          send: (v, final) => {
            if (v >= 0 && entry.textures[v]) {
              send(l.k, { texture: entry.textures[v].texture }, final);
            }
          },
        });
        entry.catalogList = LifecastList.create({ row: entry.catalog });
        dest.appendChild(entry.catalog.el);
      }
      entry.r = colorRow(l.k, entry, "Red");
      entry.g = colorRow(l.k, entry, "Green");
      entry.b = colorRow(l.k, entry, "Blue");
      entry.alpha = LifecastControls.row({
        label: "Intensity",
        min: 0,
        max: 100,
        step: 1,
        button: 5,
        typed: true,
        format: (v) => String(Math.round(v)) + "%",
        send: (v, final) => send(l.k, { alpha: v / 100 }, final),
      });
      for (const row of [entry.r, entry.g, entry.b, entry.alpha]) {
        dest.appendChild(row.el);
      }
      entry.grid = LifecastControls.el("div", "swatch-grid");
      dest.appendChild(entry.grid);
      layers.set(l.k, entry);
    }
  }

  function fillSwatches(key, entry, label, swatches) {
    const same = entry.swatches && entry.swatches.join() === swatches.join();
    entry.swatches = swatches;
    entry.grid.hidden = swatches.length === 0;
    if (same) {
      return;
    }
    entry.grid.replaceChildren();
    for (const color of swatches) {
      const b = LifecastControls.el("button", "swatch");
      b.type = "button";
      b.tabIndex = -1;
      b.style.background = color;
      b.title = color;
      // A swatch does not go through a colour row's send, so it records its
      // own entry: read the colour BEFORE setColor() moves the R/G/B rows.
      const pick = (hex) => {
        setColor(entry, hex);
        send(key, { color: hex }, true);
      };
      b.addEventListener("click", () => {
        const before = colorOf(entry);
        pick(color);
        // colorOf() is always upper case; a swatch from the DLL need not be,
        // and the same colour in two spellings would be an entry that undoes
        // to itself.
        LifecastHistory.record({ label: label, before: before, after: color.toUpperCase(), apply: pick });
      });
      entry.grid.appendChild(b);
    }
  }

  LifecastBridge.on("tints", (data) => {
    const list = data && data.ready && Array.isArray(data.layers) ? data.layers : [];
    const next = list.map((l) => l.k + ":" + l.kind).join();
    if (next !== keys) {
      keys = next;
      build(list);
    }
    section.hidden = list.length === 0;
    for (const l of list) {
      const entry = layers.get(l.k);
      if (!entry) {
        continue;
      }
      // A picked layer with nothing showing has no colour of its own to
      // edit until a texture is chosen.
      const none = l.kind === "pick" && l.index < 0;
      entry.none = none;
      if (none) {
        entry.picker.el.hidden = true;
      }
      if (entry.pick) {
        entry.count = l.count;
        entry.pick.setRange(-1, l.count - 1);
        if (!entry.pick.dragging()) {
          entry.pick.set(l.index);
        }
        if (l.list && Array.isArray(l.list.items)) {
          LifecastList.verify("tint:" + l.k, l.list.items, l.list);
          entry.pickList.setItems(l.list.items.map((a) => ({ name: a[0] })), l.index);
        }
      }
      if (none) {
        for (const row of [entry.r, entry.g, entry.b, entry.alpha]) {
          row.set(null);
        }
        entry.chip.style.background = "transparent";
      } else if (!entry.picker.dragging()) {
        setColor(entry, l.color);
        if (!entry.alpha.dragging()) {
          entry.alpha.set(Math.round(l.alpha * 100));
        }
      }
      if (entry.catalog) {
        entry.textures = Array.isArray(l.catalog) ? l.catalog : [];
        entry.texture = typeof l.texture === "string" ? l.texture : "";
        let at = -1;
        for (let i = 0; i < entry.textures.length; i += 1) {
          if (String(entry.textures[i].texture).toLowerCase() === entry.texture.toLowerCase()) {
            at = i;
            break;
          }
        }
        entry.catalog.setRange(-1, entry.textures.length - 1);
        if (l.catalogSig) {
          LifecastList.verify("tint:paint-catalog", entry.textures.map((p) => [p.name, p.texture]), l.catalogSig);
        }
        entry.catalogList.setItems(entry.textures.map((p) => ({ name: p.name, title: p.texture })), at);
        if (entry.textures.length === 0) {
          entry.catalog.set(null);
        } else if (!entry.catalog.dragging()) {
          entry.catalog.set(at);
        }
      }
      fillSwatches(l.k, entry, l.label, none ? [] : Array.isArray(l.swatches) ? l.swatches : []);
    }
  });

  LifecastBridge.whenAttached((ok) => {
    if (ok) {
      LifecastBridge.send("tints-get");
    }
  });
})();
