// keyboard.js
// Lifecast - corrections to the keyboard as each backend delivers it to text
// fields. Measured in the game, ABNT2 layout.
//
// Meridian UI:
//   1. A dead key does not compose. The accent arrives as its own character
//      and the next vowel as another: "~" then "a" gives "~a", not the
//      composed letter. Here the pair is joined back into one character.
//   2. Some key presses arrive twice as two whole presses. The duplicate is
//      dropped by KEY STATE: a second keydown for a key that has not come up
//      again, close behind the first, is not a person pressing twice.
//
// Prisma UI:
//   3. A dead key does not compose either, and differently: the accent key
//      arrives as a keydown that usually inserts NOTHING (the acute on ABNT2
//      measured as keydown "[" keyCode 219, no input, no composition), and the
//      vowel after it arrives plain. Sometimes, press by press, Prisma instead
//      inserts the bare accent twice right after that keydown, and the vowel
//      still arrives plain. Here the accent is read from the keydown, any bare
//      accent Prisma inserts for it is taken back out, and the accent is put
//      on the next character. No doubled press was seen on Prisma, so 2 does
//      not run there.
//
// Everything happens in the page and applies to every text field in it.
// Which backend is running comes from the bridge (LifecastBridge.backend());
// before the bridge has attached, nothing is corrected. When a backend
// delivers the keyboard correctly, its corrections have nothing to do.
"use strict";

