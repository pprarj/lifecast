// Face.cpp
// Lifecast

#include "Panel/Smf/Face.h"

#include "Panel/Smf/Common.h"
#include "Panel/Smf/DeadKeys.h"
#include "Panel/Smf/History.h"
#include "Panel/Smf/Theme.h"

#include "SKSEMenuFramework/SKSEMenuFramework.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

namespace Lifecast::Panel::Smf::Face
{
	namespace
	{
		namespace IG = ImGuiMCP;
		using json = nlohmann::json;
		using Clock = std::chrono::steady_clock;
		using Common::Rect;
		using Common::Send;

		constexpr auto kSendEvery = std::chrono::milliseconds(50);
		constexpr float kListHeight = 200.0f;  // CSS pixels

		// ------------------------------------------------------------- Look

		struct Item
		{
			std::string name;
			std::string source;
			std::string label;  // "name  -  source", what the row shows
			std::string lower;  // the label in lower case, for the search
		};

		struct List
		{
			bool                     present = false;
			int                      count = 0;
			int                      index = -1;
			std::string              name;
			std::string              source;
			std::string              color;     // hair-color: the colour worn
			std::vector<std::string> swatches;  // hair-color: the race's colours
			std::vector<Item>        items;     // the four lists "look-lists" sends whole
			char                     find[96] = {};
			std::string              filteredFor = "\x01";
			std::size_t              filteredItems = 0;
			std::vector<int>         filtered;
			int                      dragged = -1;
			json                     dragBefore;  // the place when the hand took the index slider (History)
		};

		struct Weight
		{
			bool  present = false;
			float min = 0.0f;
			float max = 100.0f;
			float v = 0.0f;
			bool  active = false;
		};

		// The order and the labels of look.js (its SECTIONS and FACE_LISTS).
		struct Row
		{
			const char* key;
			const char* label;
		};
		constexpr Row kRows[] = {
			{ "hair", "Hair" },
			{ "hair-color", "Hair Color" },
			{ "facial-hair", "Facial Hair" },
			{ "eyes", "Eye Color" },
			{ "brows", "Brow Type" },
			{ "scar", "Scar" },
			{ "complexion", "Complexion" },
			{ "nose-type", "Nose Type" },
			{ "eye-shape", "Eye Shape" },
			{ "mouth-shape", "Mouth Shape" },
		};
		constexpr const char* kWholeLists[] = { "hair", "eyes", "brows", "facial-hair" };

		bool                        g_lookReady = false;
		std::map<std::string, List> g_lists;
		Weight                      g_weight;
		Clock::time_point           g_weightSent{};
		float                       g_weightBefore = 0.0f;  // the weight when the hand took the slider (History)
		char                        g_hairHex[16] = {};
		std::uint32_t               g_lookSets = 0;

		// The answers to the DLL's list stamps (listpick.js verify()): one per
		// distinct list, keyed like the page's ("look:hair", "race-presets").
		std::map<std::string, std::string> g_answered;
		std::uint32_t                      g_sigAgree = 0;
		std::uint32_t                      g_sigDiffer = 0;

		// ------------------------------------------------------ Race and sex

		struct Race
		{
			std::string name;
			std::string id;
		};

		bool              g_raceReady = false;
		std::vector<Race> g_races;
		int               g_race = -1;
		int               g_female = -1;  // -1 unknown, 0 male, 1 female
		std::string       g_rsPending;    // "race" or "sex" until "race-sex-applied"
		std::string       g_rsStatus = "--";

		// ------------------------------------------------- Race preset faces

		int                      g_rpCount = 0;
		std::vector<std::string> g_rpNames;
		std::string              g_rpKey;
		int                      g_rpAt = 0;        // where the list rests
		int                      g_rpApplied = -1;  // the preset worn, until the face is edited
		std::string              g_rpStatus = "--";

		// -------------------------------------------------------- the stamp

		std::string Fnv1a(std::string_view a_bytes)
		{
			std::uint32_t h = 0x811C9DC5U;
			for (const char c : a_bytes) {
				h ^= static_cast<std::uint8_t>(c);
				h *= 0x01000193U;
			}
			return fmt::format("{:08x}", h);
		}

