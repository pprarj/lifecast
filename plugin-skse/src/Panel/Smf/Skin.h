// Skin.h
// Lifecast - the sections of the ImGui view that edit the skin and the body:
// Skin & Tints (tints.js), Paint (overlays.js) and OBody (body.js), with the
// list stamps those files answer.
//
// Driven by the bridge protocol only, the same messages the web page trades;
// nothing here calls Edit/ or Core/. Every function is called from View.cpp
// with the view's lock held, on the game thread.
//
// No texture is shown as an image: the web page shows names, and so does
// this view.

#pragma once

#include <nlohmann/json.hpp>

namespace Lifecast::Panel::Smf::Skin
{
	// One message from the DLL. True when it belongs to these sections.
	bool Receive(const std::string& a_type, const nlohmann::json& a_data);

	// The messages that fill these sections ("tints-get", "overlays-get",
	// "body-get"), sent when the view comes on screen.
	void Ask();

	void DrawTints();
	void DrawPaint();
	void DrawBody();

	// The OBody section exists only while OBody answers with presets
	// (body.js): no empty section, no disabled one.
	bool BodyPresent();

	// One line with these sections' state, for the backend's LogState.
	void LogState(const char* a_what);
}
