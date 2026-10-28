// Presets.cpp
// Lifecast

#include "Panel/Smf/Presets.h"

#include "Panel/Smf/Common.h"
#include "Panel/Smf/DeadKeys.h"
#include "Panel/Smf/History.h"
#include "Panel/Smf/Theme.h"

#include "SKSEMenuFramework/SKSEMenuFramework.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <map>
#include <string>
#include <vector>

namespace Lifecast::Panel::Smf::Presets
{
	namespace
	{
		namespace IG = ImGuiMCP;
		using json = nlohmann::json;
		using Clock = std::chrono::steady_clock;
		using Common::Lower;
		using Common::Rect;
		using Common::Send;

		// A second press within this time confirms overwriting a preset that
		// already has the name (presets.js CONFIRM_MS).
		constexpr auto kConfirm = std::chrono::milliseconds(3000);

		// The longest preset name the DLL accepts.
		constexpr std::size_t kNameMax = 64;

		struct Item
		{
			std::string name;
			std::string lower;
			std::int64_t time = 0;
			std::uint64_t size = 0;
			std::string bad;  // empty: readable
		};

		// A line of a preset's description (presets.js detail()).
		struct Line
		{
			int         kind = 0;  // 0 plain, 1 dim, 2 bad
			std::string key;       // a part's type, before its label
			std::string text;
		};

		std::vector<Item>            g_items;
		std::map<std::string, json>  g_infoBy;  // name (lower case) -> the DLL's description
		bool                         g_listed = false;
		std::string                  g_opened;   // the row whose description is open (lower case)
		std::string                  g_loadingName;
		bool                         g_loading = false;
		std::string                  g_sortBy = "name";
		std::string                  g_selected;  // lower case
		Clock::time_point            g_confirmUntil{};
		std::string                  g_notice;
		char                         g_field[96] = {};

		bool        g_fallback = false;
		std::string g_fallbackWhat;  // "load" | "save"

		bool        g_guard = false;
		std::string g_guardTitle;
		std::string g_guardNote;

		std::string g_previewName;  // empty: no preview shown
		bool        g_previewShown = false;
		std::string g_previewTitle;
		std::string g_previewGain;
		std::string g_previewLoss;
		std::string g_previewChosen;
		std::string g_previewScope;
		std::string g_previewGainLabels;
		std::string g_previewLossLabels;

		bool g_portraits = false;

		std::uint32_t g_loads = 0;
		std::uint32_t g_saves = 0;

		// ------------------------------------------------------------ name

		const char* const kUnreadable = "The character's name could not be read, so this panel cannot finish. "
		                                "Use RaceMenu's Done button.";

		bool        g_newGame = false;
		int         g_readable = -1;  // -1: no "name" yet; 0: the DLL could not read it; 1: read
		json        g_session;        // the menu session the field's text belongs to
		bool        g_edited = false;
		bool        g_sent = false;
		char        g_name[96] = {};
		std::string g_note;
		int         g_noteKind = 0;  // 0 plain, 1 bad, 2 ok

		// ------------------------------------------------------------ helpers

		std::string Trim(std::string_view a_s)
		{
			const auto b = a_s.find_first_not_of(" \t");
			if (b == std::string_view::npos) {
				return {};
			}
			const auto e = a_s.find_last_not_of(" \t");
			return std::string(a_s.substr(b, e - b + 1));
		}

		bool Same(const std::string& a, const std::string& b)
		{
			return Lower(a) == Lower(b);
		}

