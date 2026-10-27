// Panel.cpp
// Lifecast

#include "Panel/Panel.h"

#include "Core/Settings.h"
#include "Panel/Backend.h"
#include "Panel/Bridge.h"
#include "Panel/MeridianBackend.h"
#include "Panel/PrismaBackend.h"
#include "Version.h"

namespace Lifecast::Panel
{
	namespace
	{
		// The order is the choice under "auto": the first that answered wins.
		// A new backend is a row here and a file of its own.
		struct Row
		{
			const char* name;
			Backend& (*get)();
		};

		constexpr std::array<Row, 2> kTable{ {
			{ "prisma", &PrismaBackend },
			{ "meridian", &MeridianBackend },
		} };

		Backend* g_backend = nullptr;
		bool     g_chosen = false;
		bool     g_visible = false;
		bool     g_card = false;

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
	}

	void Choose()
	{
		if (g_chosen) {
			return;
		}
		g_chosen = true;

		const auto setting = Core::Settings::Backend();

		std::string answered;
		for (const auto& row : kTable) {
			const bool ok = row.get().Available();
			answered += fmt::format("{}{} {}", answered.empty() ? "" : ", ", row.name, ok ? "answered" : "absent");
		}

		Backend*    forced = nullptr;
		const char* note = "";
		if (setting != "auto") {
			for (const auto& row : kTable) {
				if (setting == row.name) {
					forced = &row.get();
				}
			}
			if (forced && !forced->Available()) {
				forced = nullptr;
				note = " - the forced backend did not answer, so auto decides";
			}
		}

		if (forced) {
			g_backend = forced;
		} else {
			for (const auto& row : kTable) {
				if (row.get().Available()) {
					g_backend = &row.get();
					break;
				}
			}
		}

		if (!g_backend) {
			spdlog::warn("[panel] backend: setting \"{}\" ({}) -> none - no panel; the Lifecast stays out of the way and "
						 "RaceMenu opens as it always does",
				setting, answered);
			return;
		}
		spdlog::info("[panel] backend: setting \"{}\" ({}) -> {}{}", setting, answered, g_backend->Name(), note);
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
