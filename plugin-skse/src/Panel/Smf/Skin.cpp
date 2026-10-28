// Skin.cpp
// Lifecast

#include "Panel/Smf/Skin.h"

#include "Panel/Smf/Common.h"
#include "Panel/Smf/DeadKeys.h"
#include "Panel/Smf/History.h"
#include "Panel/Smf/Theme.h"

#include "SKSEMenuFramework/SKSEMenuFramework.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

namespace Lifecast::Panel::Smf::Skin
{
	namespace
	{
		namespace IG = ImGuiMCP;
		using json = nlohmann::json;
		using Clock = std::chrono::steady_clock;
		using Common::Rect;
		using Common::Send;

		constexpr auto kSendEvery = std::chrono::milliseconds(50);

		// ------------------------------------------------------------ helpers

		bool ParseHex(std::string_view a_text, int a_rgb[3])
		{
			if (!a_text.empty() && a_text.front() == '#') {
				a_text.remove_prefix(1);
			}
			if (a_text.size() != 6) {
				return false;
			}
			int out[3] = {};
			for (int i = 0; i < 3; ++i) {
				char  part[3] = { a_text[i * 2], a_text[i * 2 + 1], 0 };
				char* end = nullptr;
				const auto v = std::strtol(part, &end, 16);
				if (!end || *end != 0) {
					return false;
				}
				out[i] = static_cast<int>(v);
			}
			std::copy(out, out + 3, a_rgb);
			return true;
		}

		std::string HexOf(const int a_rgb[3])
		{
			return fmt::format("#{:02X}{:02X}{:02X}", a_rgb[0], a_rgb[1], a_rgb[2]);
		}

		IG::ImVec4 Vec(const int a_rgb[3], float a_a = 1.0f)
		{
			return IG::ImVec4{ a_rgb[0] / 255.0f, a_rgb[1] / 255.0f, a_rgb[2] / 255.0f, a_a };
		}

		// tints.js fileName(): the file name without its folder and ".dds".
		std::string FileName(const std::string& a_path)
		{
			const auto cut = a_path.find_last_of("\\/");
			auto       name = cut == std::string::npos ? a_path : a_path.substr(cut + 1);
			if (name.size() > 4 && Common::Lower(name.substr(name.size() - 4)) == ".dds") {
				name.resize(name.size() - 4);
			}
			return name;
		}

		// One entry of a paint catalog (war paint, the four overlay areas).
		struct Paint
		{
			std::string name;
			std::string texture;
			std::string lower;  // the name in lower case, for the search
		};

		std::vector<Paint> ReadCatalog(const json& a_catalog, std::string& a_canonical)
		{
			std::vector<Paint> out;
			a_canonical.clear();
			if (!a_catalog.is_array()) {
				return out;
			}
			for (const auto& p : a_catalog) {
				Paint e;
				if (p.is_object()) {
					e.name = p.value("name", std::string());
					e.texture = p.value("texture", std::string());
				}
				a_canonical += e.name + "\t" + e.texture + "\n";
				e.lower = Common::Lower(e.name);
				out.push_back(std::move(e));
			}
			return out;
		}

		// The entry whose texture is the one worn (case aside), or -1.
		int CatalogAt(const std::vector<Paint>& a_catalog, const std::string& a_texture)
		{
			if (a_texture.empty()) {
				return -1;
			}
			const auto want = Common::Lower(a_texture);
			for (int i = 0; i < static_cast<int>(a_catalog.size()); ++i) {
				if (Common::Lower(a_catalog[i].texture) == want) {
					return i;
				}
			}
			return -1;
		}

		// A search over a catalog: the rows that match, rebuilt when the words
		// or the catalog change.
		struct Find
		{
			char             text[96] = {};
			std::string      filteredFor = "\x01";
			std::size_t      filteredItems = 0;
			std::vector<int> rows;
		};

		void Filter(Find& a_f, const std::vector<Paint>& a_catalog, const char* a_what)
		{
			const auto words = Common::Lower(a_f.text);
			if (words == a_f.filteredFor && a_f.filteredItems == a_catalog.size()) {
				return;
			}
			a_f.filteredFor = words;
			a_f.filteredItems = a_catalog.size();
			a_f.rows.clear();
			for (int i = 0; i < static_cast<int>(a_catalog.size()); ++i) {
				if (words.empty() || a_catalog[i].lower.find(words) != std::string::npos) {
					a_f.rows.push_back(i);
				}
			}
			if (!words.empty()) {
				spdlog::info("[smf] view: {} search '{}' -> {} of {}", a_what, a_f.text, a_f.rows.size(), a_catalog.size());
			}
			Common::LayoutSoon();
		}

