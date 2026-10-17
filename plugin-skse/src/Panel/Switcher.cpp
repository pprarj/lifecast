// Switcher.cpp
// Lifecast

#include "Panel/Switcher.h"

#include "Edit/FilmPaint.h"
#include "Edit/Session.h"
#include "Panel/Bridge.h"
#include "Panel/Panel.h"
#include "Panel/RaceMenuFilm.h"
#include "Panel/RaceMenuTabs.h"
#include "Panel/Stage.h"

namespace Lifecast::Panel::Switcher
{
	namespace
	{
		using Key = RE::BSKeyboardDevice::Key;
		using Clock = std::chrono::steady_clock;

		// F4 swaps the two editors - the key the players of the previous
		// RaceMenu front-end already know.
		constexpr std::uint32_t kSwapKey = Key::kF4;

		// How long the panel may stay on screen waiting for Meridian to grant
		// focus. Focus asked for before the page has loaded is applied at
		// load end, so this covers a slow first load; past it, something is
		// wrong and the player gets RaceMenu back rather than a panel that
		// does not answer.
		constexpr auto kFocusGrace = std::chrono::seconds(5);

		enum class Mode
		{
			kClosed,    // the RaceSex Menu is not open
			kLifecast,   // the panel is on screen and owns (or is getting) focus
			kRaceMenu,  // RaceMenu's film is on screen, the panel is hidden
			kPortraits, // RaceMenu stays hidden; a window of another mod is in front, the panel is a card
		};

		Mode g_mode = Mode::kClosed;

		// Focus is applied asynchronously, so "not focused" right after the
		// request is normal. Losing focus only counts once it has been seen.
		bool              g_focusSeen = false;
		Clock::time_point g_shownAt{};

		// A native MessageBoxMenu is open over the Lifecast, and the panel has let go of
		// the focus so the box can be answered. A focused browser keeps the keyboard
		// from reaching a native menu and takes the mouse as well (measured in
		// Docs/MEDIDO-E23.md: Esc closes such a box with the panel released, never
		// with it focused). While this is set the focus watch does not count the
		// loss as the player leaving.
		bool g_boxSuspended = false;

		std::uint32_t g_opens = 0;
		std::uint32_t g_swaps = 0;

		// While another mod's window holds the Presets mode, the loads it asks
		// of RaceMenu's editor ("Load onto my character") are heard through the
		// editor's own "loadedPreset" event - the one the Lifecast's loads wait
		// on - so that coming back can re-read the character. Not a dependency:
		// the event is RaceMenu's, and who raised it does not matter here.
		constexpr auto kEditor = "_root.RaceSexMenuBaseInstance.RaceSexPanelsInstance.presetEditor";

		struct LoadHeard
		{
			std::uint32_t heard{ 0 };
			std::uint32_t ok{ 0 };
		};
		LoadHeard g_loads;

		class LoadedPresetHandler : public RE::GFxFunctionHandler
		{
		public:
			void Call(Params& a_params) override
			{
				RE::GFxValue success;
				bool         ok = false;
				if (a_params.argCount > 0 && a_params.args[0].IsObject() &&
					a_params.args[0].GetMember("success", &success) && success.IsBool()) {
					ok = success.GetBool();
				}
				++g_loads.heard;
				g_loads.ok += ok ? 1 : 0;
				spdlog::info("[switch] loadedPreset heard while the panel is away: success {} ({} heard, {} ok)", ok,
					g_loads.heard, g_loads.ok);
			}
		};

		// Alive only between ListenForLoads() and StopListening() or the menu
		// closing, UI thread.
		//
		// The three values are managed: each one's destructor goes through an
		// interface that lives in the film it was made in, and a managed value
		// holds the object, not the film. The menu holds the film, and the film
		// can be gone by the time the close event arrives - releasing the values
		// then writes into a freed film. So the listener holds the film too, and
		// the members are in this order on purpose: the values go first, the
		// film after them.
		struct Listener
		{
			RE::GPtr<RE::GFxMovieView>  movie;
			std::array<RE::GFxValue, 3> args;
		};
		std::optional<Listener> g_listener;

