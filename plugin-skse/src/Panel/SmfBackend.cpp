// SmfBackend.cpp
// Lifecast

#include "Panel/SmfBackend.h"

#include "Panel/Bridge.h"
#include "Panel/Panel.h"
#include "Panel/Smf/DeadKeys.h"
#include "Panel/Smf/Theme.h"
#include "Panel/Smf/View.h"
#include "Panel/Stage.h"

#include "SKSEMenuFramework/SKSEMenuFramework.h"

#include <algorithm>
#include <atomic>
#include <chrono>

namespace Lifecast::Panel
{
	namespace
	{
		namespace IG = ImGuiMCP;
		using Clock = std::chrono::steady_clock;

		// Relative to the game folder. The SMF package has no page; it ships
		// this file so that "auto" can tell it was installed (D286).
		constexpr auto kMarker = "Data\\SKSE\\Plugins\\Lifecast\\smf-package.txt";

		// A chord key this soon after the panel took the input is the press
		// that brought it back (the same guard as on Magelight).
		constexpr long long kChordSettleMs = 250;

		// The game's input sinks are not called while the window holds the
		// input; past this, the stage's slide is ticked from the frame.
		constexpr long long kInputSilentMs = 60;

		// Frames the hybrid form waits after a change of owner before it
		// looks again (1.6 E1: SMF moves the cursor on the frame after).
		constexpr int kFlipHold = 3;

		MENU_WINDOW                g_win = nullptr;
		std::atomic_bool           g_visible = false;
		std::atomic_bool           g_wantFocus = false;
		std::atomic<std::uint32_t> g_chordDik = 0;
		std::atomic<long long>     g_focusAtMs = 0;
		std::atomic<long long>     g_inputAtMs = 0;
		std::atomic<std::uint32_t> g_chordHits = 0;

		// Render function only (the game thread).
		std::uint64_t g_frames = 0;
		std::uint32_t g_gameTid = 0;
		bool          g_saidFirst = false;
		bool          g_block = false;
		int           g_hold = 0;
		bool          g_pendingResync = false;
		float         g_rx = 0.0f;
		float         g_ry = 0.0f;
		bool          g_stageFromFrame = false;
		std::uint32_t g_flips = 0;
		Smf::Frame    g_last;

		long long NowMs()
		{
			return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();
		}

		bool Foreground()
		{
			DWORD pid = 0;
			::GetWindowThreadProcessId(::GetForegroundWindow(), &pid);
			return pid == ::GetCurrentProcessId();
		}

		void MenuCursorAt(float& a_x, float& a_y)
		{
			auto* c = RE::MenuCursor::GetSingleton();
			a_x = c ? c->GetRuntimeData().cursorPosX : -1.0f;
			a_y = c ? c->GetRuntimeData().cursorPosY : -1.0f;
		}

		// F1-F12 as the game's scan codes to ImGui's keys; 0 for any other.
		int ImGuiKeyOf(std::uint32_t a_dik)
		{
			if (a_dik >= 0x3B && a_dik <= 0x44) {
				return IG::ImGuiKey_F1 + static_cast<int>(a_dik - 0x3B);
			}
			if (a_dik == 0x57) {
				return IG::ImGuiKey_F1 + 10;
			}
			if (a_dik == 0x58) {
				return IG::ImGuiKey_F12;
			}
			return 0;
		}

		void SetBlock(bool a_block)
		{
			g_block = a_block;
			if (g_win) {
				g_win->BlockUserInput = a_block;
			}
		}

		// Any thread that heard the armed key with the panel focused. The swap
		// itself runs on a later game-thread task, never inside a frame.
		void PostExit(const char* a_from)
		{
			if (!g_wantFocus) {
				return;
			}
			const auto since = NowMs() - g_focusAtMs.load();
			if (since < kChordSettleMs) {
				spdlog::info("[smf] exit chord ({}) {} ms after the panel took the input - the press that brought it back; "
							 "nothing done",
					a_from, since);
				return;
			}
			g_wantFocus = false;
			if (g_win) {
				g_win->BlockUserInput = false;
			}
			const auto n = ++g_chordHits;
			spdlog::info("[smf] exit chord #{} ({}) - the swap goes to the Switcher", n, a_from);
			if (auto* tasks = SKSE::GetTaskInterface()) {
				tasks->AddTask([]() { ExitChordHeard("smf"); });
			}
		}

