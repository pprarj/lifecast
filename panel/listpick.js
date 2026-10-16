// listpick.js
// Lifecast - choose from a list directly, instead of walking it one step at
// a time.
//
// A list row (hair, eyes, a tint's texture, a paint layer's texture...) moves
// through its entries with the slider and the < > buttons, and neither says
// what the next entry is. This adds a second way to the same value: a chip
// beside the row's name opens a pop-up under the row with every entry, and
// choosing one is exactly what pressing the step buttons does - the row sets
// the value, sends it as final and writes it to the undo history. The row stays
// as it was; the pop-up is one more way in, not a replacement. The pop-up stays
// open after a choice, so entries can be tried one after another; the chip
// closes it.
//
// It opens inline under the row like the colour picker does (nothing floats
// over the stage). When the entries come from more than one plugin it offers
// a strip of plugin chips that narrows the list to one. A long list also gets
// a text field: what is typed narrows the entries by name, AND with the chip
// (an entry shows when it is from the chosen plugin and its name has every
// word typed), so the chip never shows one plugin while the field lists
// another. The field only hides entries: the row's value, the highlighted
// entry and the list itself stay as they were, and the entry the row rests on
// is still the current one while the filter hides it. The field is typed text,
// which keyboard.js corrects for the in-game browser like every other field.
//
// Every list that reaches a pop-up is also checked against the DLL. The DLL
// stamps each list it sends with the FNV-1a signature and byte length of the
// list as text; verify() rebuilds that text from what the page received,
// signs it the same way and answers with its own stamp, and the DLL logs both
// ends. A page older than the DLL does not know the stamp and never answers,
// and the DLL logs that silence.
"use strict";