		// The search field and the catalog's names, "None" first when a_none.
		// Returns the entry picked: -1 for "None", -2 for nothing.
		int CatalogList(const char* a_id, Find& a_f, const std::vector<Paint>& a_catalog, int a_at, bool a_none,
			float a_height, const char* a_what)
		{
			int picked = -2;
			IG::PushID(a_id);
			IG::SetNextItemWidth(-1.0f);
			IG::InputTextWithHint("##find", "Search this list", a_f.text, sizeof(a_f.text),
				IG::ImGuiInputTextFlags_CallbackCharFilter, &DeadKeys::Filter);
			Rect(fmt::format("{}:find", a_id));
			Filter(a_f, a_catalog, a_what);
			IG::TextDisabled("%d of %d", static_cast<int>(a_f.rows.size()), static_cast<int>(a_catalog.size()));
			IG::BeginChild("##items", IG::ImVec2{ 0.0f, Theme::Px(a_height) }, IG::ImGuiChildFlags_Border);
			Theme::Body();
			const int extra = a_none ? 1 : 0;
			auto*     clip = IG::ImGuiListClipperManager::Create();
			IG::ImGuiListClipperManager::Begin(clip, static_cast<int>(a_f.rows.size()) + extra, -1.0f);
			while (IG::ImGuiListClipperManager::Step(clip)) {
				for (int r = clip->DisplayStart; r < clip->DisplayEnd; ++r) {
					if (a_none && r == 0) {
						if (IG::Selectable("None##none", a_at < 0)) {
							picked = -1;
						}
						Rect(fmt::format("{}:none", a_id));
						continue;
					}
					const int i = a_f.rows[r - extra];
					IG::PushID(i);
					if (IG::Selectable(a_catalog[i].name.empty() ? "(unnamed)" : a_catalog[i].name.c_str(), i == a_at)) {
						picked = i;
					}
					Rect(fmt::format("{}:{}", a_id, i));
					IG::PopID();
				}
			}
			IG::ImGuiListClipperManager::End(clip);
			IG::ImGuiListClipperManager::Destroy(clip);
			IG::EndChild();
			IG::PopID();
			return picked;
		}

		// A colour edited as red, green and blue (tints.js colorRow) and as a
		// typed hex (the picker's field).
		struct ColorEd
		{
			int               rgb[3] = { 255, 255, 255 };
			bool              active = false;
			char              hex[12] = {};
			Clock::time_point sent{};
			std::string       before;  // the colour when the hand took a row (History)
		};

		// True when a send is due; a_final says whether it is the last of a drag.
		bool ColorRows(const std::string& a_rect, ColorEd& a_e, bool& a_final)
		{
			static constexpr const char* kNames[] = { "Red", "Green", "Blue" };
			static constexpr const char* kKeys[] = { "r", "g", "b" };
			bool send = false;
			bool any = false;
			a_final = false;
			const auto was = HexOf(a_e.rgb);
			// A layer has two of these (colour and glow): their widgets need ids of their own.
			IG::PushID(a_rect.c_str());
			for (int c = 0; c < 3; ++c) {
				IG::PushID(c);
				IG::AlignTextToFramePadding();
				IG::TextUnformatted(kNames[c]);
				IG::SameLine(Theme::Px(80.0f), -1.0f);
				IG::SetNextItemWidth(-1.0f);
				int v = a_e.rgb[c];
				if (IG::SliderInt("##c", &v, 0, 255, "%d")) {
					a_e.rgb[c] = v;
					const auto now = Clock::now();
					if (now - a_e.sent >= kSendEvery) {
						a_e.sent = now;
						send = true;
					}
				}
				Rect(fmt::format("{}:{}", a_rect, kKeys[c]));
				if (IG::IsItemActivated()) {
					a_e.before = was;
				}
				if (IG::IsItemDeactivatedAfterEdit()) {
					send = true;
					a_final = true;
				}
				any = any || IG::IsItemActive();
				IG::PopID();
			}
			a_e.active = any;
			IG::AlignTextToFramePadding();
			IG::TextUnformatted("Hex");
			IG::SameLine(Theme::Px(80.0f), -1.0f);
			IG::ColorButton("##now", Vec(a_e.rgb), 0, IG::ImVec2{ Theme::Px(18.0f), Theme::Px(18.0f) });
			IG::SameLine();
			IG::SetNextItemWidth(Theme::Px(80.0f));
			if (IG::InputTextWithHint("##hex", "RRGGBB", a_e.hex, sizeof(a_e.hex),
					IG::ImGuiInputTextFlags_CharsHexadecimal | IG::ImGuiInputTextFlags_EnterReturnsTrue |
						IG::ImGuiInputTextFlags_AutoSelectAll)) {
				int rgb[3];
				if (ParseHex(a_e.hex, rgb)) {
					a_e.before = was;
					std::copy(rgb, rgb + 3, a_e.rgb);
					spdlog::info("[smf] view: {} colour typed {}", a_rect, HexOf(rgb));
					send = true;
					a_final = true;
				} else {
					spdlog::info("[smf] view: {} colour '{}' is not six hex digits - not sent", a_rect, a_e.hex);
				}
			}
			Rect(fmt::format("{}:hex", a_rect));
			IG::PopID();
			return send;
		}