		// Signs the list as received (fields joined by a tab, one line per
		// item), compares it with the DLL's stamp and answers once per list.
		void Verify(const std::string& a_key, const std::string& a_canonical, std::size_t a_n, const json& a_stamp)
		{
			const auto sig = Fnv1a(a_canonical);
			const auto len = a_canonical.size();
			const bool agree = a_stamp.is_object() && a_stamp.value("sig", std::string()) == sig &&
			                   a_stamp.value("len", static_cast<std::size_t>(0)) == len &&
			                   (!a_stamp.contains("n") || a_stamp.value("n", static_cast<std::size_t>(0)) == a_n);
			const auto tag = fmt::format("{}/{}", sig, len);
			if (g_answered[a_key] == tag) {
				return;
			}
			g_answered[a_key] = tag;
			agree ? ++g_sigAgree : ++g_sigDiffer;
			if (!agree) {
				spdlog::warn("[smf] list {}: the DLL stamped {}, the view computed {}", a_key, a_stamp.dump(), tag);
			}
			Send("list-sig", { { "k", a_key }, { "sig", sig }, { "len", len }, { "n", a_n } });
		}

		// -------------------------------------------------------- receivers

		void OnLook(const json& a_data)
		{
			g_lookReady = a_data.value("ready", false);
			const auto controls = a_data.contains("controls") && a_data["controls"].is_object() ? a_data["controls"] :
			                                                                                     json::object();
			for (const auto& r : kRows) {
				auto&      l = g_lists[r.key];
				const auto it = controls.find(r.key);
				if (it == controls.end() || !it->is_object()) {
					l.present = false;
					continue;
				}
				const auto& c = *it;
				l.present = true;
				l.count = c.value("count", 0);
				l.index = c.value("index", -1);
				l.name = c.value("name", std::string());
				l.source = c.value("source", std::string());
				l.color = c.value("color", std::string());
				if (c.contains("swatches") && c["swatches"].is_array()) {
					l.swatches.clear();
					for (const auto& s : c["swatches"]) {
						l.swatches.push_back(s.is_string() ? s.get<std::string>() : std::string("#000000"));
					}
				}
			}
			const auto w = controls.find("weight");
			g_weight.present = w != controls.end() && w->is_object();
			if (g_weight.present) {
				g_weight.min = w->value("min", 0.0f);
				g_weight.max = w->value("max", 100.0f);
				if (!g_weight.active) {
					g_weight.v = w->value("value", 0.0f);
				}
			}
		}

		void OnLookLists(const json& a_data)
		{
			if (!a_data.contains("lists") || !a_data["lists"].is_object()) {
				return;
			}
			const auto& lists = a_data["lists"];
			for (const auto* key : kWholeLists) {
				const auto it = lists.find(key);
				if (it == lists.end() || !it->is_object() || !it->contains("items") || !(*it)["items"].is_array()) {
					continue;
				}
				auto&       l = g_lists[key];
				std::string canonical;
				l.items.clear();
				for (const auto& a : (*it)["items"]) {
					Item item;
					if (a.is_array()) {
						item.name = a.size() > 0 && a[0].is_string() ? a[0].get<std::string>() : std::string();
						item.source = a.size() > 1 && a[1].is_string() ? a[1].get<std::string>() : std::string();
					}
					canonical += item.name + "\t" + item.source + "\n";
					item.label = item.source.empty() ? item.name : item.name + "  -  " + item.source;
					item.lower = Common::Lower(item.label);
					l.items.push_back(std::move(item));
				}
				l.filteredFor = "\x01";
				Verify(std::string("look:") + key, canonical, l.items.size(), *it);
			}
			Common::LayoutSoon();
		}

		void OnRaceSex(const json& a_data)
		{
			g_raceReady = a_data.value("ready", false);
			if (!g_raceReady) {
				g_rsStatus = "not available";
				return;
			}
			g_races.clear();
			if (a_data.contains("races") && a_data["races"].is_array()) {
				for (const auto& r : a_data["races"]) {
					Race race;
					race.name = r.is_object() ? r.value("name", std::string()) : std::string();
					race.id = r.is_object() ? r.value("id", std::string()) : std::string();
					if (race.name.empty()) {
						race.name = "(unnamed)";
					}
					g_races.push_back(std::move(race));
				}
			}
			g_race = a_data.value("race", -1);
			g_female = a_data.contains("female") && a_data["female"].is_boolean() ? (a_data["female"].get<bool>() ? 1 : 0) :
			                                                                         -1;
			if (g_rsPending.empty()) {
				const auto* now = g_race >= 0 && g_race < static_cast<int>(g_races.size()) ? &g_races[g_race] : nullptr;
				g_rsStatus = fmt::format("Current: {}, {}", now ? now->name : "not a race in this list",
					g_female == 1 ? "female" : g_female == 0 ? "male" : "--");
			}
			Common::LayoutSoon();
		}