		void ListenForLoads()
		{
			g_loads = {};
			auto*        ui = RE::UI::GetSingleton();
			auto         menu = ui ? ui->GetMenu<RE::RaceSexMenu>() : nullptr;
			auto*        movie = menu ? menu->uiMovie.get() : nullptr;
			RE::GFxValue editor;
			if (!movie || !movie->GetVariable(&editor, kEditor) || !editor.IsObject() ||
				!editor.HasMember("addEventListener")) {
				spdlog::info("[switch] the preset editor cannot be reached - a load from the window will not be heard");
				return;
			}
			static auto  handler = RE::make_gptr<LoadedPresetHandler>();
			RE::GFxValue fn;
			RE::GFxValue scope;
			RE::GFxValue type;
			RE::GFxValue member;
			movie->CreateFunction(&fn, handler.get());
			movie->CreateObject(&scope);
			scope.SetMember("heard", fn);
			movie->CreateString(&type, "loadedPreset");
			movie->CreateString(&member, "heard");
			g_listener.reset();
			g_listener.emplace(Listener{ RE::GPtr<RE::GFxMovieView>(movie), { type, scope, member } });
			const bool added = editor.Invoke("addEventListener", g_listener->args);
			spdlog::info("[switch] listening for loadedPreset while RaceMenu or a window of another mod is in front: {}", added);
		}

		// Returns how many loads were heard that succeeded since ListenForLoads().
		// 0 when nothing was listening: a count left over from an earlier menu
		// session must not read as a load.
		std::uint32_t StopListening()
		{
			if (!g_listener) {
				return 0;
			}
			{
				auto*        ui = RE::UI::GetSingleton();
				auto         menu = ui ? ui->GetMenu<RE::RaceSexMenu>() : nullptr;
				auto*        movie = menu ? menu->uiMovie.get() : nullptr;
				RE::GFxValue editor;
				if (movie && movie == g_listener->movie.get() && movie->GetVariable(&editor, kEditor) &&
					editor.IsObject()) {
					editor.Invoke("removeEventListener", g_listener->args);
				}
				g_listener.reset();
			}
			const auto ok = g_loads.ok;
			spdlog::info("[switch] back in front of the panel: {} load(s) heard while it was away, {} ok", g_loads.heard,
				ok);
			g_loads = {};
			return ok;
		}

		const char* ModeName(Mode a_mode)
		{
			switch (a_mode) {
			case Mode::kClosed:
				return "closed";
			case Mode::kLifecast:
				return "lifecast";
			case Mode::kRaceMenu:
				return "racemenu";
			case Mode::kPortraits:
				return "portraits";
			default:
				return "?";
			}
		}

		// RaceMenu's film is hidden and shown through the engine's movie API.
		// The film itself is never modified; it keeps running underneath and
		// keeps every value the player set there.
		void SetFilmVisible(bool a_visible)
		{
			auto* ui = RE::UI::GetSingleton();
			auto  menu = ui ? ui->GetMenu(RE::RaceSexMenu::MENU_NAME) : nullptr;
			auto* movie = menu ? menu->uiMovie.get() : nullptr;
			if (!movie) {
				spdlog::warn("[switch] RaceMenu film not reachable - cannot {} it",
					a_visible ? "show" : "hide");
				return;
			}

			movie->SetVisible(a_visible);
			spdlog::info("[switch] RaceMenu film visible = {} (read back {})",
				a_visible, movie->GetVisible());
		}