		// A 0 to 100 percent row (Intensity, Glow). True when a send is due.
		struct Percent
		{
			int               v = 100;
			bool              active = false;
			Clock::time_point sent{};
			int               before = 0;  // the value when the hand took the row (History)
		};

		bool PercentRow(const char* a_label, const std::string& a_rect, Percent& a_p, bool& a_final)
		{
			bool send = false;
			a_final = false;
			IG::PushID(a_label);
			IG::AlignTextToFramePadding();
			IG::TextUnformatted(a_label);
			IG::SameLine(Theme::Px(80.0f), -1.0f);
			IG::SetNextItemWidth(-1.0f);
			int       v = a_p.v;
			const int was = a_p.v;
			if (IG::SliderInt("##p", &v, 0, 100, "%d%%")) {
				a_p.v = v;
				const auto now = Clock::now();
				if (now - a_p.sent >= kSendEvery) {
					a_p.sent = now;
					send = true;
				}
			}
			Rect(a_rect);
			if (IG::IsItemActivated()) {
				a_p.before = was;
			}
			if (IG::IsItemDeactivatedAfterEdit()) {
				send = true;
				a_final = true;
			}
			a_p.active = IG::IsItemActive();
			IG::PopID();
			return send;
		}

		void Chips(const std::vector<std::pair<std::string, std::string>>& a_items, std::string& a_on,
			const char* a_rectPrefix, const char* a_what)
		{
			const auto* style = IG::GetStyle();
			const float right = IG::GetCursorScreenPos().x + IG::GetContentRegionAvail().x;
			bool        first = true;
			for (const auto& [key, label] : a_items) {
				const float w = IG::CalcTextSize(label.c_str()).x + style->FramePadding.x * 2.0f;
				if (!first) {
					IG::SameLine(0.0f, Theme::Px(4.0f));
					if (IG::GetCursorScreenPos().x + w > right) {
						IG::NewLine();
					}
				}
				first = false;
				const auto id = fmt::format("{}###{}{}", label, a_rectPrefix, key);
				if (Common::Chip(id.c_str(), a_on == key) && a_on != key) {
					a_on = key;
					spdlog::info("[smf] view: {} {}", a_what, key);
					Common::LayoutSoon();
				}
				Rect(fmt::format("{}{}", a_rectPrefix, key));
			}
		}

		// -------------------------------------------------------------- tints

		struct Tint
		{
			std::string              k;
			std::string              label;
			bool                     pick = false;
			std::string              color;
			std::vector<std::string> swatches;
			int                      count = 0;
			int                      index = 0;
			std::vector<std::string> names;  // a picked layer's masks, by texture
			bool                     hasCatalog = false;
			std::string              texture;
			std::vector<Paint>       catalog;
			ColorEd                  col;
			Percent                  alpha;
			Find                     find;

			bool None() const { return pick && index < 0; }
		};

		bool              g_tintsReady = false;
		std::vector<Tint> g_tints;
		std::string       g_tintKeys;
		std::string       g_tintOn;
		std::uint32_t     g_tintSets = 0;

		void SendTint(const std::string& a_k, json a_fields, bool a_final)
		{
			++g_tintSets;
			a_fields["k"] = a_k;
			a_fields["final"] = a_final;
			Send("tint-set", std::move(a_fields));
		}

		// Puts a tint layer's field back from the history, as its own row sends it.
		void ApplyTint(const std::string& a_k, const std::string& a_field, const json& a_v)
		{
			const auto it = std::find_if(g_tints.begin(), g_tints.end(), [&](const Tint& t) { return t.k == a_k; });
			if (it == g_tints.end()) {
				return;
			}
			auto& t = *it;
			if (a_field == "index" && a_v.is_number_integer()) {
				t.index = a_v.get<int>();
			} else if (a_field == "texture" && a_v.is_string()) {
				t.texture = a_v.get<std::string>();
			} else if (a_field == "color" && a_v.is_string()) {
				ParseHex(a_v.get<std::string>(), t.col.rgb);
				t.col.active = false;
			} else if (a_field == "alpha" && a_v.is_number()) {
				t.alpha.v = static_cast<int>(std::lround(a_v.get<double>() * 100.0));
				t.alpha.active = false;
			} else {
				return;
			}
			SendTint(a_k, { { a_field, a_v } }, true);
		}

		void RecordTint(const Tint& a_t, const char* a_field, const char* a_what, json a_before, json a_after)
		{
			const auto        k = a_t.k;
			const std::string field = a_field;
			History::Record(fmt::format("{} {}", a_t.label, a_what), std::move(a_before), std::move(a_after),
				[k, field](const json& a_v) { ApplyTint(k, field, a_v); });
		}

