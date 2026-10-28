// Presets.h
// Lifecast - the presets section of the ImGui view (presets.js), and the
// character's name with the Finish button on the belt (finish.js, D129).
//
// Driven by the bridge protocol only, the same messages the web page trades:
// "presets-get", "preset-load", "preset-save", "preset-save-cancel",
// "portraits-get", "show-portraits", "show-racemenu", "name-get", "finish",
// "settings-set" (the sort order, the one preference the section keeps).
// Nothing here calls Preset/, Edit/ or Core/. Search and sort run over the
// view's copy of the list, so typing sends nothing.
//
// No image of a preset: the web page shows none either; "RaceMenu
// Portraits" opens that mod's own window, and only when the DLL says it is
// there ("portraits", available).
//
// Every function is called from View.cpp with the view's lock held, on the
// game thread.

#pragma once

#include <nlohmann/json.hpp>

namespace Lifecast::Panel::Smf::Presets
{
	// One message from the DLL. True when it belongs to these parts.
	bool Receive(const std::string& a_type, const nlohmann::json& a_data);

	// The "settings" message: the saved sort order.
	void OnSettings(const nlohmann::json& a_data);

	// The messages asked once, when the view is created ("presets-get",
	// "portraits-get", "name-get"); the DLL sends the list and the name again
	// each time the panel comes on screen.
	void Ask();

	// The section, in the small panel.
	void DrawSection();

	// The name field and Finish, on the belt, in the current window; at most
	// a_width pixels wide (the note after the button included).
	void DrawFinish(float a_width);

	// One line with these parts' state, for the backend's LogState.
	void LogState(const char* a_what);
}
