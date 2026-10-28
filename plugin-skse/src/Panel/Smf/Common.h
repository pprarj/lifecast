// Common.h
// Lifecast - what the parts of the ImGui view share (defined in View.cpp).

#pragma once

#include <nlohmann/json.hpp>

namespace Lifecast::Panel::Smf::Common
{
	// Queues one bridge message; View's Flush hands it to the bridge.
	void Send(std::string_view a_type, nlohmann::json a_data = nlohmann::json::object());

	// Notes where the last widget was drawn, for the state line a test bench
	// clicks by. Widgets out of view are left out.
	void Rect(std::string_view a_name);

	// Logs the layout line a few frames from now, once what just changed
	// (a section, a search, a rebuilt list) has been drawn.
	void LayoutSoon();

	// ASCII lower case; other bytes as they are.
	std::string Lower(std::string a_s);

	// A chip of the panel's head (.region-chip): the one on is drawn in the
	// accent over the selected row colour. True when clicked.
	bool Chip(const char* a_label, bool a_on);

	// Signs a list as received (fields joined by a tab, one line per item),
	// compares it with the DLL's stamp and answers "list-sig" once per list
	// (defined in Face.cpp, which counts the answers).
	void Verify(const std::string& a_key, const std::string& a_canonical, std::size_t a_n,
		const nlohmann::json& a_stamp);
}