		// RaceMenu is usually opened from the console (showracemenu), which
		// stays open underneath. Once the panel owns the keyboard, the key
		// that would close the console never reaches the game, and the
		// console sits over the Lifecast until the player goes back to
		// RaceMenu. So it is closed before the panel asks for focus.
		void CloseConsole()
		{
			auto* ui = RE::UI::GetSingleton();
			if (!ui || !ui->IsMenuOpen(RE::Console::MENU_NAME)) {
				return;
			}

			auto* queue = RE::UIMessageQueue::GetSingleton();
			if (!queue) {
				spdlog::warn("[switch] the console is open and there is no UI message queue to close it");
				return;
			}

			queue->AddMessage(RE::Console::MENU_NAME, RE::UI_MESSAGE_TYPE::kHide, nullptr);
			spdlog::info("[switch] the console was open - asked it to close");
		}

		// ----- showracemenu typed into the console --------------------------
		//
		// Opened while the console is still open, the editor's second figure is
		// born with no render passes and never draws (D267); opened once the
		// console has left the menu stack, it draws (D268). So the command's
		// executor is ours. With the console open it asks the console to close
		// and returns - nothing opens inside the command - and the input tick
		// opens the editor a moment after the console is gone, by running the
		// game's own showracemenu again, which this executor passes straight
		// through. Every other call - the console closed, the editor already
		// open, that re-run - goes to the game's executor untouched.
		//
		// The engine has no API to register or replace a command: the executor
		// pointer of the command's table entry is rewritten in place, the way
		// SKSE plugins take table entries over.

		constexpr auto kOpenDelay = std::chrono::milliseconds(100);

		// Past this without the console leaving for good, the open is dropped
		// rather than the editor popping up long after the command was typed.
		constexpr auto kOpenGiveUp = std::chrono::seconds(30);

		RE::SCRIPT_FUNCTION::Execute_t* g_showRaceMenuFn = nullptr;

		struct PendingOpen
		{
			bool              on{ false };
			bool              gone{ false };  // the console was read off the stack
			Clock::time_point askedAt{};
			Clock::time_point goneAt{};
			std::uint32_t     reopened{ 0 };
		};
		PendingOpen g_pending;

		// The game's command is running from OpenEditor().
		bool g_rerun = false;

		bool IsOpen(std::string_view a_menu)
		{
			auto* ui = RE::UI::GetSingleton();
			return ui && ui->IsMenuOpen(a_menu);
		}

		long long MsSince(Clock::time_point a_then)
		{
			return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - a_then).count();
		}

		// The game's showracemenu, run again with the console closed: whatever
		// the command does besides showing the menu, it still does.
		void OpenEditor()
		{
			auto* factory = RE::IFormFactory::GetConcreteFormFactoryByType<RE::Script>();
			auto* script = factory ? factory->Create() : nullptr;
			if (!script) {
				auto* queue = RE::UIMessageQueue::GetSingleton();
				if (!queue) {
					spdlog::error("[switch] showracemenu: no script to run the command and no UI message queue - the editor does not open");
					return;
				}
				queue->AddMessage(RE::RaceSexMenu::MENU_NAME, RE::UI_MESSAGE_TYPE::kShow, nullptr);
				spdlog::warn("[switch] showracemenu: no script to run the command - the editor is shown through the UI message queue");
				return;
			}
			script->SetCommand("showracemenu");
			g_rerun = true;
			script->CompileAndRun(RE::PlayerCharacter::GetSingleton());
			g_rerun = false;
			delete script;
		}