		// The hybrid form: the window holds the input while the cursor is over
		// one of the view's windows (the small panel, the belt, the stage bar;
		// the screen between them is the game's); a press keeps the owner it
		// found until it is released, and a field being typed in keeps the
		// keyboard.
		bool HybridOwner(IG::ImGuiIO* a_io)
		{
			if (g_hold > 0) {
				--g_hold;
				return g_block;
			}
			const bool held = ((::GetAsyncKeyState(VK_LBUTTON) | ::GetAsyncKeyState(VK_RBUTTON) |
								   ::GetAsyncKeyState(VK_MBUTTON)) &
								  0x8000) != 0;
			if (held) {
				return g_block;
			}
			if (g_block && g_last.wantText) {
				return true;
			}
			float cx = 0.0f;
			float cy = 0.0f;
			if (g_block) {
				cx = a_io->MousePos.x;  // the OS cursor, while the window holds the input
				cy = a_io->MousePos.y;
			} else {
				MenuCursorAt(cx, cy);  // RaceMenu's cursor, while the game does
				// RaceMenu's cursor stops ON the screen size (x = width, y =
				// height), one past the last pixel, and the window boxes end
				// there exclusively: a fast throw to the bottom edge stopped
				// outside the belt and never flipped (D301). Bring it onto the
				// screen first.
				const float lastX = a_io->DisplaySize.x > 1.0f ? a_io->DisplaySize.x - 1.0f : 0.0f;
				const float lastY = a_io->DisplaySize.y > 1.0f ? a_io->DisplaySize.y - 1.0f : 0.0f;
				cx = std::clamp(cx, 0.0f, lastX);
				cy = std::clamp(cy, 0.0f, lastY);
			}
			const Smf::Box* over = g_last.drawn ? g_last.At(cx, cy) : nullptr;
			const bool      inside = over != nullptr;
			if (inside == g_block) {
				return g_block;
			}
			g_hold = kFlipHold;
			++g_flips;
			if (inside) {
				// SMF centres the OS cursor on the frame after it takes the
				// input: put it back where it was.
				g_pendingResync = true;
				g_rx = cx;
				g_ry = cy;
			} else if (auto* c = RE::MenuCursor::GetSingleton()) {
				c->GetRuntimeData().cursorPosX = cx;  // RaceMenu's cursor goes on from where the OS one was
				c->GetRuntimeData().cursorPosY = cy;
			}
			spdlog::info("[smf] input (hybrid) #{}: {} at {:.0f},{:.0f}{}", g_flips, inside ? "window" : "game", cx, cy,
				over ? fmt::format(" [{}]", over->name) : std::string());
			return inside;
		}

		void ResyncIfPending()
		{
			if (!g_pendingResync) {
				return;
			}
			g_pendingResync = false;
			if (!Foreground()) {
				return;
			}
			POINT p{ static_cast<LONG>(g_rx), static_cast<LONG>(g_ry) };
			::ClientToScreen(::GetForegroundWindow(), &p);
			::SetCursorPos(p.x, p.y);
		}

