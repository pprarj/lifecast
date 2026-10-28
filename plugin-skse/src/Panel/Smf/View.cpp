// View.cpp
// Lifecast

#include "Panel/Smf/View.h"

#include "Panel/Smf/Common.h"
#include "Panel/Smf/DeadKeys.h"
#include "Panel/Smf/Face.h"
#include "Panel/Smf/Theme.h"

#include "Panel/Bridge.h"
#include "Version.h"

#include "SKSEMenuFramework/SKSEMenuFramework.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace Lifecast::Panel::Smf
{
	namespace
	{
		namespace IG = ImGuiMCP;
		using json = nlohmann::json;
		using Clock = std::chrono::steady_clock;

		// The stage's numbers, the same as stage.js's: degrees per pixel of a
		// drag, degrees per button press, units per button press, and how
		// often a drag is sent.
		constexpr float kTurnPerPx = 0.4f;
		constexpr float kTurnStep = 15.0f;
		constexpr float kDistanceStep = 10.0f;
		constexpr auto  kSendEvery = std::chrono::milliseconds(50);

		struct Slider
		{
			std::string k;
			std::string label;
			std::string lower;  // the label in lower case, for the search
			std::string tab;
			float       min = 0.0f;
			float       max = 1.0f;
			float       step = 0.01f;
			bool        has = false;
			float       v = 0.0f;
			bool        active = false;
			char        typed[24] = {};
		};

		struct Tab
		{
			std::string              id;
			std::string              label;
			std::string              key;
			std::uint32_t            flag = 0;
			int                      order = 1 << 20;
			std::vector<std::size_t> rows;
		};

		struct StageState
		{
			bool        ready = false;
			bool        free = false;
			double      turn = 0.0;
			double      distance = 0.0;
			std::string why;
		};

		// The model. Written by Receive, read by Draw; both on the game thread
		// (measured), and held under the lock all the same.
		std::mutex                                g_mx;
		bool                                      g_helloed = false;
		std::string                               g_dll = "?";
		bool                                      g_compatible = true;
		bool                                      g_visible = false;
		bool                                      g_card = false;
		std::string                               g_cardKey = "F4";
		std::atomic_bool                          g_hybrid = true;  // the default form (D289)
		int                                       g_section = 0;
		bool                                      g_slidersReady = false;
		std::vector<Slider>                       g_sliders;
		std::unordered_map<std::string, std::size_t> g_byKey;
		std::vector<Tab>                          g_tabs;
		std::string                               g_signature;
		bool                                      g_needDrawn = false;
		json                                      g_categories = json::array();
		StageState                                g_stage;
		std::uint32_t                             g_received = 0;
		std::uint32_t                             g_ignored = 0;

		// What the view sends, until a task hands it to the bridge.
		std::mutex               g_outMx;
		std::vector<std::string> g_out;
		std::uint32_t            g_sent = 0;

		// Render-only.
		char              g_search[128] = {};
		bool              g_dragging = false;
		float             g_dragAcc = 0.0f;
		Clock::time_point g_dragSent{};
		Clock::time_point g_sliderSent{};
		// RaceMenu's zoom is a toggle the DLL asks for (Stage.cpp OnZoom) and
		// cannot read back, so the view keeps no guess of it: one neutral label,
		// and the wheel toggles whatever its direction (D290).
		std::uint32_t     g_zooms = 0;
		Clock::time_point g_wheelZoomAt{};
		bool              g_scaleHeard = false;
		float             g_frameLeft = -1.0f;
		float             g_frameRight = -1.0f;
		std::string       g_tab = "\x01";  // the slider category shown (a tab id; "\x01" until the first)

		// Where the widgets were last frame, screen pixels, for the state
		// line: a test bench clicks by it.
		std::vector<std::string> g_layout;
		std::string              g_lastLayout;
		int                      g_layoutSoon = 0;  // frames left before the layout is logged
		float                    g_clipBottom = 0.0f;

		using Common::Lower;
		using Common::Rect;
		using Common::Send;

		std::string Hex(const char* a_s)
		{
			std::string out;
			for (const unsigned char* p = reinterpret_cast<const unsigned char*>(a_s); *p; ++p) {
				out += fmt::format("{}{:02X}", out.empty() ? "" : " ", *p);
			}
			return out;
		}

		// The tabs in the order RaceMenu lists its categories; the sliders
		// with no tab first, and a tab RaceMenu does not list last.
		void ApplyOrder()
		{
			for (auto& t : g_tabs) {
				if (t.id.empty()) {
					t.order = -1;
					continue;
				}
				t.order = 1 << 20;
				for (const auto& e : g_categories) {
					if (!e.is_object()) {
						continue;
					}
					const auto key = e.value("key", std::string());
					const auto flag = e.value("flag", 0u);
					const auto index = e.value("index", 0);
					if ((!t.key.empty() && key == t.key) || (t.key.empty() && flag != 0 && flag == t.flag)) {
						t.order = index;
						break;
					}
				}
			}
			std::stable_sort(g_tabs.begin(), g_tabs.end(), [](const Tab& a, const Tab& b) { return a.order < b.order; });
		}

		float Num(const json& a_o, const char* a_key, float a_fallback)
		{
			const auto it = a_o.find(a_key);
			return it != a_o.end() && it->is_number() ? it->get<float>() : a_fallback;
		}

		void SetValue(Slider& a_s, const json& a_v)
		{
			if (a_s.active) {
				return;
			}
			a_s.has = a_v.is_number();
			if (a_s.has) {
				a_s.v = a_v.get<float>();
			}
		}

		void OnSliders(const json& a_data)
		{
			if (!a_data.value("ready", false) || !a_data.contains("tabs") || !a_data["tabs"].is_array() ||
				!a_data.contains("sliders") || !a_data["sliders"].is_array()) {
				return;
			}
			const auto& tabs = a_data["tabs"];
			const auto& sliders = a_data["sliders"];

			std::string sig;
			for (const auto& t : tabs) {
				sig += t.value("id", std::string()) + "\x1f" + t.value("label", std::string()) + "\x1e";
			}
			for (const auto& s : sliders) {
				sig += fmt::format("{}\x1f{}\x1f{}\x1f{}\x1f{}\x1f{}\x1e", s.value("k", std::string()),
					s.value("label", std::string()), s.value("tab", std::string()), Num(s, "min", 0.0f), Num(s, "max", 1.0f),
					Num(s, "step", 0.01f));
			}
			g_slidersReady = true;
			if (sig == g_signature) {
				for (const auto& s : sliders) {
					const auto it = g_byKey.find(s.value("k", std::string()));
					if (it != g_byKey.end() && s.contains("value")) {
						SetValue(g_sliders[it->second], s["value"]);
					}
				}
				return;
			}
			g_signature = std::move(sig);

			g_tabs.clear();
			g_tabs.push_back({ "", "General", "", 0, -1, {} });
			for (const auto& t : tabs) {
				Tab tab;
				tab.id = t.value("id", std::string());
				tab.label = t.value("label", tab.id);
				tab.key = t.value("key", std::string());
				tab.flag = t.value("flag", 0u);
				if (!tab.id.empty()) {
					g_tabs.push_back(std::move(tab));
				}
			}
			g_sliders.clear();
			g_byKey.clear();
			for (const auto& s : sliders) {
				Slider row;
				row.k = s.value("k", std::string());
				row.label = s.value("label", s.value("text", row.k));
				row.lower = Lower(row.label);
				row.tab = s.value("tab", std::string());
				row.min = Num(s, "min", 0.0f);
				row.max = Num(s, "max", 1.0f);
				row.step = Num(s, "step", 0.01f);
				if (row.max <= row.min) {
					row.max = row.min + 1.0f;
				}
				if (s.contains("value")) {
					SetValue(row, s["value"]);
				}
				if (row.k.empty()) {
					continue;
				}
				g_byKey[row.k] = g_sliders.size();
				g_sliders.push_back(std::move(row));
			}
			ApplyOrder();
			for (auto& t : g_tabs) {
				t.rows.clear();
			}
			for (std::size_t i = 0; i < g_sliders.size(); ++i) {
				std::size_t home = 0;
				for (std::size_t t = 0; t < g_tabs.size(); ++t) {
					if (g_tabs[t].id == g_sliders[i].tab) {
						home = t;
						break;
					}
				}
				g_tabs[home].rows.push_back(i);
			}
			g_needDrawn = true;
			spdlog::info("[smf] view: {} sliders in {} tabs", g_sliders.size(), g_tabs.size());
		}

		void SendDrawn()
		{
			g_needDrawn = false;
			json        sections = json::object();
			std::size_t rows = 0;
			for (const auto& t : g_tabs) {
				if (!t.rows.empty()) {
					sections[t.id.empty() ? "general" : t.id] = t.rows.size();
					rows += t.rows.size();
				}
			}
			Send("sliders-drawn",
				{ { "rows", rows }, { "sliders", g_sliders.size() }, { "sections", std::move(sections) },
					{ "notDrawn", json::array() } });
		}

		void AskAll()
		{
			Send("settings-get");
			Send("racemenu-categories-get");
			Send("sliders-get");
			Send("stage-get");
			Face::Ask();
		}

		void DrawCard(IG::ImGuiIO* a_io)
		{
			const Theme::Scope theme(Theme::Kind::kCard);
			IG::SetNextWindowPos(IG::ImVec2{ a_io->DisplaySize.x * 0.5f, Theme::Px(24.0f) }, IG::ImGuiCond_Always,
				IG::ImVec2{ 0.5f, 0.0f });
			IG::Begin("##lifecast-card", nullptr,
				IG::ImGuiWindowFlags_NoDecoration | IG::ImGuiWindowFlags_NoInputs | IG::ImGuiWindowFlags_AlwaysAutoResize |
					IG::ImGuiWindowFlags_NoSavedSettings | IG::ImGuiWindowFlags_NoFocusOnAppearing);
			Theme::Body();
			Theme::LogWindow(Theme::Kind::kCard);
			IG::TextColored(Theme::Color::Accent(), "Lifecast");
			IG::SameLine();
			IG::Text("- %s brings the panel back", g_cardKey.c_str());
			IG::End();
		}

		// The bar over the stage (stage.js): turn, then the camera.
		void DrawStage()
		{
			if (!g_stage.ready) {
				IG::TextDisabled("Character view: %s", g_stage.why.empty() ? "not ready" : g_stage.why.c_str());
			}
			IG::AlignTextToFramePadding();
			IG::TextDisabled("Turn");
			IG::SameLine();
			if (IG::Button("<##turn-left")) {
				Send("stage-move", { { "turn", -kTurnStep } });
			}
			Rect("turn-left");
			IG::SameLine();
			if (IG::Button(">##turn-right")) {
				Send("stage-move", { { "turn", kTurnStep } });
			}
			Rect("turn-right");
			IG::SameLine();
			IG::TextDisabled("|");
			IG::SameLine();
			IG::TextDisabled("Camera");
			IG::SameLine();
			if (g_stage.free) {
				if (IG::Button("Closer")) {
					Send("stage-move", { { "distance", -kDistanceStep } });
				}
				IG::SameLine();
				if (IG::Button("Farther")) {
					Send("stage-move", { { "distance", kDistanceStep } });
				}
			} else if (IG::Button("Zoom##stage")) {
				Send("stage-zoom", { { "from", "button" } });
				++g_zooms;
			}
			if (!g_stage.free) {
				Rect("zoom");
			}
			IG::SameLine();
			if (IG::Button("Reset##stage")) {
				Send("stage-reset");
			}
			Rect("reset");
			IG::SameLine();
			IG::TextDisabled("turn %.0f, distance %.0f", g_stage.turn, g_stage.distance);
		}

		void DrawRow(Slider& a_s)
		{
			IG::PushID(a_s.k.c_str());
			IG::TextUnformatted(a_s.label.c_str());
			if (!a_s.has) {
				IG::SameLine();
				IG::TextDisabled("(no value)");
				IG::PopID();
				return;
			}
			const char* format = a_s.step >= 1.0f ? "%.0f" : "%.2f";
			IG::SetNextItemWidth(-Theme::Px(70.0f));
			float v = a_s.v;
			if (IG::SliderFloat("##v", &v, a_s.min, a_s.max, format)) {
				a_s.v = v;
				a_s.active = true;
				const auto now = Clock::now();
				if (now - g_sliderSent >= kSendEvery) {
					g_sliderSent = now;
					Send("slider-set", { { "k", a_s.k }, { "v", a_s.v }, { "final", false } });
				}
			}
			Rect("s:" + a_s.k);
			if (IG::IsItemDeactivatedAfterEdit()) {
				a_s.active = false;
				Send("slider-set", { { "k", a_s.k }, { "v", a_s.v }, { "final", true } });
			} else if (!IG::IsItemActive()) {
				a_s.active = false;
			}
			IG::SameLine();
			IG::SetNextItemWidth(Theme::Px(62.0f));
			if (IG::InputTextWithHint("##typed", "type", a_s.typed, sizeof(a_s.typed),
					IG::ImGuiInputTextFlags_CharsDecimal | IG::ImGuiInputTextFlags_EnterReturnsTrue |
						IG::ImGuiInputTextFlags_AutoSelectAll | IG::ImGuiInputTextFlags_CallbackCharFilter,
					&DeadKeys::Filter)) {
				char*       end = nullptr;
				const float t = std::strtof(a_s.typed, &end);
				if (end && end != a_s.typed) {
					a_s.v = (std::clamp)(t, a_s.min, a_s.max);
					spdlog::info("[smf] typed {} for {} -> {}", a_s.typed, a_s.k, a_s.v);
					Send("slider-set", { { "k", a_s.k }, { "v", a_s.v }, { "final", true } });
				}
			}
			Rect("t:" + a_s.k);
			IG::PopID();
		}

		// A chip of the panel's head (.region-chip): the one shown in the
		// accent over the selected row colour.
		bool Chip(const char* a_label, bool a_on)
		{
			if (a_on) {
				IG::PushStyleColor(IG::ImGuiCol_Button, Theme::Color::RowSelected());
				IG::PushStyleColor(IG::ImGuiCol_Text, Theme::Color::Accent());
			}
			IG::PushStyleVar(IG::ImGuiStyleVar_FrameRounding, 4.0f);
			const bool hit = IG::Button(a_label);
			IG::PopStyleVar();
			if (a_on) {
				IG::PopStyleColor(2);
			}
			return hit;
		}

		// The head of Sliders: the search over the RaceMenu categories, as
		// chips that wrap.
		void DrawSlidersHead()
		{
			IG::SetNextItemWidth(-1.0f);
			if (IG::InputTextWithHint("##search", "Search sliders", g_search, sizeof(g_search),
					IG::ImGuiInputTextFlags_CallbackCharFilter, &DeadKeys::Filter)) {
				spdlog::info("[smf] search field '{}' hex [{}]", g_search, Hex(g_search));
			}
			Rect("search");
			if (!g_slidersReady) {
				return;
			}
			const auto* style = IG::GetStyle();
			const float right = IG::GetCursorScreenPos().x + IG::GetContentRegionAvail().x;
			bool        first = true;
			for (auto& t : g_tabs) {
				if (t.rows.empty()) {
					continue;
				}
				const auto id = t.id.empty() ? std::string("general") : t.id;
				if (g_tab == "\x01") {
					g_tab = id;
				}
				const auto  label = fmt::format("{}###tab-{}", t.label, id);
				const float w = IG::CalcTextSize(t.label.c_str()).x + style->FramePadding.x * 2.0f;
				if (!first) {
					IG::SameLine(0.0f, Theme::Px(4.0f));
					if (IG::GetCursorScreenPos().x + w > right) {
						IG::NewLine();
					}
				}
				first = false;
				if (Chip(label.c_str(), g_tab == id) && g_tab != id) {
					g_tab = id;
					spdlog::info("[smf] view: slider category {}", id);
					Common::LayoutSoon();
				}
				Rect("tab:" + id);
			}
		}

		void DrawSliders()
		{
			if (!g_slidersReady) {
				IG::TextDisabled("Reading RaceMenu's sliders...");
				return;
			}
			const auto find = Lower(g_search);
			IG::BeginChild("##rows");
			Theme::Body();
			for (auto& t : g_tabs) {
				const auto id = t.id.empty() ? std::string("general") : t.id;
				// A search looks through every category, as the page's does.
				if (find.empty() && id != g_tab) {
					continue;
				}
				for (const auto i : t.rows) {
					auto& s = g_sliders[i];
					if (!find.empty() && s.lower.find(find) == std::string::npos) {
						continue;
					}
					DrawRow(s);
				}
			}
			IG::EndChild();
		}

		// The sections: the belt's categories (.belt-cat), and the small
		// panel shows the one that is on.
		constexpr const char* kSectionLabels[] = { "SLIDERS###sec-sliders", "LOOK###sec-look", "RACE & SEX###sec-race",
			"RACE FACES###sec-faces" };
		constexpr const char* kSectionTitles[] = { "Sliders", "Look", "Race & Sex", "Race Faces" };
		constexpr const char* kSectionRects[] = { "sec:sliders", "sec:look", "sec:race", "sec:faces" };

		void DrawCategories()
		{
			auto* dl = IG::GetWindowDrawList();
			for (int i = 0; i < 4; ++i) {
				if (i > 0) {
					IG::SameLine(0.0f, Theme::Px(2.0f));
				}
				const bool on = g_section == i;
				IG::PushStyleColor(IG::ImGuiCol_Text, on ? Theme::Color::Accent() : Theme::Color::TextDim());
				IG::PushStyleColor(IG::ImGuiCol_ButtonHovered, IG::ImVec4{ 0.0f, 0.0f, 0.0f, 0.0f });
				IG::PushStyleColor(IG::ImGuiCol_ButtonActive, IG::ImVec4{ 0.0f, 0.0f, 0.0f, 0.0f });
				IG::PushStyleVar(IG::ImGuiStyleVar_FrameBorderSize, 0.0f);
				IG::PushStyleVar(IG::ImGuiStyleVar_FramePadding, IG::ImVec2{ Theme::Px(10.0f), Theme::Px(7.0f) });
				const bool hit = IG::Button(kSectionLabels[i]);
				IG::PopStyleVar(2);
				IG::PopStyleColor(3);
				const auto a = IG::GetItemRectMin();
				const auto b = IG::GetItemRectMax();
				if (on) {
					const float t = Theme::Px(2.0f);
					IG::ImDrawListManager::AddRectFilled(dl, IG::ImVec2{ a.x, b.y - t }, IG::ImVec2{ b.x, b.y },
						IG::ColorConvertFloat4ToU32(Theme::Color::Accent()), 0.0f, 0);
				} else if (IG::IsItemHovered()) {
					// .belt-cat:hover lights the word; ImGui has no per-item text hover colour, so a hairline marks it
					IG::ImDrawListManager::AddRectFilled(dl, IG::ImVec2{ a.x, b.y - 1.0f }, IG::ImVec2{ b.x, b.y },
						IG::ColorConvertFloat4ToU32(Theme::Color::Text()), 0.0f, 0);
				}
				Rect(kSectionRects[i]);
				if (hit && g_section != i) {
					g_section = i;
					spdlog::info("[smf] view: section {}", kSectionRects[i] + 4);
					Common::LayoutSoon();
				}
			}
		}

		void DrawSection()
		{
			switch (g_section) {
			case 0:
				DrawSliders();
				break;
			case 1:
				Face::DrawLook();
				break;
			case 2:
				Face::DrawRaceSex();
				break;
			default:
				Face::DrawRacePresets();
				break;
			}
		}

		// The stage under the "all" form: a drag or the wheel outside the
		// panel, which only the window sees while it holds the input.
		void StageInput(IG::ImGuiIO* a_io, bool a_holds)
		{
			if (!a_holds || g_hybrid) {
				g_dragging = false;
				return;
			}
			const bool over = IG::IsWindowHovered(IG::ImGuiHoveredFlags_AnyWindow);
			if (IG::IsMouseClicked(0) && !over) {
				g_dragging = true;
				g_dragAcc = 0.0f;
				g_dragSent = Clock::now();
			}
			if (g_dragging) {
				g_dragAcc += a_io->MouseDelta.x;
				const bool up = !IG::IsMouseDown(0);
				const auto now = Clock::now();
				if (g_dragAcc != 0.0f && (up || now - g_dragSent >= kSendEvery)) {
					Send("stage-move", { { "turn", g_dragAcc * kTurnPerPx } });
					g_dragAcc = 0.0f;
					g_dragSent = now;
				}
				if (up) {
					g_dragging = false;
				}
			}
			if (!over && a_io->MouseWheel != 0.0f) {
				if (g_stage.free) {
					Send("stage-move", { { "distance", -a_io->MouseWheel * kDistanceStep } });
				} else {
					// One toggle per wheel gesture: the notches of one turn arrive
					// over several frames, and RaceMenu's swap takes longer than this.
					const auto now = Clock::now();
					if (now - g_wheelZoomAt >= std::chrono::milliseconds(450)) {
						Send("stage-zoom", { { "from", "wheel" } });
						++g_zooms;
					}
					g_wheelZoomAt = now;
				}
			}
		}

		// Where the character view is: the stage, beside the small panel and
		// over the belt (stage.js's frame), in shares of the screen's width.
		void StageFrame(IG::ImGuiIO* a_io, float a_left, float a_right)
		{
			const float dw = a_io->DisplaySize.x;
			if (dw <= 0.0f) {
				return;
			}
			const float left = (std::clamp)(a_left / dw, 0.0f, 0.9f);
			const float right = (std::clamp)(a_right / dw, left + 0.1f, 1.0f);
			if (g_frameRight >= 0.0f && std::abs(right - g_frameRight) < 0.002f && std::abs(left - g_frameLeft) < 0.002f) {
				return;
			}
			g_frameLeft = left;
			g_frameRight = right;
			Send("stage-frame", { { "on", true }, { "left", left }, { "right", right } });
		}

		std::string BoxText(const Box& a_b)
		{
			return fmt::format("{} {:.0f},{:.0f},{:.0f},{:.0f}", a_b.name, a_b.x0, a_b.y0, a_b.x1, a_b.y1);
		}

		Box WindowBox(const char* a_name)
		{
			const auto pos = IG::GetWindowPos();
			const auto size = IG::GetWindowSize();
			return Box{ a_name, pos.x, pos.y, pos.x + size.x, pos.y + size.y };
		}

		constexpr IG::ImGuiWindowFlags kFixed = IG::ImGuiWindowFlags_NoTitleBar | IG::ImGuiWindowFlags_NoResize |
		                                        IG::ImGuiWindowFlags_NoMove | IG::ImGuiWindowFlags_NoCollapse |
		                                        IG::ImGuiWindowFlags_NoSavedSettings;
	}

	namespace Common
	{
		void LayoutSoon()
		{
			g_layoutSoon = 3;
		}

		void Rect(std::string_view a_name)
		{
			if (!IG::IsItemVisible()) {
				return;
			}
			const auto a = IG::GetItemRectMin();
			const auto b = IG::GetItemRectMax();
			if (b.y > g_clipBottom) {
				return;
			}
			g_layout.push_back(fmt::format("{} {:.0f},{:.0f},{:.0f},{:.0f}", a_name, a.x, a.y, b.x, b.y));
		}

		void Send(std::string_view a_type, json a_data)
		{
			Face::Sent(a_type);
			json m = { { "v", Bridge::kProtocolVersion }, { "type", a_type }, { "data", std::move(a_data) } };
			std::lock_guard l(g_outMx);
			g_out.push_back(m.dump());
			++g_sent;
		}

		std::string Lower(std::string a_s)
		{
			std::transform(a_s.begin(), a_s.end(), a_s.begin(),
				[](unsigned char c) { return static_cast<char>(c < 0x80 ? std::tolower(c) : c); });
			return a_s;
		}
	}

	void Receive(const char* a_text)
	{
		if (!a_text) {
			return;
		}
		const auto m = json::parse(a_text, nullptr, false);
		if (m.is_discarded() || !m.is_object()) {
			return;
		}
		const auto type = m.value("type", std::string());
		const auto data = m.contains("data") ? m["data"] : json::object();
		std::lock_guard l(g_mx);
		++g_received;
		if (type == "hello") {
			g_helloed = true;
			g_dll = data.value("dll", std::string("?"));
			g_compatible = data.value("compatible", true);
		} else if (type == "settings") {
			const bool hybrid = data.value("smfInput", std::string("hybrid")) == "hybrid";
			if (hybrid != g_hybrid) {
				spdlog::info("[smf] view: input form {}", hybrid ? "hybrid" : "all");
			}
			g_hybrid = hybrid;
			// The page's own size (--panel-scale, 0.8 to 2.5); 1 when the settings do not carry it.
			const auto  sc = data.find("panelScale");
			const float scale = sc != data.end() && sc->is_number() ? sc->get<float>() : 1.0f;
			if (std::abs(scale - Theme::Scale()) > 0.001f || !g_scaleHeard) {
				g_scaleHeard = true;
				Theme::SetScale(scale);
				spdlog::info("[smf] view: panel scale {:.2f} (settings: {})", Theme::Scale(),
					sc != data.end() ? sc->dump() : std::string("no panelScale"));
				Common::LayoutSoon();
			}
		} else if (type == "visibility") {
			const bool v = data.value("visible", false);
			if (v && !g_visible) {
				g_frameLeft = -1.0f;
				g_frameRight = -1.0f;
				AskAll();
			}
			g_visible = v;
		} else if (type == "card") {
			g_card = data.value("on", false);
			if (data.contains("key") && data["key"].is_string()) {
				g_cardKey = data["key"].get<std::string>();
			}
		} else if (type == "sliders") {
			OnSliders(data);
		} else if (type == "slider-values") {
			if (data.contains("values") && data["values"].is_object()) {
				for (const auto& [k, v] : data["values"].items()) {
					if (const auto it = g_byKey.find(k); it != g_byKey.end()) {
						SetValue(g_sliders[it->second], v);
					}
				}
			}
		} else if (type == "racemenu-categories") {
			if (data.value("ready", false) && data.contains("entries") && data["entries"].is_array()) {
				g_categories = data["entries"];
				ApplyOrder();
			}
		} else if (type == "stage") {
			g_stage.ready = data.value("ready", false);
			g_stage.why = data.value("why", std::string());
			g_stage.free = data.value("free", false);
			g_stage.turn = data.value("turn", 0.0);
			g_stage.distance = data.value("distance", 0.0);
		} else if (!Face::Receive(type, data)) {
			++g_ignored;
		}
		// A list stamp is answered within seconds (Core/ListSig.cpp); the
		// answer must not wait for the window to draw again.
		Flush();
	}

	void Open()
	{
		Send("hello", { { "page", fmt::format("smf-view {}", Version::kString) }, { "protocol", Bridge::kProtocolVersion } });
		AskAll();
		Flush();
	}

	Frame Draw(bool a_holds)
	{
		Frame f;
		auto* io = IG::GetIO();
		if (!io) {
			return f;
		}
		std::lock_guard l(g_mx);
		if (g_card) {
			DrawCard(io);
			return f;
		}
		if (!g_visible) {
			return f;
		}

		// The frame of the web panel (D119, panel.css): the belt along the
		// bottom, the small panel over its first column, the stage in the rest
		// of the screen with its bar at the bottom. Fixed windows - no title,
		// no moving, no resizing - and the screen between them is the game's.
		const float dw = io->DisplaySize.x;
		const float dh = io->DisplaySize.y;
		const float gap = Theme::Px(8.0f);                                // --gap
		const float beltH = Theme::Px(48.0f);                             // --belt-height
		const float col = (std::min)(Theme::Px(430.0f), dw * 0.34f);      // --panel-col
		const float panelH = (std::min)(dh * 0.64f, dh - beltH - gap);    // --panel-tall, the panel always open
		const float beltY = dh - beltH;
		const float stageL = col + 2.0f * gap;
		const float stageT = 2.0f * gap;
		const float stageR = dw - 2.0f * gap;
		const float stageB = beltY - 2.0f * gap;
		const auto  border = IG::ColorConvertFloat4ToU32(Theme::Color::PanelBorder());
		g_layout.clear();

		// The stage: a frame only (.stage), on the background list - under every window.
		IG::ImDrawListManager::AddRect(IG::GetBackgroundDrawList(), IG::ImVec2{ stageL, stageT }, IG::ImVec2{ stageR, stageB },
			IG::ColorConvertFloat4ToU32(Theme::Color::StageBorder()), 6.0f, 0, 1.0f);

		// The small panel.
		{
			const Theme::Scope theme(Theme::Kind::kPanel);
			IG::SetNextWindowPos(IG::ImVec2{ 0.0f, beltY - panelH }, IG::ImGuiCond_Always);
			IG::SetNextWindowSize(IG::ImVec2{ col, panelH }, IG::ImGuiCond_Always);
			IG::Begin("##lifecast-panel", nullptr, kFixed | IG::ImGuiWindowFlags_NoScrollbar | IG::ImGuiWindowFlags_NoScrollWithMouse);
			Theme::Body();
			Theme::LogWindow(Theme::Kind::kPanel);
			f.boxes[f.count++] = WindowBox("panel");
			g_clipBottom = f.boxes[0].y1;
			auto* dl = IG::GetWindowDrawList();
			const auto& b = f.boxes[0];
			// .belt-panel: border on top and on the right
			IG::ImDrawListManager::AddLine(dl, IG::ImVec2{ b.x0, b.y0 + 0.5f }, IG::ImVec2{ b.x1, b.y0 + 0.5f }, border, 1.0f);
			IG::ImDrawListManager::AddLine(dl, IG::ImVec2{ b.x1 - 0.5f, b.y0 }, IG::ImVec2{ b.x1 - 0.5f, b.y1 }, border, 1.0f);

			Theme::SectionTitle(kSectionTitles[g_section]);
			if (g_helloed && !g_compatible) {
				IG::TextColored(Theme::Color::Bad(), "This view and the DLL speak different protocol versions.");
			}
			if (g_section == 0) {
				DrawSlidersHead();
			}
			IG::Separator();
			DrawSection();
			IG::End();
		}

		// The belt: header (the title, as wide as the panel), the categories,
		// room for the actions E6 brings, the footer.
		{
			const Theme::Scope theme(Theme::Kind::kBelt);
			IG::SetNextWindowPos(IG::ImVec2{ 0.0f, beltY }, IG::ImGuiCond_Always);
			IG::SetNextWindowSize(IG::ImVec2{ dw, beltH }, IG::ImGuiCond_Always);
			IG::Begin("##lifecast-belt", nullptr, kFixed | IG::ImGuiWindowFlags_NoScrollbar | IG::ImGuiWindowFlags_NoScrollWithMouse);
			Theme::Body(13.0f);  // .belt-cat
			Theme::LogWindow(Theme::Kind::kBelt);
			f.boxes[f.count++] = WindowBox("belt");
			const auto& b = f.boxes[f.count - 1];
			g_clipBottom = b.y1;
			auto* dl = IG::GetWindowDrawList();
			const float pad = gap * 1.5f;
			IG::ImDrawListManager::AddLine(dl, IG::ImVec2{ b.x0, b.y0 + 0.5f }, IG::ImVec2{ b.x1, b.y0 + 0.5f }, border, 1.0f);
			IG::ImDrawListManager::AddLine(dl, IG::ImVec2{ col - 0.5f, b.y0 }, IG::ImVec2{ col - 0.5f, b.y1 }, border, 1.0f);

			// .panel-header: the title
			IG::SetCursorScreenPos(IG::ImVec2{ pad, beltY + (beltH - Theme::Px(22.0f)) * 0.5f });
			Theme::Title("Lifecast");

			// .belt-cats
			const float rowY = beltY + (beltH - IG::GetFrameHeight()) * 0.5f;
			IG::SetCursorScreenPos(IG::ImVec2{ col + pad, rowY });
			DrawCategories();
			const float catsEnd = IG::GetItemRectMax().x + pad;
			IG::ImDrawListManager::AddLine(dl, IG::ImVec2{ catsEnd - 0.5f, b.y0 }, IG::ImVec2{ catsEnd - 0.5f, b.y1 }, border, 1.0f);

			// .panel-footer: the input form (D291) and the way back to RaceMenu.
			const auto* style = IG::GetStyle();
			const float radio = IG::GetFrameHeight() + style->ItemInnerSpacing.x;
			const float footW = IG::CalcTextSize("Input").x + radio * 2.0f + IG::CalcTextSize("All").x +
			                    IG::CalcTextSize("Hybrid").x + IG::CalcTextSize("F4  RaceMenu").x + style->ItemSpacing.x * 4.0f;
			const float footX = dw - footW - pad;
			IG::ImDrawListManager::AddLine(dl, IG::ImVec2{ footX - pad + 0.5f, b.y0 }, IG::ImVec2{ footX - pad + 0.5f, b.y1 }, border,
				1.0f);
			IG::SetCursorScreenPos(IG::ImVec2{ footX, rowY });
			const bool hybrid = g_hybrid;
			IG::AlignTextToFramePadding();
			IG::TextDisabled("Input");
			IG::SameLine();
			if (IG::RadioButton("All##input", !hybrid) && hybrid) {
				Send("settings-set", { { "key", "smfInput" }, { "value", "all" } });
			}
			Rect("input-all");
			IG::SameLine();
			if (IG::RadioButton("Hybrid##input", hybrid) && !hybrid) {
				Send("settings-set", { { "key", "smfInput" }, { "value", "hybrid" } });
			}
			Rect("input-hybrid");
			IG::SameLine();
			IG::TextDisabled("F4  RaceMenu");
			IG::End();
		}

		// The stage bar (.stage-bar): centred at the bottom of the stage.
		{
			const Theme::Scope theme(Theme::Kind::kStageBar);
			IG::SetNextWindowPos(IG::ImVec2{ (stageL + stageR) * 0.5f, stageB - gap }, IG::ImGuiCond_Always,
				IG::ImVec2{ 0.5f, 1.0f });
			IG::Begin("##lifecast-stagebar", nullptr, kFixed | IG::ImGuiWindowFlags_AlwaysAutoResize | IG::ImGuiWindowFlags_NoScrollbar);
			Theme::Body(12.0f);  // .panel-button
			Theme::LogWindow(Theme::Kind::kStageBar);
			f.boxes[f.count++] = WindowBox("stagebar");
			g_clipBottom = f.boxes[f.count - 1].y1;
			DrawStage();
			IG::End();
		}

		f.wantText = io->WantTextInput;
		f.drawn = true;
		g_lastLayout = fmt::format("window {:.0f},{:.0f},{:.0f},{:.0f}; {}; {}; stage {:.0f},{:.0f},{:.0f},{:.0f}", f.boxes[0].x0,
			f.boxes[0].y0, f.boxes[0].x1, f.boxes[0].y1, BoxText(f.boxes[1]), BoxText(f.boxes[2]), stageL, stageT, stageR, stageB);
		for (const auto& e : g_layout) {
			g_lastLayout += "; " + e;
		}
		if (g_layoutSoon > 0 && --g_layoutSoon == 0) {
			spdlog::info("[smf] view layout: {}", g_lastLayout);
		}

		StageInput(io, a_holds);
		StageFrame(io, stageL, stageR);
		if (g_needDrawn) {
			SendDrawn();
		}
		return f;
	}

	void Flush()
	{
		std::vector<std::string> out;
		{
			std::lock_guard l(g_outMx);
			out.swap(g_out);
		}
		if (out.empty()) {
			return;
		}
		auto* tasks = SKSE::GetTaskInterface();
		if (!tasks) {
			spdlog::warn("[smf] view: no task interface - {} message(s) dropped", out.size());
			return;
		}
		tasks->AddTask([out = std::move(out)]() {
			for (const auto& t : out) {
				Bridge::Receive(t.c_str(), 1, false);
			}
		});
	}

	bool Hybrid()
	{
		return g_hybrid;
	}

	bool Card()
	{
		std::lock_guard l(g_mx);
		return g_card;
	}

	void LogState(const char* a_what)
	{
		std::lock_guard l(g_mx);
		spdlog::info("[smf] view {}: visible {}, card {}, form {}, sliders {} in {} tabs (ready {}), stage ready {} free {} "
					 "turn {:.1f} distance {:.1f}, zoom asked {} | received {}, not drawn {}, sent {}",
			a_what, g_visible, g_card, g_hybrid ? "hybrid" : "all", g_sliders.size(), g_tabs.size(), g_slidersReady,
			g_stage.ready, g_stage.free, g_stage.turn, g_stage.distance, g_zooms, g_received, g_ignored, g_sent);
		spdlog::info("[smf] view {}: section {}", a_what, g_section);
		Face::LogState(a_what);
		spdlog::info("[smf] view layout: {}", g_lastLayout.empty() ? std::string("not drawn yet") : g_lastLayout);
	}
}
