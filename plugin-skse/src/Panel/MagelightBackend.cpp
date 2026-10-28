// MagelightBackend.cpp
// Lifecast

#include "Panel/MagelightBackend.h"

#include "Panel/Bridge.h"
#include "Panel/Panel.h"
#include "Version.h"

#include "MagelightUI/MagelightUI_API.h"

namespace Lifecast::Panel
{
	namespace
	{
		namespace ML = MAGELIGHT_API;
		using Clock = std::chrono::steady_clock;

		constexpr auto kModId = "Lifecast";
		constexpr auto kViewName = "panel";

		// Relative to the game folder; MO2 serves it from the mod's own
		// Magelight\Lifecast\views\. Magelight takes an absolute path or one
		// relative to its own runtime folder, so the absolute one is built
		// from the game's executable.
		constexpr auto kPanelFile = "Data\\Magelight\\Lifecast\\views\\index.html";

		// The page's globals on this backend. bridge.js builds
		// window.Lifecast.send/on over the first two, as on Prisma.
		constexpr auto kSendListener = "lifecastSend";
		constexpr auto kReceiveFunction = "lifecastReceive";
		constexpr auto kChordListener = "lifecastChord";

		// The host this was written and measured against: PostGameTask,
		// SetEscapeCapture and CancelUIModeRequest are all older than it.
		constexpr std::uint32_t kMinHost = ML::PackVersion(1, 5, 0);

		const ML::MagelightApi4* g_api = nullptr;
		ML::ModId                g_mod = 0;
		ML::ViewId               g_view = 0;
		std::string              g_html;
		Clock::time_point        g_createdAt{};
		bool                     g_dead = false;
		bool                     g_shown = false;

		// Game thread writes these from events; the page-chord listener reads
		// them on the render thread.
		std::atomic_bool g_domReady = false;
		std::atomic_bool g_own = false;

		// What the panel last asked for. Held until DOM ready, as on Prisma;
		// read by the page-chord listener.
		std::atomic_bool g_wantFocus = false;

		// A RequestUIMode returned Ok and its UIModeEntered has not come yet.
		bool g_requested = false;

		// The exit chord, as the page's keyCode (VK_F1..VK_F12); 0 = disarmed.
		std::atomic<std::uint32_t> g_chordVk = 0;
		std::atomic<std::uint32_t> g_chordHits = 0;

		// When the panel last took (or asked for) the keyboard, in steady-clock
		// ms. The F4 that brings the panel back is heard by the game, and the
		// same press can then reach the page a few ms after the mode is entered
		// (1.5 E2b boot 3: 2 of 7 returns, 6 ms after UIModeEntered) - read as
		// an exit chord it sends the panel straight back to RaceMenu. A chord
		// key this soon after the panel took the keyboard is that press.
		std::atomic<long long> g_focusAtMs = 0;
		constexpr long long    kChordSettleMs = 250;

		long long NowMs()
		{
			return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();
		}

		long long MsSinceCreate()
		{
			return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - g_createdAt).count();
		}

		const char* LastError()
		{
			if (!g_api || !g_mod || !g_api->GetLastErrorMessage) {
				return "";
			}
			const char* e = g_api->GetLastErrorMessage(g_mod);
			return e ? e : "";
		}

		bool OwnsMode()
		{
			return g_api && g_mod && g_api->GetUIModeOwner() == g_mod;
		}

		// F1-F12 as DirectInput scan codes (RE::BSKeyboardDevice::Keys) to
		// the browser's keyCode. Nothing else can be heard by the page.
		std::uint32_t DikToVk(std::uint32_t a_dik)
		{
			if (a_dik >= 0x3B && a_dik <= 0x44) {
				return 0x70 + (a_dik - 0x3B);  // F1..F10
			}
			if (a_dik == 0x57) {
				return 0x7A;  // F11
			}
			if (a_dik == 0x58) {
				return 0x7B;  // F12
			}
			return 0;
		}

		const char* EventName(ML::Event a_type)
		{
			switch (a_type) {
			case ML::Event::ViewDomReady:   return "ViewDomReady";
			case ML::Event::ViewLoadFailed: return "ViewLoadFailed";
			case ML::Event::ViewReloaded:   return "ViewReloaded";
			case ML::Event::ViewDestroyed:  return "ViewDestroyed";
			case ML::Event::UIModeEntered:  return "UIModeEntered";
			case ML::Event::UIModeExited:   return "UIModeExited";
			case ML::Event::FocusDenied:    return "FocusDenied";
			case ML::Event::DisplayResized: return "DisplayResized";
			case ML::Event::ConsoleMessage: return "ConsoleMessage";
			case ML::Event::RenderDead:     return "RenderDead";
			case ML::Event::HostShutdown:   return "HostShutdown";
			case ML::Event::UIModeRefused:  return "UIModeRefused";
			default:                        return "?";
			}
		}

