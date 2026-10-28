// Panel.cpp
// Lifecast

#include "Panel/Panel.h"

#include "Core/Settings.h"
#include "Panel/Backend.h"
#include "Panel/Bridge.h"
#include "Panel/MagelightBackend.h"
#include "Panel/MeridianBackend.h"
#include "Panel/PrismaBackend.h"
#include "Version.h"

namespace Lifecast::Panel
{
	namespace
	{
		// The order is the choice under "auto": the first whose page is
		// installed and whose framework answered.
		// A new backend is a row here and a file of its own.
		struct Row
		{
			const char* name;
			Backend& (*get)();
		};

		constexpr std::array<Row, 3> kTable{ {
			{ "prisma", &PrismaBackend },
			{ "magelight", &MagelightBackend },
			{ "meridian", &MeridianBackend },
		} };

		Backend* g_backend = nullptr;
		bool     g_chosen = false;
		bool     g_visible = false;
		bool     g_card = false;

		// What the player is told on screen when there is no backend, and
		// whether it was said this game session.
		std::string g_notice;
		bool        g_noticed = false;

		// The Switcher's swap to RaceMenu, for a backend whose page hears the
		// exit chord itself (Magelight).
		void (*g_exitHandler)() = nullptr;

		// The backend's page is in this install: the package was made for it.
		bool PagePresent(Backend& a_backend)
		{
			std::error_code ec;
			return std::filesystem::exists(a_backend.PanelFile(), ec);
		}

		Backend* Live()
		{
			return g_backend && g_backend->Exists() ? g_backend : nullptr;
		}

		// The page says hello when its script starts, and the answer tells it
		// which DLL it is talking to and whether they speak the same
		// protocol. A page that gets no answer at all knows the bridge is
		// down; a page that gets compatible = false knows it is out of date.
		void OnHello(const nlohmann::json& a_data)
		{
			std::uint32_t pageProtocol = 0;
			if (a_data.contains("protocol") && a_data["protocol"].is_number_unsigned()) {
				pageProtocol = a_data["protocol"].get<std::uint32_t>();
			}

			std::string pageVersion = "?";
			if (a_data.contains("page") && a_data["page"].is_string()) {
				pageVersion = a_data["page"].get<std::string>();
			}

			const bool compatible = pageProtocol == Bridge::kProtocolVersion;
			spdlog::info("[panel] hello from page {} speaking protocol v{} - {}", pageVersion, pageProtocol,
				compatible ? "compatible" : "INCOMPATIBLE");

			Bridge::Send("hello", {
				{ "dll", Version::kString },
				{ "protocol", Bridge::kProtocolVersion },
				{ "compatible", compatible },
				{ "visible", g_visible },
				{ "backend", BackendName() },
			});
		}

		// The page says what it drew for the card, read from its own DOM: the
		// backend's state in the log is the backend's, this is the page's.
		void OnCardState(const nlohmann::json& a_data)
		{
			spdlog::info("[panel] card state from the page: {}", a_data.dump());
			if (auto* b = Live()) {
				b->LogState("card state read");
			}
		}

		void LogState(const char* a_what)
		{
			if (auto* b = Live()) {
				b->LogState(a_what);
			} else {
				spdlog::info("[panel] {} - no page", a_what);
			}
		}
	}

	void Init()
	{
		Bridge::On("hello", &OnHello);
		Bridge::On("card-state", &OnCardState);
	}

	void OnInputLoaded()
	{
		PrismaStart();
		MagelightStart("kInputLoaded");
	}

