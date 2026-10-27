// MeridianBackend.cpp
// Lifecast
//
// The code the panel had when Meridian was its only backend, behind
// Panel/Backend.h: same calls, same order, same log lines.

#include "Panel/MeridianBackend.h"

#include "Core/MeridianLink.h"
#include "Panel/Bridge.h"

#include "MeridianUIAPI/IBrowser.h"
#include "MeridianUIAPI/JSTypes.h"

namespace Lifecast::Panel
{
	namespace
	{
		namespace MUI = Meridian::UI;

		// The mod:// host is the folder under Data/MeridianUI/, and the browser
		// name is a process-wide key shared with every other Meridian consumer.
		constexpr auto kBrowserName = "Lifecast.Panel";
		constexpr auto kStartUrl = "mod://Lifecast/index.html";

		// window.Lifecast.send(...) and window.Lifecast.on(...), installed by
		// Meridian from the bindings below.
		constexpr auto kObjectName = "Lifecast";
		constexpr auto kSendFunction = "send";
		constexpr auto kEventFunction = "on";

		// The single event name every DLL-to-page message travels under. The
		// page dispatches on the "type" field, not on the event name.
		constexpr auto kEventName = "message";

		MUI::IUIPlatformAPI::BrowserRefHandle g_handle = MUI::IUIPlatformAPI::InvalidBrowserRefHandle;
		Meridian::CEF::IBrowser* g_browser = nullptr;

		// Set by Meridian's shutdown callback. From then on no browser call
		// is allowed, and Create() must not make a new one.
		std::atomic_bool g_shutdown = false;

		void OnMeridianShutdown()
		{
			g_shutdown = true;
			g_browser = nullptr;
			g_handle = MUI::IUIPlatformAPI::InvalidBrowserRefHandle;
			spdlog::info("[panel] Meridian is shutting down - browser dropped");
		}

		Meridian::CEF::IBrowser* Live()
		{
			return g_shutdown ? nullptr : g_browser;
		}

		// The send binding. Registered with executeInGameThread = true, so
		// this runs on the game thread and the pointers in a_args are valid
		// for the duration of the call. Meridian hands every argument over
		// as a JSON document of its own - a JS string arrives QUOTED - so the
		// bridge may decode it twice.
		void OnPageSend(const char** a_args, int a_argsCount)
		{
			if (a_argsCount < 1 || !a_args || !a_args[0]) {
				spdlog::warn("[bridge] page->dll: send() called with {} arguments - expected 1", a_argsCount);
				return;
			}
			Bridge::Receive(a_args[0], a_argsCount, true);
		}

		Meridian::JS::JSFuncInfo& SendBinding()
		{
			static Meridian::JS::JSFuncInfo s_info = [] {
				Meridian::JS::JSFuncInfo info{};
				info.objectName = kObjectName;
				info.funcName = kSendFunction;
				info.callbackData.callback = &OnPageSend;
				info.callbackData.executeInGameThread = true;
				return info;
			}();
			return s_info;
		}

		// The event function can only be passed at creation time -
		// AddFunctionCallback cannot add one later. The array and what it
		// points to live for the whole process.
		Meridian::JS::JSFuncInfo& EventBinding()
		{
			static Meridian::JS::JSFuncInfo s_info = [] {
				Meridian::JS::JSFuncInfo info{};
				info.objectName = kObjectName;
				info.funcName = kEventFunction;
				info.callbackData.isEventFunction = true;
				return info;
			}();
			return s_info;
		}

		Meridian::JS::JSFuncInfo* const* Bindings()
		{
			static Meridian::JS::JSFuncInfo* const s_bindings[] = { &SendBinding(), &EventBinding() };
			return s_bindings;
		}

		class MeridianPanel final : public Backend
		{
		public:
			const char* Name() const override { return "meridian"; }

			bool Available() override { return Core::MeridianLink::Platform() != nullptr; }

			bool Create() override
			{
				if (g_shutdown) {
					return false;
				}
				if (g_browser) {
					return true;
				}

				auto* platform = Core::MeridianLink::Platform();
				if (!platform) {
					spdlog::error("[panel] the Meridian platform API never answered - no panel");
					return false;
				}

				static bool s_shutdownRegistered = false;
				if (!s_shutdownRegistered) {
					platform->RegisterOnShutdown(&OnMeridianShutdown);
					s_shutdownRegistered = true;
				}

				Meridian::CEF::IBrowser* browser = nullptr;
				const auto handle = platform->AddOrGetBrowser(kBrowserName, Bindings(), 2, kStartUrl, browser);

				spdlog::info("[panel] AddOrGetBrowser(\"{}\", \"{}\") = handle {}, browser {}", kBrowserName, kStartUrl,
					handle, static_cast<const void*>(browser));

				if (handle == MUI::IUIPlatformAPI::InvalidBrowserRefHandle || !browser) {
					spdlog::error("[panel] the browser was refused - no panel");
					return false;
				}

				g_handle = handle;
				g_browser = browser;

				// Created hidden: what shows it is the caller's decision, never a
				// side effect of creation.
				g_browser->SetBrowserVisible(false);
				LogState("created");
				return true;
			}

			bool Exists() const override { return Live() != nullptr; }

			void SetVisible(bool a_visible) override
			{
				if (auto* b = Live()) {
					// Hiding also releases focus - Meridian's own rule.
					b->SetBrowserVisible(a_visible);
				}
			}

			void SetFocused(bool a_focused) override
			{
				// Asked before the page has loaded, Meridian applies it at load end.
				if (auto* b = Live()) {
					b->SetBrowserFocused(a_focused);
				}
			}

			bool IsFocused() override
			{
				auto* b = Live();
				return b && b->IsBrowserFocused();
			}

			void ArmChord(std::uint32_t a_key1, std::uint32_t a_key2) override
			{
				// Registrations belong to one browser instance, so this is called
				// every time the panel is shown, not once. Meridian consumes the
				// chord: it reaches neither the page nor the game.
				if (auto* b = Live()) {
					b->ToggleBrowserFocusByKeys(a_key1, a_key2);
					spdlog::info("[panel] focus chord armed: 0x{:X} + 0x{:X}", a_key1, a_key2);
				}
			}

			void DisarmChord() override
			{
				if (auto* b = Live()) {
					b->ToggleBrowserFocusByKeys(0, 0);
					spdlog::info("[panel] focus chord disarmed");
				}
			}

			bool PageReady() override
			{
				// Meridian's guide: wait for IsPageLoaded() before
				// ExecEventFunction, or the message may never reach the listener.
				auto* b = Live();
				return b && b->IsPageLoaded();
			}

			void Post(const char* a_text) override
			{
				if (auto* b = Live()) {
					b->ExecEventFunction(kEventName, a_text);
				}
			}

			void LogState(const char* a_what) override
			{
				auto* b = Live();
				if (!b) {
					spdlog::info("[panel] {} - no browser", a_what);
					return;
				}
				spdlog::info("[panel] {} - handle {}, browserReady {}, pageLoaded {}, visible {}, focused {}", a_what,
					g_handle, b->IsBrowserReady(), b->IsPageLoaded(), b->IsBrowserVisible(), b->IsBrowserFocused());
			}
		};
	}

	Backend& MeridianBackend()
	{
		static MeridianPanel s_backend;
		return s_backend;
	}
}
