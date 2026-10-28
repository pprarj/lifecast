// View.cpp
// Lifecast

#include "Panel/Smf/View.h"

#include "Panel/Smf/DeadKeys.h"

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
		float             g_frameRight = -1.0f;

		// Where the widgets were last frame, screen pixels, for the state
		// line: a test bench clicks by it.
		std::vector<std::string> g_layout;
		std::string              g_lastLayout;
		float                    g_clipBottom = 0.0f;

		void Rect(std::string_view a_name)
		{
			const auto a = IG::GetItemRectMin();
			const auto b = IG::GetItemRectMax();
			if (b.y > g_clipBottom) {
				return;
			}
			g_layout.push_back(fmt::format("{} {:.0f},{:.0f},{:.0f},{:.0f}", a_name, a.x, a.y, b.x, b.y));
		}

		void Send(std::string_view a_type, json a_data = json::object())
		{
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
		}

		void DrawCard(IG::ImGuiIO* a_io)
		{
			IG::SetNextWindowPos(IG::ImVec2{ a_io->DisplaySize.x * 0.5f, 24.0f }, IG::ImGuiCond_Always, IG::ImVec2{ 0.5f, 0.0f });
			IG::SetNextWindowBgAlpha(0.8f);
			IG::Begin("##lifecast-card", nullptr,
				IG::ImGuiWindowFlags_NoDecoration | IG::ImGuiWindowFlags_NoInputs | IG::ImGuiWindowFlags_AlwaysAutoResize |
					IG::ImGuiWindowFlags_NoSavedSettings | IG::ImGuiWindowFlags_NoFocusOnAppearing);
			IG::Text("Lifecast - %s brings the panel back", g_cardKey.c_str());
			IG::End();
		}

		void DrawStage()
		{
			if (!g_stage.ready) {
				IG::TextDisabled("Character view: %s", g_stage.why.empty() ? "not ready" : g_stage.why.c_str());
			}
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
			IG::SetNextItemWidth(-110.0f);
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
			IG::SetNextItemWidth(100.0f);
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

		void DrawSliders()
		{
			if (IG::InputTextWithHint("##search", "Search sliders", g_search, sizeof(g_search),
					IG::ImGuiInputTextFlags_CallbackCharFilter, &DeadKeys::Filter)) {
				spdlog::info("[smf] search field '{}' hex [{}]", g_search, Hex(g_search));
			}
			Rect("search");
			if (!g_slidersReady) {
				IG::TextDisabled("Reading RaceMenu's sliders...");
				return;
			}
			const auto find = Lower(g_search);
			if (!IG::BeginTabBar("##lifecast-categories", IG::ImGuiTabBarFlags_FittingPolicyScroll)) {
				return;
			}
			for (auto& t : g_tabs) {
				if (t.rows.empty()) {
					continue;
				}
				const auto label = fmt::format("{}###tab-{}", t.label, t.id.empty() ? "general" : t.id);
				const bool open = IG::BeginTabItem(label.c_str());
				Rect("tab:" + (t.id.empty() ? std::string("general") : t.id));
				if (!open) {
					continue;
				}
				IG::BeginChild("##rows");
				for (const auto i : t.rows) {
					auto& s = g_sliders[i];
					if (!find.empty() && s.lower.find(find) == std::string::npos) {
						continue;
					}
					DrawRow(s);
				}
				IG::EndChild();
				IG::EndTabItem();
			}
			IG::EndTabBar();
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

		// Where the character view is: the screen left of the panel.
		void StageFrame(IG::ImGuiIO* a_io, const Frame& a_f)
		{
			if (a_io->DisplaySize.x <= 0.0f) {
				return;
			}
			const float right = (std::clamp)(a_f.x / a_io->DisplaySize.x, 0.1f, 1.0f);
			if (g_frameRight >= 0.0f && std::abs(right - g_frameRight) < 0.002f) {
				return;
			}
			g_frameRight = right;
			Send("stage-frame", { { "on", true }, { "left", 0.0 }, { "right", right } });
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
		} else if (type == "visibility") {
			const bool v = data.value("visible", false);
			if (v && !g_visible) {
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
		} else {
			++g_ignored;
		}
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

		const float dw = io->DisplaySize.x;
		const float dh = io->DisplaySize.y;
		const float w = (std::max)(440.0f, dw * 0.30f);
		IG::SetNextWindowPos(IG::ImVec2{ dw - w - 24.0f, 24.0f }, IG::ImGuiCond_Appearing);
		IG::SetNextWindowSize(IG::ImVec2{ w, dh - 48.0f }, IG::ImGuiCond_Appearing);
		IG::Begin("Lifecast###lifecast-smf", nullptr, IG::ImGuiWindowFlags_NoCollapse | IG::ImGuiWindowFlags_NoSavedSettings);
		const auto pos = IG::GetWindowPos();
		const auto size = IG::GetWindowSize();
		f.x = pos.x;
		f.y = pos.y;
		f.w = size.x;
		f.h = size.y;
		g_layout.clear();
		g_clipBottom = pos.y + size.y;

		IG::Text("Lifecast %s", g_dll.c_str());
		IG::SameLine();
		IG::TextDisabled("- SKSE Menu Framework");
		if (g_helloed && !g_compatible) {
			IG::TextUnformatted("This view and the DLL speak different protocol versions.");
		}
		// The input form, while both are being measured (1.6 E2).
		const bool hybrid = g_hybrid;
		IG::TextUnformatted("Input:");
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
		IG::Separator();
		DrawStage();
		IG::Separator();
		DrawSliders();
		f.wantText = io->WantTextInput;
		f.drawn = true;
		IG::End();
		g_lastLayout = fmt::format("window {:.0f},{:.0f},{:.0f},{:.0f}", f.x, f.y, f.x + f.w, f.y + f.h);
		for (const auto& e : g_layout) {
			g_lastLayout += "; " + e;
		}

		StageInput(io, a_holds);
		StageFrame(io, f);
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
		spdlog::info("[smf] view layout: {}", g_lastLayout.empty() ? std::string("not drawn yet") : g_lastLayout);
	}
}
