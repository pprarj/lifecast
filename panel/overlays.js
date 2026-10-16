// overlays.js
// Lifecast - the paint overlays: body, hand, foot and face.
//
// One section per area, each holding the layers SKEE gives that area - six
// on the body and three elsewhere, by the player's own skee64.ini, so the
// count is read and never assumed. A layer is a texture from RaceMenu's
// catalog plus a colour, an intensity, a glow colour and a glow strength.
//
// A layer with no texture on it has nothing to colour, so its rows are
// disabled until one is picked - the same shape tints.js uses for a picked
// tint with nothing showing.
//
// Colour is picked from the chip, not from red/green/blue rows. Fifteen
// layers with three colour rows each would be forty-five rows added to a
// page that has about a hundred, to say what one picker already says.
//
// $MAKEUP is not here. It is RaceMenu's war paint - a tint mask, not an
// overlay - and it is edited in the Skin & Tints section (D084).
"use strict";

(() => {
  // Which section key each area mounts under. Face paint belongs with the
  // face and the rest with the body, and the grouping in groups.js reads the
  // key's prefix - so the key is where that belonging is said.
  const SECTION_KEY = {
    body: "overlay:body",
    hand: "overlay:hand",
    foot: "overlay:foot",
    face: "face:paint",
  };

  const areas = new Map(); // area key -> { section, layers: Map, catalog, sig }

  function hex2(n) {
    return Math.round(n).toString(16).padStart(2, "0").toUpperCase();
  }

  function send(areaKey, index, fields, final) {
    LifecastBridge.send("overlay-set", Object.assign({ k: areaKey, i: index, final: final }, fields));
  }

  // A colour chip that opens the picker, next to the layer's name.
  function chip(title, get, put) {
    const button = LifecastControls.el("button", "swatch-chip picker-chip");
    button.type = "button";
    button.tabIndex = -1;
    button.title = title;
    const picker = LifecastPicker.create({ label: title, get: get, send: put });
    button.addEventListener("click", () => {
      if (!button.disabled) {
        picker.toggle();
      }
    });
    return { button: button, picker: picker };
  }

  function buildLayer(areaKey, area, index) {
    const entry = { color: "#FFFFFF", glow: "#FFFFFF", painted: false, textures: [], current: "" };

    const head = LifecastControls.el("div", "tint-head");
    const name = LifecastControls.el("span", "slider-name", "Layer " + String(index + 1));
    entry.colorChip = chip("Paint colour", () => entry.color, (hex, final) => {
      entry.color = hex;
      entry.colorChip.button.style.background = hex;
      send(areaKey, index, { color: hex }, final);
    });
    entry.glowChip = chip("Glow colour", () => entry.glow, (hex, final) => {
      entry.glow = hex;
      entry.glowChip.button.style.background = hex;
      send(areaKey, index, { glow: hex }, final);
    });
    name.appendChild(entry.colorChip.button);
    name.appendChild(entry.glowChip.button);
    head.appendChild(name);
    area.section.body.appendChild(head);
    area.section.body.appendChild(entry.colorChip.picker.el);
    area.section.body.appendChild(entry.glowChip.picker.el);

    // The catalog index, with -1 for "no paint". A texture the catalog does
    // not carry - painted by something that never registered it with
    // RaceMenu - also reads as -1, and its own name is shown instead of
    // "None" so it is not mistaken for an empty layer. Moving the row off it
    // clears it, which is what "None" means.
    entry.texture = LifecastControls.row({
      label: "Texture",
      min: -1,
      max: 0,
      step: 1,
      button: 1,
      format: (v) => {
        if (v >= 0 && entry.textures[v]) {
          return entry.textures[v].name;
        }
        return entry.painted && entry.currentName ? entry.currentName : "None";
      },
      send: (v, final) => {
        const picked = v >= 0 && entry.textures[v] ? entry.textures[v].texture : "";
        send(areaKey, index, { texture: picked }, final);
      },
    });
    entry.alpha = LifecastControls.row({
      label: "Intensity",
      min: 0,
      max: 100,
      step: 1,
      button: 5,
      typed: true,
      format: (v) => String(Math.round(v)) + "%",
      send: (v, final) => send(areaKey, index, { alpha: v / 100 }, final),
    });
    entry.glowAlpha = LifecastControls.row({
      label: "Glow",
      min: 0,
      max: 100,
      step: 1,
      button: 5,
      typed: true,
      format: (v) => String(Math.round(v)) + "%",
      send: (v, final) => send(areaKey, index, { glowAlpha: v / 100 }, final),
    });
    entry.textureList = LifecastList.create({ row: entry.texture, none: true });
    for (const row of [entry.texture, entry.alpha, entry.glowAlpha]) {
      area.section.body.appendChild(row.el);
    }
    return entry;
  }

  function build(areaData) {
    const area = { section: LifecastControls.section(areaData.label), layers: new Map() };
    for (const layer of areaData.layers) {
      area.layers.set(layer.i, buildLayer(areaData.k, area, layer.i));
    }
    LifecastControls.mount(area.section, SECTION_KEY[areaData.k] || "overlay:" + areaData.k);
    return area;
  }

  function apply(areaData, area) {
    const catalog = Array.isArray(areaData.catalog) ? areaData.catalog : [];
    if (areaData.catalogSig) {
      LifecastList.verify("overlay:" + areaData.k, catalog.map((p) => [p.name, p.texture]), areaData.catalogSig);
    }
    const items = catalog.map((p) => ({ name: p.name, title: p.texture }));
    for (const layer of areaData.layers) {
      const entry = area.layers.get(layer.i);
      if (!entry) {
        continue;
      }
      entry.textures = catalog;
      entry.painted = typeof layer.texture === "string" && layer.texture !== "";
      entry.currentName = layer.name || "";
      entry.color = layer.color;
      entry.glow = layer.glow;

      let at = -1;
      for (let i = 0; i < catalog.length; i += 1) {
        if (String(catalog[i].texture).toLowerCase() === String(layer.texture).toLowerCase()) {
          at = i;
          break;
        }
      }
      entry.texture.setRange(-1, catalog.length - 1);
      entry.textureList.setItems(items, at);
      if (!entry.texture.dragging()) {
        entry.texture.set(at);
      }

      for (const part of [entry.colorChip, entry.glowChip]) {
        part.button.disabled = !entry.painted;
        part.button.style.background = entry.painted ? (part === entry.colorChip ? layer.color : layer.glow) : "transparent";
        if (!entry.painted) {
          part.picker.el.hidden = true;
        } else {
          part.picker.refresh();
        }
      }
      if (!entry.painted) {
        entry.alpha.set(null);
        entry.glowAlpha.set(null);
        continue;
      }
      if (!entry.alpha.dragging()) {
        entry.alpha.set(Math.round(layer.alpha * 100));
      }
      if (!entry.glowAlpha.dragging()) {
        entry.glowAlpha.set(Math.round(layer.glowAlpha * 100));
      }
    }
  }

  LifecastBridge.on("overlays", (data) => {
    const list = data && data.ready && Array.isArray(data.areas) ? data.areas : [];
    for (const areaData of list) {
      if (!areaData || typeof areaData.k !== "string" || !Array.isArray(areaData.layers)) {
        continue;
      }
      // How many layers an area has is the player's setting and does not
      // change while the menu is open; the rows are built once and only
      // rebuilt if that count ever does.
      const sig = String(areaData.layers.length);
      let area = areas.get(areaData.k);
      if (area && area.sig !== sig) {
        area.section.remove();
        area = null;
      }
      if (!area) {
        area = build(areaData);
        area.sig = sig;
        areas.set(areaData.k, area);
      }
      apply(areaData, area);
    }
  });

  LifecastBridge.whenAttached((ok) => {
    if (ok) {
      LifecastBridge.send("overlays-get");
    }
  });
})();
