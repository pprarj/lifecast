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

	// The look is drawn control by control, in the section its key gives
	// (look.js SECTIONS and FACE_LISTS, D325): "weight" or a list's key.
	// Nothing is drawn for a control the look does not have now, nor for an
	// empty list (F4).
	bool LookReady();
	void DrawLookRow(const char* a_key);

	// racesex.js: Sex (category Body) and Race (category Race) are two
	// sections. a_height: the list's height, 0 to fill what is left.
	void DrawSex();
	void DrawRace(float a_height);
	void DrawRacePresets(float a_height);

	// One line with these sections' state, for the backend's LogState.
	void LogState(const char* a_what);
}
