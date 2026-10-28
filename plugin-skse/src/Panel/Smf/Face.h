// Face.h
// Lifecast - the sections of the ImGui view that edit the face and the race:
// Look (look.js), Race & Sex (racesex.js) and the race's preset faces
// (racepresets.js), with the list stamps listpick.js answers.
//
// Driven by the bridge protocol only, the same messages the web page trades;
// nothing here calls Edit/ or Core/. Every function is called from View.cpp
// with the view's lock held, on the game thread.

#pragma once

#include <nlohmann/json.hpp>

namespace Lifecast::Panel::Smf::Face
{
	// One message from the DLL. True when it belongs to these sections.
	bool Receive(const std::string& a_type, const nlohmann::json& a_data);

	// The messages that fill these sections ("look-get", "race-sex-get",
	// "race-presets-get"), sent when the view comes on screen.
	void Ask();

	// A message the view sends: an edit to the face ends the "the face is on
	// preset N" state, so the same preset can be applied again.
	void Sent(std::string_view a_type);

	void DrawLook();
	void DrawRaceSex();
	void DrawRacePresets();

	// One line with these sections' state, for the backend's LogState.
	void LogState(const char* a_what);
}
