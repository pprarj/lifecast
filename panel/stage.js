// stage.js
// Lifecast - the character view: a frame to the right of the panel, and the
// controls that move the character inside it.
//
// The page draws nothing of the character. It is transparent here, and the
// game draws the RaceSex Menu's character behind it; the DLL moves the camera
// and turns the body so that the character sits in the frame. This file only
// draws the border and the controls, and sends the moves.
//
// Moves are DELTAS; the DLL keeps where the view is, clamps it to its limits
// and answers with the state ("stage"), which is what the readout and the
// disabled buttons show. A drag sends at most one message per SEND_MS, with
// everything moved since the last one.
//
// TWO MODES (D152). STANDARD is what the menu opens in: RaceMenu's own camera,
// two fixed positions - far, the body, and near, the face - swapped by LAlt or
// the Zoom button, or the wheel (up in, down out). The right button does nothing, and the left
// button turns the character's body, which is the one thing that differs from
// RaceMenu. FREE is the camera as it was before the modes, with every move
// below. A switch under the frame changes between them; the comparison forces
// FREE on and the switch shows it, locked, until the comparison closes.
//
// In the frame, with the mouse, in FREE:
//   drag                  turn the character
//   drag, Shift held      turn the second figure, with the comparison on (E13)
//   drag, right button    move the camera, up/down and left/right
//   drag, right, Shift    move the second figure, with the comparison on (E14):
//                         sideways, and up the screen is away from the camera
//   drag, Ctrl held       tilt the camera (when the DLL can)
//   wheel                 zoom in / out (RaceMenu's two positions)
// In STANDARD only the first of those, the drag, is alive.
// The buttons under the frame repeat the turn, the distance, the height and
// the tilt, so none of THOSE depends on knowing a mouse gesture.
//
// SIDEWAYS IS THE EXCEPTION, and it is a gap rather than a decision: the pan
// axis has no button, so moving the camera left and right is reachable only by
// the right-button drag. This comment used to claim every move had a button,
// and it was true when it was written - the pan axis did not exist yet. It
// arrived later, on the drag, without a button, and the sentence above it
// stayed. A reader who trusted it would conclude the keyboard and the buttons
// cover the whole view, and they do not.
//
// When the DLL says the view is not ready, the frame shows why and the
// controls go away; nothing else in the panel changes.
//
// A development build of the DLL also sends "tuning", and then a section of
// number boxes appears under Settings (D132): the seven numbers of the view
// retuned during in-game review, each with what the DLL said it did. A release DLL never sends that key, so the
// section is never built - there is no flag here to set, and nothing to
// remember to turn off. Said here because the comment above this one was once
// left behind by a feature that arrived later (the pan axis), and a reader who
// trusts a stale header concludes the wrong thing about what this file draws.
"use strict";

