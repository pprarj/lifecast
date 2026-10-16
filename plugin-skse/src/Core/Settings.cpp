// Settings.cpp
// Lifecast

#include "Core/Settings.h"

#include "Panel/Bridge.h"

#include <fstream>
#include <sstream>

namespace Lifecast::Core::Settings
{
	namespace
	{
		using json = nlohmann::json;
		namespace fs = std::filesystem;

		// The version of the file's shape, written into it. A file with a
		// newer one is still read key by key: what this version knows is
		// kept, the rest is ignored.
		constexpr int kFileVersion = 1;

		struct Key
		{
			const char* name;
			json        fallback;
			bool (*accepts)(const json&);
		};

		bool IsSortOrder(const json& a_value)
		{
			return a_value.is_string() && (a_value == "name" || a_value == "time");
		}

		// Mirrors panel.js's SCALE_MIN/SCALE_MAX (H-5). The two are not the
		// same file by construction - this side only has to reject what would
		// crash CSS math, not police the UI's own bounds - but they are the
		// same NUMBERS today, and a future change to one is a change to both.
		bool IsPanelScale(const json& a_value)
		{
			return a_value.is_number() && a_value.get<double>() >= 0.8 && a_value.get<double>() <= 2.5;
		}

		// Every value under the key has to be a bool; the key names themselves
		// are section keys from the page, which this module does not know and
		// does not validate - an unknown one is simply a bit nobody reads yet.
		bool IsSectionsOpen(const json& a_value)
		{
			if (!a_value.is_object()) {
				return false;
			}
			for (const auto& [name, v] : a_value.items()) {
				if (!v.is_boolean()) {
					return false;
				}
			}
			return true;
		}

		const std::array<Key, 3> kKeys{ {
			{ "presetsSort", "name", &IsSortOrder },
			{ "panelScale", 1.0, &IsPanelScale },
			{ "sectionsOpen", json::object(), &IsSectionsOpen },
		} };

		json g_values = json::object();

		fs::path Path() { return fs::path(kFolder) / kFile; }

		json Defaults()
		{
			json out = json::object();
			for (const auto& k : kKeys) {
				out[k.name] = k.fallback;
			}
			return out;
		}

		const Key* Find(const std::string& a_name)
		{
			for (const auto& k : kKeys) {
				if (a_name == k.name) {
					return &k;
				}
			}
			return nullptr;
		}

		void Load()
		{
			g_values = Defaults();

			const auto      path = Path();
			std::error_code ec;
			if (!fs::is_regular_file(path, ec)) {
				spdlog::info("[settings] no {} - every value is its default", path.string());
				return;
			}

			std::ifstream in(path, std::ios::binary);
			std::stringstream text;
			text << in.rdbuf();
			json file;
			try {
				file = json::parse(text.str());
			} catch (const std::exception& e) {
				spdlog::warn("[settings] {} is not JSON ({}) - every value is its default; the file is left "
							 "until a value changes",
					path.string(), e.what());
				return;
			}
			if (!file.is_object()) {
				spdlog::warn("[settings] {} is not a JSON object - every value is its default; the file is left "
							 "until a value changes",
					path.string());
				return;
			}

			for (const auto& k : kKeys) {
				const auto it = file.find(k.name);
				if (it == file.end()) {
					continue;
				}
				if (k.accepts(*it)) {
					g_values[k.name] = *it;
				} else {
					spdlog::warn("[settings] {} = {} is not accepted - {} is used", k.name, it->dump(), k.fallback.dump());
				}
			}
			for (const auto& [name, value] : file.items()) {
				if (name != "version" && !Find(name)) {
					spdlog::info("[settings] {} is not a key this version knows - ignored", name);
				}
			}
			spdlog::info("[settings] read {} (version {}): {}", path.string(), file.value("version", 0),
				g_values.dump());
		}

		bool Write()
		{
			const auto      path = Path();
			const auto      temp = fs::path(kFolder) / (std::string(kFile) + ".tmp");
			std::error_code ec;
			fs::create_directories(kFolder, ec);
			if (ec) {
				spdlog::warn("[settings] {} could not be created ({}) - the value holds for this game only", kFolder,
					ec.message());
				return false;
			}

			json file = g_values;
			file["version"] = kFileVersion;
			{
				std::ofstream out(temp, std::ios::binary | std::ios::trunc);
				out << file.dump(2) << '\n';
				out.close();
				if (!out) {
					spdlog::warn("[settings] {} could not be written - the value holds for this game only",
						temp.string());
					fs::remove(temp, ec);
					return false;
				}
			}
			fs::rename(temp, path, ec);
			if (ec) {
				spdlog::warn("[settings] {} could not replace {} ({}) - the value holds for this game only",
					temp.string(), path.string(), ec.message());
				fs::remove(temp, ec);
				return false;
			}
			// Only the path that was asked for is logged, relative to the game
			// folder. Under Mod Organizer that folder is virtual and the file
			// lands where its virtual file system puts it (its overwrite
			// folder, for a file no mod ships) - a place this code never sees.
			// Resolving the path here would print the game folder, which is
			// where the file is NOT; so no absolute path is printed at all.
			spdlog::info("[settings] wrote {} (relative to the game folder; under a mod manager the file "
						 "is wherever its virtual file system put it)",
				path.string());
			return true;
		}

		void Send() { Panel::Bridge::Send("settings", g_values); }

		void OnGet(const json&) { Send(); }

		void OnSet(const json& a_data)
		{
			const auto name = a_data.is_object() && a_data.contains("key") && a_data["key"].is_string() ?
			                      a_data["key"].get<std::string>() :
			                      std::string();
			const auto* key = Find(name);
			if (!key) {
				spdlog::warn("[settings] settings-set for an unknown key \"{}\" - refused", name);
				Send();
				return;
			}
			const auto value = a_data.contains("value") ? a_data["value"] : json();
			if (!key->accepts(value)) {
				spdlog::warn("[settings] {} = {} is not accepted - kept {}", name, value.dump(), g_values[name].dump());
				Send();
				return;
			}
			if (g_values[name] == value) {
				Send();
				return;
			}
			spdlog::info("[settings] {}: {} -> {}", name, g_values[name].dump(), value.dump());
			g_values[name] = value;
			Write();
			Send();
		}

		bool g_installed = false;
	}

	void Install()
	{
		if (g_installed) {
			return;
		}
		Load();
		Panel::Bridge::On("settings-get", &OnGet);
		Panel::Bridge::On("settings-set", &OnSet);
		g_installed = true;
		spdlog::info("[settings] installed - {} key(s)", kKeys.size());
	}
}
