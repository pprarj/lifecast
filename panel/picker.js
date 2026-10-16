// picker.js
// Lifecast - a free colour picker: a saturation/value square and a hue bar.
//
// Any control that offers a colour gets one, next to its swatches: the
// swatches are the race's presets, the picker is everything else. It opens
// under the control when its colour chip is clicked, and closes on a second
// click. The hex under the bar is typeable: click it, type RRGGBB, Enter.
//
// While the square or the bar is dragged the picker reports at most one
// colour per SEND_GAP_MS, always the latest; when the drag ends it reports
// the final colour marked as such, like a slider row does.
"use strict";

const LifecastPicker = (() => {
  const SEND_GAP_MS = 50;

  function clamp01(v) {
    return Math.min(1, Math.max(0, v));
  }

  function hex2(n) {
    return Math.round(n).toString(16).padStart(2, "0").toUpperCase();
  }

  function hsvToHex(h, s, v) {
    const f = (n) => {
      const k = (n + h / 60) % 6;
      return v - v * s * Math.max(0, Math.min(k, 4 - k, 1));
    };
    return "#" + hex2(f(5) * 255) + hex2(f(3) * 255) + hex2(f(1) * 255);
  }

  function hexToHsv(hex) {
    const n = parseInt(String(hex).slice(1), 16);
    if (!isFinite(n)) {
      return { h: 0, s: 0, v: 0 };
    }
    const r = ((n >> 16) & 255) / 255;
    const g = ((n >> 8) & 255) / 255;
    const b = (n & 255) / 255;
    const max = Math.max(r, g, b);
    const d = max - Math.min(r, g, b);
    let h = 0;
    if (d > 0) {
      if (max === r) h = 60 * (((g - b) / d + 6) % 6);
      else if (max === g) h = 60 * ((b - r) / d + 2);
      else h = 60 * ((r - g) / d + 4);
    }
    return { h: h, s: max > 0 ? d / max : 0, v: max };
  }

  // a_opts: { label, get() -> "#RRGGBB", send(hex, final) }
  // label names the control in the undo history; it is not drawn.
  // Returns { el, toggle(), refresh() }. el goes where the picker should
  // open; it is hidden until toggled.
  function create(a_opts) {
    const wrap = LifecastControls.el("div", "picker");
    wrap.hidden = true;
    const square = LifecastControls.el("canvas", "picker-square");
    const hue = LifecastControls.el("canvas", "picker-hue");
    const hexLabel = LifecastControls.el("span", "picker-hex", "");
    wrap.appendChild(square);
    wrap.appendChild(hue);
    wrap.appendChild(hexLabel);

    const hsv = { h: 0, s: 0, v: 0 };
    const state = { dragging: false, timer: null, lastSent: 0, last: null, editing: null };

    function current() {
      return hsvToHex(hsv.h, hsv.s, hsv.v);
    }

    // Same shape as a slider row's: state.last is the colour before the drag
    // now ending, and refresh() - which is where a colour from the DLL
    // arrives - is its only other writer. refresh() bows out while dragging,
    // so the "before" survives the echoes that land mid-drag.
    function commit(a_hex) {
      const before = state.last;
      state.last = a_hex;
      if (typeof before !== "string") {
        return;
      }
      LifecastHistory.record({
        label: a_opts.label || "Colour",
        before: before,
        after: a_hex,
        apply: (v) => {
          const next = hexToHsv(v);
          if (next.s > 0 && next.v > 0) {
            hsv.h = next.h;
          }
          hsv.s = next.s;
          hsv.v = next.v;
          state.last = current();
          draw();
          a_opts.send(current(), true);
        },
      });
    }

    function draw() {
      const w = wrap.clientWidth || 240;
      for (const [c, h] of [[square, 110], [hue, 12]]) {
        if (c.width !== w || c.height !== h) {
          c.width = w;
          c.height = h;
        }
      }
      const sq = square.getContext("2d");
      sq.fillStyle = hsvToHex(hsv.h, 1, 1);
      sq.fillRect(0, 0, square.width, square.height);
      const white = sq.createLinearGradient(0, 0, square.width, 0);
      white.addColorStop(0, "rgba(255,255,255,1)");
      white.addColorStop(1, "rgba(255,255,255,0)");
      sq.fillStyle = white;
      sq.fillRect(0, 0, square.width, square.height);
      const black = sq.createLinearGradient(0, 0, 0, square.height);
      black.addColorStop(0, "rgba(0,0,0,0)");
      black.addColorStop(1, "rgba(0,0,0,1)");
      sq.fillStyle = black;
      sq.fillRect(0, 0, square.width, square.height);
      const x = hsv.s * square.width;
      const y = (1 - hsv.v) * square.height;
      sq.strokeStyle = hsv.v > 0.5 ? "#000" : "#FFF";
      sq.lineWidth = 2;
      sq.beginPath();
      sq.arc(x, y, 5, 0, Math.PI * 2);
      sq.stroke();

      const hb = hue.getContext("2d");
      const g = hb.createLinearGradient(0, 0, hue.width, 0);
      for (let i = 0; i <= 6; i++) {
        g.addColorStop(i / 6, hsvToHex(i * 60, 1, 1));
      }
      hb.fillStyle = g;
      hb.fillRect(0, 0, hue.width, hue.height);
      const hx = (hsv.h / 360) * hue.width;
      hb.fillStyle = "#FFF";
      hb.fillRect(Math.max(0, hx - 1.5), 0, 3, hue.height);

      if (!state.editing) {
        hexLabel.textContent = current();
      }
    }

    // The typed hex: RRGGBB with or without #, any case. Anything else sends
    // nothing and the hex that was there comes back. An applied colour takes
    // the same road the undo's apply takes.
    function endHexEdit(a_text) {
      const field = state.editing;
      if (!field) {
        return;
      }
      state.editing = null;
      field.remove();
      const m = a_text === null ? null : /^#?([0-9a-f]{6})$/i.exec(String(a_text).trim());
      if (m) {
        const next = hexToHsv("#" + m[1]);
        if (next.s > 0 && next.v > 0) {
          hsv.h = next.h;
        }
        hsv.s = next.s;
        hsv.v = next.v;
        draw();
        report(true);
      } else {
        draw();
      }
    }

    function beginHexEdit() {
      if (state.editing || state.dragging) {
        return;
      }
      const field = LifecastControls.el("input", "picker-hex-edit");
      field.type = "text";
      field.value = current().slice(1);
      field.maxLength = 7;
      field.setAttribute("aria-label", "Hex colour");
      state.editing = field;
      hexLabel.textContent = "";
      hexLabel.appendChild(field);
      field.addEventListener("keydown", (e) => {
        if (e.key === "Enter") {
          e.preventDefault();
          endHexEdit(field.value);
        } else if (e.key === "Escape") {
          e.preventDefault();
          e.stopPropagation();
          endHexEdit(null);
        }
      });
      field.addEventListener("blur", () => {
        if (state.editing === field) {
          endHexEdit(field.value);
        }
      });
      field.focus();
      field.select();
    }

    hexLabel.classList.add("is-typed");
    hexLabel.addEventListener("click", beginHexEdit);

    function report(final) {
      if (state.timer !== null) {
        clearTimeout(state.timer);
        state.timer = null;
      }
      state.lastSent = performance.now();
      a_opts.send(current(), final);
      if (final) {
        commit(current());
      }
    }

    function moved() {
      draw();
      const wait = SEND_GAP_MS - (performance.now() - state.lastSent);
      if (wait <= 0) {
        report(false);
      } else if (state.timer === null) {
        state.timer = setTimeout(() => report(false), wait);
      }
    }

    function drag(canvas, apply) {
      const at = (e) => {
        const r = canvas.getBoundingClientRect();
        apply(clamp01((e.clientX - r.left) / r.width), clamp01((e.clientY - r.top) / r.height));
        moved();
      };
      canvas.addEventListener("pointerdown", (e) => {
        state.dragging = true;
        canvas.setPointerCapture(e.pointerId);
        at(e);
      });
      canvas.addEventListener("pointermove", (e) => {
        if (state.dragging) at(e);
      });
      const end = () => {
        if (!state.dragging) return;
        state.dragging = false;
        report(true);
      };
      canvas.addEventListener("pointerup", end);
      canvas.addEventListener("pointercancel", end);
    }

    drag(square, (x, y) => {
      hsv.s = x;
      hsv.v = 1 - y;
    });
    drag(hue, (x) => {
      hsv.h = Math.min(359.99, x * 360);
    });

    // Takes the control's colour, unless the player is dragging - the hand
    // wins over a value from before it moved. Hue is kept for greys, where
    // the colour alone does not say it.
    function refresh() {
      if (state.dragging || wrap.hidden) {
        return;
      }
      const next = hexToHsv(a_opts.get());
      if (current() !== String(a_opts.get()).toUpperCase()) {
        if (next.s > 0 && next.v > 0) hsv.h = next.h;
        hsv.s = next.s;
        hsv.v = next.v;
      }
      state.last = current();
      draw();
    }

    // Opened, it scrolls into view: it opens inline under its chip, and in
    // the small panel a chip near the bottom would open it out of sight
    // (E4, D120 - nothing floats over the stage).
    function toggle() {
      wrap.hidden = !wrap.hidden;
      refresh();
      if (!wrap.hidden) {
        wrap.scrollIntoView({ block: "nearest" });
      }
    }

    return { el: wrap, toggle: toggle, refresh: refresh, dragging: () => state.dragging };
  }

  return { create: create };
})();