		void OnRaceSexApplied(const json& a_data)
		{
			const auto what = a_data.value("what", g_rsPending);
			const bool ok = a_data.value("ok", false);
			g_rsPending.clear();
			if (!ok) {
				const auto why = a_data.value("why", std::string());
				g_rsStatus = fmt::format("{} not changed{}{}", what, why.empty() ? "" : " - ", why);
			}
			spdlog::info("[smf] view: {} change answered {}", what, ok ? "ok" : "refused");
		}

		void OnRacePresets(const json& a_data)
		{
			g_rpCount = a_data.value("count", 0);
			g_rpNames.clear();
			std::string canonical;
			if (a_data.contains("names") && a_data["names"].is_array()) {
				for (const auto& n : a_data["names"]) {
					g_rpNames.push_back(n.is_string() ? n.get<std::string>() : std::string());
					canonical += g_rpNames.back() + "\n";
				}
			}
			std::string key;
			for (const auto& n : g_rpNames) {
				key += n + "|";
			}
			if (key != g_rpKey) {
				g_rpKey = key;
				g_rpApplied = -1;
			}
			g_rpAt = g_rpCount > 0 ? (std::clamp)(g_rpAt, 0, g_rpCount - 1) : 0;
			if (g_rpCount == 0) {
				g_rpStatus = "this race has no preset faces";
			}
			if (a_data.contains("sig")) {
				Verify("race-presets", canonical, g_rpNames.size(), a_data["sig"]);
			}
			Common::LayoutSoon();
		}

		// ------------------------------------------------------------ draws

		void SendLook(const char* a_key, json a_v, bool a_final)
		{
			++g_lookSets;
			Send("look-set", { { "k", a_key }, { "v", std::move(a_v) }, { "final", a_final } });
		}

		// ---------------------------------------------------------- history

		const char* LabelOf(std::string_view a_key)
		{
			for (const auto& r : kRows) {
				if (a_key == r.key) {
					return r.label;
				}
			}
			return "Look";
		}

		// A list's place, for the history: null while the list rests on no entry.
		json Place(const List& a_l)
		{
			return a_l.index >= 0 ? json(a_l.index) : json();
		}

		// Puts a list back on an entry, as its arrows do.
		void ApplyPlace(const std::string& a_key, const json& a_v)
		{
			if (!a_v.is_number_integer()) {
				return;
			}
			auto& l = g_lists[a_key];
			l.index = a_v.get<int>();
			l.dragged = -1;
			SendLook(a_key.c_str(), l.index, true);
		}

		void RecordPlace(const char* a_key, json a_before, int a_after)
		{
			const std::string k = a_key;
			History::Record(LabelOf(a_key), std::move(a_before), a_after, [k](const json& a_v) { ApplyPlace(k, a_v); });
		}

		// The hair colour is a swatch (its index) or a free colour (its hex).
		json HairNow(const List& a_l)
		{
			if (a_l.index >= 0) {
				return a_l.index;
			}
			return a_l.color.empty() ? json() : json(a_l.color);
		}

		void ApplyHair(const json& a_v)
		{
			auto& l = g_lists["hair-color"];
			if (a_v.is_number_integer()) {
				l.index = a_v.get<int>();
				if (l.index < static_cast<int>(l.swatches.size())) {
					l.color = l.swatches[l.index];
				}
				SendLook("hair-color", l.index, true);
			} else if (a_v.is_string()) {
				l.index = -1;
				l.color = a_v.get<std::string>();
				SendLook("hair-rgb", l.color, true);
			}
		}

		void RecordHair(json a_before, json a_after)
		{
			History::Record("Hair Color", std::move(a_before), std::move(a_after), [](const json& a_v) { ApplyHair(a_v); });
		}

