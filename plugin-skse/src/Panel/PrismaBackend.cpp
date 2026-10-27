// PrismaBackend.cpp
// Lifecast

#include "Panel/PrismaBackend.h"

#include "Panel/Bridge.h"

// The header calls GetModuleHandle with a wide string, which only compiles
// with UNICODE defined; this project builds without it. The header is a
// third-party file and stays unedited (extern/PrismaUI/ORIGEM.md), so the
// name is mapped here, for this one include.
#undef GetModuleHandle
#define GetModuleHandle GetModuleHandleW
#include "PrismaUI/PrismaUI_API.h"
#undef GetModuleHandle

namespace Lifecast::Panel
{
	namespace
	{
		using Clock = std::chrono::steady_clock;

		// Relative to Data/PrismaUI/views/.
		constexpr auto kViewPath = "Lifecast/index.html";

		// The page's two globals on this backend. bridge.js builds
		// window.Lifecast.send/on over them, so the rest of the page does not
		// know which backend it runs on.
		constexpr auto kSendListener = "lifecastSend";
		constexpr auto kReceiveFunction = "lifecastReceive";

		PRISMA_UI_API::IVPrismaUI1* g_api = nullptr;
		PrismaView                  g_view = 0;
		Clock::time_point           g_createdAt{};

		// Written from Prisma's thread by the DOM-ready callback, read on the
		// game thread.
		std::atomic_bool g_domReady = false;

		// What the panel last asked for. Prisma applies focus late, and drops
		// it before DOM ready; this is what DOM ready and the chord act on.
		bool g_wantFocus = false;

		// The exit chord. Read by the input sink on the input thread.
		std::atomic<std::uint32_t> g_chordKey1 = 0;
		std::atomic<std::uint32_t> g_chordKey2 = 0;
		std::atomic_bool           g_chordKey2Held = false;
		std::uint32_t              g_chordHits = 0;

		long long MsSinceCreate()
		{
			return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - g_createdAt).count();
		}

		bool HasFocusNow()
		{
			return g_api && g_view && g_api->HasFocus(g_view);
		}

		// Game thread, DOM ready.
		void IssueFocus(const char* a_when)
		{
			if (!g_api || !g_view || !g_wantFocus) {
				return;
			}
			// Focus() returns early when the view still reports focus from
			// before an Unfocus that has not landed yet - and then does not
			// install its keyboard capture. One task turn later the read is
			// settled.
			if (g_api->HasFocus(g_view)) {
				spdlog::info("[prisma] focus ({}): HasFocus is still true - issued one task turn later", a_when);
				if (auto* tasks = SKSE::GetTaskInterface()) {
					tasks->AddTask([]() {
						if (g_wantFocus && g_api && g_view) {
							const bool r = g_api->Focus(g_view, false, false);
							spdlog::info("[prisma] focus (after a task turn): returned {}", r);
						}
					});
				}
				return;
			}
			const bool r = g_api->Focus(g_view, false, false);
			spdlog::info("[prisma] focus ({}): returned {}, HasFocus in the same call {}", a_when, r,
				g_api->HasFocus(g_view));
		}

		void OnDomReady(PrismaView)
		{
			// Prisma's thread: nothing but the flag here.
			g_domReady = true;
			if (auto* tasks = SKSE::GetTaskInterface()) {
				tasks->AddTask([]() {
					spdlog::info("[prisma] DOM ready, {} ms after CreateView", MsSinceCreate());
					if (g_wantFocus) {
						IssueFocus("held until DOM ready");
					}
				});
			}
		}

		void OnPageSend(const char* a_arg)
		{
			// Prisma's thread. The pointer is not ours past this call.
			std::string text = a_arg ? a_arg : "";
			if (auto* tasks = SKSE::GetTaskInterface()) {
				tasks->AddTask([text = std::move(text)]() { Bridge::Receive(text.c_str(), 1, false); });
			}
		}

		class ChordSink : public RE::BSTEventSink<RE::InputEvent*>
		{
		public:
			static ChordSink* Get()
			{
				static ChordSink s_sink;
				return &s_sink;
			}

			RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* a_event,
				RE::BSTEventSource<RE::InputEvent*>*) override
			{
				const auto key1 = g_chordKey1.load();
				const auto key2 = g_chordKey2.load();
				if (!a_event || !g_view || key1 == 0) {
					return RE::BSEventNotifyControl::kContinue;
				}
				for (auto* event = *a_event; event; event = event->next) {
					if (event->GetDevice() != RE::INPUT_DEVICES::kKeyboard) {
						continue;
					}
					const auto* button = event->AsButtonEvent();
					if (!button) {
						continue;
					}
					const auto code = button->GetIDCode();
					if (key2 != 0 && code == key2) {
						g_chordKey2Held = button->IsPressed();
						continue;
					}
					if (code != key1 || !button->IsDown() || (key2 != 0 && !g_chordKey2Held)) {
						continue;
					}
					const bool hf = HasFocusNow();
					if (!hf && !g_wantFocus) {
						// Armed but not focused: the key is the game's.
						return RE::BSEventNotifyControl::kContinue;
					}
					++g_chordHits;
					g_wantFocus = false;
					g_api->Unfocus(g_view);
					spdlog::info("[prisma] exit chord #{} (0x{:X}): Unfocus, HasFocus before {}; the key stops here",
						g_chordHits, code, hf);
					return RE::BSEventNotifyControl::kStop;
				}
				return RE::BSEventNotifyControl::kContinue;
			}