		void OnTints(const json& a_data)
		{
			const bool ready = a_data.value("ready", false) && a_data.contains("layers") && a_data["layers"].is_array();
			g_tintsReady = a_data.value("ready", false);
			const auto& layers = ready ? a_data["layers"] : json::array();
			std::string next;
			for (const auto& l : layers) {
				next += fmt::format("{}:{},", l.value("k", std::string()), l.value("kind", std::string()));
			}
			if (next != g_tintKeys || g_tints.size() != layers.size()) {
				g_tintKeys = next;
				g_tints.clear();
				for (const auto& l : layers) {
					Tint t;
					t.k = l.value("k", std::string());
					t.label = l.value("label", t.k);
					t.pick = l.value("kind", std::string()) == "pick";
					g_tints.push_back(std::move(t));
				}
				spdlog::info("[smf] view: {} tint layers ({})", g_tints.size(), next);
				Common::LayoutSoon();
			}
			bool found = false;
			for (const auto& t : g_tints) {
				found = found || t.k == g_tintOn;
			}
			if (!found) {
				g_tintOn = g_tints.empty() ? std::string() : g_tints.front().k;
			}
			std::size_t i = 0;
			for (const auto& l : layers) {
				auto& t = g_tints[i++];
				t.color = l.value("color", std::string("#FFFFFF"));
				if (!t.col.active) {
					ParseHex(t.color, t.col.rgb);
				}
				if (!t.alpha.active) {
					t.alpha.v = static_cast<int>(std::lround(l.value("alpha", 1.0) * 100.0));
				}
				t.swatches.clear();
				if (l.contains("swatches") && l["swatches"].is_array()) {
					for (const auto& s : l["swatches"]) {
						t.swatches.push_back(s.is_string() ? s.get<std::string>() : std::string("#000000"));
					}
				}
				if (t.pick) {
					t.count = l.value("count", 0);
					t.index = l.value("index", -1);
					if (l.contains("list") && l["list"].is_object() && l["list"].contains("items") &&
						l["list"]["items"].is_array()) {
						t.names.clear();
						std::string canonical;
						for (const auto& a : l["list"]["items"]) {
							const auto name = a.is_array() && !a.empty() && a[0].is_string() ? a[0].get<std::string>() :
							                                                                     std::string();
							canonical += name + "\n";
							t.names.push_back(name);
						}
						Common::Verify("tint:" + t.k, canonical, t.names.size(), l["list"]);
					}
				}
				t.hasCatalog = l.contains("catalog") && l["catalog"].is_array();
				if (t.hasCatalog) {
					std::string canonical;
					t.catalog = ReadCatalog(l["catalog"], canonical);
					t.texture = l.value("texture", std::string());
					if (l.contains("catalogSig")) {
						Common::Verify("tint:paint-catalog", canonical, t.catalog.size(), l["catalogSig"]);
					}
				}
			}
		}