		std::string DateText(std::int64_t a_seconds)
		{
			if (a_seconds <= 0) {
				return {};
			}
			const std::time_t t = static_cast<std::time_t>(a_seconds);
			std::tm           tm{};
			if (localtime_s(&tm, &t) != 0) {
				return {};
			}
			return fmt::format("{:04}-{:02}-{:02}", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
		}

		std::string SizeText(std::uint64_t a_bytes)
		{
			return a_bytes >= 1024 ? fmt::format("{:.1f} KB", static_cast<double>(a_bytes) / 1024.0) :
			                         fmt::format("{} bytes", a_bytes);
		}

		std::string Plural(long long a_n, const char* a_one, const char* a_many)
		{
			return fmt::format("{} {}", a_n, a_n == 1 ? a_one : a_many);
		}

		long long Count(const json& a_o, const char* a_key)
		{
			const auto it = a_o.find(a_key);
			return it != a_o.end() && it->is_number() ? it->get<long long>() : 0;
		}

		std::string Str(const json& a_o, const char* a_key, std::string a_fallback = {})
		{
			const auto it = a_o.find(a_key);
			if (it == a_o.end() || it->is_null()) {
				return a_fallback;
			}
			return it->is_string() ? it->get<std::string>() : it->dump();
		}

		// The test presets.js makes with "if (c.x)": false, 0, "" and null are
		// out, anything else is in.
		bool Truthy(const json& a_o, const char* a_key)
		{
			const auto it = a_o.find(a_key);
			if (it == a_o.end() || it->is_null()) {
				return false;
			}
			if (it->is_boolean()) {
				return it->get<bool>();
			}
			if (it->is_number()) {
				return it->get<double>() != 0.0;
			}
			if (it->is_string()) {
				return !it->get<std::string>().empty();
			}
			return true;
		}

		// A number as JavaScript prints it: a whole number without ".0".
		std::string JsNumber(const json& a_v)
		{
			if (a_v.is_number_float()) {
				const double d = a_v.get<double>();
				if (d == static_cast<double>(static_cast<long long>(d)) && d > -1e15 && d < 1e15) {
					return fmt::format("{}", static_cast<long long>(d));
				}
			}
			return a_v.dump();
		}

		// What a refused overwrite would have destroyed, in the player's words (presets.js LOSS_WORDS).
		std::string LossText(const json& a_loss)
		{
			std::string out;
			for (const auto& l : a_loss) {
				if (!l.is_object()) {
					continue;
				}
				const auto section = Str(l, "section");
				const auto n = Count(l, "count");
				std::string w;
				if (section == "sculpt") {
					w = "sculpt";
				} else if (section == "custom") {
					w = fmt::format("{} extended sliders", n);
				} else if (section == "bodyMorphs") {
					w = fmt::format("{} body morphs", n);
				} else if (section == "transforms") {
					w = fmt::format("{} node transforms", n);
				} else if (section == "overrides") {
					w = fmt::format("{} overlays", n);
				} else if (section == "skinOverrides") {
					w = fmt::format("{} skin overrides", n);
				} else {
					w = fmt::format("{} {}", n, section);
				}
				out += (out.empty() ? "" : ", ") + w;
			}
			return out;
		}

		// What an overwrite would gain or lose, entry by entry (presets.js ENTRY_WORDS), in their order.
		struct Words
		{
			const char* section;
			const char* one;
			const char* many;
		};
		constexpr Words kEntryWords[] = {
			{ "sculpt", "sculpted mesh", "sculpted meshes" },
			{ "custom", "extended slider", "extended sliders" },
			{ "bodyMorphs", "body morph value", "body morph values" },
			{ "transforms", "node transform value", "node transform values" },
			{ "overrides", "overlay value", "overlay values" },
			{ "skinOverrides", "skin override", "skin overrides" },
			{ "headParts", "head part", "head parts" },
			{ "tintInfo", "tint layer", "tint layers" },
			{ "faceTextures", "face texture", "face textures" },
		};

		std::string EntryText(const json& a_diff, const char* a_side)
		{
			std::string out;
			for (const auto& d : a_diff) {
				if (!d.is_object()) {
					continue;
				}
				const auto n = Count(d, a_side);
				if (n <= 0) {
					continue;
				}
				const auto section = Str(d, "section");
				std::string one = section + " entry";
				std::string many = section + " entries";
				for (const auto& w : kEntryWords) {
					if (section == w.section) {
						one = w.one;
						many = w.many;
					}
				}
				out += (out.empty() ? "" : ", ") + fmt::format("{} {}", n, n == 1 ? one : many);
			}
			return out;
		}

		std::string EntryLabels(const json& a_diff, const char* a_key)
		{
			std::string out;
			for (const auto& d : a_diff) {
				if (!d.is_object() || !d.contains(a_key) || !d[a_key].is_array() || d[a_key].empty()) {
					continue;
				}
				std::string labels;
				for (const auto& l : d[a_key]) {
					labels += (labels.empty() ? "" : "; ") + (l.is_string() ? l.get<std::string>() : l.dump());
				}
				out += (out.empty() ? "" : "\n") + Str(d, "section") + ": " + labels;
			}
			return out;
		}

		void HidePreview(bool a_cancel)
		{
			if (a_cancel && !g_previewName.empty()) {
				Send("preset-save-cancel", { { "name", g_previewName } });
			}
			g_previewName.clear();
			g_previewShown = false;
		}

		bool Exists(const std::string& a_name)
		{
			return std::any_of(g_items.begin(), g_items.end(), [&](const Item& it) { return Same(it.name, a_name); });
		}

		// The first "<name> 2", "<name> 3"... no preset uses, within the length the DLL accepts.
		std::string FreeName(const std::string& a_name)
		{
			for (int i = 2; i < 1000; ++i) {
				const auto suffix = fmt::format(" {}", i);
				auto       base = a_name.substr(0, kNameMax > suffix.size() ? kNameMax - suffix.size() : 0);
				while (!base.empty() && base.back() == ' ') {
					base.pop_back();
				}
				const auto candidate = base + suffix;
				if (!Exists(candidate)) {
					return candidate;
				}
			}
			return a_name;
		}

		void SetField(const std::string& a_text)
		{
			const auto n = (std::min)(a_text.size(), sizeof(g_field) - 1);
			std::copy_n(a_text.data(), n, g_field);
			g_field[n] = 0;
		}

		void ShowFallback(const std::string& a_what)
		{
			g_fallback = true;
			g_fallbackWhat = a_what;
		}

		// What the preset holds, under its row: only what the DLL said (presets.js detail()).
		std::vector<Line> Detail(const Item& a_it)
		{
			std::vector<Line> out;
			if (!a_it.bad.empty()) {
				out.push_back({ 2, "", "Unreadable: " + a_it.bad });
			}
			const auto found = g_infoBy.find(a_it.lower);
			if (found != g_infoBy.end()) {
				const auto& info = found->second;
				static constexpr const char* kOrder[] = { "Hair", "Beard", "Eyes", "Brows", "Scar" };
				auto at = [](const std::string& a_type) {
					for (int i = 0; i < 5; ++i) {
						if (a_type == kOrder[i]) {
							return i;
						}
					}
					return 5;
				};
				std::vector<json> parts;
				if (info.contains("parts") && info["parts"].is_array()) {
					parts.assign(info["parts"].begin(), info["parts"].end());
				}
				std::stable_sort(parts.begin(), parts.end(),
					[&](const json& a, const json& b) { return at(Str(a, "type")) < at(Str(b, "type")); });
				for (const auto& p : parts) {
					if (!p.value("missing", false)) {
						out.push_back({ 0, Str(p, "type"), Str(p, "label") });
					}
				}
				for (const auto& p : parts) {
					if (p.value("missing", false)) {
						out.push_back({ 2, "", "Not found in this game: " + Str(p, "label") });
					}
				}
				const json c = info.contains("counts") && info["counts"].is_object() ? info["counts"] : json::object();
				std::vector<std::string> held;
				if (info.contains("weight") && info["weight"].is_number()) {
					held.push_back("weight " + JsNumber(info["weight"]));
				}
				if (const auto n = Count(c, "sliders")) {
					held.push_back(Plural(n, "slider", "sliders"));
				}
				if (const auto n = Count(c, "tints")) {
					held.push_back(Plural(n, "tint layer", "tint layers"));
				}
				if (const auto n = Count(c, "overlays")) {
					held.push_back(Plural(n, "overlay", "overlays"));
				}
				if (const auto n = Count(c, "bodyMorphs")) {
					held.push_back(Plural(n, "body morph", "body morphs"));
				}
				if (Truthy(c, "sculpt")) {
					held.push_back("sculpt");
				}
				if (Truthy(c, "transforms")) {
					held.push_back("node transforms");
				}
				if (!held.empty()) {
					std::string s;
					for (const auto& h : held) {
						s += (s.empty() ? "" : " \xC2\xB7 ") + h;
					}
					out.push_back({ 1, "", s });
				}
				std::vector<json> plugins;
				if (info.contains("plugins") && info["plugins"].is_array()) {
					plugins.assign(info["plugins"].begin(), info["plugins"].end());
				}
				if (!plugins.empty()) {
					std::string present;
					long long   missing = 0;
					for (const auto& x : plugins) {
						if (x.value("missing", false)) {
							++missing;
						} else {
							present += (present.empty() ? "" : ", ") + Str(x, "name");
						}
					}
					const auto total = static_cast<long long>(plugins.size());
					std::string tail;
					if (missing) {
						tail = (total > missing ? ", and " : "") + Plural(missing, "that is", "that are") + " not active:";
					}
					out.push_back({ 1, "", "Needs " + Plural(total, "plugin", "plugins") + ": " + present + tail });
					for (const auto& x : plugins) {
						if (x.value("missing", false)) {
							out.push_back({ 2, "", "Not active: " + Str(x, "name") });
						}
					}
				}
			}
			out.push_back({ 1, "", (a_it.time ? "Saved " + DateText(a_it.time) + ", " : std::string()) + SizeText(a_it.size) });
			return out;
		}

		void LogDetail(const Item& a_it)
		{
			std::string s;
			for (const auto& l : Detail(a_it)) {
				s += (s.empty() ? "" : " | ") + (l.key.empty() ? l.text : l.key + ": " + l.text);
			}
			spdlog::info("[smf] view: preset info '{}' ({} description): {}", a_it.name,
				g_infoBy.count(a_it.lower) ? "with" : "no", s);
		}

		void LoadPreset(const std::string& a_name)
		{
			if (g_loading) {
				return;
			}
			g_loading = true;
			g_loadingName = a_name;
			g_selected = Lower(a_name);
			g_guard = false;
			g_fallback = false;
			g_notice = "loading " + a_name + "...";
			++g_loads;
			spdlog::info("[smf] view: preset-load '{}'", a_name);
			Send("preset-load", { { "name", a_name } });
		}

		void SetSort(const char* a_by)
		{
			if (g_sortBy == a_by) {
				return;
			}
			g_sortBy = a_by;
			spdlog::info("[smf] view: presets sort {}", a_by);
			Send("settings-set", { { "key", "presetsSort" }, { "value", a_by } });
			Common::LayoutSoon();
		}

		void Save()
		{
			const auto name = Trim(g_field);
			if (name.empty()) {
				return;
			}
			const bool exists = Exists(name);
			if (exists && Clock::now() >= g_confirmUntil) {
				g_confirmUntil = Clock::now() + kConfirm;
				spdlog::info("[smf] view: preset save '{}' - exists, press again to overwrite", name);
				return;
			}
			g_confirmUntil = {};
			g_guard = false;
			HidePreview(false);
			g_fallback = false;
			g_notice = "saving " + name + "...";
			++g_saves;
			spdlog::info("[smf] view: preset-save '{}' overwrite {}", name, exists);
			Send("preset-save", { { "name", name }, { "overwrite", exists } });
		}

		// A box the player reads or answers (.delegate): a title, notes, buttons.
		void BoxBegin(const char* a_id)
		{
			IG::PushStyleColor(IG::ImGuiCol_ChildBg, Theme::Color::RowSelected());
			IG::BeginChild(a_id, IG::ImVec2{ 0.0f, 0.0f }, IG::ImGuiChildFlags_Border | IG::ImGuiChildFlags_AutoResizeY);
			Theme::Body();
			IG::PushTextWrapPos(0.0f);
		}

		void BoxEnd()
		{
			IG::PopTextWrapPos();
			IG::EndChild();
			IG::PopStyleColor();
		}

		void DrawBoxes()
		{
			if (g_fallback) {
				BoxBegin("##preset-fallback");
				IG::TextUnformatted(g_fallbackWhat == "load" ? "Load in RaceMenu" : "Save in RaceMenu");
				IG::TextDisabled("%s", g_fallbackWhat == "load" ?
				                           "RaceMenu did not load from here. Its own Presets page loads the same files." :
				                           "RaceMenu did not save from here. Its own Presets page saves the same files.");
				if (IG::Button("Open##preset-fallback")) {
					Send("show-racemenu", { { "for", "presets" } });
				}
				Rect("pr:fallback-open");
				IG::SetItemTooltip("Opens RaceMenu (F4 comes back)");
				BoxEnd();
			}
			if (g_guard) {
				BoxBegin("##preset-guard");
				IG::TextUnformatted(g_guardTitle.c_str());
				IG::TextDisabled("%s", g_guardNote.c_str());
				if (IG::Button("New name##preset-guard")) {
					SetField(FreeName(Trim(g_field)));
					g_guard = false;
					g_confirmUntil = {};
					g_notice.clear();
					spdlog::info("[smf] view: preset guard - new name '{}'", g_field);
				}
				Rect("pr:guard-rename");
				IG::SetItemTooltip("Keeps the existing preset and saves under a free name");
				IG::SameLine();
				if (IG::Button("RaceMenu##preset-guard")) {
					Send("show-racemenu", { { "for", "presets" } });
				}
				Rect("pr:guard-racemenu");
				IG::SetItemTooltip("Opens RaceMenu, whose own save replaces the file (F4 comes back)");
				BoxEnd();
			}
			if (g_previewShown) {
				BoxBegin("##preset-preview");
				IG::TextUnformatted(g_previewTitle.c_str());
				IG::TextDisabled("%s", g_previewGain.c_str());
				if (!g_previewGainLabels.empty()) {
					IG::SetItemTooltip("%s", g_previewGainLabels.c_str());
				}
				IG::TextDisabled("%s", g_previewLoss.c_str());
				if (!g_previewLossLabels.empty()) {
					IG::SetItemTooltip("%s", g_previewLossLabels.c_str());
				}
				if (!g_previewChosen.empty()) {
					IG::TextDisabled("%s", g_previewChosen.c_str());
				}
				IG::TextDisabled("%s", g_previewScope.c_str());
				if (IG::Button("Save anyway##preset-preview") && !g_previewName.empty()) {
					const auto name = g_previewName;
					HidePreview(false);
					g_notice = "saving " + name + "...";
					++g_saves;
					spdlog::info("[smf] view: preset-save '{}' confirmed (save anyway)", name);
					Send("preset-save", { { "name", name }, { "overwrite", true }, { "confirm", true } });
				}
				Rect("pr:save-anyway");
				IG::SetItemTooltip("Writes the preset exactly as compared here");
				IG::SameLine();
				if (IG::Button("Cancel##preset-preview")) {
					HidePreview(true);
					g_notice = "not saved";
					spdlog::info("[smf] view: preset preview cancelled");
				}
				Rect("pr:save-cancel");
				IG::SetItemTooltip("Keeps the existing preset as it is");
				BoxEnd();
			}
		}

		void Say(std::string a_text, int a_kind = 0)
		{
			g_note = std::move(a_text);
			g_noteKind = a_kind;
		}

		void Finish()
		{
			if (g_sent) {
				return;
			}
			if (g_readable != 1) {
				Say(kUnreadable, 1);
				return;
			}
			const auto name = Trim(g_name);
			if (name.empty()) {
				Say("The character needs a name.", 1);
				return;
			}
			g_sent = true;
			Say(g_newGame ? "Finishing..." : "Closing...");
			spdlog::info("[smf] view: finish '{}' (new game {})", name, g_newGame);
			Send("finish", { { "name", name } });
		}

		void OnName(const json& a_data)
		{
			const json incoming = a_data.contains("session") ? a_data["session"] : json();
			g_newGame = a_data.value("newGame", false);
			g_readable = a_data.value("readable", false) == true ? 1 : 0;
			if (incoming != g_session) {
				g_session = incoming;
				g_edited = false;
				g_sent = false;
				Say("");
			}
			if (!g_edited && !g_sent) {
				const auto name = g_readable == 1 && a_data.contains("name") && a_data["name"].is_string() ?
				                      a_data["name"].get<std::string>() :
				                      std::string();
				const auto n = (std::min)(name.size(), sizeof(g_name) - 1);
				std::copy_n(name.data(), n, g_name);
				g_name[n] = 0;
			}
			if (g_readable != 1) {
				Say(kUnreadable, 1);
			} else if (g_note == kUnreadable) {
				Say("");
			}
			spdlog::info("[smf] view: name '{}' readable {} new game {} session {}", g_name, g_readable == 1, g_newGame,
				g_session.dump());
		}
	}

