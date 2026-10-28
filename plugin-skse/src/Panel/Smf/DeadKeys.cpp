// DeadKeys.cpp
// Lifecast

#include "Panel/Smf/DeadKeys.h"

#include <chrono>
#include <vector>

namespace Lifecast::Panel::Smf::DeadKeys
{
	namespace
	{
		namespace IG = ImGuiMCP;
		using Clock = std::chrono::steady_clock;

		enum class Accent
		{
			kNone,
			kAcute,
			kGrave,
			kCircumflex,
			kTilde,
			kDiaeresis,
		};

		struct Dead
		{
			int     vk = 0;
			bool    shift = false;
			Accent  accent = Accent::kNone;
			wchar_t spacing = 0;  // what the layout writes for the key alone
		};

		// A pending accent older than this is dropped: the letter it was for
		// never came.
		constexpr auto kPendingFor = std::chrono::seconds(3);

		bool              g_inited = false;
		bool              g_on = false;  // Media Keys Fix is loaded
		HKL               g_hkl = nullptr;
		std::vector<Dead> g_dead;
		bool              g_was[256] = {};

		Accent            g_pending = Accent::kNone;
		wchar_t           g_pendingSpacing = 0;
		Clock::time_point g_pendingAt{};

		const char* Name(Accent a_accent)
		{
			switch (a_accent) {
			case Accent::kAcute:      return "acute";
			case Accent::kGrave:      return "grave";
			case Accent::kCircumflex: return "circumflex";
			case Accent::kTilde:      return "tilde";
			case Accent::kDiaeresis:  return "diaeresis";
			default:                  return "none";
			}
		}

		// The spacing character a layout writes for a dead key pressed twice
		// or before a space. US-International writes the apostrophe and the
		// quote for the acute and the diaeresis.
		Accent AccentOf(wchar_t a_c)
		{
			switch (a_c) {
			case 0x00B4:
			case 0x0027:
				return Accent::kAcute;
			case 0x0060:
				return Accent::kGrave;
			case 0x005E:
			case 0x02C6:
				return Accent::kCircumflex;
			case 0x007E:
			case 0x02DC:
				return Accent::kTilde;
			case 0x00A8:
			case 0x0022:
				return Accent::kDiaeresis;
			default:
				return Accent::kNone;
			}
		}

		// The accented letter, Latin-1 (and the one Latin Extended-A capital
		// Y with diaeresis); 0 when the pair has none.
		wchar_t Compose(Accent a_accent, wchar_t a_base)
		{
			struct Pair
			{
				char    base;
				wchar_t out;
			};
			static constexpr Pair kAcute[] = { { 'A', 0xC1 }, { 'E', 0xC9 }, { 'I', 0xCD }, { 'O', 0xD3 }, { 'U', 0xDA },
				{ 'Y', 0xDD }, { 'a', 0xE1 }, { 'e', 0xE9 }, { 'i', 0xED }, { 'o', 0xF3 }, { 'u', 0xFA }, { 'y', 0xFD } };
			static constexpr Pair kGrave[] = { { 'A', 0xC0 }, { 'E', 0xC8 }, { 'I', 0xCC }, { 'O', 0xD2 }, { 'U', 0xD9 },
				{ 'a', 0xE0 }, { 'e', 0xE8 }, { 'i', 0xEC }, { 'o', 0xF2 }, { 'u', 0xF9 } };
			static constexpr Pair kCirc[] = { { 'A', 0xC2 }, { 'E', 0xCA }, { 'I', 0xCE }, { 'O', 0xD4 }, { 'U', 0xDB },
				{ 'a', 0xE2 }, { 'e', 0xEA }, { 'i', 0xEE }, { 'o', 0xF4 }, { 'u', 0xFB } };
			static constexpr Pair kTilde[] = { { 'A', 0xC3 }, { 'O', 0xD5 }, { 'N', 0xD1 }, { 'a', 0xE3 }, { 'o', 0xF5 },
				{ 'n', 0xF1 } };
			static constexpr Pair kDiaer[] = { { 'A', 0xC4 }, { 'E', 0xCB }, { 'I', 0xCF }, { 'O', 0xD6 }, { 'U', 0xDC },
				{ 'Y', 0x178 }, { 'a', 0xE4 }, { 'e', 0xEB }, { 'i', 0xEF }, { 'o', 0xF6 }, { 'u', 0xFC }, { 'y', 0xFF } };

			auto find = [a_base](const auto& a_table) -> wchar_t {
				for (const auto& p : a_table) {
					if (static_cast<wchar_t>(p.base) == a_base) {
						return p.out;
					}
				}
				return 0;
			};
			switch (a_accent) {
			case Accent::kAcute:      return find(kAcute);
			case Accent::kGrave:      return find(kGrave);
			case Accent::kCircumflex: return find(kCirc);
			case Accent::kTilde:      return find(kTilde);
			case Accent::kDiaeresis:  return find(kDiaer);
			default:                  return 0;
			}
		}

