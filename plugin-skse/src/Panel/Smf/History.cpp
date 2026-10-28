// History.cpp
// Lifecast

#include "Panel/Smf/History.h"

#include "Panel/Smf/Common.h"
#include "Panel/Smf/Theme.h"

#include "SKSEMenuFramework/SKSEMenuFramework.h"

#include <deque>

namespace Lifecast::Panel::Smf::History
{
	namespace
	{
		namespace IG = ImGuiMCP;
		using json = nlohmann::json;

		// Entries kept: well past one sitting; the cap only bounds a long session.
		constexpr std::size_t kLimit = 100;

		struct Entry
		{
			std::string label;
			json        before;
			json        after;
			Apply       apply;
		};

		// entries[0 .. at-1] are done; entries[at ..] were undone and can be
		// redone. A new entry throws the second half away.
		std::deque<Entry> g_entries;
		std::size_t       g_at = 0;

		// While an entry is applied, its own send must not record itself.
		bool g_applying = false;

		std::uint32_t g_recorded = 0;
		std::uint32_t g_undone = 0;
		std::uint32_t g_redone = 0;
		std::uint32_t g_cleared = 0;

		// The keys, by state: down last frame.
		bool g_zDown = false;
		bool g_yDown = false;

		std::string Shown(const json& a_v)
		{
			auto s = a_v.dump();
			return s.size() > 80 ? s.substr(0, 77) + "..." : s;
		}

		void Note(const char* a_what, const Entry* a_e, const json* a_to, const char* a_from)
		{
			if (a_e) {
				spdlog::info("[smf] history {} '{}' -> {} (at {} of {}){}{}", a_what, a_e->label, a_to ? Shown(*a_to) : "",
					g_at, g_entries.size(), a_from ? ", from " : "", a_from ? a_from : "");
			} else {
				spdlog::info("[smf] history {} (at {} of {}){}{}", a_what, g_at, g_entries.size(), a_from ? ", from " : "",
					a_from ? a_from : "");
			}
		}

		void Run(const Entry& a_e, const json& a_v)
		{
			g_applying = true;
			try {
				a_e.apply(a_v);
			} catch (const std::exception& e) {
				spdlog::error("[smf] history: putting '{}' back threw: {}", a_e.label, e.what());
			}
			g_applying = false;
		}

		bool Pressed(int a_vk)
		{
			return (::GetAsyncKeyState(a_vk) & 0x8000) != 0;
		}

		bool GameInFront()
		{
			const HWND fg = ::GetForegroundWindow();
			DWORD      pid = 0;
			if (fg) {
				::GetWindowThreadProcessId(fg, &pid);
			}
			return pid == ::GetCurrentProcessId();
		}

		void Report(char a_letter, const char* a_what, bool a_ctrl, bool a_shift, bool a_alt)
		{
			Common::Send("undo-key", { { "l", std::string(1, a_letter) }, { "what", a_what }, { "ctrl", a_ctrl },
										 { "sh", a_shift }, { "alt", a_alt }, { "src", "smf-view" } });
		}
	}

	bool Record(std::string a_label, json a_before, json a_after, Apply a_apply)
	{
		if (g_applying || !a_apply || a_before.is_null() || a_before == a_after) {
			return false;
		}
		g_entries.resize(g_at);
		g_entries.push_back({ std::move(a_label), std::move(a_before), std::move(a_after), std::move(a_apply) });
		if (g_entries.size() > kLimit) {
			g_entries.pop_front();
		}
		g_at = g_entries.size();
		++g_recorded;
		const auto& e = g_entries.back();
		spdlog::info("[smf] history record '{}' {} -> {} (at {} of {})", e.label, Shown(e.before), Shown(e.after), g_at,
			g_entries.size());
		return true;
	}

	bool Undo(const char* a_from)
	{
		if (g_at == 0) {
			Note("undo-empty", nullptr, nullptr, a_from);
			return false;
		}
		const auto& e = g_entries[--g_at];
		Run(e, e.before);
		++g_undone;
		Note("undo", &e, &e.before, a_from);
		return true;
	}

	bool Redo(const char* a_from)
	{
		if (g_at >= g_entries.size()) {
			Note("redo-empty", nullptr, nullptr, a_from);
			return false;
		}
		const auto& e = g_entries[g_at++];
		Run(e, e.after);
		++g_redone;
		Note("redo", &e, &e.after, a_from);
		return true;
	}

	bool UndoAll(const char* a_from)
	{
		if (g_at == 0) {
			Note("undo-all-empty", nullptr, nullptr, a_from);
			return false;
		}
		const auto steps = g_at;
		while (g_at > 0) {
			const auto& e = g_entries[--g_at];
			Run(e, e.before);
			++g_undone;
			Note("undo", &e, &e.before, a_from);
		}
		spdlog::info("[smf] history undo-all: {} steps", steps);
		return true;
	}

	void Clear(const char* a_why)
	{
		if (g_entries.empty()) {
			return;
		}
		g_entries.clear();
		g_at = 0;
		++g_cleared;
		spdlog::info("[smf] history clear: {}", a_why);
	}

	void DrawButtons()
	{
		IG::BeginDisabled(g_at == 0);
		if (IG::Button("Undo##history")) {
			Undo("button");
		}
		Common::Rect("undo");
		if (g_at > 0) {
			IG::SetItemTooltip("Undo %s (Ctrl+Z)", g_entries[g_at - 1].label.c_str());
		}
		IG::EndDisabled();
		IG::SameLine();
		IG::BeginDisabled(g_at >= g_entries.size());
		if (IG::Button("Redo##history")) {
			Redo("button");
		}
		Common::Rect("redo");
		if (g_at < g_entries.size()) {
			IG::SetItemTooltip("Redo %s (Ctrl+Y)", g_entries[g_at].label.c_str());
		}
		IG::EndDisabled();
		IG::SameLine();
		IG::BeginDisabled(g_at == 0);
		if (IG::Button("Undo all##history")) {
			UndoAll("button");
		}
		Common::Rect("undoall");
		IG::EndDisabled();
	}

	void Keys(bool a_textField)
	{
		const bool z = Pressed('Z');
		const bool y = Pressed('Y');
		const bool zPress = z && !g_zDown;
		const bool yPress = y && !g_yDown;
		g_zDown = z;
		g_yDown = y;
		if (!zPress && !yPress) {
			return;
		}
		if (!GameInFront()) {
			return;
		}
		const bool ctrl = Pressed(VK_CONTROL);
		const bool shift = Pressed(VK_SHIFT);
		const bool alt = Pressed(VK_MENU);
		const char letter = zPress ? 'z' : 'y';
		if (!ctrl) {
			if (!a_textField) {
				Report(letter, "no-ctrl", ctrl, shift, alt);
			}
			return;
		}
		if (alt) {
			Report(letter, "alt-or-meta", ctrl, shift, alt);
			return;
		}
		if (a_textField) {
			Report(letter, "text-field", ctrl, shift, alt);
			return;
		}
		const bool undo = letter == 'z' && !shift;
		Report(letter, undo ? "undo" : "redo", ctrl, shift, alt);
		if (undo) {
			Undo("key");
		} else {
			Redo("key");
		}
	}

	int Depth()
	{
		return static_cast<int>(g_at);
	}

	int Size()
	{
		return static_cast<int>(g_entries.size());
	}

	void LogState(const char* a_what)
	{
		spdlog::info("[smf] view {}: history at {} of {} | recorded {}, undone {}, redone {}, cleared {}", a_what, g_at,
			g_entries.size(), g_recorded, g_undone, g_redone, g_cleared);
	}
}
