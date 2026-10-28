// Settings.h
// Lifecast
//
// The panel's preferences, kept across games in a file of their own.
//
// There is no MCM: what the player sets for the panel is set in the panel.
// This module keeps those values on disk, and is the only place in the
// Lifecast that writes a state of its own to disk.
//
// WHERE. Data\SKSE\Plugins\Lifecast\settings.json, written by the game at the
// moment the player changes something - never shipped with the mod. A file
// in the package would be replaced by every update of the mod, taking the
// player's choices with it; and a file the game rewrites cannot live in a
// folder the project versions. Nothing is read from anywhere else: the
// defaults are in the code below.
//
// WHAT CAN GO WRONG, and what happens - the point of this module more than
// any one value it keeps:
//
//   no file (first run, or deleted)  every value is its default
//   a file that is not JSON, or not
//   an object                        every value is its default; the file is
//                                    left as it is until the player changes
//                                    something, and then replaced
//   a value of the wrong type, or
//   outside what the key accepts     that value is its default, the others
//                                    are kept
//   a key this version does not know ignored, and dropped at the next write
//   the write fails                  the value stays in memory for this game,
//                                    and the log says so
//
// Each of these is one log line, and none of them stops the panel.
//
// The write goes to a temporary file in the same folder, renamed over the
// real one, so a reader never meets half a file.
//
// Messages (see Panel/Bridge.h for the envelope):
//
//   page -> dll  "settings-get"  {}
//   dll -> page  "settings"      { "<key>": value, ... }   every key, always
//   page -> dll  "settings-set"  { "key": text, "value": any }
//
// A "settings-set" is always answered with "settings": the page ends showing
// what the DLL kept, whether it took the value or refused it.
//
// THE KEYS (1.0):
//
//   presetsSort   "name" | "time"      how the preset list is ordered; "name"
//   panelScale    number, 0.8 to 2.5   the panel's size (--panel-scale);
//                                      panel.js's own SCALE_MIN/SCALE_MAX
//   sectionsOpen  object, key -> bool  which sections the player left open;
//                                      an object because the set of keys
//                                      grows with what mounts that session -
//                                      a section this version does not build
//                                      keeps its saved bit unread, not lost
//
// THE KEYS (1.5):
//
//   backend       "auto" | "prisma"    which web UI framework draws the panel
//                 | "magelight"        (Panel/Panel.h); "auto". Read once at
//                 | "meridian"         kDataLoaded: a change takes effect at
//                                      the next game start. The panel offers
//                                      no control for it - it is set by
//                                      writing the file, e.g. the line
//                                          "backend": "meridian"

#pragma once

namespace Lifecast::Core::Settings
{
	inline constexpr auto kFolder = "Data\\SKSE\\Plugins\\Lifecast";
	inline constexpr auto kFile = "settings.json";

	// Reads the file and registers the bridge handlers. Call once, at
	// kDataLoaded. Safe to call twice.
	void Install();

	// The "backend" key: "auto", "prisma", "magelight" or "meridian". "auto" before
	// Install() has read the file.
	std::string Backend();
}