		// The layout's dead keys, plain and with Shift. Flag 0x4 keeps
		// ToUnicodeEx from touching the keyboard state Windows composes with -
		// without it, looking would eat a dead key the player just pressed.
		void Scan(HKL a_hkl)
		{
			g_hkl = a_hkl;
			g_dead.clear();
			std::string list;
			for (int vk = 0x08; vk <= 0xFE; ++vk) {
				for (int s = 0; s < 2; ++s) {
					BYTE state[256] = {};
					if (s) {
						state[VK_SHIFT] = 0x80;
					}
					const UINT scan = ::MapVirtualKeyExW(static_cast<UINT>(vk), MAPVK_VK_TO_VSC, a_hkl);
					if (scan == 0) {
						continue;
					}
					wchar_t buf[8] = {};
					const int r = ::ToUnicodeEx(static_cast<UINT>(vk), scan, state, buf, 8, 0x4, a_hkl);
					if (r >= 0) {
						continue;
					}
					const auto accent = AccentOf(buf[0]);
					if (accent == Accent::kNone) {
						continue;
					}
					g_dead.push_back({ vk, s != 0, accent, buf[0] });
					list += fmt::format("{}vk 0x{:02X}{} {} (U+{:04X})", list.empty() ? "" : ", ", vk, s ? "+Shift" : "",
						Name(accent), static_cast<unsigned>(buf[0]));
				}
			}
			spdlog::info("[smf] dead keys of layout 0x{:X}: {}", reinterpret_cast<std::uintptr_t>(a_hkl),
				list.empty() ? std::string("none") : list);
		}

		void Watch()
		{
			const bool shift = (::GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
			const bool altGr = (::GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0 && (::GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
			// One look per key; which entry the press means depends on Shift.
			int seen[256] = {};
			for (const auto& d : g_dead) {
				if (seen[d.vk]++) {
					continue;
				}
				const SHORT st = ::GetAsyncKeyState(d.vk);
				const bool  down = (st & 0x8000) != 0;
				// The low bit: pressed since the last look, for a press shorter
				// than a frame.
				const bool pressed = (down && !g_was[d.vk]) || (!down && (st & 0x0001) != 0);
				g_was[d.vk] = down;
				if (!pressed || altGr) {
					continue;
				}
				const Dead* hit = nullptr;
				for (const auto& e : g_dead) {
					if (e.vk == d.vk && e.shift == shift) {
						hit = &e;
					}
				}
				if (!hit) {
					continue;
				}
				g_pending = hit->accent;
				g_pendingSpacing = hit->spacing;
				g_pendingAt = Clock::now();
				spdlog::info("[smf] dead key vk 0x{:02X}{}: {} pending", d.vk, shift ? "+Shift" : "", Name(hit->accent));
			}
		}

		// The pending accent is dropped when the letter it was for never came.
		void Expire()
		{
			if (g_pending != Accent::kNone && Clock::now() - g_pendingAt > kPendingFor) {
				spdlog::info("[smf] dead key: {} dropped, no letter came", Name(g_pending));
				g_pending = Accent::kNone;
			}
		}
	}

	void Init()
	{
		if (g_inited) {
			return;
		}
		g_inited = true;
		g_on = ::GetModuleHandleW(L"MediaKeysFix") != nullptr;
		spdlog::info("[smf] Media Keys Fix {} - the view {} dead keys", g_on ? "loaded" : "not loaded",
			g_on ? "composes" : "leaves Windows to compose");
		if (g_on) {
			Scan(::GetKeyboardLayout(0));
		}
	}

	void Frame(IG::ImGuiIO* a_io)
	{
		if (!g_on || !a_io) {
			return;
		}
		if (const HKL hkl = ::GetKeyboardLayout(0); hkl != g_hkl) {
			Scan(hkl);
		}
		Watch();
		Expire();
	}

	int Filter(IG::ImGuiInputTextCallbackData* a_data)
	{
		if (!g_on || !a_data || a_data->EventFlag != IG::ImGuiInputTextFlags_CallbackCharFilter) {
			return 0;
		}
		Expire();
		if (g_pending == Accent::kNone) {
			return 0;
		}
		const auto    accent = g_pending;
		const wchar_t c = static_cast<wchar_t>(a_data->EventChar);
		g_pending = Accent::kNone;
		// What the queue rewrite of E2-2 would have read at this moment: the
		// letter is being typed, so ImGui's queue holds it (D287).
		if (const auto* io = IG::GetIO()) {
			spdlog::info("[smf] dead key: the filter has U+{:04X}; the header's InputQueueCharacters reads size {}",
				static_cast<unsigned>(c), io->InputQueueCharacters.Size);
		}
		if (c >= 0x80) {
			spdlog::info("[smf] dead key: {} cancelled, U+{:04X} arrived composed already", Name(accent),
				static_cast<unsigned>(c));
			return 0;
		}
		if (c == L' ') {
			a_data->EventChar = static_cast<IG::ImWchar>(g_pendingSpacing);
			spdlog::info("[smf] dead key: {} + space -> U+{:04X}", Name(accent), static_cast<unsigned>(g_pendingSpacing));
			return 0;
		}
		if (const auto out = Compose(accent, c); out != 0) {
			a_data->EventChar = static_cast<IG::ImWchar>(out);
			spdlog::info("[smf] dead key: {} + U+{:04X} -> U+{:04X}", Name(accent), static_cast<unsigned>(c),
				static_cast<unsigned>(out));
			return 0;
		}
		// A filter changes one character and cannot add one: the letter stays,
		// the accent is said and dropped.
		spdlog::info("[smf] dead key: {} + U+{:04X} has no accented form - the letter goes alone", Name(accent),
			static_cast<unsigned>(c));
		return 0;
	}
}