	bool Receive(const std::string& a_type, const json& a_data)
	{
		if (a_type == "presets") {
			g_items.clear();
			if (a_data.contains("items") && a_data["items"].is_array()) {
				for (const auto& it : a_data["items"]) {
					Item item;
					item.name = Str(it, "name");
					item.lower = Lower(item.name);
					item.time = it.contains("time") && it["time"].is_number() ? it["time"].get<std::int64_t>() : 0;
					item.size = it.contains("size") && it["size"].is_number() ? it["size"].get<std::uint64_t>() : 0;
					g_items.push_back(std::move(item));
				}
			}
			g_listed = true;
			spdlog::info("[smf] view: presets {} in the list", g_items.size());
			Common::LayoutSoon();
		} else if (a_type == "presets-checked") {
			g_infoBy.clear();
			if (a_data.contains("info") && a_data["info"].is_array()) {
				for (const auto& i : a_data["info"]) {
					if (i.is_object() && i.contains("name") && i["name"].is_string()) {
						g_infoBy[Lower(i["name"].get<std::string>())] = i;
					}
				}
			}
			std::size_t bad = 0;
			if (a_data.contains("bad") && a_data["bad"].is_array()) {
				for (const auto& b : a_data["bad"]) {
					const auto name = Str(b, "name");
					for (auto& it : g_items) {
						if (it.name == name) {
							it.bad = Str(b, "why", "unreadable");
							++bad;
						}
					}
				}
			}
			spdlog::info("[smf] view: presets checked - {} described, {} unreadable", g_infoBy.size(), bad);
		} else if (a_type == "preset-save-preview") {
			const json diff = a_data.contains("diff") && a_data["diff"].is_array() ? a_data["diff"] : json::array();
			const auto gain = EntryText(diff, "gained");
			const auto loss = EntryText(diff, "lost");
			const auto chosen = EntryText(diff, "chosen");
			g_previewName = Str(a_data, "name");
			g_previewTitle = "Save over " + g_previewName + "?";
			g_previewGain = "It would GAIN: " + (gain.empty() ? std::string("nothing") : gain) + ".";
			g_previewLoss = "It would LOSE: " + (loss.empty() ? std::string("nothing") : loss) + ".";
			g_previewChosen = chosen.empty() ? std::string() : "Chosen in this panel, not counted above: " + chosen + ".";
			g_previewGainLabels = EntryLabels(diff, "gainedLabels");
			g_previewLossLabels = EntryLabels(diff, "lostLabels");
			std::string scope;
			for (const auto& w : kEntryWords) {
				scope += (scope.empty() ? "" : ", ") + std::string(w.many);
			}
			g_previewScope = "Compared: " + scope + ". A value that only changed is an edit, and is not listed.";
			g_previewShown = true;
			g_notice = "not saved yet - see what it would change";
			spdlog::info("[smf] view: preset preview '{}': {} | {} | {}", g_previewName, g_previewGain, g_previewLoss,
				g_previewChosen.empty() ? "nothing chosen here" : g_previewChosen);
			Common::LayoutSoon();
		} else if (a_type == "preset-save-preview-dropped") {
			const bool showing = !g_previewName.empty();
			HidePreview(false);
			if (showing) {
				g_notice = "not saved: " + Str(a_data, "why", "the preview ended") + " - save again to see what it would change";
			}
			spdlog::info("[smf] view: preset preview dropped ({}): {}", showing ? "a box was showing" : "no box",
				Str(a_data, "why"));
		} else if (a_type == "preset-saved") {
			const auto name = Str(a_data, "name");
			if (!g_previewName.empty() && Same(name, g_previewName)) {
				HidePreview(false);
			}
			if (a_data.value("ok", false)) {
				g_selected = Lower(name);
				g_notice = "saved " + name;
			} else if (a_data.contains("loss") && a_data["loss"].is_array()) {
				g_notice = "not saved";
				g_guardTitle = "Keep " + name + "?";
				g_guardNote = !a_data["loss"].empty() ?
				                  "Saving over it would lose what was not loaded here: " + LossText(a_data["loss"]) + "." :
				                  "Saving over it is not safe: " + Str(a_data, "why", "unknown reason") + ".";
				g_guard = true;
			} else {
				g_notice = "not saved: " + Str(a_data, "why", "unknown reason");
				if (a_data.value("fallback", false)) {
					ShowFallback("save");
				}
			}
			spdlog::info("[smf] view: preset saved '{}': {}{}", name, g_notice, g_guard ? " | " + g_guardNote : std::string());
			Common::LayoutSoon();
		} else if (a_type == "preset-loaded") {
			g_loading = false;
			g_loadingName.clear();
			const auto name = Str(a_data, "name");
			if (a_data.value("ok", false)) {
				g_selected = Lower(name);
				g_notice = "loaded " + name +
				           (a_data.value("late", false) ? fmt::format(" (late, after {} ms)", Str(a_data, "ms")) : std::string());
				g_fallback = false;
				// A preset replaces the whole character at once (presets.js).
				History::Clear("a preset was loaded");
			} else {
				g_notice = "not loaded: " + Str(a_data, "why", "unknown reason");
				if (a_data.value("fallback", false)) {
					ShowFallback("load");
				}
			}
			spdlog::info("[smf] view: preset loaded '{}': {}", name, g_notice);
		} else if (a_type == "preset-loading") {
			if (!g_loading || g_loadingName.empty() || !Same(g_loadingName, Str(a_data, "name"))) {
				return true;
			}
			if (a_data.value("silent", false)) {
				g_loading = false;
				const auto ms = a_data.contains("ms") && a_data["ms"].is_number() ? a_data["ms"].get<double>() : 0.0;
				g_notice = fmt::format("RaceMenu has not answered after {:.0f} s - {} may still apply; another preset can be loaded",
					ms / 1000.0, g_loadingName);
				g_loadingName.clear();
			} else {
				g_notice = "applying " + Str(a_data, "name") + " - the game is busy, RaceMenu will answer when it can";
			}
		} else if (a_type == "portraits") {
			g_portraits = a_data.value("available", false) == true;
			spdlog::info("[smf] view: RaceMenu Portraits tab {}", g_portraits ? "shown" : "not shown");
		} else if (a_type == "menu-closed") {
			g_guard = false;
			g_fallback = false;
			HidePreview(false);
			g_confirmUntil = {};
			g_notice.clear();
		} else if (a_type == "name") {
			OnName(a_data);
		} else if (a_type == "finish-result") {
			if (a_data.value("ok", false)) {
				Say(g_newGame ? "Finished." : "Closed.", 2);
			} else {
				g_sent = false;
				Say("Not finished: " + Str(a_data, "why", "unknown reason") + ".", 1);
			}
			spdlog::info("[smf] view: finish-result ok {} - {}", a_data.value("ok", false), g_note);
		} else {
			return false;
		}
		return true;
	}

