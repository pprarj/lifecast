// Groups.cpp
// Lifecast

#include "Panel/Smf/Groups.h"

#include <iterator>

namespace Lifecast::Panel::Smf::Groups
{
	namespace
	{
		bool Starts(std::string_view a_key, std::string_view a_prefix)
		{
			return a_key.substr(0, a_prefix.size()) == a_prefix;
		}

		// groups.js GROUPS: title and test, in the same order.
		struct Entry
		{
			const char* title;
			bool (*test)(std::string_view);
		};
		constexpr Entry kGroups[] = {
			{ "Skin & Paint",
				[](std::string_view k) { return k == "tints" || Starts(k, "overlay:") || k == "face:paint"; } },
			{ "Morphs", [](std::string_view k) { return Starts(k, "morph:"); } },
			{ "Head",
				[](std::string_view k) {
					return k == "look:Hair" || k == "look:Features" || (Starts(k, "face:") && k != "face:paint");
				} },
			{ "Body",
				[](std::string_view k) {
					return k == "sex" || k == "look:Body" || k == "body:sliders" || k == "scales" || k == "obody";
				} },
			{ "Race", [](std::string_view k) { return k == "race" || k == "delegate"; } },
			{ "Settings", [](std::string_view k) { return k == "debug" || k == "debug:tuning"; } },
			{ "Presets", [](std::string_view k) { return k == "presets"; } },
		};
		static_assert(std::size(kGroups) == kCount);

		// groups.js setOrder.
		constexpr std::string_view kOrder[] = {
			"tints", "overlay:body", "overlay:hand", "overlay:foot", "face:paint",
			"morph:*",
			"look:Hair", "look:Features",
			"face:RacePresets", "face:Nose", "face:Jaw", "face:Cheekbones", "face:Eyes", "face:Brows", "face:Mouth", "face:Chin", "face:*",
			"sex", "look:Body", "body:sliders", "scales", "obody",
			"race", "delegate",
			"presets",
			"debug:tuning",
			"debug",
		};
	}

	const char* Title(int a_group)
	{
		return a_group >= 0 && a_group < kCount ? kGroups[a_group].title : "";
	}

	int Of(std::string_view a_key)
	{
		for (int i = 0; i < kCount; ++i) {
			if (kGroups[i].test(a_key)) {
				return i;
			}
		}
		return kNone;
	}

	int Rank(std::string_view a_key)
	{
		constexpr int n = static_cast<int>(std::size(kOrder));
		for (int i = 0; i < n; ++i) {
			if (kOrder[i] == a_key) {
				return i;
			}
		}
		for (int i = 0; i < n; ++i) {
			const auto o = kOrder[i];
			if (o.ends_with('*') && Starts(a_key, o.substr(0, o.size() - 1))) {
				return i;
			}
		}
		return n;
	}
}