		// Game thread.
		void IssueFocus(const char* a_when)
		{
			if (!g_api || !g_view || !g_wantFocus) {
				return;
			}
			// Reclaim: a release of ours that has not run yet is withdrawn
			// instead of exiting the mode right after this entry.
			const auto r = g_api->RequestUIMode(g_view, ML::kUIModeFlagReclaim);
			g_requested = r == ML::Result::Ok;
			if (g_requested) {
				g_focusAtMs = NowMs();
			}
			spdlog::info("[magelight] focus ({}): RequestUIMode -> {}{}{}", a_when, static_cast<int>(r),
				r == ML::Result::Ok ? "" : " - ", r == ML::Result::Ok ? "" : LastError());
		}

		// Game thread.
		void GiveFocusBack(const char* a_why)
		{
			if (!g_api || !g_mod) {
				return;
			}
			if (g_requested && !g_own) {
				// The entry has not run yet: withdraw it before any release, as
				// the header asks.
				const auto c = g_api->CancelUIModeRequest(g_mod);
				spdlog::info("[magelight] focus released ({}): the entry had not run - CancelUIModeRequest -> {}", a_why,
					static_cast<int>(c));
			}
			g_requested = false;
			if (OwnsMode()) {
				const auto r = g_api->ReleaseUIMode(g_mod);
				spdlog::info("[magelight] focus released ({}): ReleaseUIMode -> {}", a_why, static_cast<int>(r));
			}
		}

		void OnDomReadyGameThread()
		{
			g_domReady = true;
			spdlog::info("[magelight] DOM ready, {} ms after CreateViewEx", MsSinceCreate());
			// Escape belongs to the page while it holds the mode, as on
			// Meridian: it closes what the page has open, and the panel keeps
			// the keyboard.
			const auto e = g_api->SetEscapeCapture(g_view, true);
			spdlog::info("[magelight] SetEscapeCapture(on) -> {}", static_cast<int>(e));
			if (g_wantFocus) {
				IssueFocus("held until DOM ready");
			}
		}

		// Game thread (CallbackThread::GameThread).
		void OnEvent(const ML::EventData* a_ev, void*)
		{
			if (!a_ev) {
				return;
			}
			const bool ours = g_view != 0 && a_ev->view == g_view;
			const char* detail = a_ev->detail ? a_ev->detail : "";
			switch (a_ev->type) {
			case ML::Event::ViewDomReady:
				if (ours) {
					OnDomReadyGameThread();
				}
				return;
			case ML::Event::ViewLoadFailed:
				if (ours) {
					spdlog::error("[magelight] the page did not load: {}", detail);
				}
				return;
			case ML::Event::UIModeEntered:
				if (ours) {
					g_own = true;
					g_requested = false;
					g_focusAtMs = NowMs();
					spdlog::info("[magelight] UI mode entered{}{}", *detail ? " - " : "", detail);
				}
				return;
			case ML::Event::UIModeExited:
				if (ours || g_own) {
					const bool wanted = g_wantFocus;
					g_own = false;
					g_requested = false;
					g_wantFocus = false;
					spdlog::info("[magelight] UI mode left{}{}{}", *detail ? " - " : "", detail,
						wanted ? " (the panel still wanted it: the focus watch puts RaceMenu back)" : "");
				}
				return;
			case ML::Event::UIModeRefused:
				if (ours) {
					g_requested = false;
					spdlog::warn("[magelight] UI mode refused: {}", detail);
				}
				return;
			case ML::Event::FocusDenied:
				spdlog::info("[magelight] another mod asked for UI mode while the panel holds it: {}", detail);
				return;
			case ML::Event::RenderDead:
				g_dead = true;
				spdlog::error("[magelight] the overlay disabled itself for this session - no panel");
				return;
			case ML::Event::ConsoleMessage:
				if (ours && a_ev->x == 2) {
					spdlog::warn("[magelight] page error (line {}): {}", a_ev->y, detail);
				}
				return;
			default:
				if (ours) {
					spdlog::info("[magelight] event {} ({}, {}) {}", EventName(a_ev->type), a_ev->x, a_ev->y, detail);
				}
				return;
			}
		}

		void OnHostLog(int a_level, const char* a_message, void*)
		{
			const char* m = a_message ? a_message : "";
			if (a_level >= 1) {
				spdlog::warn("[magelight] host: {}", m);
			} else {
				spdlog::info("[magelight] host: {}", m);
			}
		}