	void OnSettings(const json& a_data)
	{
		const auto saved = Str(a_data, "presetsSort");
		if ((saved == "name" || saved == "time") && saved != g_sortBy) {
			g_sortBy = saved;
			spdlog::info("[smf] view: presets sort {} (from the settings)", saved);
		}
	}

	void Ask()
	{
		Send("presets-get");
		Send("portraits-get");
		Send("name-get");
	}

	void DrawSection()
	{
		if (g_portraits) {
			Common::Chip("Presets###pr-tab-presets", true);
			Rect("pr:tab-presets");
			IG::SameLine(0.0f, Theme::Px(4.0f));
			if (Common::Chip("RaceMenu Portraits###pr-tab-portraits", false)) {
				spdlog::info("[smf] view: show-portraits");
				Send("show-portraits");
			}
			Rect("pr:tab-portraits");
			IG::SetItemTooltip("Opens RaceMenu Portraits' own window over the scene (F4 comes back)");
		}
		IG::SetNextItemWidth(-1.0f);
		if (IG::InputTextWithHint("##preset-search", "Search, or a name to save as", g_field, sizeof(g_field),
				IG::ImGuiInputTextFlags_CallbackCharFilter, &DeadKeys::Filter)) {
			g_confirmUntil = {};
			g_notice.clear();
			g_guard = false;
			HidePreview(true);
			spdlog::info("[smf] view: presets search '{}'", g_field);
			Common::LayoutSoon();
		}
		Rect("pr:search");

		if (Common::Chip("A-Z###pr-az", g_sortBy == "name")) {
			SetSort("name");
		}
		Rect("pr:az");
		IG::SameLine(0.0f, Theme::Px(4.0f));
		if (Common::Chip("Newest###pr-new", g_sortBy == "time")) {
			SetSort("time");
		}
		Rect("pr:new");
		IG::SameLine(0.0f, Theme::Px(4.0f));
		const bool empty = Trim(g_field).empty();
		IG::BeginDisabled(empty);
		const bool confirming = Clock::now() < g_confirmUntil;
		// .preset-bar .preset-save: margin-left auto - Save at the right end of the row
		const float saveW = IG::CalcTextSize(confirming ? "Overwrite?" : "Save").x + IG::GetStyle()->FramePadding.x * 2.0f;
		IG::SetCursorPosX(IG::GetCursorPosX() + (std::max)(IG::GetContentRegionAvail().x - saveW, 0.0f));
		if (IG::Button(confirming ? "Overwrite?###pr-save" : "Save###pr-save")) {
			Save();
		}
		Rect("pr:save");
		IG::EndDisabled();

		// The list, searched and sorted here.
		const auto        query = Lower(Trim(g_field));
		std::vector<int>  shown;
		for (int i = 0; i < static_cast<int>(g_items.size()); ++i) {
			if (query.empty() || g_items[i].lower.find(query) != std::string::npos) {
				shown.push_back(i);
			}
		}
		if (g_sortBy == "time") {
			std::stable_sort(shown.begin(), shown.end(), [](int a, int b) {
				const auto& x = g_items[a];
				const auto& y = g_items[b];
				return x.time != y.time ? x.time > y.time : x.lower < y.lower;
			});
		} else {
			std::stable_sort(shown.begin(), shown.end(), [](int a, int b) { return g_items[a].lower < g_items[b].lower; });
		}
		// What the list shows, once per change of the search, the order or the list.
		static std::string shownFor = "\x01";
		if (const auto key = fmt::format("{}|{}|{}", query, g_sortBy, g_items.size()); key != shownFor) {
			shownFor = key;
			std::string first;
			for (std::size_t n = 0; n < shown.size() && n < 5; ++n) {
				first += (first.empty() ? "" : " | ") + g_items[shown[n]].name;
			}
			spdlog::info("[smf] view: presets shown {} of {} for '{}', sort {}: {}", shown.size(), g_items.size(), query,
				g_sortBy, first);
		}
		std::size_t bad = 0;
		for (const auto& it : g_items) {
			bad += it.bad.empty() ? 0 : 1;
		}
		std::string status = !g_listed ? std::string("Reading RaceMenu's preset folder...") :
		                     g_items.empty() ?
		                                     std::string("No presets in RaceMenu's preset folder.") :
		                                     fmt::format("{} of {} presets{}", shown.size(), g_items.size(),
											   bad ? fmt::format(", {} unreadable", bad) : std::string());
		if (!g_notice.empty()) {
			status += " - " + g_notice;
		}
		IG::PushTextWrapPos(0.0f);
		IG::TextDisabled("%s", status.c_str());
		IG::PopTextWrapPos();
		Rect("pr:status");

		DrawBoxes();

		// The list is as tall as its rows, up to what the panel has left
		// (panel.css .preset-list, alone in its region: max-height 46vh) - one
		// row is one row high, not the whole panel.
		const float listMax = (std::min)(IG::GetContentRegionAvail().y, IG::GetIO()->DisplaySize.y * 0.46f);
		IG::SetNextWindowSizeConstraints(IG::ImVec2{ 0.0f, 0.0f }, IG::ImVec2{ 1.0e6f, (std::max)(listMax, IG::GetFrameHeight()) });
		IG::BeginChild("##preset-list", IG::ImVec2{ 0.0f, 0.0f }, IG::ImGuiChildFlags_Border | IG::ImGuiChildFlags_AutoResizeY);
		Theme::Body();
		const float buttons = IG::CalcTextSize("i").x + IG::CalcTextSize("Loading").x + IG::GetStyle()->FramePadding.x * 4.0f +
		                      IG::GetStyle()->ItemSpacing.x * 2.0f;
		for (const int i : shown) {
			const auto& it = g_items[i];
			IG::PushID(it.lower.c_str());
			const bool isOpen = !g_opened.empty() && g_opened == it.lower;
			const bool isSel = !g_selected.empty() && g_selected == it.lower;
			const float rowStart = IG::GetCursorScreenPos().x;
			const float avail = IG::GetContentRegionAvail().x;
			const auto  date = it.bad.empty() ? DateText(it.time) : std::string("unreadable");
			const auto  label = fmt::format("{}##row", it.name);
			if (!it.bad.empty()) {
				IG::PushStyleColor(IG::ImGuiCol_Text, Theme::Color::Bad());
			}
			if (IG::Selectable(label.c_str(), isSel, 0, IG::ImVec2{ (std::max)(avail - buttons, 10.0f), 0.0f })) {
				g_selected = it.lower;
			}
			if (!it.bad.empty()) {
				IG::PopStyleColor();
				IG::SetItemTooltip("%s", it.bad.c_str());
			}
			Rect("pn:" + it.name);
			// the date, right-aligned inside the name's row
			const float dw = IG::CalcTextSize(date.c_str()).x;
			IG::SameLine(rowStart - IG::GetWindowPos().x + (std::max)(avail - buttons - dw - Theme::Px(6.0f), 0.0f));
			IG::TextDisabled("%s", date.c_str());
			IG::SameLine(rowStart - IG::GetWindowPos().x + (std::max)(avail - buttons, 10.0f) + IG::GetStyle()->ItemSpacing.x);
			if (Common::Chip("i##more", isOpen)) {
				g_opened = isOpen ? std::string() : it.lower;
				if (!isOpen) {
					LogDetail(it);
				}
				Common::LayoutSoon();
			}
			Rect("pi:" + it.name);
			IG::SetItemTooltip("What this preset holds");
			IG::SameLine();
			const bool loadingThis = g_loading && !g_loadingName.empty() && Same(g_loadingName, it.name);
			IG::BeginDisabled(g_loading || !it.bad.empty());
			if (IG::Button(loadingThis ? "Loading##load" : "Load##load")) {
				LoadPreset(it.name);
			}
			Rect("pl:" + it.name);
			IG::EndDisabled();
			if (isOpen) {
				IG::Indent(Theme::Px(10.0f));
				IG::PushTextWrapPos(0.0f);
				for (const auto& l : Detail(it)) {
					if (!l.key.empty()) {
						IG::TextDisabled("%s", l.key.c_str());
						IG::SameLine();
						IG::TextUnformatted(l.text.c_str());
					} else if (l.kind == 2) {
						IG::TextColored(Theme::Color::Bad(), "%s", l.text.c_str());
					} else if (l.kind == 1) {
						IG::TextDisabled("%s", l.text.c_str());
					} else {
						IG::TextUnformatted(l.text.c_str());
					}
				}
				IG::PopTextWrapPos();
				IG::Unindent(Theme::Px(10.0f));
				Rect("pd:" + it.name);
			}
			IG::PopID();
		}
		IG::EndChild();
		Rect("pr:list");
	}