(() => {
  const SEND_MS = 50;

  // Drag and wheel scales: degrees or units per pixel, units per wheel step.
  const TURN_PER_PX = 0.4;
  const LIFT_PER_PX = 0.3;
  const PAN_PER_PX = 0.3;
  const TILT_PER_PX = 0.1;
  const DISTANCE_PER_WHEEL = 0.1;  // wheel deltaY is about 100 per notch

  // Button steps.
  const TURN_STEP = 15;
  const DISTANCE_STEP = 10;
  const LIFT_STEP = 10;
  const TILT_STEP = 5;

  const stage = document.getElementById("stage");
  const note = document.getElementById("stage-note");
  const bar = document.getElementById("stage-bar");
  const readout = document.getElementById("stage-readout");

  let state = null;
  const pending = { turn: 0, distance: 0, lift: 0, pan: 0, tilt: 0 };
  let timer = null;

  // The Compare copy (E13). `compareOn` is told by control.js, which owns the "compare" message (the bridge keeps one
  // handler per type); `copyTurn` is what the Shift drag has turned her since the last send.
  let compareOn = false;
  let copyTurn = 0;
  // E14: what Shift + the right button has moved her since the last send, in world units.
  const copyMove = { across: 0, depth: 0 };
  document.addEventListener("lifecast-compare", (e) => {
    compareOn = !!(e.detail && e.detail.on);
    if (!compareOn) {
      copyTurn = 0;
      copyMove.across = copyMove.depth = 0;
    }
  });

  function flush() {
    timer = null;
    if (copyTurn) {
      // A step is at most 180 degrees; a 50 ms drag is far below that.
      const deg = Math.max(-180, Math.min(180, copyTurn));
      copyTurn = 0;
      LifecastBridge.send("compare-turn", { deg: deg });
    }
    if (copyMove.across || copyMove.depth) {
      const data = { across: copyMove.across, depth: copyMove.depth };
      copyMove.across = copyMove.depth = 0;
      LifecastBridge.send("compare-move", data);
    }
    const moved = pending.turn || pending.distance || pending.lift || pending.pan || pending.tilt;
    if (!moved) {
      return;
    }
    const data = {};
    for (const key of ["turn", "distance", "lift", "pan", "tilt"]) {
      if (pending[key]) {
        data[key] = pending[key];
        pending[key] = 0;
      }
    }
    LifecastBridge.send("stage-move", data);
  }

  function move(key, amount) {
    if (!state || !state.ready || !amount) {
      return;
    }
    // With the lock on the DLL holds the head's height by tilt (E21, F30), so a tilt asked for by hand would be
    // taken back at once: the lock has to be off for the tilt to be the player's.
    if (key === "tilt" && (!state.tilt || state.lock !== false)) {
      return;
    }
    pending[key] += amount;
    if (timer === null) {
      timer = setTimeout(flush, SEND_MS);
    }
  }

  function moveCopy(amount) {
    if (!state || !state.ready || !compareOn || !amount) {
      return;
    }
    // A positive compare-turn is a positive angle of the copy, and the character's own turn is the other way round
    // on the screen: measured with both at +90 the character faces the right and the copy the left. The drag has to
    // feel the same for both, so the sign is taken here.
    copyTurn -= amount;
    if (timer === null) {
      timer = setTimeout(flush, SEND_MS);
    }
  }

  function moveCopyPlace(across, depth) {
    if (!state || !state.ready || !compareOn || (!across && !depth)) {
      return;
    }
    // The DLL keeps the total and cuts it to its limits; this only gathers the drag between two sends.
    copyMove.across += across;
    copyMove.depth += depth;
    if (timer === null) {
      timer = setTimeout(flush, SEND_MS);
    }
  }

  // ----- buttons -----------------------------------------------------------

  const buttons = [];

  function button(label, title, onClick, isAtLimit) {
    const b = LifecastControls.el("button", "panel-button stage-button", label);
    b.type = "button";
    b.title = title;
    b.addEventListener("click", onClick);
    buttons.push({ el: b, isAtLimit: isAtLimit });
    return b;
  }

  function group(title, items) {
    const g = LifecastControls.el("div", "stage-group");
    g.appendChild(LifecastControls.el("span", "stage-group-title", title));
    items.forEach((i) => g.appendChild(i));
    bar.appendChild(g);
    return g;
  }

  const near = 0.01;
  group("Turn", [
    button("\u25C0", "Turn left", () => move("turn", -TURN_STEP), null),
    button("\u25B6", "Turn right", () => move("turn", TURN_STEP), null),
  ]);
  const modeSwitch = button("Free camera: off",
    "Standard is RaceMenu's two positions; free is every move", () => {
      if (state && state.ready && !state.forced) {
        LifecastBridge.send("stage-mode", { free: !state.free });
      }
    }, (s) => !!s.forced);
  // The lock on the character (Acabamento 9). Its state is in its own label, so the
  // player can read whether the camera is framing her without pressing anything.
  const lockSwitch = button("Lock on character: on",
    "On, the camera keeps the character framed as she moves; off, it stays where you put it", () => {
      if (state && state.ready && state.free && !state.forced) {
        LifecastBridge.send("stage-lock", { on: !state.lock });
      }
    }, (s) => !!s.forced);
  const nearButton = button("Zoom", "RaceMenu's own zoom, far and near (LAlt)", () => sendZoom("button"), null);
  group("Camera", [modeSwitch, lockSwitch, nearButton]);
  const zoomGroup = group("Zoom", [
    button("+", "Closer", () => move("distance", -DISTANCE_STEP),
      (s) => s.distance <= s.limits.minDistance + near),
    button("\u2212", "Farther", () => move("distance", DISTANCE_STEP),
      (s) => s.distance >= s.limits.maxDistance - near),
  ]);
  const heightGroup = group("Height", [
    button("\u25B2", "Camera up", () => move("lift", LIFT_STEP),
      (s) => s.lift >= s.limits.liftMax - near),
    button("\u25BC", "Camera down", () => move("lift", -LIFT_STEP),
      (s) => s.lift <= s.limits.liftMin + near),
  ]);
  const tiltGroup = group("Tilt", [
    button("\u25B2", "Tilt one way", () => move("tilt", TILT_STEP),
      (s) => s.lock !== false || s.tiltDeg >= s.limits.tiltMax - near),
    button("\u25BC", "Tilt the other way", () => move("tilt", -TILT_STEP),
      (s) => s.lock !== false || s.tiltDeg <= -s.limits.tiltMax + near),
  ]);
  const reframe = LifecastControls.el("button", "panel-button stage-reframe", "Reframe");
  reframe.type = "button";
  reframe.title = "Back to the view the menu opened with";
  reframe.addEventListener("click", () => {
    pending.turn = pending.distance = pending.lift = pending.pan = pending.tilt = 0;
    LifecastBridge.send("stage-reset");
  });
  bar.appendChild(reframe);

  // ----- mouse in the frame --------------------------------------------------

  let drag = null;

  // Controls that sit inside the frame and must keep their own mouse.
  function onControls(t) {
    return bar.contains(t);
  }

  stage.addEventListener("pointerdown", (e) => {
    if (onControls(e.target) || !state || !state.ready) {
      return;
    }
    // Standard: the left button turns, and nothing else answers. With the comparison on, Shift + the left button
    // turns the copy instead of the character (E13); without Shift, or without the comparison, nothing changes.
    // Shift + the right button moves her (E14); without Shift the right button is still the camera's lift.
    const mode = (compareOn && e.shiftKey && e.button === 0 && !e.ctrlKey) ? "copy"
      : (compareOn && e.shiftKey && e.button === 2 && !e.ctrlKey) ? "copy-move"
      : !state.free ? (e.button === 0 && !e.ctrlKey ? "turn" : null)
      : (e.button === 2 ? "lift" : (e.ctrlKey ? "tilt" : (e.button === 0 ? "turn" : null)));
    if (!mode) {
      return;
    }
    drag = { mode: mode, x: e.clientX, y: e.clientY, id: e.pointerId };
    stage.setPointerCapture(e.pointerId);
    stage.classList.add("is-dragging");
    e.preventDefault();
  });

  stage.addEventListener("pointermove", (e) => {
    if (!drag || e.pointerId !== drag.id) {
      return;
    }
    const dx = e.clientX - drag.x;
    const dy = e.clientY - drag.y;
    drag.x = e.clientX;
    drag.y = e.clientY;
    if (drag.mode === "copy") {
      moveCopy(dx * TURN_PER_PX);
    } else if (drag.mode === "copy-move") {
      // She follows the cursor: right is right on the screen, and up the screen is away from the camera. The scale
      // is the pan's, world units per pixel.
      moveCopyPlace(dx * PAN_PER_PX, -dy * PAN_PER_PX);
    } else if (drag.mode === "turn") {
      move("turn", dx * TURN_PER_PX);
    } else if (drag.mode === "lift") {
      // E4 (Fase 6.5): the old vertical-only, inverted
      // drag was measured as wrong - "o eixo ta invertido... alem disso ele so mexe na
      // vertical." Both axes now follow the same grab convention as a map
      // or a Blender pan: the view moves WITH the cursor, so the camera
      // itself moves the opposite way.
      move("lift", dy * LIFT_PER_PX);
      move("pan", -dx * PAN_PER_PX);
    } else {
      move("tilt", -dy * TILT_PER_PX);
    }
  });

  function endDrag(e) {
    if (drag && e.pointerId === drag.id) {
      drag = null;
      stage.classList.remove("is-dragging");
    }
  }
  stage.addEventListener("pointerup", endDrag);
  stage.addEventListener("pointercancel", endDrag);
  stage.addEventListener("contextmenu", (e) => e.preventDefault());

  // The controller's right stick (pad.js): the left-button drag's turn, in the drag's own pixels.
  document.addEventListener("lifecast-stage-drag", (e) => {
    const dx = e.detail ? Number(e.detail.dx) : 0;
    if (dx) {
      move("turn", dx * TURN_PER_PX);
    }
  });

  // The controller's LT (pad.js): what the Zoom button does in standard, one
  // step of the + in the free camera.
  document.addEventListener("lifecast-stage-zoom", () => {
    if (!state || !state.ready) {
      return;
    }
    if (state.free) {
      move("distance", -DISTANCE_STEP);
    } else {
      sendZoom("pad");
    }
  });

  // The controller's RT (pad.js): what the Farther button (-) does in the free
  // camera; in standard RaceMenu's zoom is a toggle, so it is the LT's.
  document.addEventListener("lifecast-stage-farther", () => {
    if (!state || !state.ready) {
      return;
    }
    if (state.free) {
      move("distance", DISTANCE_STEP);
    } else {
      sendZoom("pad");
    }
  });

  stage.addEventListener("wheel", (e) => {
    if (onControls(e.target)) {
      return;
    }
    if (state && state.ready && !state.free) {
      e.preventDefault();
      // Wheel up zooms in, down zooms out; RaceMenu only has the two positions,
      // so a turn toward the one already shown does nothing.
      if (e.deltaY !== 0 && (e.deltaY < 0) !== zoomedIn) {
        sendZoom("wheel");
      }
      return;
    }
    move("distance", e.deltaY * DISTANCE_PER_WHEEL);
    e.preventDefault();
  }, { passive: false });

  // LAlt swaps far and near in standard. On Meridian and Magelight the key
  // reaches the DOM with the panel focused (measured 2026-09-28 and in 1.5),
  // so nothing has to be routed to the game; preventDefault keeps the browser
  // from taking the key for a menu. On Prisma it does not reach the page as
  // AltLeft (1.5 E5, D283), so the DLL hears it on the game's own input and
  // sends "key-lalt" instead. Both end in sendZoom, so a press that came both
  // ways is one swap.
  // The DLL drops a second request inside 400 ms; the page keeps the same
  // window so its far/near guess flips only when a request really went out.
  // from names what asked (key, dll-lalt, wheel, button, pad): the DLL logs
  // every page message raw, so the log says which way a swap came.
  let zoomedIn = false;
  let zoomAt = 0;
  function sendZoom(from) {
    const now = Date.now();
    if (state && state.ready && !state.free && now - zoomAt >= 400) {
      zoomAt = now;
      zoomedIn = !zoomedIn;
      LifecastBridge.send("stage-zoom", { from: from || "?" });
    }
  }

  document.addEventListener("keydown", (e) => {
    if (e.code !== "AltLeft" || e.repeat) {
      return;
    }
    if (state && state.ready && !state.free) {
      e.preventDefault();
      sendZoom("key");
    }
  });

  LifecastBridge.on("key-lalt", () => {
    sendZoom("dll-lalt");
  });

  // ----- the tuning knobs (development builds only) ---------------------------

  // These exist only when the DLL was built with LIFECAST_TUNING. There is no
  // preprocessor here to gate them with, and none is needed: a release DLL
  // never sends "tuning", so the section is never built and never shown. The
  // gate is the DLL's, which is the only place it can be enforced anyway.
  //
  // The seven are read from "tuning" and NOT from "limits". Four of them are in
  // both, and taking them from "limits" would quietly make this section stop
  // working the day a fourth number joins it that is not a bound.
  const TUNABLES = [
    ["maxDistance", "Max distance", "The farthest the camera can pull back", 1],
    ["liftUp", "Max height", "The highest it rises above where the menu opened", 1],
    ["panMax", "Max sideways", "How far it travels left and right", 1],
    ["tiltMax", "Max tilt", "How far it can tip up or down, in degrees", 1],
    ["pairApart", "Pair separation", "World distance between you and the second figure", 1],
    ["pairSpan", "Frame share", "How much of the view the pair spans, 0.5 being half", 0.01],
    ["ease", "Easing", "Share of the remaining gap covered per frame; 1 is no easing", 0.01],
  ];

  // The section lives under Settings (D132: every debug or configuration
  // control goes there), mounted the first time the DLL sends "tuning" - so a
  // release build never has an empty one. Each row has a line under it that
  // says what the DLL DID with the number: a limit the camera is already
  // inside moves nothing, and without the line that looks like a number that
  // was ignored (Acabamento 2).
  let tuningBox = null;
  const tuningInputs = new Map();
  const tuningSaid = new Map();
  let tuningNone = null;

  function buildTuning() {
    tuningBox = LifecastControls.section("Tuning (development build)", { open: true });
    for (const [key, label, title, step] of TUNABLES) {
      const row = LifecastControls.el("label", "stage-tuning-row");
      row.title = title;
      row.appendChild(LifecastControls.el("span", "stage-tuning-label", label));
      const input = document.createElement("input");
      input.type = "number";
      input.step = step;
      input.className = "stage-tuning-input";
      // "change" and not "input": committing on every keystroke sends a
      // message per digit, and typing "120" would pass through 1 and 12 -
      // each one a real camera move, and 1 is outside every sane bound.
      input.addEventListener("change", () => {
        const v = parseFloat(input.value);
        if (!isFinite(v)) {
          return;
        }
        LifecastBridge.send("stage-tune", { [key]: v });
      });
      tuningInputs.set(key, input);
      row.appendChild(input);
      // Back to the COMPILED value, which the DLL reports in "tuningDefaults" -
      // not the one this box opened with. It goes through the same message a
      // typed number does.
      const reset = LifecastControls.el("button", "panel-button stage-tuning-reset", "Reset");
      reset.type = "button";
      reset.title = "Back to the compiled value";
      reset.addEventListener("click", () => {
        const d = state && state.tuningDefaults;
        if (d && typeof d[key] === "number") {
          LifecastBridge.send("stage-tune", { [key]: d[key] });
        }
      });
      row.appendChild(reset);
      tuningBox.body.appendChild(row);
      const said = LifecastControls.el("span", "stage-tuning-said");
      said.hidden = true;
      tuningSaid.set(key, said);
      tuningBox.body.appendChild(said);
    }
    const resetAll = LifecastControls.el("button", "panel-button stage-tuning-reset-all", "Reset all");
    resetAll.type = "button";
    resetAll.title = "All seven back to the compiled values, in one message";
    resetAll.addEventListener("click", () => {
      const d = state && state.tuningDefaults;
      if (!d) {
        return;
      }
      const all = {};
      for (const [key] of TUNABLES) {
        if (typeof d[key] === "number") {
          all[key] = d[key];
        }
      }
      LifecastBridge.send("stage-tune", all);
    });
    tuningBox.body.appendChild(resetAll);
    tuningNone = LifecastControls.el("span", "stage-tuning-said");
    tuningNone.hidden = true;
    tuningBox.body.appendChild(tuningNone);
    LifecastControls.mount(tuningBox, "debug:tuning");
  }

  function renderTuning() {
    const t = state && state.tuning;
    if (!t) {
      if (tuningBox) {
        tuningBox.hidden = true;
      }
      return;
    }
    if (!tuningBox) {
      buildTuning();
    }
    tuningBox.hidden = false;
    const said = (state && state.tuneSaid) || {};
    for (const [key] of TUNABLES) {
      const input = tuningInputs.get(key);
      // Never overwrite the box he is typing in. The DLL echoes the whole set
      // on every answer, and a tick answers while a value is half-typed.
      if (input && document.activeElement !== input && typeof t[key] === "number") {
        input.value = String(t[key]);
      }
      const line = tuningSaid.get(key);
      if (line) {
        const text = typeof said[key] === "string" ? said[key] : "";
        line.textContent = text;
        line.hidden = text === "";
      }
    }
    const none = typeof said.none === "string" ? said.none : "";
    tuningNone.textContent = none;
    tuningNone.hidden = none === "";
  }

  // ----- what the DLL says ----------------------------------------------------

  function render() {
    const ready = !!(state && state.ready);
    bar.hidden = !ready;
    readout.hidden = !ready;
    stage.classList.toggle("is-ready", ready);
    // Before the early return: a refused view carries no "tuning" either, and
    // the knobs have to go away with the rest of the controls.
    renderTuning();
    if (!ready) {
      note.hidden = false;
      note.textContent = "Character view unavailable" + (state && state.why ? ": " + state.why : "");
      return;
    }
    note.hidden = true;
    const free = !!state.free;
    modeSwitch.textContent = "Free camera: " + (free ? "on" : "off") + (state.forced ? " (comparison)" : "");
    modeSwitch.classList.toggle("is-on", free);
    nearButton.hidden = free;
    // Absent "lock" means on: that is what the view did before the switch existed.
    const lock = state.lock !== false;
    lockSwitch.hidden = !free;
    lockSwitch.textContent = "Lock on character: " + (lock ? "on" : "off");
    lockSwitch.classList.toggle("is-on", lock);
    zoomGroup.hidden = !free;
    heightGroup.hidden = !free;
    reframe.hidden = !free;
    tiltGroup.hidden = !free || !state.tilt;
    for (const b of buttons) {
      b.el.disabled = b.isAtLimit ? b.isAtLimit(state) : false;
    }
    readout.textContent = free
      ? "turn " + Math.round(state.turn) + " deg  |  distance " + Math.round(state.distance) +
        "  |  height " + Math.round(state.lift) + "  |  pan " + Math.round(state.pan) +
        (state.tilt ? "  |  tilt " + Math.round(state.tiltDeg) + " deg" : "")
      : "turn " + Math.round(state.turn) + " deg  |  RaceMenu camera";
  }

  LifecastBridge.on("stage", (data) => {
    state = data && typeof data === "object" ? data : null;
    render();
  });

  // The DLL can hide the frame without touching anything else: the view
  // underneath stays exactly where it is.
  LifecastBridge.on("stage-visible", (data) => {
    stage.hidden = !(data && data.on);
  });

  // ----- telling the DLL where the stage IS ------------------------------
  // The camera has to centre the character in the stage - the window MINUS
  // the panel - and not in the window, so somebody has to know where the
  // stage's edges are. The page does: #stage is laid out from the panel's
  // right edge to the window's right edge, so its own rect IS the answer,
  // already including the panel's width, the panel scale the player set, the
  // belt's narrow form and the resolution. The DLL is told rather than
  // working it out, because none of those four are visible from C++ and any
  // constant there would be a number that is right on one screen.
  //
  // Measured, never assumed: the same reason control.js reads this rect for
  // the comparison instead of keeping it.
  function frame() {
    const width = window.innerWidth;
    if (!stage || stage.hidden || !(width > 0)) {
      return null;
    }
    const rect = stage.getBoundingClientRect();
    if (!(rect.width > 0)) {
      return null;
    }
    return { left: rect.left / width, right: rect.right / width };
  }

  // Resent only when the edges actually moved, and rounded to the fraction
  // the comparison is worth before that test: the observers below fire on
  // every frame of the panel's transition, and a solve per frame would be a
  // camera chasing an animation instead of following a layout. A twentieth of
  // a per cent of the window is far below the 2% the framing is judged by.
  const FRAME_EPSILON = 0.0002;
  let sentFrame = null;

  function sendFrame() {
    const where = frame();
    if (!where) {
      return;
    }
    if (sentFrame &&
        Math.abs(where.left - sentFrame.left) < FRAME_EPSILON &&
        Math.abs(where.right - sentFrame.right) < FRAME_EPSILON) {
      return;
    }
    if (LifecastBridge.send("stage-frame", { on: true, left: where.left, right: where.right })) {
      sentFrame = where;
    }
  }

  // The panel opening and closing is an attribute on <html> and the stage's
  // width follows it in CSS, so the attribute is not watched directly: the
  // stage's own resize is, and that catches the scale and the window too
  // without knowing about any of them. Same approach control.js uses to keep
  // its bar out of the camera bar's way.
  window.addEventListener("resize", sendFrame);
  if (typeof ResizeObserver !== "undefined") {
    new ResizeObserver(sendFrame).observe(stage);
  }

  render();
  LifecastBridge.whenAttached((ok) => {
    if (ok) {
      LifecastBridge.send("stage-get");
      sendFrame();
    }
  });
})();
