// Groups.h
// Lifecast - the categories of the ImGui view's belt and the order of the
// sections inside them: panel/groups.js (GROUPS and the setOrder list) and
// the rank rule of panel/controls.js, ported as one table (D325).
//
// A section's key decides its category here and nowhere else, as on the
// page. Keep this file and groups.js saying the same thing.

#pragma once

#include <string_view>

namespace Lifecast::Panel::Smf::Groups
{
	// groups.js GROUPS, in its order. The first kBelt are the belt's
	// categories (groups.js "names"); Settings is the footer's button and
	// Presets a quick button (D126, D132).
	enum : int
	{
		kNone = -1,
		kSkinPaint = 0,
		kMorphs,
		kHead,
		kBody,
		kRace,
		kSettings,
		kPresets,
		kCount
	};
	constexpr int kBelt = kSettings;

	// The group's title as the page writes it ("Skin & Paint").
	const char* Title(int a_group);

	// The group a section key belongs to, or kNone.
	int Of(std::string_view a_key);

	// controls.js rank(): the place of a key in setOrder - an exact entry
	// wins over a prefix one ("morph:*"); a key named by neither sorts after
	// every named one. Ties keep the order the sections were mounted in.
	int Rank(std::string_view a_key);
}