		// Game thread, from PostGameTask.
		void ReceiveTask(void* a_user)
		{
			std::unique_ptr<std::string> text(static_cast<std::string*>(a_user));
			Bridge::Receive(text->c_str(), 1, false);
		}

		// Render thread. The argument arrives raw, as the page passed it (a
		// JSON text, not a quoted string - measured), and is not ours past
		// this call.
		void OnPageSend(ML::ViewId, const char* a_arg, void*)
		{
			auto* text = new std::string(a_arg ? a_arg : "");
			if (g_api->PostGameTask(&ReceiveTask, text) != ML::Result::Ok) {
				delete text;
				spdlog::warn("[magelight] a page message could not be passed to the game thread - dropped");
			}
		}

		// Game thread, from PostGameTask.
		void ChordTask(void*)
		{
			g_requested = false;
			ExitChordHeard("magelight");
		}

		// Render thread. The page heard a function key while it held the
		// keyboard; only the armed one counts, and only while the panel holds
		// or wants the mode - so a key the page sees after the swap does not
		// swap twice.
		void OnPageChord(ML::ViewId, const char* a_arg, void*)
		{
			std::uint32_t vk = 0;
			if (a_arg) {
				const char* p = a_arg;
				while (*p == '"' || *p == ' ') {
					++p;
				}
				vk = static_cast<std::uint32_t>(std::strtoul(p, nullptr, 10));
			}
			const auto armed = g_chordVk.load();
			if (armed == 0 || vk != armed) {
				return;
			}
			if (!g_wantFocus && !g_own) {
				spdlog::info("[magelight] page key {} with the panel not focused - nothing done", vk);
				return;
			}
			const auto since = NowMs() - g_focusAtMs.load();
			if (since < kChordSettleMs) {
				spdlog::info("[magelight] page key {} {} ms after the panel took the keyboard - the press that brought it "
							 "back, not an exit; nothing done",
					vk, since);
				return;
			}
			g_wantFocus = false;
			const auto r = g_api->ReleaseUIMode(g_mod);
			const auto n = ++g_chordHits;
			const auto p = g_api->PostGameTask(&ChordTask, nullptr);
			spdlog::info("[magelight] exit chord #{} (page key {}): ReleaseUIMode -> {}; swap posted to the game thread -> {}", n,
				vk, static_cast<int>(r), static_cast<int>(p));
		}

		class MagelightPanel final : public Backend
		{
		public:
			const char* Name() const override { return "magelight"; }
			const char* Framework() const override { return "Magelight UI"; }
			const char* PanelFile() const override { return kPanelFile; }

			bool Available() override { return g_api && g_mod && !g_dead; }

			void StandDown() override
			{
				if (g_api && g_mod) {
					g_api->UnregisterMod(g_mod);
					spdlog::info("[magelight] another backend draws the panel - mod \"{}\" unregistered", kModId);
				}
				g_mod = 0;
			}

			bool Create() override
			{
				if (g_view) {
					return true;
				}
				if (!Available()) {
					spdlog::error("[magelight] Magelight UI is not registered - no panel");
					return false;
				}
				ML::ViewDesc d{};
				d.size = sizeof(ML::ViewDesc);
				d.name = kViewName;
				d.htmlPath = g_html.c_str();
				d.anchor = ML::Anchor::TopLeft;
				d.fullscreen = true;
				d.clickThrough = false;
				d.startVisible = false;
				d.layer = ML::Layer::Panel;
				d.uiScale = 0.0f;
				d.onDomReady = nullptr;  // the ViewDomReady event, on the game thread, carries the same

				g_domReady = false;
				g_createdAt = Clock::now();
				ML::ViewId view = 0;
				const auto r = g_api->CreateViewEx(g_mod, &d, &view);
				spdlog::info("[magelight] CreateViewEx(\"{}\") -> {} view {}{}{}", g_html, static_cast<int>(r), view,
					r == ML::Result::Ok ? "" : " - ", r == ML::Result::Ok ? "" : LastError());
				if (r != ML::Result::Ok || !view) {
					spdlog::error("[magelight] the view was refused - no panel");
					return false;
				}
				g_view = view;
				const auto s = g_api->RegisterJSListenerEx(g_view, kSendListener, &OnPageSend, nullptr);
				const auto c = g_api->RegisterJSListenerEx(g_view, kChordListener, &OnPageChord, nullptr);
				spdlog::info("[magelight] listeners {} -> {}, {} -> {}", kSendListener, static_cast<int>(s), kChordListener,
					static_cast<int>(c));
				g_shown = false;
				LogState("created");
				return true;
			}

