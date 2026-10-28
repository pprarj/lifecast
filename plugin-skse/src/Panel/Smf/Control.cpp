// Control.cpp
// Lifecast

#include "Panel/Smf/Control.h"

#include "Panel/Smf/Common.h"
#include "Panel/Smf/Theme.h"

#include "SKSEMenuFramework/SKSEMenuFramework.h"

#include <string>
#include <vector>

namespace Lifecast::Panel::Smf::Control
{
	namespace
	{
		namespace IG = ImGuiMCP;
		using json = nlohmann::json;
		using Common::Rect;
		using Common::Send;

		struct Toggle
		{
			bool on = false;    // what the DLL last said
			bool busy = false;  // a "-set" went out and has not been answered
		};

		struct Pose
		{
			std::string event;
			std::string label;
		};

		Toggle            g_undress;
		Toggle            g_light{ true, false };  // control.js starts it lit
		Toggle            g_freeze;
		Toggle            g_compare;
		bool              g_compareAvailable = false;  // hidden until the DLL says the figure will draw (control.js)
		std::string       g_compareWhy;
		std::vector<Pose> g_poses;
		std::string       g_poseShown;  // the event the list shows; empty is "Pose"
		std::string       g_poseWhy;
		std::uint32_t     g_answers = 0;

		bool Flag(const json& a_data, const char* a_key, bool a_fallback)
		{
			const auto it = a_data.is_object() ? a_data.find(a_key) : a_data.end();
			return it != a_data.end() && it->is_boolean() ? it->get<bool>() : a_fallback;
		}

		std::string Text(const json& a_data, const char* a_key)
		{
			const auto it = a_data.is_object() ? a_data.find(a_key) : a_data.end();
			return it != a_data.end() && it->is_string() ? it->get<std::string>() : std::string();
		}

		// One toggle button: the label from the DLL's last word, disabled while
		// a click waits for its answer.
		void DrawToggle(Toggle& a_t, const char* a_offLabel, const char* a_onLabel, const char* a_id, const char* a_type)
		{
			IG::BeginDisabled(a_t.busy);
			const std::string label = std::string(a_t.on ? a_onLabel : a_offLabel) + "##" + a_id;
			if (IG::Button(label.c_str())) {
				a_t.busy = true;
				Send(a_type, { { "on", !a_t.on } });
			}
			Rect(a_id);
			IG::EndDisabled();
		}

		json Frame(float a_left, float a_right)
		{
			json m = json::object();
			if (a_left >= 0.0f && a_right > a_left) {
				m["frame"] = { { "left", a_left }, { "right", a_right } };
			}
			return m;
		}

		const char* PoseLabel()
		{
			for (const auto& p : g_poses) {
				if (p.event == g_poseShown) {
					return p.label.c_str();
				}
			}
			return "Pose";
		}
	}

	bool Receive(const std::string& a_type, const json& a_data)
	{
		if (a_type == "undress") {
			g_undress = { Flag(a_data, "on", false), false };
			spdlog::info("[smf] view control: undress on {} - the button reads '{}'", g_undress.on, g_undress.on ? "Dress" : "Undress");
		} else if (a_type == "light") {
			g_light = { Flag(a_data, "on", true), false };
			spdlog::info("[smf] view control: light on {} - the button reads '{}'", g_light.on, g_light.on ? "Light Off" : "Light On");
		} else if (a_type == "freeze") {
			g_freeze = { Flag(a_data, "on", false), false };
			spdlog::info("[smf] view control: freeze on {} - the button reads '{}'", g_freeze.on, g_freeze.on ? "Unfreeze" : "Freeze");
		} else if (a_type == "compare") {
			g_compare = { Flag(a_data, "on", false), false };
			g_compareAvailable = Flag(a_data, "available", false);
			// The DLL's own words when it refused; empty when it did not, so a
			// toggle that went through clears the last refusal.
			g_compareWhy = Text(a_data, "why");
			spdlog::info("[smf] view control: compare on {}, available {} - the button reads '{}'{}{}", g_compare.on,
				g_compareAvailable, !g_compareAvailable ? "(hidden)" : g_compare.on ? "Compare Off" : "Compare",
				g_compareWhy.empty() ? "" : ", the note reads: ", g_compareWhy);
		} else if (a_type == "poses") {
			g_poses.clear();
			if (a_data.is_object() && a_data.contains("poses") && a_data["poses"].is_array()) {
				for (const auto& p : a_data["poses"]) {
					if (p.is_object()) {
						g_poses.push_back({ Text(p, "event"), Text(p, "label") });
					}
				}
			}
			spdlog::info("[smf] view: {} pose(s) in the list", g_poses.size());
		} else if (a_type == "pose") {
			const bool refused = Flag(a_data, "accepted", true) == false;
			g_poseWhy = refused ? Text(a_data, "why") : std::string();
			if (refused) {
				g_poseShown.clear();
			}
		} else {
			return false;
		}
		// the labels change the stage bar's width: the bench reads where things went
		Common::LayoutSoon();
		++g_answers;
		return true;
	}