		void ApplyWeight(const json& a_v)
		{
			if (!a_v.is_number()) {
				return;
			}
			g_weight.v = a_v.get<float>();
			g_weight.active = false;
			SendLook("weight", g_weight.v, true);
		}

		bool ParseHex(std::string_view a_text, float a_rgb[3])
		{
			if (!a_text.empty() && a_text.front() == '#') {
				a_text.remove_prefix(1);
			}
			if (a_text.size() != 6) {
				return false;
			}
			for (int i = 0; i < 3; ++i) {
				char  part[3] = { a_text[i * 2], a_text[i * 2 + 1], 0 };
				char* end = nullptr;
				const auto v = std::strtol(part, &end, 16);
				if (!end || *end != 0) {
					return false;
				}
				a_rgb[i] = static_cast<float>(v) / 255.0f;
			}
			return true;
		}

		// Previous and next buttons and the place in the list: the row of a
		// list whose entries have no names (and of a named one, beside the
		// search).
		void Stepper(const char* a_key, List& a_l)
		{
			IG::PushID(a_key);
			const int last = a_l.count - 1;
			if (IG::ArrowButton("##prev", IG::ImGuiDir_Left) && a_l.count > 0) {
				const int to = a_l.index <= 0 ? last : a_l.index - 1;
				auto      before = Place(a_l);
				a_l.index = to;
				SendLook(a_key, to, true);
				RecordPlace(a_key, std::move(before), to);
			}
			Rect(fmt::format("look:{}:prev", a_key));
			IG::SameLine();
			if (IG::ArrowButton("##next", IG::ImGuiDir_Right) && a_l.count > 0) {
				const int to = a_l.index >= last ? 0 : a_l.index + 1;
				auto      before = Place(a_l);
				a_l.index = to;
				SendLook(a_key, to, true);
				RecordPlace(a_key, std::move(before), to);
			}
			Rect(fmt::format("look:{}:next", a_key));
			IG::SameLine();
			IG::SetNextItemWidth(-1.0f);
			int v = a_l.dragged >= 0 ? a_l.dragged : (std::max)(a_l.index, 0);
			const auto format = a_l.index < 0 && a_l.dragged < 0 ? std::string("-- / %d") :
			                                                       fmt::format("%d / {}", last);
			if (IG::SliderInt("##index", &v, 0, (std::max)(last, 0), format.c_str())) {
				a_l.dragged = v;
			}
			Rect(fmt::format("look:{}:slider", a_key));
			if (IG::IsItemActivated()) {
				a_l.dragBefore = Place(a_l);
			}
			if (IG::IsItemDeactivatedAfterEdit() && a_l.dragged >= 0) {
				a_l.index = a_l.dragged;
				SendLook(a_key, a_l.dragged, true);
				RecordPlace(a_key, a_l.dragBefore, a_l.dragged);
			}
			if (!IG::IsItemActive()) {
				a_l.dragged = -1;
			}
			IG::PopID();
		}