		void DrawTint(Tint& a_t)
		{
			const bool none = a_t.None();
			Theme::Group(a_t.label.c_str());
			if (!none) {
				IG::SameLine();
				IG::ColorButton("##worn", Vec(a_t.col.rgb), 0, IG::ImVec2{ Theme::Px(14.0f), Theme::Px(14.0f) });
			}
			const auto rect = "tint:" + a_t.k;
			if (a_t.pick) {
				IG::AlignTextToFramePadding();
				IG::TextUnformatted("Texture");
				IG::SameLine(Theme::Px(80.0f), -1.0f);
				int to = -2;
				if (IG::ArrowButton("##prev", IG::ImGuiDir_Left) && a_t.index > -1) {
					to = a_t.index - 1;
				}
				Rect(rect + ":prev");
				IG::SameLine();
				if (IG::ArrowButton("##next", IG::ImGuiDir_Right) && a_t.index < a_t.count - 1) {
					to = a_t.index + 1;
				}
				Rect(rect + ":next");
				IG::SameLine();
				if (a_t.index < 0) {
					IG::TextUnformatted("None");
				} else {
					const auto& n = a_t.index < static_cast<int>(a_t.names.size()) ? a_t.names[a_t.index] : std::string();
					IG::Text("%d / %d  %s", a_t.index + 1, a_t.count, n.c_str());
				}
				const int rows = (std::min)(a_t.count + 1, 6);
				IG::BeginChild("##masks", IG::ImVec2{ 0.0f, IG::GetFrameHeight() * static_cast<float>(rows) + Theme::Px(6.0f) },
					IG::ImGuiChildFlags_Border);
				Theme::Body();
				if (IG::Selectable("None##mask-none", a_t.index < 0)) {
					to = -1;
				}
				Rect(rect + ":mask:none");
				for (int i = 0; i < a_t.count; ++i) {
					IG::PushID(i);
					const auto label = fmt::format("{}  {}", i + 1,
						i < static_cast<int>(a_t.names.size()) ? a_t.names[i] : std::string());
					if (IG::Selectable(label.c_str(), i == a_t.index)) {
						to = i;
					}
					Rect(fmt::format("{}:mask:{}", rect, i));
					IG::PopID();
				}
				IG::EndChild();
				if (to != -2 && to != a_t.index) {
					spdlog::info("[smf] view: tint {} texture {} -> {}", a_t.k, a_t.index, to);
					const int before = a_t.index;
					a_t.index = to;
					SendTint(a_t.k, { { "index", to } }, true);
					RecordTint(a_t, "index", "Texture", before, to);
				}
			}
			if (a_t.hasCatalog) {
				const int at = CatalogAt(a_t.catalog, a_t.texture);
				IG::AlignTextToFramePadding();
				IG::TextUnformatted("Catalog");
				IG::SameLine(Theme::Px(80.0f), -1.0f);
				IG::TextDisabled("%s", at >= 0 ? a_t.catalog[at].name.c_str() :
				                       (a_t.texture.empty() ? "None" : FileName(a_t.texture).c_str()));
				// Only a catalog path can be written (tints.js): no "None" row here.
				const int picked = CatalogList("tcat", a_t.find, a_t.catalog, at, false, 160.0f, "war paint catalog");
				if (picked >= 0) {
					spdlog::info("[smf] view: tint {} catalog {} '{}'", a_t.k, picked, a_t.catalog[picked].name);
					auto before = a_t.texture.empty() ? json() : json(a_t.texture);
					a_t.texture = a_t.catalog[picked].texture;
					SendTint(a_t.k, { { "texture", a_t.catalog[picked].texture } }, true);
					RecordTint(a_t, "texture", "Catalog", std::move(before), a_t.texture);
				}
			}
			if (none) {
				IG::TextDisabled("No texture on this layer: choose one to colour it.");
				return;
			}
			bool final = false;
			if (ColorRows(rect, a_t.col, final)) {
				SendTint(a_t.k, { { "color", HexOf(a_t.col.rgb) } }, final);
				if (final) {
					RecordTint(a_t, "color", "Color", a_t.col.before, HexOf(a_t.col.rgb));
				}
			}
			if (!a_t.swatches.empty()) {
				for (int i = 0; i < static_cast<int>(a_t.swatches.size()); ++i) {
					int rgb[3] = { 0, 0, 0 };
					ParseHex(a_t.swatches[i], rgb);
					if (i > 0 && i % 12 != 0) {
						IG::SameLine();
					}
					IG::PushID(i);
					if (IG::ColorButton("##swatch", Vec(rgb), 0, IG::ImVec2{ Theme::Px(18.0f), Theme::Px(18.0f) })) {
						const auto before = HexOf(a_t.col.rgb);
						std::copy(rgb, rgb + 3, a_t.col.rgb);
						const auto hex = HexOf(rgb);
						spdlog::info("[smf] view: tint {} swatch {} {}", a_t.k, i, hex);
						SendTint(a_t.k, { { "color", hex } }, true);
						RecordTint(a_t, "color", "Color", before, hex);
					}
					Rect(fmt::format("{}:sw:{}", rect, i));
					IG::PopID();
				}
			}
			if (PercentRow("Intensity", rect + ":alpha", a_t.alpha, final)) {
				SendTint(a_t.k, { { "alpha", a_t.alpha.v / 100.0 } }, final);
				if (final) {
					RecordTint(a_t, "alpha", "Intensity", a_t.alpha.before / 100.0, a_t.alpha.v / 100.0);
				}
			}
		}

		// ----------------------------------------------------------- overlays

		struct Layer
		{
			int         i = 0;
			std::string texture;
			std::string name;
			ColorEd     col;
			ColorEd     glow;
			Percent     alpha;
			Percent     glowAlpha;

			bool Painted() const { return !texture.empty(); }
		};

		struct Area
		{
			std::string        k;
			std::string        label;
			std::vector<Paint> catalog;
			std::vector<Layer> layers;
			Find               find;
			int                on = 0;  // the layer shown
		};

		bool              g_overlaysReady = false;
		std::vector<Area> g_areas;
		std::string       g_areaOn;
		std::uint32_t     g_overlaySets = 0;

		void SendOverlay(const Area& a_a, const Layer& a_l, json a_fields, bool a_final)
		{
			++g_overlaySets;
			a_fields["k"] = a_a.k;
			a_fields["i"] = a_l.i;
			a_fields["final"] = a_final;
			Send("overlay-set", std::move(a_fields));
		}