	void Ask()
	{
		Send("undress-get");
		Send("light-get");
		Send("freeze-get");
		Send("pose-list");
		Send("compare-get");
	}

	void Draw(float a_left, float a_right)
	{
		IG::AlignTextToFramePadding();
		IG::TextDisabled("Character");
		IG::SameLine();
		DrawToggle(g_undress, "Undress", "Dress", "undress", "undress-set");
		IG::SameLine();
		DrawToggle(g_light, "Light On", "Light Off", "light", "light-set");
		IG::SameLine();
		DrawToggle(g_freeze, "Freeze", "Unfreeze", "freeze", "freeze-set");
		IG::SameLine();
		IG::SetNextItemWidth(Theme::Px(150.0f));
		if (IG::BeginCombo("##pose", PoseLabel())) {
			Common::Popup("poselist");
			int i = 0;
			for (const auto& p : g_poses) {
				IG::PushID(p.event.c_str());
				if (IG::Selectable(p.label.c_str(), p.event == g_poseShown)) {
					g_poseShown = p.event;
					g_poseWhy.clear();
					Send("pose-play", { { "event", p.event } });
				}
				Rect(fmt::format("pose:{}", ++i));
				IG::PopID();
			}
			Common::PopupEnd();
			IG::EndCombo();
		}
		Rect("pose");
		IG::SameLine();
		if (IG::Button("Stop pose")) {
			g_poseShown.clear();
			g_poseWhy.clear();
			Send("pose-play", { { "event", "" } });
		}
		Rect("pose-stop");
		if (g_compareAvailable) {
			IG::SameLine();
			IG::TextDisabled("|");
			IG::SameLine();
			IG::BeginDisabled(g_compare.busy);
			if (IG::Button(g_compare.on ? "Compare Off##compare" : "Compare##compare")) {
				g_compare.busy = true;
				g_compareWhy.clear();
				auto m = Frame(a_left, a_right);
				m["on"] = !g_compare.on;
				Send("compare-set", m);
			}
			Rect("compare");
			IG::EndDisabled();
			IG::SameLine();
			// Only with the comparison on: it makes the copy again from the
			// character as she is now.
			IG::BeginDisabled(g_compare.busy || !g_compare.on);
			if (IG::Button("Update copy")) {
				g_compare.busy = true;
				g_compareWhy.clear();
				Send("compare-update", Frame(a_left, a_right));
			}
			Rect("compare-update");
			IG::EndDisabled();
		}
		if (g_compareAvailable && !g_compareWhy.empty()) {
			IG::TextColored(Theme::Color::Warn(), "%s", g_compareWhy.c_str());
			Rect("compare-why");
		}
		if (!g_poseWhy.empty()) {
			IG::TextColored(Theme::Color::Warn(), "%s", g_poseWhy.c_str());
			Rect("pose-why");
		}
	}

	bool CompareOn()
	{
		return g_compare.on;
	}

	void LogState(const char* a_what)
	{
		spdlog::info("[smf] view control ({}): undress {}, light {}, freeze {}, compare {} (available {}, busy {}, why '{}'), "
					 "{} pose(s), shown '{}', {} answer(s)",
			a_what, g_undress.on, g_light.on, g_freeze.on, g_compare.on, g_compareAvailable, g_compare.busy, g_compareWhy,
			g_poses.size(), g_poseShown, g_answers);
	}
}