		// A named list: the search and the entries, drawn through a clipper.
		void Picker(const char* a_key, List& a_l)
		{
			IG::PushID(a_key);
			IG::SetNextItemWidth(-1.0f);
			IG::InputTextWithHint("##find", "Search this list", a_l.find, sizeof(a_l.find),
				IG::ImGuiInputTextFlags_CallbackCharFilter, &DeadKeys::Filter);
			Rect(fmt::format("look:{}:find", a_key));
			const std::string words = Common::Lower(a_l.find);
			if (words != a_l.filteredFor || a_l.filteredItems != a_l.items.size()) {
				a_l.filteredFor = words;
				a_l.filteredItems = a_l.items.size();
				a_l.filtered.clear();
				for (int i = 0; i < static_cast<int>(a_l.items.size()); ++i) {
					if (words.empty() || a_l.items[i].lower.find(words) != std::string::npos) {
						a_l.filtered.push_back(i);
					}
				}
				if (!words.empty()) {
					spdlog::info("[smf] view: look {} search '{}' -> {} of {}", a_key, a_l.find, a_l.filtered.size(),
						a_l.items.size());
				}
				Common::LayoutSoon();
			}
			IG::TextDisabled("%d of %d", static_cast<int>(a_l.filtered.size()), static_cast<int>(a_l.items.size()));
			IG::BeginChild("##items", IG::ImVec2{ 0.0f, Theme::Px(kListHeight) }, IG::ImGuiChildFlags_Border);
			Theme::Body();
			auto* clip = IG::ImGuiListClipperManager::Create();
			IG::ImGuiListClipperManager::Begin(clip, static_cast<int>(a_l.filtered.size()), -1.0f);
			while (IG::ImGuiListClipperManager::Step(clip)) {
				for (int r = clip->DisplayStart; r < clip->DisplayEnd; ++r) {
					const int   i = a_l.filtered[r];
					const auto& it = a_l.items[i];
					IG::PushID(i);
					if (IG::Selectable(it.label.c_str(), i == a_l.index)) {
						spdlog::info("[smf] view: look {} pick {} '{}'", a_key, i, it.label);
						auto before = Place(a_l);
						a_l.index = i;
						SendLook(a_key, i, true);
						RecordPlace(a_key, std::move(before), i);
					}
					Rect(fmt::format("li:{}:{}", a_key, i));
					IG::PopID();
				}
			}
			IG::ImGuiListClipperManager::End(clip);
			IG::ImGuiListClipperManager::Destroy(clip);
			IG::EndChild();
			IG::PopID();
		}

		void HairColor(List& a_l)
		{
			IG::PushID("hair-color");
			for (int i = 0; i < static_cast<int>(a_l.swatches.size()); ++i) {
				float rgb[3] = { 0.0f, 0.0f, 0.0f };
				ParseHex(a_l.swatches[i], rgb);
				if (i > 0 && i % 12 != 0) {
					IG::SameLine();
				}
				IG::PushID(i);
				const bool current = i == a_l.index;
				if (current) {
					IG::PushStyleColor(IG::ImGuiCol_Border, IG::ImVec4{ 1.0f, 1.0f, 1.0f, 1.0f });
				}
				if (IG::ColorButton("##swatch", IG::ImVec4{ rgb[0], rgb[1], rgb[2], 1.0f },
						current ? 0 : IG::ImGuiColorEditFlags_NoBorder, IG::ImVec2{ Theme::Px(18.0f), Theme::Px(18.0f) })) {
					spdlog::info("[smf] view: hair colour swatch {} {}", i, a_l.swatches[i]);
					auto before = HairNow(a_l);
					a_l.index = i;
					a_l.color = a_l.swatches[i];
					SendLook("hair-color", i, true);
					RecordHair(std::move(before), i);
				}
				if (current) {
					IG::PopStyleColor();
				}
				Rect(fmt::format("sw:{}", i));
				IG::PopID();
			}
			float worn[3] = { 0.0f, 0.0f, 0.0f };
			const bool hasWorn = ParseHex(a_l.color, worn);
			IG::ColorButton("##worn", IG::ImVec4{ worn[0], worn[1], worn[2], hasWorn ? 1.0f : 0.0f }, 0,
				IG::ImVec2{ Theme::Px(18.0f), Theme::Px(18.0f) });
			IG::SameLine();
			IG::TextUnformatted(a_l.index < 0 ? (a_l.color.empty() ? "--" : a_l.color.c_str()) :
			                                    fmt::format("{} / {}", a_l.index, a_l.count - 1).c_str());
			IG::SameLine();
			IG::SetNextItemWidth(Theme::Px(70.0f));
			if (IG::InputTextWithHint("##hex", "RRGGBB", g_hairHex, sizeof(g_hairHex),
					IG::ImGuiInputTextFlags_CharsHexadecimal | IG::ImGuiInputTextFlags_EnterReturnsTrue |
						IG::ImGuiInputTextFlags_AutoSelectAll)) {
				float rgb[3];
				if (ParseHex(g_hairHex, rgb)) {
					const auto hex = fmt::format("#{:02X}{:02X}{:02X}", static_cast<int>(rgb[0] * 255.0f + 0.5f),
						static_cast<int>(rgb[1] * 255.0f + 0.5f), static_cast<int>(rgb[2] * 255.0f + 0.5f));
					spdlog::info("[smf] view: hair colour typed {}", hex);
					auto before = HairNow(a_l);
					a_l.index = -1;
					a_l.color = hex;
					SendLook("hair-rgb", hex, true);
					RecordHair(std::move(before), hex);
				} else {
					spdlog::info("[smf] view: hair colour '{}' is not six hex digits - not sent", g_hairHex);
				}
			}
			Rect("look:hair-rgb");
			IG::PopID();
		}
	}