		// Puts a paint layer's field back from the history, as its own row sends it.
		void ApplyOverlay(const std::string& a_k, int a_i, const std::string& a_field, const json& a_v)
		{
			const auto at = std::find_if(g_areas.begin(), g_areas.end(), [&](const Area& a) { return a.k == a_k; });
			if (at == g_areas.end()) {
				return;
			}
			auto&      area = *at;
			const auto lt = std::find_if(area.layers.begin(), area.layers.end(), [&](const Layer& l) { return l.i == a_i; });
			if (lt == area.layers.end()) {
				return;
			}
			auto& l = *lt;
			if (a_field == "texture" && a_v.is_string()) {
				l.texture = a_v.get<std::string>();
				const int c = CatalogAt(area.catalog, l.texture);
				l.name = c >= 0 ? area.catalog[c].name : std::string();
			} else if ((a_field == "color" || a_field == "glow") && a_v.is_string()) {
				auto& e = a_field == "color" ? l.col : l.glow;
				ParseHex(a_v.get<std::string>(), e.rgb);
				e.active = false;
			} else if ((a_field == "alpha" || a_field == "glowAlpha") && a_v.is_number()) {
				auto& p = a_field == "alpha" ? l.alpha : l.glowAlpha;
				p.v = static_cast<int>(std::lround(a_v.get<double>() * 100.0));
				p.active = false;
			} else {
				return;
			}
			SendOverlay(area, l, { { a_field, a_v } }, true);
		}

		void RecordOverlay(const Area& a_a, const Layer& a_l, const char* a_field, const char* a_what, json a_before,
			json a_after)
		{
			const auto        k = a_a.k;
			const int         i = a_l.i;
			const std::string field = a_field;
			History::Record(fmt::format("{} Layer {} {}", a_a.label, a_l.i + 1, a_what), std::move(a_before),
				std::move(a_after), [k, i, field](const json& a_v) { ApplyOverlay(k, i, field, a_v); });
		}

		void OnOverlays(const json& a_data)
		{
			g_overlaysReady = a_data.value("ready", false);
			if (!g_overlaysReady || !a_data.contains("areas") || !a_data["areas"].is_array()) {
				return;
			}
			for (const auto& d : a_data["areas"]) {
				if (!d.is_object() || !d.contains("k") || !d["k"].is_string() || !d.contains("layers") ||
					!d["layers"].is_array()) {
					continue;
				}
				const auto k = d["k"].get<std::string>();
				auto       it = std::find_if(g_areas.begin(), g_areas.end(), [&](const Area& a) { return a.k == k; });
				if (it == g_areas.end()) {
					Area a;
					a.k = k;
					g_areas.push_back(std::move(a));
					it = g_areas.end() - 1;
					Common::LayoutSoon();
				}
				auto& area = *it;
				area.label = d.value("label", k);
				const auto& layers = d["layers"];
				// How many layers an area has is the player's setting; the rows are
				// rebuilt only if that count changes (overlays.js).
				if (area.layers.size() != layers.size()) {
					area.layers.clear();
					for (const auto& l : layers) {
						Layer layer;
						layer.i = l.value("i", static_cast<int>(area.layers.size()));
						area.layers.push_back(std::move(layer));
					}
					area.on = 0;
					Common::LayoutSoon();
				}
				std::size_t n = 0;
				for (const auto& l : layers) {
					auto& layer = area.layers[n++];
					layer.texture = l.value("texture", std::string());
					layer.name = l.value("name", std::string());
					if (!layer.col.active) {
						ParseHex(l.value("color", std::string("#FFFFFF")), layer.col.rgb);
					}
					if (!layer.glow.active) {
						ParseHex(l.value("glow", std::string("#FFFFFF")), layer.glow.rgb);
					}
					if (!layer.alpha.active) {
						layer.alpha.v = static_cast<int>(std::lround(l.value("alpha", 1.0) * 100.0));
					}
					if (!layer.glowAlpha.active) {
						layer.glowAlpha.v = static_cast<int>(std::lround(l.value("glowAlpha", 0.0) * 100.0));
					}
				}
				std::string canonical;
				area.catalog = ReadCatalog(d.contains("catalog") ? d["catalog"] : json::array(), canonical);
				if (d.contains("catalogSig")) {
					Common::Verify("overlay:" + k, canonical, area.catalog.size(), d["catalogSig"]);
				}
			}
			bool found = false;
			for (const auto& a : g_areas) {
				found = found || a.k == g_areaOn;
			}
			if (!found) {
				g_areaOn = g_areas.empty() ? std::string() : g_areas.front().k;
			}
		}

