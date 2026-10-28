// View.cpp
// Lifecast

#include "Panel/Smf/View.h"

#include "Panel/Smf/Common.h"
#include "Panel/Smf/Control.h"
#include "Panel/Smf/DeadKeys.h"
#include "Panel/Smf/Face.h"
#include "Panel/Smf/History.h"
#include "Panel/Smf/Presets.h"
#include "Panel/Smf/Skin.h"
#include "Panel/Smf/Theme.h"

#include "Panel/Bridge.h"
#include "Version.h"

#include "SKSEMenuFramework/SKSEMenuFramework.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <mutex>
#include <string>
#include <tuple>
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
		constexpr float kLiftPerPx = 0.3f;
		constexpr float kPanPerPx = 0.3f;
		constexpr float kTiltPerPx = 0.1f;
		constexpr float kTurnStep = 15.0f;
		constexpr float kDistanceStep = 10.0f;
		constexpr float kLiftStep = 10.0f;
		constexpr float kTiltStep = 5.0f;
		constexpr float kNear = 0.01f;
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
			float       before = 0.0f;  // the value when the hand took the slider (History)
			char        typed[24] = {};
			// The first number the row ever received is its baseline, as a
			// row of the page keeps it (controls.js, V079): "changed" is the
			// value now against it.
			bool        hasInitial = false;
			float       initial = 0.0f;
		};

		// One row of "In RaceMenu" (delegate.js): a handover to RaceMenu.
		struct Door
		{
			std::string key;  // what "show-racemenu" carries as "for"
			std::string label;
			std::string note;
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

		// The "stage" message (Stage.cpp State), as stage.js reads it.
		struct StageState
		{
			bool        ready = false;
			bool        free = false;
			bool        forced = false;  // the comparison holds the free camera on
			bool        lock = true;     // absent is on, as stage.js reads it
			bool        tilt = false;    // the camera has a node to tilt by
			double      turn = 0.0;
			double      distance = 0.0;
			double      lift = 0.0;
			double      pan = 0.0;
			double      tiltDeg = 0.0;
			double      minDistance = 0.0;
			double      maxDistance = 0.0;
			double      liftMin = 0.0;
			double      liftMax = 0.0;
			double      tiltMax = 0.0;
			json        tuning;  // development builds only (LIFECAST_TUNING)
			json        tuningDefaults;
			std::string why;
		};

		// A drag in the stage frame (stage.js pointerdown): what it moves.
		enum class Drag
		{
			kNone,
			kTurn,
			kLift,
			kTilt,
			kCopy,
			kCopyMove
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
		bool                                      g_stageVisible = true;  // "stage-visible"
		Clock::time_point                         g_laltZoomAt{};
		Box                                       g_popup;
		bool                                      g_havePopup = false;
		bool                                      g_inPopup = false;
		std::uint32_t                             g_received = 0;
		std::uint32_t                             g_ignored = 0;
		std::vector<Door>                         g_doors;            // "In RaceMenu": the fixed rows, then the live ones
		Clock::time_point                         g_helloAskedAt{};   // when the view said hello (the bridge's status)
		int                                       g_cardFrames = 0;   // frames the card has been drawn since it came on
		float                                     g_scaleShown = 1.0f;  // the scale the settings last carried

		// The look is asked when the view comes on screen, as the page asks it
		// when it attaches (look.js). On a new game the view is there before
		// the RaceSex Menu has built its head-part lists, and that first
		// reading comes from the data handler (1.6 E4, cell N); the page only
		// attaches once it has loaded. So the view asks once more when
		// RaceMenu's film arrives built: a "sliders" or "racemenu-categories"
		// with entries that is not the answer to the view's own ask (the
		// first one of each after coming on screen) - the DLL sends those
		// when the film's list changes.
		bool                                      g_lookAfterFilm = false;
		Clock::time_point                         g_shownAt{};
		int                                       g_slidersSinceShown = 0;
		int                                       g_categoriesSinceShown = 0;

		// What the view sends, until a task hands it to the bridge.
		std::mutex               g_outMx;
		std::vector<std::string> g_out;
		std::uint32_t            g_sent = 0;

		// Render-only.
		char              g_search[128] = {};
		bool              g_changedOnly = false;  // the "Changed" filter (search.js)
		std::string       g_counterFor = "\x01";  // what the counter line last logged
		Drag              g_drag = Drag::kNone;
		int               g_dragButton = 0;
		bool              g_dragFromGame = false;
		float             g_accTurn = 0.0f;
		float             g_accLift = 0.0f;
		float             g_accPan = 0.0f;
		float             g_accTilt = 0.0f;
		float             g_accCopyTurn = 0.0f;
		float             g_accAcross = 0.0f;
		float             g_accDepth = 0.0f;
		Clock::time_point g_dragSent{};
		std::uint32_t     g_drags = 0;
		std::uint32_t     g_gameDrags = 0;
		std::uint32_t     g_wheels = 0;

		// The game's mouse under the hybrid form (D310), written by the
		// backend's input sink and read once a frame by the stage. Button bits:
		// 1 left, 2 right.
		std::atomic_int g_gmDx = 0;
		std::atomic_int g_gmDy = 0;
		std::atomic_int g_gmDown = 0;
		std::atomic_int g_gmUp = 0;
		std::atomic_int g_gmWheel = 0;  // notches, up positive
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
				if (!a_s.hasInitial) {
					a_s.hasInitial = true;
					a_s.initial = a_s.v;
				}
			}
		}

		bool Changed(const Slider& a_s)
		{
			return a_s.has && a_s.hasInitial && std::abs(a_s.v - a_s.initial) > 1e-6f;
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

			// A rebuilt list keeps each row's baseline: it is the first number
			// the row ever received, and a new list of the same sliders is not
			// a new first number.
			std::unordered_map<std::string, float> baselines;
			for (const auto& s : g_sliders) {
				if (s.hasInitial) {
					baselines[s.k] = s.initial;
				}
			}

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
				if (const auto b = baselines.find(row.k); b != baselines.end()) {
					row.hasInitial = true;
					row.initial = b->second;
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

		// "In RaceMenu" (delegate.js): the fixed rows (D027), then a door for
		// every live category this build does not cover. Matched by label and
		// never by flag, as the page does (D082): a label that moves becomes a
		// door, never a duplicate. A keyed category has a tab of its own in
		// Sliders, so it gets no door.
		constexpr const char* kCovered[] = { "$ALL", "RACE", "BODY", "HEAD", "FACE", "EYES", "BROW", "MOUTH", "HAIR", "$EXTRA",
			"$EXPRESSIONS", "$COLORS", "$MAKEUP", "$BODY PAINT", "$HAND PAINT", "$FOOT PAINT", "$FACE PAINT", "$BODY SCALES" };

		void BuildDoors()
		{
			std::vector<Door> doors{ { "sculpt", "Sculpt", "Free-form editing of the head mesh." } };
			for (const auto& e : g_categories) {
				if (!e.is_object()) {
					continue;
				}
				const auto text = e.value("text", std::string());
				const auto key = e.value("key", std::string());
				if (text.empty() || !key.empty() ||
					std::find(std::begin(kCovered), std::end(kCovered), text) != std::end(kCovered)) {
					continue;
				}
				doors.push_back({ "category:" + text, text, "RaceMenu has this tab; the Lifecast does not build it." });
			}
			std::string list;
			for (const auto& d : doors) {
				list += (list.empty() ? "" : ", ") + d.key;
			}
			std::string before;
			for (const auto& d : g_doors) {
				before += (before.empty() ? "" : ", ") + d.key;
			}
			g_doors = std::move(doors);
			if (list != before) {
				spdlog::info("[smf] view: in-racemenu {} door(s) from {} categories: {}", g_doors.size(), g_categories.size(),
					list);
				Common::LayoutSoon();
			}
		}

		// stage.js: LAlt swaps far and near in standard, at most once in 400 ms.
		// Two sources in the view (D318): the game's input while the hybrid
		// owner is the game (SmfBackend's sink, as "key-lalt"), and ImGui while
		// the window holds the input. A press both hear is one swap.
		void LeftAlt(const char* a_from)
		{
			const auto now = Clock::now();
			const char* what = "ignored (the stage is not ready)";
			if (g_stage.ready && g_stage.free) {
				what = "ignored (free mode)";
			} else if (g_stage.ready && now - g_laltZoomAt < std::chrono::milliseconds(400)) {
				what = "ignored (within 400 ms of the last)";
			} else if (g_stage.ready) {
				g_laltZoomAt = now;
				Send("stage-zoom", { { "from", "dll-lalt" } });
				++g_zooms;
				what = "stage-zoom";
			}
			spdlog::info("[smf] view: LAlt from {} - {}", a_from, what);
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
			Control::Ask();
			Face::Ask();
			Skin::Ask();
			g_lookAfterFilm = false;
			g_shownAt = Clock::now();
			g_slidersSinceShown = 0;
			g_categoriesSinceShown = 0;
		}

		void AskLookAfterFilm(const char* a_what, std::size_t a_n)
		{
			if (g_lookAfterFilm || a_n == 0) {
				return;
			}
			g_lookAfterFilm = true;
			const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - g_shownAt).count();
			spdlog::info("[smf] view: RaceMenu's film arrived built ({} {}, {} ms after the view asked) - asking the look again",
				a_n, a_what, ms);
			Send("look-get");
		}

		// The card for the way back (card.js): text only, no input - the window
		// takes none (NoInputs) and the backend gives the input up while the
		// card is on (Panel::ShowCard), so the keyboard and the mouse stay with
		// RaceMenu. It answers "card-state" with what it drew, as the page does
		// from its DOM, on the second frame (the first has no size yet).
		void DrawCard(IG::ImGuiIO* a_io, bool a_holds)
		{
			const Theme::Scope theme(Theme::Kind::kCard);
			IG::SetNextWindowPos(IG::ImVec2{ a_io->DisplaySize.x * 0.5f, Theme::Px(24.0f) }, IG::ImGuiCond_Always,
				IG::ImVec2{ 0.5f, 0.0f });
			IG::Begin("##lifecast-card", nullptr,
				IG::ImGuiWindowFlags_NoDecoration | IG::ImGuiWindowFlags_NoInputs | IG::ImGuiWindowFlags_AlwaysAutoResize |
					IG::ImGuiWindowFlags_NoSavedSettings | IG::ImGuiWindowFlags_NoFocusOnAppearing);
			Theme::Body();
			Theme::LogWindow(Theme::Kind::kCard);
			IG::TextUnformatted("Press");
			IG::SameLine();
			IG::TextColored(Theme::Color::Accent(), "%s", g_cardKey.c_str());
			IG::SameLine();
			IG::TextUnformatted("to return to Lifecast");
			if (++g_cardFrames == 2) {
				const auto pos = IG::GetWindowPos();
				const auto size = IG::GetWindowSize();
				const auto text = fmt::format("Press {} to return to Lifecast", g_cardKey);
				Send("card-state", { { "on", true }, { "from", "smf-view" }, { "text", text }, { "holds", a_holds },
									   { "rect", { std::lround(pos.x), std::lround(pos.y), std::lround(size.x), std::lround(size.y) } },
									   { "viewport", { std::lround(a_io->DisplaySize.x), std::lround(a_io->DisplaySize.y) } } });
				spdlog::info("[smf] view: card drawn at {:.0f},{:.0f} {:.0f}x{:.0f} '{}' - the window holds the input: {}", pos.x,
					pos.y, size.x, size.y, text, a_holds);
			}
			IG::End();
		}

		// One button of the stage bar; disabled when a_off (at its limit, or
		// held by the comparison). True when clicked.
		bool StageButton(const char* a_label, const char* a_rect, bool a_off, const char* a_tip)
		{
			IG::BeginDisabled(a_off);
			const bool clicked = IG::Button(a_label);
			Rect(a_rect);
			IG::SetItemTooltip("%s", a_tip);
			IG::EndDisabled();
			return clicked;
		}

		void Bar()
		{
			IG::SameLine();
			IG::TextDisabled("|");
			IG::SameLine();
		}

		// The tuning knobs, only when a development DLL sends "tuning"
		// (stage.js renderTuning): one field per number, sent when it is left.
		void DrawTuning()
		{
			if (!g_stage.tuning.is_object()) {
				return;
			}
			IG::AlignTextToFramePadding();
			IG::TextDisabled("Tuning");
			for (const auto& [k, v] : g_stage.tuning.items()) {
				if (!v.is_number()) {
					continue;
				}
				IG::SameLine();
				IG::SetNextItemWidth(Theme::Px(64.0f));
				float f = v.get<float>();
				const std::string id = k + "##tune";
				IG::InputFloat(id.c_str(), &f, 0.0f, 0.0f, "%.1f");
				if (IG::IsItemDeactivatedAfterEdit() && std::isfinite(f)) {
					Send("stage-tune", { { k, f } });
				}
			}
			IG::SameLine();
			if (IG::Button("Reset all##tune") && g_stage.tuningDefaults.is_object()) {
				Send("stage-tune", g_stage.tuningDefaults);
			}
		}

		// The bar over the stage (stage.js, control.js): Character Control on
		// the first row (D130), the camera on the second, with undo and redo.
		void DrawStage()
		{
			Control::Draw(g_frameLeft, g_frameRight);
			if (!g_stage.ready) {
				IG::TextDisabled("Character view unavailable%s%s", g_stage.why.empty() ? "" : ": ", g_stage.why.c_str());
				History::DrawButtons();
				return;
			}
			const auto& s = g_stage;
			IG::AlignTextToFramePadding();
			IG::TextDisabled("Turn");
			IG::SameLine();
			if (StageButton("<##turn-left", "turn-left", false, "Turn left")) {
				Send("stage-move", { { "turn", -kTurnStep } });
			}
			IG::SameLine();
			if (StageButton(">##turn-right", "turn-right", false, "Turn right")) {
				Send("stage-move", { { "turn", kTurnStep } });
			}
			Bar();
			IG::TextDisabled("Camera");
			IG::SameLine();
			const std::string mode =
				std::string("Free camera: ") + (s.free ? "on" : "off") + (s.forced ? " (comparison)" : "") + "##mode";
			if (StageButton(mode.c_str(), "mode", s.forced, "Standard is RaceMenu's two positions; free is every move")) {
				Send("stage-mode", { { "free", !s.free } });
			}
			if (s.free) {
				IG::SameLine();
				const std::string lock = std::string("Lock on character: ") + (s.lock ? "on" : "off") + "##lock";
				if (StageButton(lock.c_str(), "lock", s.forced,
						"On, the camera keeps the character framed as she moves; off, it stays where you put it")) {
					Send("stage-lock", { { "on", !s.lock } });
				}
				Bar();
				IG::TextDisabled("Zoom");
				IG::SameLine();
				if (StageButton("+##closer", "closer", s.distance <= s.minDistance + kNear, "Closer")) {
					Send("stage-move", { { "distance", -kDistanceStep } });
				}
				IG::SameLine();
				if (StageButton("-##farther", "farther", s.distance >= s.maxDistance - kNear, "Farther")) {
					Send("stage-move", { { "distance", kDistanceStep } });
				}
				Bar();
				IG::TextDisabled("Height");
				IG::SameLine();
				if (StageButton("Up##lift-up", "lift-up", s.lift >= s.liftMax - kNear, "Camera up")) {
					Send("stage-move", { { "lift", kLiftStep } });
				}
				IG::SameLine();
				if (StageButton("Down##lift-down", "lift-down", s.lift <= s.liftMin + kNear, "Camera down")) {
					Send("stage-move", { { "lift", -kLiftStep } });
				}
				if (s.tilt) {
					Bar();
					IG::TextDisabled("Tilt");
					IG::SameLine();
					if (StageButton("Up##tilt-up", "tilt-up", s.lock || s.tiltDeg >= s.tiltMax - kNear, "Tilt one way")) {
						Send("stage-move", { { "tilt", kTiltStep } });
					}
					IG::SameLine();
					if (StageButton("Down##tilt-down", "tilt-down", s.lock || s.tiltDeg <= -s.tiltMax + kNear,
							"Tilt the other way")) {
						Send("stage-move", { { "tilt", -kTiltStep } });
					}
				}
				IG::SameLine();
				if (StageButton("Reframe##stage", "reset", false, "Back to the view the menu opened with")) {
					g_accTurn = g_accLift = g_accPan = g_accTilt = 0.0f;
					Send("stage-reset");
				}
			} else {
				IG::SameLine();
				if (StageButton("Zoom##stage", "zoom", false, "RaceMenu's own zoom, far and near (LAlt)")) {
					Send("stage-zoom", { { "from", "button" } });
					++g_zooms;
				}
			}
			IG::AlignTextToFramePadding();
			if (s.free) {
				const std::string tilt = s.tilt ? fmt::format("  |  tilt {:.0f} deg", s.tiltDeg) : std::string();
				IG::TextDisabled("turn %.0f deg  |  distance %.0f  |  height %.0f  |  pan %.0f%s", s.turn, s.distance, s.lift,
					s.pan, tilt.c_str());
			} else {
				IG::TextDisabled("turn %.0f deg  |  RaceMenu camera", s.turn);
			}
			Rect("readout");
			// Undo and redo (D130: on this bar, away from Finish on the belt; D303)
			Bar();
			History::DrawButtons();
			DrawTuning();
		}

		// Puts a slider back from the history: its value here, and the same
		// final "slider-set" the row sends at the end of a drag.
		void ApplySlider(const std::string& a_k, const json& a_v)
		{
			if (!a_v.is_number()) {
				return;
			}
			const float v = a_v.get<float>();
			if (const auto it = g_byKey.find(a_k); it != g_byKey.end()) {
				auto& s = g_sliders[it->second];
				s.v = v;
				s.has = true;
				s.active = false;
			}
			Send("slider-set", { { "k", a_k }, { "v", v }, { "final", true } });
		}

		void RecordSlider(const Slider& a_s, float a_before, float a_after)
		{
			const auto k = a_s.k;
			History::Record(a_s.label, a_before, a_after, [k](const json& a_v) { ApplySlider(k, a_v); });
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
			float       v = a_s.v;
			const float was = a_s.v;
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
			if (IG::IsItemActivated()) {
				a_s.before = was;
			}
			if (IG::IsItemDeactivatedAfterEdit()) {
				a_s.active = false;
				Send("slider-set", { { "k", a_s.k }, { "v", a_s.v }, { "final", true } });
				RecordSlider(a_s, a_s.before, a_s.v);
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
					const float typedWas = a_s.v;
					a_s.v = (std::clamp)(t, a_s.min, a_s.max);
					RecordSlider(a_s, typedWas, a_s.v);
					spdlog::info("[smf] typed {} for {} -> {}", a_s.typed, a_s.k, a_s.v);
					Send("slider-set", { { "k", a_s.k }, { "v", a_s.v }, { "final", true } });
				}
			}
			Rect("t:" + a_s.k);
			IG::PopID();
		}

		bool Chip(const char* a_label, bool a_on)
		{
			return Common::Chip(a_label, a_on);
		}

		bool ChipDraw(const char* a_label, bool a_on)
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

		bool Shows(const Slider& a_s, const std::string& a_find)
		{
			return (a_find.empty() || a_s.lower.find(a_find) != std::string::npos) && (!g_changedOnly || Changed(a_s));
		}

		// The counter (search.js): "N changed of M" with the filter on, "M of T
		// sliders" with a search, "T sliders in S sections" otherwise. It counts
		// the rows of Sliders, which is where the view's search and filter
		// look (D312). Logged once per change, with the rows a filter shows.
		std::string Counter(const std::string& a_find)
		{
			std::size_t total = 0;
			std::size_t sections = 0;
			std::size_t matched = 0;
			std::size_t changedMatched = 0;
			std::string shown;
			std::size_t named = 0;
			for (const auto& t : g_tabs) {
				if (t.rows.empty()) {
					continue;
				}
				++sections;
				for (const auto i : t.rows) {
					const auto& s = g_sliders[i];
					++total;
					if (!a_find.empty() && s.lower.find(a_find) == std::string::npos) {
						continue;
					}
					++matched;
					if (Changed(s)) {
						++changedMatched;
					}
					if ((!a_find.empty() || g_changedOnly) && Shows(s, a_find) && named < 12) {
						shown += fmt::format("{}'{}'", named++ ? ", " : "", s.label);
					}
				}
			}
			const auto text = g_changedOnly ? fmt::format("{} changed of {}", changedMatched, matched) :
			                  !a_find.empty() ? fmt::format("{} of {} sliders", matched, total) :
			                                    fmt::format("{} sliders in {} sections", total, sections);
			if (const auto key = text + "|" + a_find + "|" + shown; key != g_counterFor) {
				g_counterFor = key;
				spdlog::info("[smf] view: sliders counter '{}' (search '{}', changed filter {}){}{}", text, a_find,
					g_changedOnly ? "on" : "off", shown.empty() ? "" : " - rows shown: ", shown);
			}
			return text;
		}

		// The head of Sliders: the search and the "Changed" filter (D129's
		// finder), the counter, and the RaceMenu categories as chips that wrap.
		void DrawSlidersHead()
		{
			const auto* style = IG::GetStyle();
			const float changedW = IG::CalcTextSize("Changed").x + style->FramePadding.x * 2.0f;
			IG::SetNextItemWidth((std::max)(IG::GetContentRegionAvail().x - changedW - style->ItemSpacing.x, Theme::Px(60.0f)));
			if (IG::InputTextWithHint("##search", "Search sliders", g_search, sizeof(g_search),
					IG::ImGuiInputTextFlags_CallbackCharFilter, &DeadKeys::Filter)) {
				spdlog::info("[smf] search field '{}' hex [{}]", g_search, Hex(g_search));
			}
			Rect("search");
			IG::SameLine();
			if (ChipDraw("Changed###filter-changed", g_changedOnly)) {
				g_changedOnly = !g_changedOnly;
				spdlog::info("[smf] view: changed filter {}", g_changedOnly ? "on" : "off");
				Common::LayoutSoon();
			}
			Rect("filter-changed");
			IG::SetItemTooltip("Only the sliders that moved since the editor first read them");
			if (!g_slidersReady) {
				return;
			}
			IG::TextDisabled("%s", Counter(Lower(g_search)).c_str());
			Rect("row-counter");
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
				// A search or the filter looks through every category, as the
				// page's do.
				if (find.empty() && !g_changedOnly && id != g_tab) {
					continue;
				}
				for (const auto i : t.rows) {
					auto& s = g_sliders[i];
					if (!Shows(s, find)) {
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
			"RACE FACES###sec-faces", "SKIN & TINTS###sec-tints", "PAINT###sec-paint", "OBODY###sec-obody", "PRESETS###sec-presets" };
		// Settings (D132) is a section of the small panel too, opened from the
		// footer's button and not from the belt: kSections are the belt's.
		constexpr const char* kSectionTitles[] = { "Sliders", "Look", "Race & Sex", "Race Faces", "Skin & Tints", "Paint",
			"OBody", "Presets", "Settings" };
		constexpr const char* kSectionRects[] = { "sec:sliders", "sec:look", "sec:race", "sec:faces", "sec:tints",
			"sec:paint", "sec:obody", "sec:presets", "sec:settings" };
		constexpr int kSections = 8;
		constexpr int kSectionBody = 6;
		constexpr int kSectionPresets = 7;
		constexpr int kSectionSettings = 8;

		// The panel's size (panel.js): the bounds are a guard, the step the
		// page's.
		constexpr float kScaleMin = 0.8f;
		constexpr float kScaleMax = 2.5f;
		constexpr float kScaleStep = 0.1f;

		void OpenSection(int a_i, const char* a_from)
		{
			spdlog::info("[smf] view: section {} ({}){}", kSectionRects[a_i] + 4, a_from, g_section == a_i ? ", already open" : "");
			g_section = a_i;
			Common::LayoutSoon();
		}

		// The OBody section is there only while OBody answers with presets
		// (body.js); leaving it on when it goes falls back to the first one.
		bool SectionShown(int a_i)
		{
			return a_i != kSectionBody || Skin::BodyPresent();
		}

		void DrawCategories()
		{
			auto* dl = IG::GetWindowDrawList();
			if (!SectionShown(g_section)) {
				g_section = 0;
				Common::LayoutSoon();
			}
			for (int i = 0; i < kSections; ++i) {
				if (!SectionShown(i)) {
					continue;
				}
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

		// The always-visible buttons after the categories (belt.js QUICK, D126):
		// Presets and OBody open their section from anywhere, Sculpt hands over
		// to RaceMenu. OBody only while its section exists (D007).
		void DrawQuick()
		{
			struct Quick
			{
				const char* label;
				const char* rect;
				int         section;
				const char* send;
				const char* tip;
			};
			static constexpr Quick kQuick[] = {
				{ "Presets###quick-presets", "quick-presets", kSectionPresets, nullptr, nullptr },
				{ "OBody###quick-obody", "quick-obody", kSectionBody, nullptr, nullptr },
				{ "Sculpt###quick-sculpt", "quick-sculpt", -1, "sculpt", "Opens RaceMenu's Sculpt (F4 comes back)" },
			};
			IG::SameLine(0.0f, Theme::Px(10.0f));
			IG::TextDisabled("|");  // .belt-sep
			for (const auto& q : kQuick) {
				if (q.section >= 0 && !SectionShown(q.section)) {
					continue;
				}
				IG::SameLine(0.0f, Theme::Px(4.0f));
				const bool hit = IG::Button(q.label);
				Rect(q.rect);
				if (q.tip) {
					IG::SetItemTooltip("%s", q.tip);
				}
				if (!hit) {
					continue;
				}
				if (q.send) {
					spdlog::info("[smf] view: quick {} - show-racemenu for {}", q.rect + 6, q.send);
					Send("show-racemenu", { { "for", q.send } });
				} else {
					OpenSection(q.section, "quick button");
				}
			}
		}

		// The height "In RaceMenu" takes under Race & Sex: its heading and two
		// lines a door (the name with Open, the note).
		float DelegateHeight()
		{
			const float n = static_cast<float>(g_doors.size());
			return IG::GetTextLineHeightWithSpacing() * 2.0f +
			       n * (IG::GetFrameHeightWithSpacing() + IG::GetTextLineHeightWithSpacing());
		}

		// "In RaceMenu" (delegate.js): one row a door, Open hands over to
		// RaceMenu with the door's key (Switcher, RaceMenuTabs::Select). The web
		// panel keeps it in the Race category, beside the race picker
		// (groups.js), so here it is under Race & Sex.
		void DrawDelegate()
		{
			IG::Separator();
			Theme::Group("In RaceMenu");
			const auto* style = IG::GetStyle();
			const float openW = IG::CalcTextSize("Open").x + style->FramePadding.x * 2.0f;
			for (const auto& d : g_doors) {
				IG::PushID(d.key.c_str());
				IG::AlignTextToFramePadding();
				IG::TextUnformatted(d.label.c_str());
				IG::SameLine((std::max)(IG::GetContentRegionMax().x - openW, IG::GetCursorPosX()));
				if (IG::Button("Open")) {
					spdlog::info("[smf] view: in-racemenu open {} - show-racemenu", d.key);
					Send("show-racemenu", { { "for", d.key } });
				}
				Rect("door:" + d.key);
				IG::SetItemTooltip("Opens RaceMenu (F4 comes back)");
				IG::TextDisabled("%s", d.note.c_str());
				IG::PopID();
			}
		}

		// Settings (D132): Debug - the bridge and the versions (panel.js) - and
		// the panel's size. A bridge in trouble also marks the footer's button
		// (D133).
		bool BridgeTrouble(std::string& a_text)
		{
			if (g_helloed) {
				a_text = g_compatible ? "connected" : "version mismatch";
				return !g_compatible;
			}
			if (Clock::now() - g_helloAskedAt > std::chrono::seconds(3)) {
				a_text = "no answer";
				return true;
			}
			a_text = "connecting";
			return false;
		}

		void StepScale(int a_dir)
		{
			const float next = std::round((g_scaleShown + static_cast<float>(a_dir) * kScaleStep) / kScaleStep) * kScaleStep;
			const float to = std::round((std::clamp)(next, kScaleMin, kScaleMax) * 100.0f) / 100.0f;
			spdlog::info("[smf] view: panel scale asked {:.2f} -> {:.2f} (settings-set)", g_scaleShown, to);
			Send("settings-set", { { "key", "panelScale" }, { "value", static_cast<double>(to) } });
		}

		void DrawSettings()
		{
			std::string status;
			const bool  trouble = BridgeTrouble(status);
			Theme::Group("Debug");
			IG::TextDisabled("Bridge");
			IG::SameLine();
			IG::TextColored(trouble ? Theme::Color::Bad() : Theme::Color::Accent(), "%s", status.c_str());
			Rect("bridge-status");
			IG::TextDisabled("view %s / dll %s / protocol %u", Version::kString, g_dll.c_str(), static_cast<unsigned>(Bridge::kProtocolVersion));
			Rect("versions");
			IG::Separator();
			Theme::Group("Panel size");
			IG::BeginDisabled(g_scaleShown <= kScaleMin + 0.001f);
			if (IG::Button("-###scale-down")) {
				StepScale(-1);
			}
			Rect("scale-down");
			IG::SetItemTooltip("Smaller");
			IG::EndDisabled();
			IG::SameLine();
			IG::Text("%d%%", static_cast<int>(std::lround(g_scaleShown * 100.0f)));
			Rect("scale-value");
			IG::SameLine();
			IG::BeginDisabled(g_scaleShown >= kScaleMax - 0.001f);
			if (IG::Button("+###scale-up")) {
				StepScale(1);
			}
			Rect("scale-up");
			IG::SetItemTooltip("Bigger");
			IG::EndDisabled();
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
				Face::DrawRaceSex(DelegateHeight());
				DrawDelegate();
				break;
			case kSectionSettings:
				DrawSettings();
				break;
			case 3:
				Face::DrawRacePresets();
				break;
			case 4:
				Skin::DrawTints();
				break;
			case 5:
				Skin::DrawPaint();
				break;
			case 6:
				Skin::DrawBody();
				break;
			default:
				Presets::DrawSection();
				break;
			}
		}

		// stage.js pointerdown: what a press with this button and these keys
		// drags. a_button: 0 left, 1 right.
		Drag PickDrag(int a_button, bool a_shift, bool a_ctrl)
		{
			if (!g_stage.ready || !g_stageVisible) {
				return Drag::kNone;
			}
			if (Control::CompareOn() && a_shift && !a_ctrl) {
				return a_button == 0 ? Drag::kCopy : Drag::kCopyMove;
			}
			if (!g_stage.free) {
				return a_button == 0 && !a_ctrl ? Drag::kTurn : Drag::kNone;
			}
			if (a_button == 1) {
				return Drag::kLift;
			}
			return a_ctrl ? Drag::kTilt : Drag::kTurn;
		}

		const char* DragName(Drag a_d)
		{
			switch (a_d) {
			case Drag::kTurn:
				return "turn";
			case Drag::kLift:
				return "lift";
			case Drag::kTilt:
				return "tilt";
			case Drag::kCopy:
				return "copy";
			case Drag::kCopyMove:
				return "copy-move";
			default:
				return "none";
			}
		}

		void DragStart(int a_button, bool a_shift, bool a_ctrl, bool a_fromGame)
		{
			const Drag d = PickDrag(a_button, a_shift, a_ctrl);
			if (d == Drag::kNone) {
				return;
			}
			g_drag = d;
			g_dragButton = a_button;
			g_dragFromGame = a_fromGame;
			g_dragSent = Clock::now();
			++g_drags;
			if (a_fromGame) {
				++g_gameDrags;
			}
			spdlog::info("[smf] view: stage drag {} (button {}, shift {}, ctrl {}, from {})", DragName(d), a_button, a_shift,
				a_ctrl, a_fromGame ? "the game's input" : "the window");
		}

		// stage.js pointermove: pixels into what the drag moves (move, moveCopy,
		// moveCopyPlace), gathered until the next send.
		void DragMove(float a_dx, float a_dy)
		{
			switch (g_drag) {
			case Drag::kTurn:
				g_accTurn += a_dx * kTurnPerPx;
				break;
			case Drag::kLift:
				// the view moves with the cursor, so the camera the other way (E4, Fase 6.5)
				g_accLift += a_dy * kLiftPerPx;
				g_accPan += -a_dx * kPanPerPx;
				break;
			case Drag::kTilt:
				// with the lock on the DLL holds the head's height by tilt, so a hand tilt would be taken back
				if (g_stage.tilt && !g_stage.lock) {
					g_accTilt += -a_dy * kTiltPerPx;
				}
				break;
			case Drag::kCopy:
				// the copy's angle is the other way round on screen from the character's
				g_accCopyTurn -= a_dx * kTurnPerPx;
				break;
			case Drag::kCopyMove:
				g_accAcross += a_dx * kPanPerPx;
				g_accDepth += -a_dy * kPanPerPx;
				break;
			default:
				break;
			}
		}

		// stage.js flush: one message of each kind that moved.
		void DragFlush()
		{
			if (g_accCopyTurn != 0.0f) {
				Send("compare-turn", { { "deg", (std::clamp)(g_accCopyTurn, -180.0f, 180.0f) } });
				g_accCopyTurn = 0.0f;
			}
			if (g_accAcross != 0.0f || g_accDepth != 0.0f) {
				Send("compare-move", { { "across", g_accAcross }, { "depth", g_accDepth } });
				g_accAcross = g_accDepth = 0.0f;
			}
			json m = json::object();
			const std::pair<const char*, float*> keys[] = { { "turn", &g_accTurn }, { "lift", &g_accLift },
				{ "pan", &g_accPan }, { "tilt", &g_accTilt } };
			for (const auto& [k, v] : keys) {
				if (*v != 0.0f) {
					m[k] = *v;
					*v = 0.0f;
				}
			}
			if (!m.empty()) {
				Send("stage-move", m);
			}
			g_dragSent = Clock::now();
		}

		void DragTick(bool a_up)
		{
			if (a_up || Clock::now() - g_dragSent >= kSendEvery) {
				DragFlush();
			}
			if (a_up) {
				g_drag = Drag::kNone;
			}
		}

		// The wheel in the stage: the distance in free, RaceMenu's zoom in
		// standard (one toggle per gesture: its notches arrive over several
		// frames, and the swap takes longer than that; D290).
		void Wheel(float a_notches)
		{
			if (a_notches == 0.0f || !g_stage.ready || !g_stageVisible) {
				return;
			}
			++g_wheels;
			if (g_stage.free) {
				Send("stage-move", { { "distance", -a_notches * kDistanceStep } });
				return;
			}
			const auto now = Clock::now();
			if (now - g_wheelZoomAt >= std::chrono::milliseconds(450)) {
				Send("stage-zoom", { { "from", "wheel" } });
				++g_zooms;
			}
			g_wheelZoomAt = now;
		}

		bool KeyHeld(int a_vk)
		{
			return (::GetAsyncKeyState(a_vk) & 0x8000) != 0;
		}

		// The stage's drags and wheel (D310). Under "all" the window holds the
		// input and the stage reads ImGui; under "hybrid" a press outside the
		// windows goes to the game, and the stage reads what the backend's
		// input sink heard - the owner stays the game (D291).
		void StageInput(IG::ImGuiIO* a_io, bool a_holds)
		{
			const int dx = g_gmDx.exchange(0);
			const int dy = g_gmDy.exchange(0);
			const int down = g_gmDown.exchange(0);
			const int up = g_gmUp.exchange(0);
			const int wheel = g_gmWheel.exchange(0);
			if (g_hybrid) {
				if (g_drag != Drag::kNone && !g_dragFromGame) {
					// a drag the window started, ended by the form changing under it
					DragTick(true);
				}
				if (g_drag == Drag::kNone && (down & 3) != 0) {
					DragStart((down & 1) ? 0 : 1, KeyHeld(VK_SHIFT), KeyHeld(VK_CONTROL), true);
				}
				if (g_drag != Drag::kNone) {
					DragMove(static_cast<float>(dx), static_cast<float>(dy));
					DragTick((up & (1 << g_dragButton)) != 0);
				}
				Wheel(static_cast<float>(wheel));
				return;
			}
			if (!a_holds) {
				if (g_drag != Drag::kNone) {
					DragTick(true);
				}
				return;
			}
			const bool over = IG::IsWindowHovered(IG::ImGuiHoveredFlags_AnyWindow);
			if (g_drag == Drag::kNone && !over) {
				for (int b = 0; b < 2; ++b) {
					if (IG::IsMouseClicked(b)) {
						DragStart(b, a_io->KeyShift, a_io->KeyCtrl, false);
						break;
					}
				}
			}
			if (g_drag != Drag::kNone) {
				DragMove(a_io->MouseDelta.x, a_io->MouseDelta.y);
				DragTick(!IG::IsMouseDown(g_dragButton));
			}
			if (!over) {
				Wheel(a_io->MouseWheel);
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
			if (b.y > g_clipBottom && !g_inPopup) {
				return;
			}
			g_layout.push_back(fmt::format("{} {:.0f},{:.0f},{:.0f},{:.0f}", a_name, a.x, a.y, b.x, b.y));
		}

		void Popup(const char* a_name)
		{
			if (IG::IsWindowAppearing()) {
				LayoutSoon();
			}
			g_popup = WindowBox(a_name);
			g_havePopup = true;
			g_inPopup = true;
		}

		void PopupEnd()
		{
			g_inPopup = false;
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

		bool Chip(const char* a_label, bool a_on)
		{
			return ChipDraw(a_label, a_on);
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
			Presets::OnSettings(data);
			const bool hybrid = data.value("smfInput", std::string("hybrid")) == "hybrid";
			if (hybrid != g_hybrid) {
				spdlog::info("[smf] view: input form {}", hybrid ? "hybrid" : "all");
			}
			g_hybrid = hybrid;
			// The page's own size (--panel-scale, 0.8 to 2.5); 1 when the settings do not carry it.
			const auto  sc = data.find("panelScale");
			const float scale = sc != data.end() && sc->is_number() ? sc->get<float>() : 1.0f;
			g_scaleShown = scale;
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
			if (!v && g_visible) {
				// history.js: nothing undoes across the panel being hidden
				History::Clear("the panel was hidden");
			}
			g_visible = v;
		} else if (type == "card") {
			const bool on = data.value("on", false);
			if (on && !g_card) {
				g_cardFrames = 0;
			}
			g_card = on;
			if (data.contains("key") && data["key"].is_string()) {
				g_cardKey = data["key"].get<std::string>();
			}
		} else if (type == "sliders") {
			OnSliders(data);
			if (g_visible && ++g_slidersSinceShown > 1 && g_slidersReady) {
				AskLookAfterFilm("sliders", g_sliders.size());
			}
		} else if (type == "slider-values") {
			if (data.contains("values") && data["values"].is_object()) {
				for (const auto& [k, v] : data["values"].items()) {
					if (const auto it = g_byKey.find(k); it != g_byKey.end()) {
						SetValue(g_sliders[it->second], v);
					}
				}
			}
		} else if (type == "racemenu-categories") {
			++g_categoriesSinceShown;
			if (data.value("ready", false) && data.contains("entries") && data["entries"].is_array()) {
				g_categories = data["entries"];
				ApplyOrder();
				BuildDoors();
				if (g_visible && g_categoriesSinceShown > 1) {
					AskLookAfterFilm("RaceMenu categories", g_categories.size());
				}
			}
		} else if (type == "stage") {
			const auto num = [](const json& a_o, const char* a_k) {
				const auto it = a_o.is_object() ? a_o.find(a_k) : a_o.end();
				return it != a_o.end() && it->is_number() ? it->get<double>() : 0.0;
			};
			const auto flag = [&data](const char* a_k, bool a_fallback) {
				const auto it = data.is_object() ? data.find(a_k) : data.end();
				return it != data.end() && it->is_boolean() ? it->get<bool>() : a_fallback;
			};
			auto&      s = g_stage;
			const auto shape = std::make_tuple(s.ready, s.free, s.forced, s.lock, s.tilt);
			s.ready = flag("ready", false);
			s.why = data.value("why", std::string());
			s.free = flag("free", false);
			s.forced = flag("forced", false);
			s.lock = flag("lock", true);
			s.tilt = flag("tilt", false);
			s.turn = num(data, "turn");
			s.distance = num(data, "distance");
			s.lift = num(data, "lift");
			s.pan = num(data, "pan");
			s.tiltDeg = num(data, "tiltDeg");
			const json lim = data.contains("limits") ? data["limits"] : json::object();
			s.minDistance = num(lim, "minDistance");
			s.maxDistance = num(lim, "maxDistance");
			s.liftMin = num(lim, "liftMin");
			s.liftMax = num(lim, "liftMax");
			s.tiltMax = num(lim, "tiltMax");
			s.tuning = data.contains("tuning") ? data["tuning"] : json();
			s.tuningDefaults = data.contains("tuningDefaults") ? data["tuningDefaults"] : json();
			if (shape != std::make_tuple(s.ready, s.free, s.forced, s.lock, s.tilt)) {
				Common::LayoutSoon();
			}
		} else if (type == "stage-visible") {
			g_stageVisible = data.value("on", true);
		} else if (type == "key-lalt") {
			LeftAlt("the game's input");
		} else if (!Control::Receive(type, data) && !Face::Receive(type, data) && !Skin::Receive(type, data) &&
				   !Presets::Receive(type, data)) {
			++g_ignored;
		}
		// A list stamp is answered within seconds (Core/ListSig.cpp); the
		// answer must not wait for the window to draw again.
		Flush();
	}

	void Open()
	{
		{
			std::lock_guard l(g_mx);
			g_helloAskedAt = Clock::now();
			BuildDoors();
		}
		Send("hello",{ { "page", fmt::format("smf-view {}", Version::kString) }, { "protocol", Bridge::kProtocolVersion } });
		AskAll();
		// Asked once, as the page does when it mounts: the DLL sends the list
		// and the name again each time the panel comes on screen.
		Presets::Ask();
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
			// RaceMenu's film is in front and has the mouse: what the game's
			// input heard meanwhile is RaceMenu's, not a drag for the stage.
			g_gmDx = 0;
			g_gmDy = 0;
			g_gmDown = 0;
			g_gmUp = 0;
			g_gmWheel = 0;
			if (g_drag != Drag::kNone) {
				DragTick(true);
			}
			DrawCard(io, a_holds);
			return f;
		}
		// The press, as an edge of the key or of the Alt modifier (the
		// framework may feed only the modifier); AltGr is Ctrl + Alt and is
		// not LAlt.
		{
			static bool s_alt = false;
			const bool  alt = IG::IsKeyDown(IG::ImGuiKey_LeftAlt) || (io->KeyAlt && !io->KeyCtrl);
			if (a_holds && alt && !s_alt) {
				LeftAlt("ImGui");
			}
			s_alt = alt;
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
		// The fixed windows in whole pixels: ImGui truncates a window's position
		// and size, so a belt 57.6 px tall (scale 1.2) ended at 1439 and the
		// last line of the screen - where a thrown cursor stops - was in no
		// window (D319).
		const float gap = Theme::Px(8.0f);                                            // --gap
		const float beltH = std::round(Theme::Px(48.0f));                             // --belt-height
		const float col = std::round((std::min)(Theme::Px(430.0f), dw * 0.34f));      // --panel-col
		const float panelH = std::round((std::min)(dh * 0.64f, dh - beltH - gap));    // --panel-tall, the panel always open
		const float beltY = dh - beltH;
		const float stageL = col + 2.0f * gap;
		const float stageT = 2.0f * gap;
		const float stageR = dw - 2.0f * gap;
		const float stageB = beltY - 2.0f * gap;
		const auto  border = IG::ColorConvertFloat4ToU32(Theme::Color::PanelBorder());
		g_layout.clear();
		g_havePopup = false;

		// The stage: a frame only (.stage), on the background list - under every window.
		if (g_stageVisible) {
			IG::ImDrawListManager::AddRect(IG::GetBackgroundDrawList(), IG::ImVec2{ stageL, stageT },
				IG::ImVec2{ stageR, stageB }, IG::ColorConvertFloat4ToU32(Theme::Color::StageBorder()), 6.0f, 0, 1.0f);
		}

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
		// the name and Finish (D129), the footer.
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
			DrawQuick();
			const float catsEnd = IG::GetItemRectMax().x + pad;
			IG::ImDrawListManager::AddLine(dl, IG::ImVec2{ catsEnd - 0.5f, b.y0 }, IG::ImVec2{ catsEnd - 0.5f, b.y1 }, border, 1.0f);

			// .panel-footer: the input form (D291), Settings (D132) and the way
			// back to RaceMenu.
			const auto* style = IG::GetStyle();
			const float radio = IG::GetFrameHeight() + style->ItemInnerSpacing.x;
			const float settingsW = IG::CalcTextSize("Settings").x + style->FramePadding.x * 2.0f;
			const float footW = IG::CalcTextSize("Input").x + radio * 2.0f + IG::CalcTextSize("All").x +
			                    IG::CalcTextSize("Hybrid").x + settingsW + IG::CalcTextSize("F4  RaceMenu").x +
			                    style->ItemSpacing.x * 5.0f;
			const float footX = dw - footW - pad;
			IG::ImDrawListManager::AddLine(dl, IG::ImVec2{ footX - pad + 0.5f, b.y0 }, IG::ImVec2{ footX - pad + 0.5f, b.y1 }, border,
				1.0f);
			// .belt-actions: the name and Finish (finish.js), between the categories and the footer
			IG::SetCursorScreenPos(IG::ImVec2{ catsEnd + pad, rowY });
			Presets::DrawFinish((std::max)(footX - pad - catsEnd - 2.0f * pad, 0.0f));
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
			std::string bridge;
			const bool  trouble = BridgeTrouble(bridge);
			if (trouble) {
				IG::PushStyleColor(IG::ImGuiCol_Text, Theme::Color::Bad());
			}
			const bool settingsHit = IG::Button("Settings###settings-button");
			if (trouble) {
				IG::PopStyleColor();
				IG::SetItemTooltip("Bridge: %s", bridge.c_str());
			}
			Rect("settings-button");
			if (settingsHit) {
				OpenSection(kSectionSettings, "footer button");
			}
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
		if (g_havePopup) {
			f.boxes[f.count++] = g_popup;
		}

		f.wantText = io->WantTextInput;
		f.drawn = true;
		// undokeys.js: Ctrl+Z, Ctrl+Y, Ctrl+Shift+Z - the text fields keep theirs
		History::Keys(io->WantTextInput);
		g_lastLayout = fmt::format("window {:.0f},{:.0f},{:.0f},{:.0f}; {}; {}; stage {:.0f},{:.0f},{:.0f},{:.0f}", f.boxes[0].x0,
			f.boxes[0].y0, f.boxes[0].x1, f.boxes[0].y1, BoxText(f.boxes[1]), BoxText(f.boxes[2]), stageL, stageT, stageR, stageB);
		if (g_havePopup) {
			g_lastLayout += "; " + BoxText(g_popup);
		}
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

	void GameMouseButton(std::uint32_t a_id, bool a_down)
	{
		if (a_id == 8 || a_id == 9) {
			if (a_down) {
				g_gmWheel += a_id == 8 ? 1 : -1;
			}
			return;
		}
		if (a_id > 1) {
			return;
		}
		(a_down ? g_gmDown : g_gmUp) |= 1 << a_id;
	}

	void GameMouseMove(int a_dx, int a_dy)
	{
		g_gmDx += a_dx;
		g_gmDy += a_dy;
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
		spdlog::info("[smf] view stage ({}): ready {}, free {}, forced {}, lock {}, tilt {} ({:.1f} deg), turn {:.1f}, "
					 "distance {:.1f}, lift {:.1f}, pan {:.1f}, visible {}, {} drag(s) ({} from the game's input), {} wheel(s)",
			a_what, g_stage.ready, g_stage.free, g_stage.forced, g_stage.lock, g_stage.tilt, g_stage.tiltDeg, g_stage.turn,
			g_stage.distance, g_stage.lift, g_stage.pan, g_stageVisible, g_drags, g_gameDrags, g_wheels);
		Control::LogState(a_what);
		Face::LogState(a_what);
		Skin::LogState(a_what);
		Presets::LogState(a_what);
		History::LogState(a_what);
		spdlog::info("[smf] view layout: {}", g_lastLayout.empty() ? std::string("not drawn yet") : g_lastLayout);
	}
}