		// Runs on every input tick; one bool read when nothing is pending.
		void TickPendingOpen()
		{
			if (!g_pending.on) {
				return;
			}
			if (Clock::now() - g_pending.askedAt > kOpenGiveUp) {
				g_pending.on = false;
				spdlog::warn("[switch] showracemenu: the console did not stay closed within {} s - the editor is not opened",
					kOpenGiveUp.count());
				return;
			}
			if (IsOpen(RE::Console::MENU_NAME)) {
				if (g_pending.gone) {
					g_pending.gone = false;
					++g_pending.reopened;
					spdlog::info("[switch] showracemenu: the console is open again - the editor waits for it to close (#{})",
						g_pending.reopened);
				}
				return;
			}
			if (!g_pending.gone) {
				g_pending.gone = true;
				g_pending.goneAt = Clock::now();
				spdlog::info("[switch] showracemenu: the console left the menu stack {} ms after the command",
					MsSince(g_pending.askedAt));
			}
			if (Clock::now() - g_pending.goneAt < kOpenDelay) {
				return;
			}
			g_pending.on = false;
			if (IsOpen(RE::RaceSexMenu::MENU_NAME)) {
				spdlog::info("[switch] showracemenu: the editor is already open - nothing to open");
				return;
			}
			spdlog::info("[switch] showracemenu: opening the editor {} ms after the console left", MsSince(g_pending.goneAt));
			OpenEditor();
		}

		bool ShowRaceMenuCommand(const RE::SCRIPT_PARAMETER* a_paramInfo, RE::SCRIPT_FUNCTION::ScriptData* a_scriptData,
			RE::TESObjectREFR* a_thisObj, RE::TESObjectREFR* a_containingObj, RE::Script* a_scriptObj,
			RE::ScriptLocals* a_locals, double& a_result, std::uint32_t& a_opcodeOffsetPtr)
		{
			auto* queue = RE::UIMessageQueue::GetSingleton();
			if (g_rerun || !queue || !IsOpen(RE::Console::MENU_NAME) || IsOpen(RE::RaceSexMenu::MENU_NAME)) {
				return g_showRaceMenuFn(a_paramInfo, a_scriptData, a_thisObj, a_containingObj, a_scriptObj, a_locals, a_result,
					a_opcodeOffsetPtr);
			}
			queue->AddMessage(RE::Console::MENU_NAME, RE::UI_MESSAGE_TYPE::kHide, nullptr);
			g_pending = {};
			g_pending.on = true;
			g_pending.askedAt = Clock::now();
			spdlog::info("[switch] showracemenu typed into the open console - it closes the console first; the editor opens {} ms "
						 "after the console has left",
				kOpenDelay.count());
			return true;
		}

		void TakeOverShowRaceMenu()
		{
			if (g_showRaceMenuFn) {
				return;
			}
			auto* command = RE::SCRIPT_FUNCTION::LocateConsoleCommand("ShowRaceMenu");
			if (!command) {
				command = RE::SCRIPT_FUNCTION::LocateScriptCommand("ShowRaceMenu");
			}
			if (!command || !command->executeFunction) {
				spdlog::warn("[switch] the showracemenu command was not found - typed into the console it opens the editor "
							 "over the console, and Compare is not offered there");
				return;
			}
			g_showRaceMenuFn = command->executeFunction;
			RE::SCRIPT_FUNCTION::Execute_t* ours = &ShowRaceMenuCommand;
			REL::safe_write(reinterpret_cast<std::uintptr_t>(&command->executeFunction), &ours, sizeof(ours));
			spdlog::info("[switch] showracemenu taken over: typed into the console, it closes the console before the editor opens");
		}

		void SetMode(Mode a_mode)
		{
			spdlog::info("[switch] {} -> {}", ModeName(g_mode), ModeName(a_mode));
			g_mode = a_mode;
		}

		// Runs on every input tick. Puts RaceMenu back when the panel is on
		// screen but no longer owns focus, or never got it.
		void Watch()
		{
			if (g_mode != Mode::kLifecast || g_boxSuspended) {
				return;
			}

			const bool focused = Panel::IsFocused();

			if (focused) {
				if (!g_focusSeen) {
					g_focusSeen = true;
					const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
						Clock::now() - g_shownAt);
					spdlog::info("[switch] focus granted, {} ms after the panel was shown", ms.count());
				}
				return;
			}

			if (g_focusSeen) {
				spdlog::info("[switch] the panel lost focus - back to RaceMenu");
				ShowRaceMenu();
				return;
			}