		private:
			ChordSink() = default;
		};

		class PrismaPanel final : public Backend
		{
		public:
			const char* Name() const override { return "prisma"; }

			bool Available() override { return g_api != nullptr; }

			bool Create() override
			{
				if (g_view) {
					return true;
				}
				if (!g_api) {
					spdlog::error("[prisma] Prisma UI never answered - no panel");
					return false;
				}
				g_domReady = false;
				g_createdAt = Clock::now();
				g_view = g_api->CreateView(kViewPath, &OnDomReady);
				spdlog::info("[prisma] CreateView(\"{}\") = view {}", kViewPath, g_view);
				if (!g_view) {
					spdlog::error("[prisma] the view was refused - no panel");
					return false;
				}
				// Created hidden: what shows it is the caller's decision.
				g_api->Hide(g_view);
				g_api->RegisterJSListener(g_view, kSendListener, &OnPageSend);
				LogState("created");
				return true;
			}

			bool Exists() const override { return g_api && g_view; }

			void SetVisible(bool a_visible) override
			{
				if (!Exists()) {
					return;
				}
				if (a_visible) {
					g_api->Show(g_view);
					return;
				}
				// Hidden is also unfocused, as on Meridian. Unfocus only when the
				// panel has or wants focus: Prisma's Unfocus closes the focus
				// menu of every Prisma view, not just this one.
				if (g_wantFocus || HasFocusNow()) {
					g_wantFocus = false;
					g_api->Unfocus(g_view);
				}
				g_api->Hide(g_view);
			}

			void SetFocused(bool a_focused) override
			{
				if (!Exists()) {
					return;
				}
				if (a_focused) {
					g_wantFocus = true;
					if (!g_domReady) {
						spdlog::info("[prisma] focus asked before DOM ready - held until it is");
						return;
					}
					IssueFocus("asked");
					return;
				}
				const bool had = g_wantFocus || HasFocusNow();
				g_wantFocus = false;
				if (had) {
					g_api->Unfocus(g_view);
				}
			}

			bool IsFocused() override { return HasFocusNow(); }

			void ArmChord(std::uint32_t a_key1, std::uint32_t a_key2) override
			{
				g_chordKey2Held = false;
				g_chordKey2 = a_key2;
				g_chordKey1 = a_key1;
				spdlog::info("[prisma] exit chord armed: 0x{:X} + 0x{:X}", a_key1, a_key2);
			}

			void DisarmChord() override
			{
				g_chordKey1 = 0;
				g_chordKey2 = 0;
				spdlog::info("[prisma] exit chord disarmed");
			}

			bool PageReady() override { return Exists() && g_domReady; }

			void Post(const char* a_text) override
			{
				if (Exists()) {
					g_api->InteropCall(g_view, kReceiveFunction, a_text);
				}
			}

			void LogState(const char* a_what) override
			{
				if (!Exists()) {
					spdlog::info("[prisma] {} - no view", a_what);
					return;
				}
				spdlog::info("[prisma] {} - view {}, domReady {}, hidden {}, focused {}, wantFocus {}, anyFocus {}", a_what,
					g_view, g_domReady.load(), g_api->IsHidden(g_view), g_api->HasFocus(g_view), g_wantFocus,
					g_api->HasAnyActiveFocus());
			}
		};
	}

	Backend& PrismaBackend()
	{
		static PrismaPanel s_backend;
		return s_backend;
	}

	void PrismaStart()
	{
		static bool s_started = false;
		if (s_started) {
			return;
		}
		s_started = true;

		g_api = static_cast<PRISMA_UI_API::IVPrismaUI1*>(
			PRISMA_UI_API::RequestPluginAPI(PRISMA_UI_API::InterfaceVersion::V1));
		if (!g_api) {
			spdlog::info("[prisma] Prisma UI is not loaded or did not answer (API v1 = null)");
			return;
		}
		spdlog::info("[prisma] API v1 = {}", static_cast<const void*>(g_api));

		auto* input = RE::BSInputDeviceManager::GetSingleton();
		if (!input) {
			spdlog::error("[prisma] no BSInputDeviceManager - the exit chord cannot be heard");
			return;
		}
		input->PrependEventSink(ChordSink::Get());
		spdlog::info("[prisma] exit-chord sink prepended on BSInputDeviceManager");
	}
}
