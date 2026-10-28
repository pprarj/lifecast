// Control.h
// Lifecast - Character Control in the ImGui view (control.js): undress,
// light, freeze, the poses and Compare, on the stage bar (D130).
//
// Driven by the bridge protocol only, the same messages the web page trades
// ("undress-*", "light-*", "freeze-*", "pose-*", "compare-*"); nothing here
// calls Edit/ or Core/. Each label is what the DLL last said, never what the
// click asked for. Every function is called from View.cpp with the view's
// lock held, on the game thread.

#pragma once

#include <nlohmann/json.hpp>

namespace Lifecast::Panel::Smf::Control
{
	// One message from the DLL. True when it belongs to Character Control.
	bool Receive(const std::string& a_type, const nlohmann::json& a_data);

	// The messages control.js sends when it attaches: "undress-get",
	// "light-get", "freeze-get", "pose-list", "compare-get".
	void Ask();

	// The row on the stage bar. a_left, a_right: the stage's edges in shares of
	// the screen's width (the "frame" Compare is placed into), or below 0 when
	// the view has not drawn the stage yet.
	void Draw(float a_left, float a_right);

	// The comparison is on, as the DLL last said (the stage's Shift drags act
	// on the second figure only then).
	bool CompareOn();

	// One line with Character Control's state, for the backend's LogState.
	void LogState(const char* a_what);
}