	bool Receive(const std::string& a_type, const json& a_data)
	{
		if (a_type == "look") {
			OnLook(a_data);
		} else if (a_type == "look-lists") {
			OnLookLists(a_data);
		} else if (a_type == "race-sex") {
			OnRaceSex(a_data);
		} else if (a_type == "race-sex-applied") {
			OnRaceSexApplied(a_data);
		} else if (a_type == "race-presets") {
			OnRacePresets(a_data);
		} else if (a_type == "race-preset-applied") {
			const bool ok = a_data.value("ok", false);
			const int  i = a_data.value("i", g_rpAt);
			const auto why = a_data.value("why", std::string());
			g_rpStatus = ok ? fmt::format("applied Preset {}", i + 1) :
			                  fmt::format("not applied{}{}", why.empty() ? "" : " - ", why);
		} else if (a_type == "race-preset-native") {
			const int i = a_data.value("i", -1);
			if (i >= 0 && i < g_rpCount) {
				g_rpAt = i;
				g_rpApplied = i;
				g_rpStatus = fmt::format("RaceMenu is on Preset {}", i + 1);
			}
		} else {
			return false;
		}
		return true;
	}

	void Ask()
	{
		Send("look-get");
		Send("race-sex-get");
		Send("race-presets-get");
	}

	void Sent(std::string_view a_type)
	{
		if (a_type == "slider-set" || a_type == "look-set") {
			g_rpApplied = -1;
		}
	}

	void DrawLook()
	{
		if (!g_lookReady) {
			IG::TextDisabled("Reading the character's look...");
			return;
		}
		IG::BeginChild("##look");
		Theme::Body();
		if (g_weight.present) {
			Theme::Group("Weight");
			IG::SetNextItemWidth(-1.0f);
			float       v = g_weight.v;
			const float was = g_weight.v;
			if (IG::SliderFloat("##weight", &v, g_weight.min, g_weight.max, "%.0f")) {
				g_weight.v = v;
				g_weight.active = true;
				const auto now = Clock::now();
				if (now - g_weightSent >= kSendEvery) {
					g_weightSent = now;
					SendLook("weight", g_weight.v, false);
				}
			}
			Rect("look:weight");
			if (IG::IsItemActivated()) {
				g_weightBefore = was;
			}
			if (IG::IsItemDeactivatedAfterEdit()) {
				g_weight.active = false;
				spdlog::info("[smf] view: weight released at {:.0f}", g_weight.v);
				SendLook("weight", g_weight.v, true);
				History::Record("Weight", g_weightBefore, g_weight.v, [](const json& a_v) { ApplyWeight(a_v); });
			} else if (!IG::IsItemActive()) {
				g_weight.active = false;
			}
			IG::Separator();
		}
		for (const auto& r : kRows) {
			auto& l = g_lists[r.key];
			if (!l.present || l.count == 0) {
				continue;
			}
			Theme::Group(r.label);
			if (!l.name.empty() && l.index >= 0) {
				IG::SameLine();
				IG::TextDisabled("%s%s%s", l.name.c_str(), l.source.empty() ? "" : "  -  ", l.source.c_str());
			}
			if (std::string_view(r.key) == "hair-color") {
				HairColor(l);
			} else {
				Stepper(r.key, l);
				if (!l.items.empty()) {
					Picker(r.key, l);
				}
			}
			IG::Separator();
		}
		IG::EndChild();
	}