const LifecastList = (() => {
  // ----- the stamp -----
  function fnv1a(bytes) {
    let h = 0x811c9dc5;
    for (let i = 0; i < bytes.length; i += 1) {
      h ^= bytes[i];
      h = Math.imul(h, 0x01000193) >>> 0;
    }
    return ("00000000" + h.toString(16)).slice(-8);
  }

  // rows: an array of arrays of strings - the fields of each item, in the
  // order the DLL joined them with a tab; one line per item.
  function canonical(rows) {
    let text = "";
    for (const r of rows) {
      text += r.join("\t") + "\n";
    }
    return text;
  }

  function sigOf(text) {
    const bytes = new TextEncoder().encode(text);
    return { sig: fnv1a(bytes), len: bytes.length };
  }

  const answered = new Map(); // key -> the last stamp this page answered

  // Signs a list as received, compares it with the DLL's stamp and answers
  // the DLL once per distinct list. Returns true when both ends agree.
  function verify(key, rows, stamp) {
    const mine = sigOf(canonical(rows));
    const agree =
      !!stamp && stamp.sig === mine.sig && stamp.len === mine.len && (stamp.n === undefined || stamp.n === rows.length);
    const tag = mine.sig + "/" + String(mine.len);
    if (answered.get(key) !== tag) {
      answered.set(key, tag);
      if (!agree) {
        console.warn("[list] " + key + ": the DLL stamped " + JSON.stringify(stamp || null) + ", the page computed " + tag);
      }
      LifecastBridge.send("list-sig", { k: key, sig: mine.sig, len: mine.len, n: rows.length });
    }
    return agree;
  }

  // ----- the pop-up -----
  // a_opts: { row, label, none }
  //   row   - the LifecastControls row the pop-up chooses for
  //   none  - true when the row's lowest value is -1, "None": the pop-up then
  //           opens with a None entry, and entry k of the list is value k
  function create(a_opts) {
    const row = a_opts.row;
    const lowest = a_opts.none ? -1 : 0;

    const chip = LifecastControls.el("button", "list-chip", "list");
    chip.type = "button";
    chip.tabIndex = -1;
    chip.title = "Choose from the whole list";
    row.el.firstChild.appendChild(chip);

    const find = LifecastControls.el("input", "preset-field listpop-find");
    find.type = "text";
    find.spellcheck = false;
    find.autocomplete = "off";
    find.placeholder = "Search this list";
    find.hidden = true;

    const wrap = LifecastControls.el("div", "listpop");
    wrap.hidden = true;
    const origins = LifecastControls.el("div", "listpop-origins");
    const count = LifecastControls.el("div", "listpop-count");
    const body = LifecastControls.el("div", "listpop-items");
    wrap.appendChild(find);
    wrap.appendChild(origins);
    wrap.appendChild(count);
    wrap.appendChild(body);
    row.el.appendChild(wrap);

    const state = {
      items: [],
      key: "",
      current: lowest - 1,
      origin: null,
      words: [],
      buttons: new Map(),
      originChips: new Map(),
    };

    // Lists shorter than this are read at a glance; a field would be clutter.
    const FIND_FROM = 16;

    // Case and accents do not matter to a player looking for a name.
    function fold(text) {
      return text.normalize("NFD").replace(/[\u0300-\u036f]/g, "").toLowerCase();
    }

    function sourcesOf(items) {
      const seen = new Set();
      for (const it of items) {
        if (it.source) {
          seen.add(it.source);
        }
      }
      return Array.from(seen).sort((a, b) => a.localeCompare(b));
    }

    function visible(it) {
      if (state.origin !== null && it.source !== state.origin) {
        return false;
      }
      if (state.words.length === 0) {
        return true;
      }
      if (it.folded === undefined) {
        it.folded = fold(it.name === "" ? "" : it.name);
      }
      return state.words.every((w) => it.folded.includes(w));
    }

    function markSelected() {
      for (const [value, b] of state.buttons) {
        b.classList.toggle("selected", value === state.current);
      }
    }

    function updateCount() {
      let shown = 0;
      for (const it of state.items) {
        if (visible(it)) {
          shown += 1;
        }
      }
      const narrowed = state.origin !== null || state.words.length > 0;
      let text = narrowed
        ? String(shown) + " of " + String(state.items.length) + " entries"
        : String(state.items.length) + " entries";
      if (state.origin !== null) {
        text += " - " + state.origin;
      }
      const cur = state.current >= 0 ? state.items[state.current] : undefined;
      if (narrowed && cur !== undefined && !visible(cur)) {
        text += " (the one in use is hidden)";
      }
      count.textContent = text;
    }

    function applyFilter() {
      for (const [value, b] of state.buttons) {
        const it = value < 0 ? null : state.items[value];
        b.hidden = it !== null && !visible(it);
      }
      for (const [source, c] of state.originChips) {
        c.classList.toggle("selected", source === state.origin);
      }
      state.allChip.classList.toggle("selected", state.origin === null);
      updateCount();
    }

    function setWords(text) {
      state.words = fold(text)
        .split(/\s+/)
        .filter((w) => w !== "");
    }

    function clearFind() {
      find.value = "";
      state.words = [];
    }

    find.addEventListener("input", () => {
      setWords(find.value);
      applyFilter();
    });

    // Choosing applies the entry and leaves the pop-up as it was - open, with
    // the same plugin chip, text and scroll - so a long list can be walked
    // entry by entry. The chip beside the name closes it.
    function pick(value) {
      row.choose(value);
      setCurrent(value);
    }

    function addButton(value, label, title) {
      const b = LifecastControls.el("button", "listpop-item", label);
      b.type = "button";
      b.tabIndex = -1;
      b.title = title || label;
      b.dataset.index = String(value);
      b.addEventListener("click", () => pick(value));
      body.appendChild(b);
      state.buttons.set(value, b);
    }

    // items: [{ name, source?, title? }], current: the row's value. Rebuilds
    // the DOM only when the list is not the one already drawn.
    function setItems(a_items, a_current) {
      const key = a_items.map((it) => it.name + "\t" + (it.source || "")).join("\n");
      if (key !== state.key) {
        state.key = key;
        state.items = a_items;
        state.origin = null;
        clearFind();
        find.hidden = a_items.length < FIND_FROM;
        state.buttons = new Map();
        state.originChips = new Map();
        body.replaceChildren();
        origins.replaceChildren();
        if (a_opts.none) {
          addButton(-1, "None");
        }
        a_items.forEach((it, i) => addButton(i, it.name === "" ? "Entry " + String(i + 1) : it.name, it.title || it.source || it.name));
        const sources = sourcesOf(a_items);
        origins.hidden = sources.length < 2;
        state.allChip = LifecastControls.el("button", "listpop-origin", "All");
        state.allChip.type = "button";
        state.allChip.tabIndex = -1;
        state.allChip.addEventListener("click", () => {
          state.origin = null;
          applyFilter();
        });
        origins.appendChild(state.allChip);
        for (const source of sources) {
          const c = LifecastControls.el("button", "listpop-origin", source);
          c.type = "button";
          c.tabIndex = -1;
          c.title = source;
          c.addEventListener("click", () => {
            state.origin = source;
            applyFilter();
          });
          origins.appendChild(c);
          state.originChips.set(source, c);
        }
        applyFilter();
      }
      setCurrent(a_current);
      chip.hidden = a_items.length === 0;
    }

    function setCurrent(a_value) {
      state.current = typeof a_value === "number" ? a_value : lowest - 1;
      markSelected();
      updateCount();
    }

    chip.addEventListener("click", () => {
      wrap.hidden = !wrap.hidden;
      if (!wrap.hidden) {
        const at = state.buttons.get(state.current);
        if (at) {
          body.scrollTop = Math.max(0, at.offsetTop - body.offsetTop - body.clientHeight / 2);
        }
        wrap.scrollIntoView({ block: "nearest" });
      }
    });

    return {
      el: wrap,
      chip: chip,
      setItems: setItems,
      setCurrent: setCurrent,
      close: () => {
        wrap.hidden = true;
      },
      // For the measurement: types into the field as a player would.
      find: (text) => {
        find.value = text;
        setWords(text);
        applyFilter();
      },
      // For the measurement: what the pop-up offers right now, in the order
      // of the list, with what the filter hides marked.
      entries: () =>
        Array.from(state.buttons.entries()).map(([value, b]) => ({ value: value, name: b.textContent, hidden: b.hidden })),
      origin: () => state.origin,
    };
  }

  return { create: create, verify: verify, sigOf: sigOf, canonical: canonical };
})();
