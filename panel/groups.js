// groups.js
// Lifecast - a level above the section (E2 item 1 - D074: read second-hand off
// Prisma captures, origin in the phase document, not copied).
//
// controls.js knows how to draw a group heading and how to reveal a section
// by key; it does not know what "Body" or "Face" means. That mapping lives
// here, as the one place a key's prefix turns into a category.
//
// This file used to do a second job as well: an "Index" section holding a
// grid of tiles, one per category, that jumped to the first section under it.
// That section was cut, so the grid, the set of keys it skipped and
// the observer that rebuilt it all left with it - they had no other caller.
// What stays is the grouping, which is the job the file is named after.
"use strict";

// The categories of the belt (Fase 7, E4: the LB structure, D119) ARE the
// groups: one mapping, read by controls.js to draw the headings of All and by
// belt.js to build the belt. A section's key decides its category here and
// nowhere else. A group marked quick is not a category word on the belt but
// one of its always-visible buttons (D126).
const LifecastGroups = (() => {
  // The belt's order (D131, D133): the categories run toward All, which sits
  // beside the name - Head, then Body, then Race next to it. Body and Race
  // stand together because the sex lives in Body (D133). Settings is not
  // on the belt at all: its button is in the footer, beside RaceMenu's -
  // where whatever is debug or configuration lives (D132).
  // footer: the footer's button, not the belt's.
  const GROUPS = [
    { title: "Skin & Paint", test: (key) => key === "tints" || key.indexOf("overlay:") === 0 || key === "face:paint" },
    { title: "Morphs", test: (key) => key.indexOf("morph:") === 0 },
    { title: "Head", test: (key) => key === "look:Hair" || key === "look:Features" || (key.indexOf("face:") === 0 && key !== "face:paint") },
    { title: "Body", test: (key) => key === "sex" || key === "look:Body" || key === "body:sliders" || key === "scales" || key === "obody" },
    // Race is the race picker (racesex.js), and still holds RaceMenu's
    // doors (delegate) that have no public way in yet (D125).
    { title: "Race", test: (key) => key === "race" || key === "delegate" },
    { title: "Settings", footer: true, test: (key) => key === "debug" || key === "debug:tuning" },
    { title: "Presets", quick: true, test: (key) => key === "presets" },
  ];

  // key can be undefined for a section still mounting (E2 item 3): a section
  // without a key must never reach a group's test, because the tests call
  // .indexOf on it without a guard of their own.
  function groupOf(key) {
    if (typeof key !== "string") {
      return null;
    }
    const hit = GROUPS.find((g) => g.test(key));
    return hit ? hit.title : null;
  }

  LifecastControls.setGroups(groupOf);

  // The order, category by category. Without a declared place a section
  // mounted on the DLL's own time (OBody, the paint overlays, a mod's morph
  // tabs) lands wherever "last" happens to be when the DLL answers - after
  // Presets and RaceMenu, for the morphs. "morph:*" and "face:*" give a place
  // to keys that cannot be named one by one (which morph tabs exist is the
  // player's mod order; the face groups are RaceMenu's); inside a prefix they
  // keep the order they mount in. "face:paint" is named on its own so it
  // joins Skin & Paint, not Head.
  LifecastControls.setOrder([
    "tints", "overlay:body", "overlay:hand", "overlay:foot", "face:paint",
    "morph:*",
    "look:Hair", "look:Features",
    "face:RacePresets", "face:Nose", "face:Jaw", "face:Cheekbones", "face:Eyes", "face:Brows", "face:Mouth", "face:Chin", "face:*",
    "sex", "look:Body", "body:sliders", "scales", "obody",
    "race", "delegate",
    "presets",
    "debug:tuning",
    "debug",
  ]);

  return {
    names: GROUPS.filter((g) => !g.quick && !g.footer).map((g) => g.title),
    footer: GROUPS.filter((g) => g.footer).map((g) => g.title),
    groupOf: groupOf,
  };
})();