	void Choose()
	{
		if (g_chosen) {
			return;
		}
		g_chosen = true;

		// The second chance Magelight's registration gets, for an install
		// where it was not ready at kInputLoaded.
		MagelightStart("kDataLoaded");

		const auto setting = Core::Settings::Backend();

		std::string answered;
		for (const auto& row : kTable) {
			auto&      b = row.get();
			const bool page = PagePresent(b);
			const bool ok = b.Available();
			answered += fmt::format("{}{} {}/{}", answered.empty() ? "" : ", ", row.name, page ? "page" : "no page",
				ok ? "answered" : "absent");
		}

		const Row* forced = nullptr;
		if (setting != "auto") {
			for (const auto& row : kTable) {
				if (setting == row.name) {
					forced = &row;
				}
			}
		}

		// The one that is missing, when nothing is chosen: the forced one, or
		// under "auto" the first backend whose page this install has.
		Backend* missing = nullptr;
		if (forced) {
			auto& b = forced->get();
			if (b.Available()) {
				g_backend = &b;
			} else {
				missing = &b;
			}
		} else {
			for (const auto& row : kTable) {
				auto& b = row.get();
				if (!PagePresent(b)) {
					continue;
				}
				if (!missing) {
					missing = &b;
				}
				if (b.Available()) {
					g_backend = &b;
					missing = nullptr;
					break;
				}
			}
		}

		for (const auto& row : kTable) {
			if (&row.get() != g_backend) {
				row.get().StandDown();
			}
		}

		if (g_backend) {
			spdlog::info("[panel] backend: setting \"{}\" ({}) -> {}", setting, answered, g_backend->Name());
			if (!PagePresent(*g_backend)) {
				spdlog::warn("[panel] the chosen backend's page is not installed ({}) - the panel will be empty",
					g_backend->PanelFile());
			}
			return;
		}

		// No backend. Never another framework than the one the package is
		// for: its page is the only one installed. The modules stay off
		// (HasBackend()), and the player is told once per game session.
		const std::string why = missing ? fmt::format("{} is not loaded", missing->Framework()) :
		                                  std::string("no backend's page is installed");
		g_notice = missing ? fmt::format("Lifecast: {} is not installed - RaceMenu opens as usual", missing->Framework()) :
		                     std::string("Lifecast: its panel files are not installed - RaceMenu opens as usual");
		const auto line = fmt::format(
			"[panel] backend: setting \"{}\" ({}) -> none - {}; no other backend is tried, the Lifecast stays out of the "
			"way and RaceMenu opens as it always does",
			setting, answered, why);
		if (forced && std::string_view(forced->name) == "meridian") {
			// The one case the Meridian log line stays an error: the setting
			// asks for it by name.
			spdlog::error("{}", line);
		} else {
			spdlog::warn("{}", line);
		}
	}

	bool HasBackend()
	{
		return g_backend != nullptr;
	}

	void OnGameLoaded()
	{
		if (!g_chosen || g_backend || g_noticed || g_notice.empty()) {
			return;
		}
		g_noticed = true;
		RE::SendHUDMessage::ShowHUDMessage(g_notice.c_str());
		spdlog::info("[panel] on-screen notice, once this game session: \"{}\"", g_notice);
	}

	void SetExitHandler(void (*a_handler)())
	{
		g_exitHandler = a_handler;
	}

	void ExitChordHeard(const char* a_backend)
	{
		spdlog::info("[panel] the page heard the exit chord ({}) - the swap goes to the Switcher", a_backend);
		if (g_exitHandler) {
			g_exitHandler();
		}
	}

	const char* BackendName()
	{
		return g_backend ? g_backend->Name() : "none";
	}

	bool Ensure()
	{
		if (!g_chosen) {
			Choose();
		}
		if (!g_backend) {
			return false;
		}
		if (g_backend->Exists()) {
			return true;
		}
		if (!g_backend->Create()) {
			return false;
		}
		g_visible = false;
		return true;
	}

	bool Exists()
	{
		return Live() != nullptr;
	}

	void SetVisible(bool a_visible)
	{
		auto* b = Live();
		if (!b) {
			return;
		}

		if (g_card) {
			g_card = false;
			Bridge::Send("card", { { "on", false } });
		}

		b->SetVisible(a_visible);
		g_visible = a_visible;
		LogState(a_visible ? "shown" : "hidden");

		Bridge::Send("visibility", { { "visible", a_visible } });
	}

	bool IsVisible()
	{
		return Live() && g_visible;
	}

	void ShowCard(const char* a_key)
	{
		auto* b = Live();
		if (!b) {
			return;
		}

		b->SetFocused(false);
		b->SetVisible(true);
		g_visible = false;
		g_card = true;
		LogState("card shown");

		Bridge::Send("visibility", { { "visible", false } });
		Bridge::Send("card", { { "on", true }, { "key", a_key } });
	}

	void ArmFocusChord(std::uint32_t a_key1, std::uint32_t a_key2)
	{
		if (auto* b = Live()) {
			b->ArmChord(a_key1, a_key2);
		}
	}

	void DisarmFocusChord()
	{
		if (auto* b = Live()) {
			b->DisarmChord();
		}
	}

	void RequestFocus()
	{
		if (auto* b = Live()) {
			b->SetFocused(true);
			LogState("focus requested");
		}
	}

	void ReleaseFocus()
	{
		if (auto* b = Live()) {
			b->SetFocused(false);
			LogState("focus released");
		}
	}

	bool IsFocused()
	{
		auto* b = Live();
		return b && b->IsFocused();
	}

	bool PageReady()
	{
		auto* b = Live();
		return b && b->PageReady();
	}

	void Post(const std::string& a_text)
	{
		if (auto* b = Live()) {
			b->Post(a_text.c_str());
		}
	}
}