		void __stdcall Render()
		{
			++g_frames;
			auto* io = IG::GetIO();
			if (!io) {
				return;
			}
			if (!g_saidFirst) {
				g_saidFirst = true;
				spdlog::info("[smf] first frame: thread {} (game thread {}), ImGui {}, display {:.0f}x{:.0f}",
					::GetCurrentThreadId(), g_gameTid, IG::GetVersion(), io->DisplaySize.x, io->DisplaySize.y);
			}

			// The slide of the stage runs from the game's input tick; while the
			// window holds the input that tick is silent, and it runs from here.
			const bool silent = NowMs() - g_inputAtMs.load() > kInputSilentMs;
			if (silent) {
				Stage::Tick();
			}
			if (silent != g_stageFromFrame) {
				g_stageFromFrame = silent;
				spdlog::info("[smf] the game's input tick is {} - the stage slides from {}", silent ? "silent" : "back",
					silent ? "the window's frame" : "the input tick");
			}

			ResyncIfPending();
			const bool focused = g_visible && g_wantFocus;
			const bool block = focused && (Smf::Hybrid() ? HybridOwner(io) : true);
			if (block != g_block) {
				SetBlock(block);
			}
			if (block) {
				Smf::DeadKeys::Frame(io);
				const int key = ImGuiKeyOf(g_chordDik.load());
				if (key && IG::IsKeyPressed(static_cast<IG::ImGuiKey>(key), false)) {
					PostExit("ImGui");
				}
			}
			Smf::Theme::FrameStart();
			g_last = Smf::Draw(block && g_wantFocus);
			Smf::Theme::FrameEnd();
			Smf::Flush();
		}

		// What the game still receives: the chord when the window does not hold
		// the input (the hybrid form, cursor away from the panel), and the
		// tick that says the game's input is being dispatched at all.
		constexpr std::uint32_t kLeftAlt = 0x38;  // DirectInput scan code, DIK_LMENU

		class ChordSink final : public RE::BSTEventSink<RE::InputEvent*>
		{
		public:
			static ChordSink* Get()
			{
				static ChordSink s_instance;
				return &s_instance;
			}

			RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* a_event, RE::BSTEventSource<RE::InputEvent*>*) override
			{
				g_inputAtMs = NowMs();
				// D310: under the hybrid form a press outside the view's windows
				// reaches the game, and the stage's drag is read from here. The
				// event goes on to RaceMenu as it always did.
				if (a_event && g_visible && Smf::Hybrid()) {
					for (auto* e = *a_event; e; e = e->next) {
						if (e->GetDevice() != RE::INPUT_DEVICES::kMouse) {
							continue;
						}
						if (e->GetEventType() == RE::INPUT_EVENT_TYPE::kMouseMove) {
							if (const auto* m = e->AsMouseMoveEvent()) {
								Smf::GameMouseMove(m->mouseInputX, m->mouseInputY);
							}
						} else if (const auto* b = e->AsButtonEvent()) {
							if (b->IsDown() || b->IsUp()) {
								Smf::GameMouseButton(b->GetIDCode(), b->IsDown());
							}
						}
					}
				}
				// D318: LAlt (stage.js's far/near swap) heard on the game's input
				// under the hybrid form, handed to the view as Prisma's backend
				// hands it to the page. The key goes on to RaceMenu.
				if (a_event && g_visible && Smf::Hybrid() && !Smf::Card()) {
					for (auto* e = *a_event; e; e = e->next) {
						const auto* b = e->GetDevice() == RE::INPUT_DEVICES::kKeyboard ? e->AsButtonEvent() : nullptr;
						if (b && b->GetIDCode() == kLeftAlt && b->IsDown()) {
							if (auto* tasks = SKSE::GetTaskInterface()) {
								tasks->AddTask([]() { Bridge::Send("key-lalt"); });
							}
						}
					}
				}
				const auto dik = g_chordDik.load();
				if (!a_event || !g_visible || !g_wantFocus || dik == 0) {
					return RE::BSEventNotifyControl::kContinue;
				}
				for (auto* e = *a_event; e; e = e->next) {
					if (e->GetDevice() != RE::INPUT_DEVICES::kKeyboard) {
						continue;
					}
					const auto* b = e->AsButtonEvent();
					if (!b || b->GetIDCode() != dik) {
						continue;
					}
					if (b->IsDown()) {
						PostExit("game input");
					}
					// The chord is the panel's while it is focused: RaceMenu
					// does not see it.
					return RE::BSEventNotifyControl::kStop;
				}
				return RE::BSEventNotifyControl::kContinue;
			}

		private:
			ChordSink() = default;
		};

		class SmfPanel final : public Backend
		{
		public:
			const char* Name() const override { return "smf"; }
			const char* Framework() const override { return "SKSE Menu Framework"; }
			const char* PanelFile() const override { return kMarker; }

