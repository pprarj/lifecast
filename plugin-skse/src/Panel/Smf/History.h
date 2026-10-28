// History.h
// Lifecast - undo and redo in the ImGui view, with the rule of the web
// page's history.js.
//
// An entry is one FINISHED gesture on one control: the value it held before
// the hand touched it, the value it ended on, and how to put either one back.
// Nothing else enters - not scrolling, not a section opened, not what was
// typed into a search field. Undo is the same "*-set" message the control
// itself sends, with the older value, so it goes the normal path in the DLL.
//
// The ImGui view has no gesture for free: a widget tells the end of its drag
// (IsItemDeactivatedAfterEdit) and its start (IsItemActivated), and each
// control of the view reads the value it held BEFORE the widget call, so the
// "before" of a drag is the value from before the hand.
//
// The history does not cross the panel being hidden (F4, the menu closing):
// between one opening and the next the character can have changed. A preset
// loaded replaces the whole character, and so does a race, a sex or a race
// preset face in this view - the web page records those as portraits of the
// face; this view starts over instead (D303).
//
// Every function is called with the view's lock held, on the game thread.

#pragma once

#include <nlohmann/json.hpp>

#include <functional>
#include <string>

namespace Lifecast::Panel::Smf::History
{
	// Puts a value back on its control and sends it as the control would.
	using Apply = std::function<void(const nlohmann::json& a_value)>;

	// One finished gesture. False when it is not an entry: no value to start
	// from (null), a gesture that ended where it started, or a send made by
	// an undo or a redo itself.
	bool Record(std::string a_label, nlohmann::json a_before, nlohmann::json a_after, Apply a_apply);

	bool Undo(const char* a_from);
	bool Redo(const char* a_from);
	bool UndoAll(const char* a_from);

	// Drops every entry (the panel hidden, a preset loaded, a race changed).
	void Clear(const char* a_why);

	// Undo, Redo and Undo all, as buttons (D130: on the bar over the stage,
	// away from Finish). Drawn in the current window.
	void DrawButtons();

	// Ctrl+Z undoes, Ctrl+Y and Ctrl+Shift+Z redo, one step per physical
	// press (undokeys.js, D281). a_textField: a text field of the view holds
	// the keyboard - the shortcut is then the field's. Read by key state, as
	// DeadKeys does: the game's input is silent while the view holds it, and
	// the view's own keys are silent while the game does.
	void Keys(bool a_textField);

	// How many entries can be undone, and how many exist.
	int Depth();
	int Size();

	// One line with the history's state, for the backend's LogState.
	void LogState(const char* a_what);
}
