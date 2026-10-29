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

#include <string>
#include <vector>

namespace Lifecast::Panel::Smf::Skin
{
	// One message from the DLL. True when it belongs to these sections.
	bool Receive(const std::string& a_type, const nlohmann::json& a_data);

	// The messages that fill these sections ("tints-get", "overlays-get",
	// "body-get"), sent when the view comes on screen.
	void Ask();

	// A tint layer or a paint area as the view's sections see it (D325):
	// the key, the label, and the rows the page builds for it (".slider",
	// counted by belt.js) - a tint layer has Red, Green, Blue and Intensity,
	// plus Texture when it is picked and Catalog when it has a catalog; a
	// paint layer has Texture, Intensity and Glow.
	struct Part
	{
		std::string key;
		std::string label;
		int         rows = 0;
	};
	bool              TintsReady();
	std::vector<Part> TintLayers();
	std::vector<Part> PaintAreas();

	// One tint layer, drawn in the section its key gives (tints.js HOME).
	void DrawTintLayer(const std::string& a_key);
	// What Skin & Tints says while it has no layer of its own to show.
	void DrawTintStatus();
	// Every layer of one paint area, one under the other (overlays.js).
	void DrawPaintArea(const std::string& a_key);
	// a_height: the list's height, 0 to fill what is left.
	void DrawBody(float a_height);

	// The OBody section exists only while OBody answers with presets
	// (body.js): no empty section, no disabled one.
	bool BodyPresent();

	// One line with these sections' state, for the backend's LogState.
	void LogState(const char* a_what);
}