		void DrawLayer(Area& a_a, Layer& a_l)
		{
			const auto rect = fmt::format("paint:{}:{}", a_a.k, a_l.i);
			IG::PushID(a_l.i);
			Theme::Group(fmt::format("Layer {}", a_l.i + 1).c_str());
			IG::SameLine();
			IG::TextDisabled("%s", a_l.Painted() ? (a_l.name.empty() ? FileName(a_l.texture).c_str() : a_l.name.c_str()) :
			                                       "None");
			// A texture the catalog does not carry reads as -1 and shows its own
			// name; moving off it clears it, which is what "None" means (overlays.js).
			const int at = CatalogAt(a_a.catalog, a_l.texture);
			const int last = static_cast<int>(a_a.catalog.size()) - 1;
			int       to = -2;
			IG::AlignTextToFramePadding();
			IG::TextUnformatted("Texture");
			IG::SameLine(Theme::Px(80.0f), -1.0f);
			if (IG::ArrowButton("##prev", IG::ImGuiDir_Left) && (at > -1 || a_l.Painted())) {
				to = at - 1 < -1 ? -1 : at - 1;
			}
			Rect(rect + ":prev");
			IG::SameLine();
			if (IG::ArrowButton("##next", IG::ImGuiDir_Right) && at < last) {
				to = at + 1;
			}
			Rect(rect + ":next");
			IG::SameLine();
			if (at >= 0) {
				IG::Text("%d / %d", at + 1, last + 1);
			} else {
				IG::TextUnformatted(a_l.Painted() ? "not in the catalog" : "None");
			}
			const int picked = CatalogList("pcat", a_a.find, a_a.catalog, at, true, 180.0f, "paint catalog");
			if (picked != -2) {
				to = picked;
			}
			if (to != -2 && (to != at || (to == -1 && a_l.Painted()))) {
				const auto texture = to >= 0 ? a_a.catalog[to].texture : std::string();
				spdlog::info("[smf] view: paint {} layer {} texture {} -> {} '{}'", a_a.k, a_l.i, at, to,
					to >= 0 ? a_a.catalog[to].name : std::string("None"));
				const auto before = a_l.texture;
				a_l.texture = texture;
				a_l.name = to >= 0 ? a_a.catalog[to].name : std::string();
				SendOverlay(a_a, a_l, { { "texture", texture } }, true);
				RecordOverlay(a_a, a_l, "texture", "Texture", before, texture);
			}
			if (!a_l.Painted()) {
				IG::TextDisabled("No texture on this layer: choose one to colour it.");
				IG::PopID();
				return;
			}
			bool final = false;
			Theme::Group("Colour");
			if (ColorRows(rect + ":color", a_l.col, final)) {
				SendOverlay(a_a, a_l, { { "color", HexOf(a_l.col.rgb) } }, final);
				if (final) {
					RecordOverlay(a_a, a_l, "color", "Colour", a_l.col.before, HexOf(a_l.col.rgb));
				}
			}
			if (PercentRow("Intensity", rect + ":alpha", a_l.alpha, final)) {
				SendOverlay(a_a, a_l, { { "alpha", a_l.alpha.v / 100.0 } }, final);
				if (final) {
					RecordOverlay(a_a, a_l, "alpha", "Intensity", a_l.alpha.before / 100.0, a_l.alpha.v / 100.0);
				}
			}
			Theme::Group("Glow");
			if (ColorRows(rect + ":glow", a_l.glow, final)) {
				SendOverlay(a_a, a_l, { { "glow", HexOf(a_l.glow.rgb) } }, final);
				if (final) {
					RecordOverlay(a_a, a_l, "glow", "Glow", a_l.glow.before, HexOf(a_l.glow.rgb));
				}
			}
			if (PercentRow("Strength", rect + ":glowalpha", a_l.glowAlpha, final)) {
				SendOverlay(a_a, a_l, { { "glowAlpha", a_l.glowAlpha.v / 100.0 } }, final);
				if (final) {
					RecordOverlay(a_a, a_l, "glowAlpha", "Glow Strength", a_l.glowAlpha.before / 100.0,
						a_l.glowAlpha.v / 100.0);
				}
			}
			IG::PopID();
		}

		// --------------------------------------------------------------- body

		bool                     g_bodyReady = false;
		std::vector<std::string> g_bodyNames;
		std::string              g_bodyCurrent;
		std::string              g_bodyPending;  // the name asked for, until the next "body"
		std::uint32_t            g_bodySets = 0;

		void OnBody(const json& a_data)
		{
			g_bodyPending.clear();
			const bool ready = a_data.value("ready", false) && a_data.contains("presets") && a_data["presets"].is_array() &&
			                   !a_data["presets"].empty();
			if (ready != g_bodyReady) {
				spdlog::info("[smf] view: OBody section {}", ready ? "present" : "absent");
				Common::LayoutSoon();
			}
			g_bodyReady = ready;
			g_bodyNames.clear();
			g_bodyCurrent.clear();
			if (!ready) {
				return;
			}
			for (const auto& n : a_data["presets"]) {
				if (n.is_string()) {
					g_bodyNames.push_back(n.get<std::string>());
				}
			}
			std::stable_sort(g_bodyNames.begin(), g_bodyNames.end(),
				[](const std::string& a, const std::string& b) { return Common::Lower(a) < Common::Lower(b); });
			g_bodyCurrent = a_data.value("current", std::string());
		}
	}