			bool Exists() const override { return g_api && g_mod && g_view && !g_dead; }

			void SetVisible(bool a_visible) override
			{
				if (!Exists()) {
					return;
				}
				if (!a_visible && (g_wantFocus || g_own || g_requested)) {
					g_wantFocus = false;
					GiveFocusBack("hidden");
				}
				g_api->ShowView(g_view, a_visible);
				g_shown = a_visible;
			}

			void SetFocused(bool a_focused) override
			{
				if (!Exists()) {
					return;
				}
				if (a_focused) {
					g_wantFocus = true;
					if (!g_domReady) {
						spdlog::info("[magelight] focus asked before DOM ready - held until it is");
						return;
					}
					IssueFocus("asked");
					return;
				}
				const bool had = g_wantFocus || g_own || g_requested;
				g_wantFocus = false;
				if (had) {
					GiveFocusBack("asked");
				}
			}

			bool IsFocused() override { return Exists() && g_own && OwnsMode(); }

			void ArmChord(std::uint32_t a_key1, std::uint32_t a_key2) override
			{
				const auto vk = DikToVk(a_key1);
				if (vk == 0) {
					spdlog::warn("[magelight] exit chord 0x{:X} is not F1-F12 - the page cannot hear it; no way out but Escape",
						a_key1);
				}
				if (a_key2 != 0) {
					spdlog::warn("[magelight] exit chord: the second key 0x{:X} is not supported on Magelight - ignored", a_key2);
				}
				g_chordVk = vk;
				spdlog::info("[magelight] exit chord armed: 0x{:X} (page keyCode {})", a_key1, vk);
			}

			void DisarmChord() override
			{
				g_chordVk = 0;
				spdlog::info("[magelight] exit chord disarmed");
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
					spdlog::info("[magelight] {} - no view", a_what);
					return;
				}
				spdlog::info("[magelight] {} - view {}, domReady {}, shown {}, own {}, owner {} (mod {}), active {}, wantFocus {}, "
							 "requested {}",
					a_what, g_view, g_domReady.load(), g_shown, g_own.load(), g_api->GetUIModeOwner(), g_mod,
					g_api->IsUIModeActive(), g_wantFocus.load(), g_requested);
			}
		};
	}

	Backend& MagelightBackend()
	{
		static MagelightPanel s_backend;
		return s_backend;
	}

	void MagelightStart(const char* a_moment)
	{
		if (g_mod) {
			return;
		}
		static bool s_askedApi = false;
		if (!s_askedApi) {
			s_askedApi = true;
			g_api = ML::RequestApi4();
			if (!g_api) {
				spdlog::info("[magelight] Magelight UI is not loaded or did not answer (API v4 = null)");
				return;
			}
			spdlog::info("[magelight] API v4 = {}, host \"{}\" ({})", static_cast<const void*>(g_api),
				g_api->hostVersion ? g_api->hostVersion : "?", g_api->hostVersionNumber);
		}
		if (!g_api) {
			return;
		}
		if (g_api->hostVersionNumber < kMinHost) {
			static bool s_saidOld = false;
			if (!s_saidOld) {
				s_saidOld = true;
				spdlog::warn("[magelight] host {} is older than {} - not used", g_api->hostVersionNumber, kMinHost);
			}
			return;
		}

		ML::ModDesc d{};
		d.size = sizeof(ML::ModDesc);
		d.modId = kModId;
		d.displayName = "Lifecast";
		d.modVersion = ML::PackVersion(Version::kMajor, Version::kMinor, Version::kPatch);
		d.minHostVersion = kMinHost;
		d.callbackThread = ML::CallbackThread::GameThread;
		d.onEvent = &OnEvent;
		d.onLog = &OnHostLog;
		d.user = nullptr;
		d.sessionName = nullptr;

		ML::ModId id = 0;
		const auto r = g_api->RegisterMod(&d, &id);
		if (r != ML::Result::Ok || !id) {
			spdlog::warn("[magelight] RegisterMod(\"{}\") at {} -> {}", kModId, a_moment, static_cast<int>(r));
			return;
		}
		g_mod = id;

		wchar_t exe[MAX_PATH] = {};
		::GetModuleFileNameW(nullptr, exe, MAX_PATH);
		const auto path = std::filesystem::path(exe).parent_path() / kPanelFile;
		const auto u8 = path.u8string();
		g_html.assign(reinterpret_cast<const char*>(u8.data()), u8.size());
		spdlog::info("[magelight] RegisterMod(\"{}\") at {} -> mod {}; page {}", kModId, a_moment, g_mod, g_html);
	}
}