(() => {
  function backend() {
    return typeof LifecastBridge !== "undefined" ? LifecastBridge.backend() : null;
  }

  // --- The doubled press ---------------------------------------------------
  //
  // Measured on the round of 2026-09-26 (Lifecast.log, page clock), five
  // doubled presses, and every one of them has the same shape:
  //
  //     keydown, keydown, keyup, keyup
  //
  // The second keydown always arrives BEFORE any keyup. That is what is used
  // here, and it is why the old guard - "same text again inside 30 ms" - let
  // three of the five through: it compared the TEXT INSERTED, and a doubled
  // "T" arrives as "T" then "t" because the Shift is already up. It also
  // missed a pair 108 ms apart. Key state catches all five with no dependence
  // on which character came out.
  //
  // The gap is measured against the FIRST keydown of the press, not the
  // previous one, and the two populations do not overlap:
  //
  //     doubled press   15, 15, 54, 57, 108 ms   (5 of 5, round of 2026-09-26)
  //     auto-repeat     520 ms to the second press, then 59-75 ms (round A4)
  //
  // 200 ms sits between them with room on both sides. Comparing against the
  // previous keydown instead would flag auto-repeat from the third press on.
  const DOUBLE_GAP_MS = 200;

  // Keys down right now: code -> time of the first keydown of this press.
  const heldSince = new Map();

  // Set by keydown, read by the insertion that keydown produces. Measured
  // ordering: keydown, then beforeinput, then input.
  let dropNextInsertion = false;

  // --- The dead key --------------------------------------------------------

  // Spacing accent, as the dead key inserts it -> combining mark.
  const ACCENTS = {
    "~": "\u0303",
    "\u00b4": "\u0301",
    "'": "\u0301",
    "`": "\u0300",
    "^": "\u0302",
    "\u00a8": "\u0308",
    "\"": "\u0308",
  };

  // Keys that do not end a pending accent.
  const NEUTRAL_KEYS = new Set(["Dead", "Shift", "Control", "Alt", "AltGraph", "CapsLock", "Meta"]);

  let deadPressed = false; // a dead key went down; its accent is the next insertion
  let accent = null;       // { char, mark }: the accent now sitting before the caret

  function isTextField(el) {
    if (!el) {
      return false;
    }
    if (el.tagName === "TEXTAREA") {
      return true;
    }
    return el.tagName === "INPUT" && /^(text|search|)$/i.test(el.type || "");
  }

  document.addEventListener("keydown", (event) => {
    if (backend() !== "meridian") {
      return;
    }
    const code = event.code;
    const now = performance.now();

    if (code) {
      const since = heldSince.get(code);
      if (since === undefined) {
        heldSince.set(code, now);
        dropNextInsertion = false;
      } else {
        // The key never came up. Auto-repeat that says so is always genuine;
        // under Meridian it does not say so, which is what the gap is for.
        dropNextInsertion = !event.repeat && (now - since) < DOUBLE_GAP_MS;
      }
    } else {
      dropNextInsertion = false;
    }

    if (event.key === "Dead") {
      deadPressed = true;
      return;
    }
    if (event.key.length > 1 && !NEUTRAL_KEYS.has(event.key)) {
      // Backspace, arrows, Enter and the like: the accent is abandoned.
      deadPressed = false;
      accent = null;
    }
  }, true);

  document.addEventListener("keyup", (event) => {
    if (backend() !== "meridian") {
      return;
    }
    if (event.code) {
      heldSince.delete(event.code);
    }
  }, true);

  document.addEventListener("beforeinput", (event) => {
    if (backend() !== "meridian") {
      return;
    }
    const el = event.target;
    if (!isTextField(el) || event.inputType !== "insertText" || event.data === null) {
      return;
    }

    // 2. The doubled press. Decided by the keydown this insertion belongs to.
    if (dropNextInsertion) {
      dropNextInsertion = false;
      event.preventDefault();
      return;
    }
    dropNextInsertion = false;

    const data = event.data;

    // 1a. The accent itself: let it show, remember where it is.
    if (deadPressed) {
      deadPressed = false;
      accent = Object.prototype.hasOwnProperty.call(ACCENTS, data) ? { char: data, mark: ACCENTS[data] } : null;
      return;
    }

    // 1b. The character after the accent.
    if (!accent) {
      return;
    }
    const pending = accent;
    accent = null;

    const pos = el.selectionStart;
    if (pos === null || pos !== el.selectionEnd || pos < 1 || el.value.charAt(pos - 1) !== pending.char) {
      return;
    }

    if (data === " ") {
      // Accent then space is the accent alone - what Windows does.
      event.preventDefault();
      return;
    }

    const composed = (data + pending.mark).normalize("NFC");
    if (composed.length !== 1) {
      // No such letter: both stay, as Windows leaves them.
      return;
    }

    event.preventDefault();
    el.setRangeText(composed, pos - 1, pos, "end");
    el.dispatchEvent(new InputEvent("input", { bubbles: true, inputType: "insertText", data: composed }));
  }, true);

  // --- 3. The dead key on Prisma -------------------------------------------
  //
  // A dead key is told from an ordinary key by what follows it: an ordinary
  // key's keydown is followed by its own insertion, a dead key's is not. So a
  // keydown that COULD be a dead key is held as a candidate; if an insertion
  // comes before the next keydown, it was an ordinary key (on a US layout the
  // same keyCode 219 is "[", and it types "["); if another keydown comes
  // first, the candidate was a dead key and its accent waits for the next
  // character.
  //
  // The echo (D277). Measured with every page event logged: on some presses,
  // with nothing different in the keydown itself, Prisma follows the dead
  // key's keydown with TWO trusted insertions of the bare accent, one
  // character each (U+00B4 twice, "^" twice), and the next vowel still
  // arrives plain. It is press by press, not key by key: the same key echoed
  // in one field and not in the next. Two bare accents is what Windows itself
  // gives for a dead key pressed while one is already pending, so the echo
  // looks like the dead key being translated twice - that part is inferred,
  // not measured. Taken as an ordinary key, the first echo used to drop the
  // accent, which is how the acute and the grave came out doubled and
  // uncomposed. Now an insertion of exactly the bare accent of the dead key
  // just pressed, before the next keydown, is removed and the key stays dead.
  //
  // Space reaches the page as keydown "Unidentified" (measured): that is not a
  // named key like Backspace, so it does not abandon the accent, and accent
  // then space gives the accent alone. Two dead keys in a row write both
  // accents and leave nothing waiting - the same two accents the echo shows
  // Windows producing.
  //
  // Which accent: ABNT2, by keyCode and Shift. Only the acute was measured on
  // Prisma; the rest of the table is the ABNT2 layout itself. Another layout's
  // dead keys are not in it and stay as Prisma delivers them.
  //
  //   keyCode 219        acute   (Shift: grave)
  //   keyCode 222        tilde   (Shift: circumflex)
  //   keyCode 54 + Shift diaeresis
  function prismaAccent(event) {
    if (event.ctrlKey || event.altKey || event.metaKey) {
      return null;
    }
    switch (event.keyCode) {
      case 219:
        return event.shiftKey ? { char: "`", mark: "\u0300" } : { char: "\u00b4", mark: "\u0301" };
      case 222:
        return event.shiftKey ? { char: "^", mark: "\u0302" } : { char: "~", mark: "\u0303" };
      case 54:
        return event.shiftKey ? { char: "\u00a8", mark: "\u0308" } : null;
      default:
        return null;
    }
  }

  let candidate = null; // accent of a keydown that has not inserted anything yet
  let armed = null;     // accent of a dead key, waiting for the next character
  let echo = null;      // bare accent Prisma may insert for the dead key just pressed

  document.addEventListener("keydown", (event) => {
    if (backend() !== "prisma") {
      return;
    }
    if (candidate) {
      // Another key went down and the candidate inserted nothing: dead key.
      armed = candidate;
      candidate = null;
    }
    // The echo only ever follows its own keydown.
    echo = null;
    if (NEUTRAL_KEYS.has(event.key)) {
      return;
    }
    const accent = prismaAccent(event);
    if (accent) {
      if (armed) {
        // Two dead keys in a row: both accents, nothing left waiting.
        const el = document.activeElement;
        if (isTextField(el)) {
          insertAt(el, armed.char + accent.char);
        }
        armed = null;
      } else {
        candidate = accent;
      }
      echo = accent.char;
      return;
    }
    if (event.key.length > 1 && event.key !== "Unidentified") {
      // Backspace, arrows, Enter and the like: the accent is abandoned.
      armed = null;
    }
  }, true);

  // The page's own insertions dispatch "input" too; those are not keys.
  let selfInput = false;

  function insertAt(el, text, start, end) {
    const s = start === undefined ? el.selectionStart : start;
    const e = end === undefined ? el.selectionEnd : end;
    if (s === null || e === null) {
      return false;
    }
    el.setRangeText(text, s, e, "end");
    selfInput = true;
    try {
      el.dispatchEvent(new InputEvent("input", text === ""
        ? { bubbles: true, inputType: "deleteContentBackward", data: null }
        : { bubbles: true, inputType: "insertText", data: text }));
    } finally {
      selfInput = false;
    }
    return true;
  }

  // Read on "input", after the character is in the field, and not on
  // "beforeinput": "input" is what was seen on Prisma, along with
  // "beforeinput". The character just typed is replaced.
  document.addEventListener("input", (event) => {
    if (backend() !== "prisma" || selfInput) {
      return;
    }
    const el = event.target;
    const data = event.data;
    if (!isTextField(el)) {
      return;
    }
    const pos = el.selectionStart;
    const caretAfter = (ch) => pos !== null && pos === el.selectionEnd && pos >= 1 && el.value.charAt(pos - 1) === ch;

    if (echo !== null && data === echo && caretAfter(data)) {
      // Prisma's echo of the dead key: out again, and the key stays dead.
      insertAt(el, "", pos - 1, pos);
      return;
    }
    if (event.inputType !== "insertText") {
      // A deletion, a paste and the like: whatever was waiting is abandoned.
      candidate = null;
      armed = null;
      return;
    }
    if (candidate) {
      // The candidate key inserted something else: an ordinary key. Let it be.
      candidate = null;
      return;
    }
    if (!armed) {
      return;
    }
    const pending = armed;
    armed = null;
    if (typeof data !== "string" || data.length !== 1 || data.normalize("NFD").length !== 1 || !caretAfter(data)) {
      // More than one character, or a letter that already carries its accent
      // (Prisma composed it): left as it came.
      return;
    }
    let text;
    if (data === " ") {
      text = pending.char; // accent then space is the accent alone
    } else {
      const composed = (data + pending.mark).normalize("NFC");
      text = composed.length === 1 ? composed : pending.char + data; // no such letter: both, as Windows does
    }
    insertAt(el, text, pos - 1, pos);
  }, true);
})();