	void DrawRaceSex(float a_below)
	{
		if (!g_raceReady) {
			IG::TextDisabled("Race and sex: %s", g_rsStatus.c_str());
			return;
		}
		const bool busy = !g_rsPending.empty();
		Theme::Group("Sex");
		for (int f = 0; f < 2; ++f) {
			IG::SameLine();
			const bool current = g_female == f;
			if (IG::RadioButton(f ? "Female##sex" : "Male##sex", current) && !current && !busy) {
				g_rsPending = "sex";
				g_rsStatus = fmt::format("changing to {}...", f ? "female" : "male");
				spdlog::info("[smf] view: sex-set {}", f ? "female" : "male");
				Send("sex-set", { { "female", f == 1 } });
				History::Clear("the sex was changed");
			}
			Rect(f ? "sex:female" : "sex:male");
		}
		IG::TextDisabled("%s", g_rsStatus.c_str());
		IG::Separator();
		Theme::Group("Race");
		// a_below: what the section draws under the list (View's "In RaceMenu")
		IG::BeginChild("##races", IG::ImVec2{ 0.0f, -a_below });
		Theme::Body();
		for (int i = 0; i < static_cast<int>(g_races.size()); ++i) {
			IG::PushID(i);
			if (IG::Selectable(g_races[i].name.c_str(), i == g_race) && i != g_race && !busy) {
				g_rsPending = "race";
				g_rsStatus = fmt::format("changing to {}...", g_races[i].name);
				spdlog::info("[smf] view: race-set {} '{}' ({})", i, g_races[i].name, g_races[i].id);
				Send("race-set", { { "i", i } });
				History::Clear("the race was changed");
			}
			Rect(fmt::format("race:{}", i));
			IG::PopID();
		}
		IG::EndChild();
	}

	void DrawRacePresets()
	{
		if (g_rpCount == 0) {
			IG::TextDisabled("%s", g_rpStatus.c_str());
			return;
		}
		auto apply = [](int a_i) {
			g_rpAt = a_i;
			if (a_i == g_rpApplied) {
				return;
			}
			g_rpApplied = a_i;
			g_rpStatus = fmt::format("applying Preset {}...", a_i + 1);
			spdlog::info("[smf] view: race-preset-apply {} ({})", a_i,
				a_i < static_cast<int>(g_rpNames.size()) ? g_rpNames[a_i] : std::string());
			Send("race-preset-apply", { { "i", a_i } });
			History::Clear("a race face was applied");
		};
		if (IG::ArrowButton("##rp-prev", IG::ImGuiDir_Left)) {
			apply(g_rpAt <= 0 ? g_rpCount - 1 : g_rpAt - 1);
		}
		Rect("rp:prev");
		IG::SameLine();
		if (IG::ArrowButton("##rp-next", IG::ImGuiDir_Right)) {
			apply(g_rpAt >= g_rpCount - 1 ? 0 : g_rpAt + 1);
		}
		Rect("rp:next");
		IG::SameLine();
		IG::Text("Preset %d / %d", g_rpAt + 1, g_rpCount);
		IG::TextDisabled("%s", g_rpStatus.c_str());
		IG::BeginChild("##rp");
		Theme::Body();
		for (int i = 0; i < g_rpCount; ++i) {
			IG::PushID(i);
			const auto label = fmt::format("Preset {}", i + 1);
			if (IG::Selectable(label.c_str(), i == g_rpAt)) {
				apply(i);
			}
			Rect(fmt::format("rp:{}", i));
			IG::PopID();
		}
		IG::EndChild();
	}

	void LogState(const char* a_what)
	{
		std::string lists;
		for (const auto& r : kRows) {
			const auto it = g_lists.find(r.key);
			if (it == g_lists.end() || !it->second.present) {
				continue;
			}
			lists += fmt::format("{}{} {}/{}{}", lists.empty() ? "" : ", ", r.key, it->second.index, it->second.count,
				it->second.items.empty() ? "" : fmt::format(" ({} named)", it->second.items.size()));
		}
		spdlog::info("[smf] view {} - look ready {}, weight {:.0f}, {} | race {} of {}, female {}, pending '{}' | "
					 "presets {} at {} worn {} | look-set {}, list stamps agree {} differ {}",
			a_what, g_lookReady, g_weight.v, lists, g_race, g_races.size(), g_female, g_rsPending, g_rpCount, g_rpAt,
			g_rpApplied, g_lookSets, g_sigAgree, g_sigDiffer);
	}
}

namespace Lifecast::Panel::Smf::Common
{
	void Verify(const std::string& a_key, const std::string& a_canonical, std::size_t a_n, const nlohmann::json& a_stamp)
	{
		Face::Verify(a_key, a_canonical, a_n, a_stamp);
	}
}