			if (Clock::now() - g_shownAt > kFocusGrace) {
				spdlog::warn("[switch] focus was never granted within {} s - back to RaceMenu",
					kFocusGrace.count());
				ShowRaceMenu();
			}
		}

		void OnMenuOpened()
		{
			++g_opens;
			spdlog::info("[switch] RaceSex Menu opened (#{})", g_opens);

			if (!Panel::Ensure()) {
				// No panel this time. RaceMenu stays exactly as it was, and
				// the player loses nothing but the Lifecast.
				SetMode(Mode::kRaceMenu);
				return;
			}

			SetMode(Mode::kRaceMenu);
			ShowLifecast();
		}

		// A message box that is not the Lifecast's own (the Lifecast answers its own the
		// instant the game queues it, see RaceMenuFilm) opens over the panel: give the
		// focus up while it is there and take it back when it is gone.
		void OnBoxOpened()
		{
			if (g_mode != Mode::kLifecast) {
				return;
			}

			// The Lifecast answers the game's own box the instant it is queued, and the
			// event for it can still arrive after the answer: an empty queue is that.
			const auto* box = RE::MessageBoxMenu::GetCurrentMessageBoxData();
			if (!box) {
				spdlog::info("[switch] a message box opened and its queue is empty - answered already, focus kept");
				return;
			}
			spdlog::info("[switch] a message box opened over the Lifecast ('{}') - the panel lets go of the focus",
				box ? box->bodyText.c_str() : "?");
			g_boxSuspended = true;
			Panel::ReleaseFocus();
		}

		void OnBoxClosed()
		{
			if (!g_boxSuspended) {
				return;
			}

			g_boxSuspended = false;
			if (g_mode != Mode::kLifecast) {
				return;
			}

			spdlog::info("[switch] the message box closed - the panel takes the focus back");
			g_focusSeen = false;
			g_shownAt = Clock::now();
			Panel::RequestFocus();
		}

		void OnMenuClosed()
		{
			spdlog::info("[switch] RaceSex Menu closed");
			g_boxSuspended = false;

			// What the page shows for a moment - a refusal, a save preview, a
			// way to RaceMenu - belongs to the menu session it was shown in,
			// and the page is told that session ended. The hidden panel is not
			// that signal: F4 hides it too, and the player comes back to it.
			Bridge::Send("menu-closed");

			// The editor goes with the menu; the listener is only let go of - its
			// values first, then the film it kept alive for them (Listener).
			if (g_listener) {
				auto*      ui = RE::UI::GetSingleton();
				const auto menu = ui ? ui->GetMenu(RE::RaceSexMenu::MENU_NAME) : nullptr;
				spdlog::info("[switch] listener let go with the menu: menu still reachable {}, same film {}",
					menu != nullptr, menu && menu->uiMovie.get() == g_listener->movie.get());
				g_listener.reset();
				spdlog::info("[switch] listener released");
			}

			// The film goes away with the menu. The panel is hidden, not
			// released: re-creating it would make a new browser instance.
			Panel::DisarmFocusChord();
			Panel::SetVisible(false);
			SetMode(Mode::kClosed);
		}

		// "show-racemenu" { "for": what }, where "for" is optional: the
		// control the page hands over to RaceMenu, logged so a round shows
		// which way the player went.
		void OnPageShowRaceMenu(const nlohmann::json& a_data)
		{
			const bool hasFor = a_data.is_object() && a_data.contains("for") && a_data["for"].is_string();
			spdlog::info("[switch] the page asked for RaceMenu{}{}", hasFor ? " - for " : "",
				hasFor ? a_data["for"].get<std::string>() : std::string());
			ShowRaceMenu();
			if (hasFor) {
				RaceMenuTabs::Select(a_data["for"].get<std::string>());
			}
		}

		// RaceMenu Portraits and the menu framework it draws with, both loaded.
		// By module, like the OBody check: no dependency, and without them the
		// page has no tab to show.
		bool PortraitsLoaded()
		{
			return REX::W32::GetModuleHandleW(L"RaceMenuPortraits.dll") &&
			       REX::W32::GetModuleHandleW(L"SKSEMenuFramework.dll");
		}

		// "portraits-get": the page asking whether its second tab exists.
		void OnPagePortraitsGet(const nlohmann::json&)
		{
			const bool loaded = PortraitsLoaded();
			spdlog::info("[switch] portraits tab: {}", loaded ? "RaceMenu Portraits and its menu framework are loaded - offered"
			                                                  : "not offered (a module is not loaded)");
			Bridge::Send("portraits", { { "available", loaded } });
		}

		// "racemenu-categories-get" with no data: the page asking what
		// categories RaceMenu actually has on this install. The answer travels
		// back as its own "racemenu-categories" message - the bridge is
		// fire-and-forget both ways.
		void OnPageCategories(const nlohmann::json&)
		{
			RaceMenuTabs::Report();
		}

		class MenuSink : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
		{
		public:
			static MenuSink* GetSingleton()
			{
				static MenuSink s_instance;
				return &s_instance;
			}

			RE::BSEventNotifyControl ProcessEvent(
				const RE::MenuOpenCloseEvent* a_event,
				RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
			{
				if (a_event && a_event->menuName == RE::RaceSexMenu::MENU_NAME) {
					if (a_event->opening) {
						OnMenuOpened();
					} else {
						OnMenuClosed();
					}
				} else if (a_event && a_event->menuName == RE::MessageBoxMenu::MENU_NAME) {
					if (a_event->opening) {
						OnBoxOpened();
					} else {
						OnBoxClosed();
					}
				}
				return RE::BSEventNotifyControl::kContinue;
			}

		private:
			MenuSink() = default;
		};

		class InputSink : public RE::BSTEventSink<RE::InputEvent*>
		{
		public:
			static InputSink* GetSingleton()
			{
				static InputSink s_instance;
				return &s_instance;
			}

			RE::BSEventNotifyControl ProcessEvent(
				RE::InputEvent* const* a_event,
				RE::BSTEventSource<RE::InputEvent*>*) override
			{
				Watch();
				TickPendingOpen();

				// This is the input tick that drives the stage, not a frame
				// tick: it fires on input dispatch, measured between 56.7
				// and 64.6 a second whether the panel holds focus or not,
				// so it is also where the character view slides. A second
				// tick of its own would buy nothing extra; the stage reads
				// two atomics and returns unless something is moving or the
				// one-figure rule is on and watching its subject drift.
				Stage::Tick();

				if (!a_event || (g_mode != Mode::kRaceMenu && g_mode != Mode::kPortraits)) {
					// With the Lifecast on screen, F4 never gets here: it is
					// the panel's focus chord and Meridian consumes it.
					return RE::BSEventNotifyControl::kContinue;
				}

				for (auto* event = *a_event; event; event = event->next) {
					if (event->GetDevice() != RE::INPUT_DEVICES::kKeyboard) {
						continue;
					}

					const auto* button = event->AsButtonEvent();
					if (button && button->IsDown() && button->GetIDCode() == kSwapKey) {
						spdlog::info("[switch] F4 from the game");
						ShowLifecast();
						break;
					}
				}

				return RE::BSEventNotifyControl::kContinue;
			}

		private:
			InputSink() = default;
		};

		bool g_installed = false;
	}

	void Install()
	{
		if (g_installed) {
			return;
		}

		auto* ui = RE::UI::GetSingleton();
		if (!ui) {
			spdlog::error("[switch] no UI singleton - the Lifecast will never open");
			return;
		}
		ui->AddEventSink<RE::MenuOpenCloseEvent>(MenuSink::GetSingleton());

		auto* input = RE::BSInputDeviceManager::GetSingleton();
		if (input) {
			input->AddEventSink(InputSink::GetSingleton());
		} else {
			// The panel still opens with the menu and the page can still send
			// the player back, but F4 from RaceMenu is dead and so is the
			// focus watch. Said here so it is not discovered by pressing F4.
			spdlog::error("[switch] no BSInputDeviceManager - F4 from RaceMenu will not work");
		}

		Bridge::On("show-racemenu", &OnPageShowRaceMenu);
		Bridge::On("racemenu-categories-get", &OnPageCategories);
		Bridge::On("portraits-get", &OnPagePortraitsGet);
		Bridge::On("show-portraits", [](const nlohmann::json&) { ShowPortraits(); });

		TakeOverShowRaceMenu();

		g_installed = true;
		spdlog::info("[switch] installed - F4 swaps the Lifecast and RaceMenu");
	}

	void ShowLifecast()
	{
		if ((g_mode != Mode::kRaceMenu && g_mode != Mode::kPortraits) || !Panel::Browser()) {
			return;
		}

		++g_swaps;
		spdlog::info("[switch] show the Lifecast (swap #{})", g_swaps);
		// The window of another mod, or RaceMenu's own film: either may have loaded a
		// preset while the panel was away (the film's own Presets tab opens the same
		// window, and its own Load button is the same event).
		const bool fromWindow = g_mode == Mode::kPortraits || g_mode == Mode::kRaceMenu;

		// The order is the point. The way out exists before the panel can
		// take the keyboard; focus is the very last thing asked for.
		CloseConsole();
		Panel::ArmFocusChord(kSwapKey, 0);
		Panel::SetVisible(true);
		SetFilmVisible(false);
		RaceMenuTabs::LeavePresets();

		g_focusSeen = false;
		g_shownAt = Clock::now();
		SetMode(Mode::kLifecast);

		Panel::RequestFocus();

		if (fromWindow) {
			// The window may have loaded a preset onto the character while the
			// panel was a card: the panel's values are from before it. The load
			// is RaceMenu's, so what the panel does is what it does for any
			// load it did not make itself - read the character again.
			if (auto* tasks = SKSE::GetTaskInterface()) {
				tasks->AddUITask([]() {
					if (StopListening() > 0) {
						Edit::Session::Replaced("a preset was loaded by a window of another mod");
					}
				});
			}
		}
	}

	void ShowPortraits()
	{
		if (g_mode != Mode::kLifecast || !Panel::Browser()) {
			return;
		}
		if (!PortraitsLoaded()) {
			spdlog::warn("[switch] show-portraits asked, and RaceMenu Portraits or its menu framework is not loaded");
			return;
		}

		++g_swaps;
		spdlog::info("[switch] show the Presets window of another mod over the Lifecast (swap #{})", g_swaps);

		// The film stays hidden: the window is drawn over the scene, not over
		// RaceMenu. Focus goes to the game (the card), so the keyboard and the
		// mouse reach the window, and F4 comes back through the input tick.
		Panel::ShowCard("F4");
		Panel::DisarmFocusChord();
		RaceMenuTabs::Select("presets");
		if (auto* tasks = SKSE::GetTaskInterface()) {
			tasks->AddUITask([]() { ListenForLoads(); });
		}

		SetMode(Mode::kPortraits);
	}

	void ShowRaceMenu()
	{
		if (g_mode != Mode::kLifecast) {
			return;
		}

		++g_swaps;
		spdlog::info("[switch] show RaceMenu (swap #{})", g_swaps);

		// The panel gives its focus up and shows only its card. The chord comes
		// off with it, so F4 reaches the game again and brings the Lifecast back.
		Panel::ShowCard("F4");
		Panel::DisarmFocusChord();
		SetFilmVisible(true);
		RaceMenuFilm::Sync();
		Edit::FilmPaint::Sync("switch to RaceMenu");
		if (auto* tasks = SKSE::GetTaskInterface()) {
			tasks->AddUITask([]() { ListenForLoads(); });
		}

		SetMode(Mode::kRaceMenu);
	}
}