	bool Receive(const std::string& a_type, const json& a_data)
	{
		if (a_type == "tints") {
			OnTints(a_data);
		} else if (a_type == "overlays") {
			OnOverlays(a_data);
		} else if (a_type == "body") {
			OnBody(a_data);
		} else {
			return false;
		}
		return true;
	}

	void Ask()
	{
		Send("body-get");
		Send("tints-get");
		Send("overlays-get");
	}

	void DrawTints()
	{
		if (!g_tintsReady) {
			IG::TextDisabled("Reading the tint layers...");
			return;
		}
		if (g_tints.empty()) {
			IG::TextDisabled("This character has no tint layers.");
			return;
		}
		std::vector<std::pair<std::string, std::string>> chips;
		for (const auto& t : g_tints) {
			chips.emplace_back(t.k, t.label);
		}
		Chips(chips, g_tintOn, "tl:", "tint layer");
		IG::Separator();
		IG::BeginChild("##tints");
		Theme::Body();
		for (auto& t : g_tints) {
			if (t.k == g_tintOn) {
				IG::PushID(t.k.c_str());
				DrawTint(t);
				IG::PopID();
			}
		}
		IG::EndChild();
	}

	void DrawPaint()
	{
		if (!g_overlaysReady || g_areas.empty()) {
			IG::TextDisabled(g_overlaysReady ? "This character has no paint layers." : "Reading the paint layers...");
			return;
		}
		std::vector<std::pair<std::string, std::string>> chips;
		for (const auto& a : g_areas) {
			chips.emplace_back(a.k, a.label);
		}
		Chips(chips, g_areaOn, "pa:", "paint area");
		auto it = std::find_if(g_areas.begin(), g_areas.end(), [](const Area& a) { return a.k == g_areaOn; });
		if (it == g_areas.end() || it->layers.empty()) {
			return;
		}
		auto& area = *it;
		std::vector<std::pair<std::string, std::string>> layers;
		for (const auto& l : area.layers) {
			layers.emplace_back(std::to_string(l.i), fmt::format("Layer {}{}", l.i + 1, l.Painted() ? " *" : ""));
		}
		auto on = std::to_string(area.layers[(std::clamp)(area.on, 0, static_cast<int>(area.layers.size()) - 1)].i);
		Chips(layers, on, "pl:", "paint layer");
		for (int i = 0; i < static_cast<int>(area.layers.size()); ++i) {
			if (std::to_string(area.layers[i].i) == on) {
				area.on = i;
			}
		}
		IG::Separator();
		IG::BeginChild("##paint");
		Theme::Body();
		IG::PushID(area.k.c_str());
		DrawLayer(area, area.layers[area.on]);
		IG::PopID();
		IG::EndChild();
	}

	void DrawBody()
	{
		if (!g_bodyReady) {
			return;
		}
		IG::TextDisabled("%s", !g_bodyPending.empty() ? fmt::format("Applying {}...", g_bodyPending).c_str() :
		                       g_bodyCurrent.empty()  ? "Current: none" :
		                                                fmt::format("Current: {}", g_bodyCurrent).c_str());
		IG::BeginChild("##obody");
		Theme::Body();
		for (int i = 0; i < static_cast<int>(g_bodyNames.size()); ++i) {
			const auto& name = g_bodyNames[i];
			IG::PushID(i);
			if (IG::Selectable(name.c_str(), name == g_bodyCurrent) && name != g_bodyCurrent && g_bodyPending.empty()) {
				g_bodyPending = name;
				++g_bodySets;
				spdlog::info("[smf] view: body-set '{}'", name);
				Send("body-set", { { "name", name } });
			}
			Rect(fmt::format("ob:{}", i));
			IG::PopID();
		}
		IG::EndChild();
	}

	bool BodyPresent()
	{
		return g_bodyReady;
	}

	void LogState(const char* a_what)
	{
		std::string areas;
		for (const auto& a : g_areas) {
			int painted = 0;
			for (const auto& l : a.layers) {
				painted += l.Painted() ? 1 : 0;
			}
			areas += fmt::format("{}{} {} layers ({} painted), catalog {}", areas.empty() ? "" : ", ", a.k, a.layers.size(),
				painted, a.catalog.size());
		}
		spdlog::info("[smf] view {} - tints ready {}, {} layers, on '{}' | paint ready {}, {} | on '{}' | OBody {}, {} "
					 "presets, current '{}', pending '{}' | tint-set {}, overlay-set {}, body-set {}",
			a_what, g_tintsReady, g_tints.size(), g_tintOn, g_overlaysReady, areas, g_areaOn, g_bodyReady ? "present" : "absent",
			g_bodyNames.size(), g_bodyCurrent, g_bodyPending, g_tintSets, g_overlaySets, g_bodySets);
	}
}