			bool Available() override
			{
				const HMODULE m = GetMenuFrameworkModule();
				const bool    ok = m && ::GetProcAddress(m, "AddWindow");
				static bool   s_said = false;
				if (!s_said) {
					s_said = true;
					if (m) {
						spdlog::info("[smf] SKSE Menu Framework loaded: version {} (API {}), AddWindow {}",
							SKSEMenuFramework::GetMenuFrameworkVersion(), SKSEMenuFramework::GetMenuFrameworkAPIVersion(),
							ok ? "exported" : "NOT exported");
					} else {
						spdlog::info("[smf] SKSE Menu Framework is not loaded");
					}
				}
				return ok;
			}

			bool Create() override
			{
				if (g_win) {
					return true;
				}
				if (!Available()) {
					spdlog::error("[smf] SKSE Menu Framework is not loaded - no panel");
					return false;
				}
				g_gameTid = ::GetCurrentThreadId();
				Smf::DeadKeys::Init();
				g_win = SKSEMenuFramework::AddWindow(&Render, false);
				if (!g_win) {
					spdlog::error("[smf] AddWindow refused - no panel");
					return false;
				}
				g_win->IsOpen = false;
				g_win->BlockUserInput = false;
				if (auto* input = RE::BSInputDeviceManager::GetSingleton()) {
					input->PrependEventSink(ChordSink::Get());
				} else {
					spdlog::warn("[smf] no BSInputDeviceManager - the exit chord is heard only while the window holds the input");
				}
				spdlog::info("[smf] AddWindow -> {} (game thread {})", static_cast<const void*>(g_win), g_gameTid);
				Smf::Open();
				return true;
			}

			bool Exists() const override { return g_win != nullptr; }

			void SetVisible(bool a_visible) override
			{
				if (!g_win) {
					return;
				}
				if (!a_visible) {
					g_wantFocus = false;
					SetBlock(false);
				}
				g_visible = a_visible;
				g_win->IsOpen = a_visible;
			}

			void SetFocused(bool a_focused) override
			{
				if (!g_win) {
					return;
				}
				g_wantFocus = a_focused;
				if (a_focused) {
					g_focusAtMs = NowMs();
					// "all" holds the input at once; "hybrid" decides on the
					// next frame, from where the cursor is.
					if (!Smf::Hybrid()) {
						SetBlock(true);
					}
				} else {
					SetBlock(false);
				}
			}

			bool IsFocused() override { return g_win && g_visible && g_wantFocus; }

			void ArmChord(std::uint32_t a_key1, std::uint32_t a_key2) override
			{
				if (ImGuiKeyOf(a_key1) == 0) {
					spdlog::warn("[smf] exit chord 0x{:X} is not F1-F12 - ImGui cannot hear it while the window holds the input",
						a_key1);
				}
				if (a_key2 != 0) {
					spdlog::warn("[smf] exit chord: the second key 0x{:X} is not supported here - ignored", a_key2);
				}
				g_chordDik = a_key1;
				spdlog::info("[smf] exit chord armed: 0x{:X}", a_key1);
			}

			void DisarmChord() override
			{
				g_chordDik = 0;
				spdlog::info("[smf] exit chord disarmed");
			}

			bool PageReady() override { return g_win != nullptr; }

			void Post(const char* a_text) override
			{
				if (g_win) {
					Smf::Receive(a_text);
				}
			}

			void LogState(const char* a_what) override
			{
				if (!g_win) {
					spdlog::info("[smf] {} - no window", a_what);
					return;
				}
				spdlog::info("[smf] {} - open {}, block {}, wantFocus {}, chord 0x{:X} (hits {}), frames {}, flips {}, any "
							 "blocking window {}",
					a_what, g_win->IsOpen.load(), g_win->BlockUserInput.load(), g_wantFocus.load(), g_chordDik.load(),
					g_chordHits.load(), g_frames, g_flips, SKSEMenuFramework::IsAnyBlockingWindowOpened());
				Smf::LogState(a_what);
				Smf::Theme::LogBalance(a_what);
			}
		};
	}

	Backend& SmfBackend()
	{
		static SmfPanel s_backend;
		return s_backend;
	}
}