	void DrawFinish(float a_width)
	{
		const float button = IG::CalcTextSize("Finish").x + IG::GetStyle()->FramePadding.x * 2.0f;
		const float field = (std::min)(Theme::Px(200.0f), (std::max)(a_width - button - Theme::Px(8.0f), Theme::Px(80.0f)));
		IG::BeginDisabled(g_sent || g_readable == 0);
		IG::SetNextItemWidth(field);
		if (IG::InputTextWithHint("##finish-name", "Name", g_name, sizeof(g_name), IG::ImGuiInputTextFlags_CallbackCharFilter,
				&DeadKeys::Filter)) {
			g_edited = true;
			if (!g_sent) {
				Say("");
			}
		}
		Rect("finish-name");
		if (IG::IsItemDeactivatedAfterEdit()) {
			spdlog::info("[smf] view: name field '{}'", g_name);
		}
		IG::SameLine();
		IG::PushStyleColor(IG::ImGuiCol_Text, Theme::Color::Accent());
		if (IG::Button("Finish##finish")) {
			Finish();
		}
		IG::PopStyleColor();
		Rect("finish");
		IG::EndDisabled();
		if (!g_note.empty()) {
			IG::SameLine();
			const auto color = g_noteKind == 1 ? Theme::Color::Bad() :
			                   g_noteKind == 2 ? Theme::Color::Accent() :
			                                     Theme::Color::TextDim();
			// The belt is one row: a note longer than the room left is cut, and
			// shown whole over the mouse.
			const float room = a_width - field - button - IG::GetStyle()->ItemSpacing.x * 2.0f;
			std::string shown = g_note;
			if (IG::CalcTextSize(shown.c_str()).x > room) {
				while (!shown.empty() && IG::CalcTextSize((shown + "...").c_str()).x > room) {
					shown.pop_back();
					while (!shown.empty() && (static_cast<unsigned char>(shown.back()) & 0xC0) == 0x80) {
						shown.pop_back();
					}
					if (!shown.empty() && static_cast<unsigned char>(shown.back()) >= 0xC0) {
						shown.pop_back();
					}
				}
				shown += "...";
			}
			IG::TextColored(color, "%s", shown.c_str());
			Rect("finish-note");
			IG::SetItemTooltip("%s", g_note.c_str());
		}
	}

	void LogState(const char* a_what)
	{
		spdlog::info("[smf] view {}: presets {} (listed {}, described {}), sort {}, loading {} '{}', loads {}, saves {}, guard {}, "
					 "preview '{}', fallback {}, portraits {} | name '{}' readable {} sent {} note '{}'",
			a_what, g_items.size(), g_listed, g_infoBy.size(), g_sortBy, g_loading, g_loadingName, g_loads, g_saves, g_guard,
			g_previewName, g_fallback ? g_fallbackWhat : std::string("no"), g_portraits, g_name, g_readable, g_sent, g_note);
	}
}
