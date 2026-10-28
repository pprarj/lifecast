// Overlays.cpp
// Lifecast

#include "Edit/Overlays.h"

#include "Core/ListSig.h"
#include "Core/RaceMenuSync.h"
#include "Core/SkeeLink.h"
#include "Edit/Session.h"
#include "Edit/Tints.h"
#include "Panel/Bridge.h"

#include <algorithm>
#include <string_view>

namespace Lifecast::Edit::Overlays
{
	namespace
	{
		using Location = Skee::IOverlayInterface::OverlayLocation;
		using OverlayType = Skee::IOverlayInterface::OverlayType;

		// The node override keys RaceMenu writes an overlay layer with. See
		// the header for what each one holds.
		constexpr Skee::skee_u16 kGlowColor = 0;
		constexpr Skee::skee_u16 kGlowMultiple = 1;
		constexpr Skee::skee_u16 kTintColor = 7;
		constexpr Skee::skee_u16 kAlpha = 8;
		constexpr Skee::skee_u16 kTexture = 9;

		// RaceMenu passes -1 as the index for every key but the texture,
		// meaning "not one of the numbered slots". The interface takes that
		// index as a byte, so -1 arrives as 0xFF and the constant says so
		// rather than hiding a cast at each call.
		constexpr Skee::skee_u8 kNoIndex = 0xFF;
		constexpr Skee::skee_u8 kFirstTexture = 0;

		// What skee64.ini calls sDefaultTexture: the transparent one every
		// layer carries until something is painted on it.
		constexpr auto kNoTexture = "Actors\\Character\\Overlays\\Default.dds";

		// RaceMenu's film, at the same path Panel/RaceMenuFilm reads the
		// slider list from, and the list of paint catalogs on it.
		constexpr auto kCatalogList = "_root.RaceSexMenuBaseInstance.RaceSexPanelsInstance.makeupList";

		struct Area
		{
			const char*   key;
			const char*   label;
			Location      location;
			const char*   nodePrefix;  // the index and "]" follow it
			std::uint32_t catalog;     // which of the film's five paint lists
			std::uint32_t armorMask;   // RevertOverlay's mask; 0 = a head overlay
			int           tintType;    // RaceMenu's TINT_TYPE_*PAINT for the area
			const char*   filmFunction;  // what RaceMenu's script calls to list the layers
		};

		// The four areas, in the order RaceMenu lists their categories. The
		// node prefixes and the catalog indices are the film's and SKEE's,
		// not ours: the prefixes are only a fallback for GetOverlayFormat
		// (NodeName below), and the catalog index 0 is war paint, which is a
		// tint mask and not an overlay - see the header.
		constexpr Area kAreas[] = {
			{ "body", "Body Paint", Location::Body, "Body [Ovl", 1, 4, 256, "RSM_AddBodyTints" },
			{ "hand", "Hand Paint", Location::Hand, "Hands [Ovl", 2, 8, 257, "RSM_AddHandTints" },
			{ "foot", "Foot Paint", Location::Feet, "Feet [Ovl", 3, 128, 258, "RSM_AddFeetTints" },
			{ "face", "Face Paint", Location::Face, "Face [Ovl", 4, 0, 259, "RSM_AddFaceTints" },
		};

		constexpr std::size_t kCatalogCount = 5;

		std::uint32_t g_sets = 0;

		// Asks SKEE for one of its interfaces and keeps it only when the version
		// it reports is the one the vendored header describes. The vtable of an
		// older interface is laid out differently (RaceMenu 0.4.16 for 1.5.97
		// ships both of these as version 1), so a call through the newer header
		// lands on the wrong slot, with no compile error and no immediate crash.
		// GetVersion is the first function after the destructor in the base
		// IPluginInterface in every revision of the header, so reading it is safe
		// whatever the version; everything past it is not. An interface below the
		// required version is dropped, and the callers already treat nullptr as
		// "paint overlays disabled".
		template <class T>
		T* AskSkee(const char* a_name, std::uint32_t a_required)
		{
			auto* map = Core::SkeeLink::GetInterfaceMap();
			auto* raw = map ? map->QueryInterface(a_name) : nullptr;
			if (!raw) {
				spdlog::info("[overlays] SKEE {} interface: not found - paint overlays disabled", a_name);
				return nullptr;
			}
			const auto version = raw->GetVersion();
			if (version < a_required) {
				spdlog::warn("[overlays] SKEE {} interface: version {}, header describes {} - refused, paint overlays disabled",
					a_name, version, a_required);
				return nullptr;
			}
			spdlog::info("[overlays] SKEE {} interface: found, version {}, header describes {} - accepted",
				a_name, version, a_required);
			return static_cast<T*>(raw);
		}

		Skee::IOverlayInterface* Overlay()
		{
			static Skee::IOverlayInterface* s_iface = nullptr;
			static bool                     s_asked = false;
			if (!s_asked) {
				s_asked = true;
				s_iface = AskSkee<Skee::IOverlayInterface>(
					"Overlay", static_cast<std::uint32_t>(Skee::IOverlayInterface::kCurrentPluginVersion));
			}
			return s_iface;
		}

		Skee::IOverrideInterface* Override()
		{
			static Skee::IOverrideInterface* s_iface = nullptr;
			static bool                      s_asked = false;
			if (!s_asked) {
				s_asked = true;
				s_iface = AskSkee<Skee::IOverrideInterface>(
					"Override", static_cast<std::uint32_t>(Skee::IOverrideInterface::kCurrentPluginVersion));
			}
			return s_iface;
		}

		// The same first-base cast Core/SkeeLink and Edit/BodyScales make, for
		// the same reason: TESObjectREFR is Actor's first base, at offset 0,
		// so only the type changes.
		Skee::TESObjectREFR* SkeeRef(RE::PlayerCharacter* a_player)
		{
			return reinterpret_cast<Skee::TESObjectREFR*>(static_cast<RE::TESObjectREFR*>(a_player));
		}

		// SKEE's NiAVObject is its own forward declaration of the engine's,
		// not a base or a derived class of RE::NiAVObject: one object, two
		// names for it, so the address is unchanged and nothing on it is read
		// here - it is handed straight back to SKEE.
		Skee::NiAVObject* SkeeNode(RE::NiAVObject* a_node)
		{
			return reinterpret_cast<Skee::NiAVObject*>(a_node);
		}

		bool IsFemale(RE::PlayerCharacter* a_player)
		{
			auto* npc = a_player->GetActorBase();
			return npc && npc->GetSex() == RE::SEX::kFemale;
		}

		// GetOverlayFormat hands back a printf format ("Body [Ovl%d]"). It is
		// treated as data and never given to printf: a format string from
		// another plugin is not ours to trust, and one stray %s would read a
		// pointer that is not there. The single %d is found and replaced by
		// hand; a format shaped any other way falls back to the area's own
		// prefix, which is what skee64.ini documents.
		std::string NodeName(const Area& a_area, std::uint32_t a_layer)
		{
			auto*             iface = Overlay();
			const char* const format = iface ? iface->GetOverlayFormat(OverlayType::Normal, a_area.location) : nullptr;
			if (format) {
				const std::string_view text(format);
				const auto             at = text.find("%d");
				if (at != std::string_view::npos && text.find('%', at + 2) == std::string_view::npos &&
					text.find('%') == at) {
					return fmt::format("{}{}{}", text.substr(0, at), a_layer, text.substr(at + 2));
				}
			}
			return fmt::format("{}{}]", a_area.nodePrefix, a_layer);
		}

		// Takes whichever of the five SKEE can answer with and keeps it. The
		// interface calls exactly one of these, so "got" says whether the
		// value exists at all.
		class Reader : public Skee::IOverrideInterface::GetVariant
		{
		public:
			void Int(const Skee::skee_i32 a_i) override
			{
				got = true;
				asInt = a_i;
			}
			void Float(const float a_f) override
			{
				got = true;
				asFloat = a_f;
			}
			void String(const char* a_str) override
			{
				got = true;
				asString = a_str ? a_str : "";
			}
			void Bool(const bool) override { got = true; }
			void TextureSet(const Skee::BGSTextureSet*) override { got = true; }

			bool           got = false;
			Skee::skee_i32 asInt = 0;
			float          asFloat = 0.0F;
			std::string    asString;
		};

		class IntValue : public Skee::IOverrideInterface::SetVariant
		{
		public:
			explicit IntValue(Skee::skee_i32 a_v) :
				_v(a_v) {}
			Type           GetType() override { return Type::Int; }
			Skee::skee_i32 Int() override { return _v; }

		private:
			Skee::skee_i32 _v;
		};

		class FloatValue : public Skee::IOverrideInterface::SetVariant
		{
		public:
			explicit FloatValue(float a_v) :
				_v(a_v) {}
			Type  GetType() override { return Type::Float; }
			float Float() override { return _v; }

		private:
			float _v;
		};

		class StringValue : public Skee::IOverrideInterface::SetVariant
		{
		public:
			explicit StringValue(std::string a_v) :
				_v(std::move(a_v)) {}
			Type        GetType() override { return Type::String; }
			const char* String() override { return _v.c_str(); }

		private:
			std::string _v;
		};

		// Whether the character actually carries this layer's node. When it
		// does, the node itself is what the player is looking at and is read
		// first; RaceMenu's own script makes the same distinction.
		bool HasNode(RE::PlayerCharacter* a_player, const std::string& a_node)
		{
			auto* root = a_player->Get3D(false);
			return root && root->GetObjectByName(RE::BSFixedString(a_node)) != nullptr;
		}

		bool ReadKey(RE::PlayerCharacter* a_player, const std::string& a_node, bool a_onNode, bool a_female,
			Skee::skee_u16 a_key, Skee::skee_u8 a_index, Reader& a_out)
		{
			auto* iface = Override();
			auto* ref = SkeeRef(a_player);
			if (!iface) {
				return false;
			}
			if (a_onNode) {
				iface->GetNodeProperty(ref, false, a_node.c_str(), a_key, a_index, a_out);
			} else {
				iface->GetNodeOverride(ref, a_female, a_node.c_str(), a_key, a_index, a_out);
			}
			return a_out.got;
		}

		std::string Hex(std::uint32_t a_rgb)
		{
			return fmt::format("#{:02X}{:02X}{:02X}", (a_rgb >> 16) & 0xFF, (a_rgb >> 8) & 0xFF, a_rgb & 0xFF);
		}

		bool ParseHex(const std::string& a_s, std::uint32_t& a_out)
		{
			if (a_s.size() != 7 || a_s[0] != '#') {
				return false;
			}
			for (std::size_t i = 1; i < 7; ++i) {
				if (!std::isxdigit(static_cast<unsigned char>(a_s[i]))) {
					return false;
				}
			}
			a_out = static_cast<std::uint32_t>(std::stoul(a_s.substr(1), nullptr, 16)) & 0xFFFFFF;
			return true;
		}

		// The name RaceMenu's film shows for a texture: the file's own name,
		// without the folders and without the extension.
		std::string DisplayName(const std::string& a_texture)
		{
			const auto slash = a_texture.find_last_of("\\/");
			const auto start = slash == std::string::npos ? 0 : slash + 1;
			auto       end = a_texture.rfind(".dds");
			if (end == std::string::npos || end < start) {
				end = a_texture.size();
			}
			return a_texture.substr(start, end - start);
		}

		// The transparent texture of an unpainted layer comes back either as
		// skee64.ini writes it or with the "textures\" root in front, in any
		// case; either slash is accepted too (D302).
		bool IsNoTexture(const std::string& a_texture)
		{
			constexpr std::string_view kRoot = "textures\\";
			std::string                t = a_texture;
			std::replace(t.begin(), t.end(), '/', '\\');
			if (t.size() > kRoot.size() && _strnicmp(t.c_str(), kRoot.data(), kRoot.size()) == 0) {
				t.erase(0, kRoot.size());
			}
			return _stricmp(t.c_str(), kNoTexture) == 0;
		}

		bool Painted(const std::string& a_texture)
		{
			return !a_texture.empty() && !IsNoTexture(a_texture);
		}

		struct Paint
		{
			std::string name;
			std::string texture;
		};

		std::mutex                                     g_catalogLock;
		std::array<std::vector<Paint>, kCatalogCount>  g_catalog;
		bool                                           g_catalogRead = false;

		void PushOverlays(const char* a_why);

		std::string FilmString(const RE::GFxValue& a_obj, const char* a_name)
		{
			RE::GFxValue v;
			return a_obj.GetMember(a_name, &v) && v.IsString() ? std::string(v.GetString()) : std::string();
		}

		// Reads the five paint catalogs off RaceMenu's film. Runs on the UI
		// thread, reads only, and leaves the film untouched - the same terms
		// Panel/RaceMenuFilm reads the slider list under.
		void ReadCatalogNow()
		{
			std::array<std::vector<Paint>, kCatalogCount> found;
			auto*                                        ui = RE::UI::GetSingleton();
			auto                                         menu = ui ? ui->GetMenu(RE::RaceSexMenu::MENU_NAME) : nullptr;
			auto*                                        movie = menu ? menu->uiMovie.get() : nullptr;
			RE::GFxValue                                 lists;
			if (!movie || !movie->GetVariable(&lists, kCatalogList) || !lists.IsArray()) {
				spdlog::warn("[overlays] no paint catalog: {} is not an array on the film", kCatalogList);
			} else {
				const auto count = std::min<std::uint32_t>(lists.GetArraySize(), kCatalogCount);
				for (std::uint32_t i = 0; i < count; ++i) {
					RE::GFxValue list;
					if (!lists.GetElement(i, &list) || !list.IsArray()) {
						continue;
					}
					const auto n = list.GetArraySize();
					for (std::uint32_t j = 0; j < n; ++j) {
						RE::GFxValue entry;
						if (!list.GetElement(j, &entry) || !entry.IsObject()) {
							continue;
						}
						const auto texture = FilmString(entry, "texture");
						if (texture.empty()) {
							continue;
						}
						auto name = FilmString(entry, "displayText");
						found[i].push_back({ name.empty() ? DisplayName(texture) : std::move(name), texture });
					}
				}
			}

			// The war paint catalog (index 0) is not offered by this file: it is
			// a tint mask, and Edit/Tints offers it on its War Paint layer
			// (WarPaintCatalog below). See D084.
			spdlog::info("[overlays] paint catalogs read: war {}, body {}, hand {}, feet {}, face {}",
				found[0].size(), found[1].size(), found[2].size(), found[3].size(), found[4].size());

			{
				std::scoped_lock lock(g_catalogLock);
				g_catalog = std::move(found);
				g_catalogRead = true;
			}
			Tints::Resend("the war paint catalog was read");
		}

		void ReadCatalog()
		{
			auto* tasks = SKSE::GetTaskInterface();
			if (!tasks) {
				spdlog::warn("[overlays] no task interface - the paint catalog stays empty");
				std::scoped_lock lock(g_catalogLock);
				g_catalogRead = true;
				return;
			}
			tasks->AddUITask([]() {
				ReadCatalogNow();
				PushOverlays("the paint catalog arrived");
			});
		}

		struct Layer
		{
			std::string texture;
			std::uint32_t color = 0;
			float         alpha = 0.0F;
			std::uint32_t glow = 0;
			float         glowAlpha = 0.0F;
		};

		Layer ReadLayer(RE::PlayerCharacter* a_player, const std::string& a_node, bool a_female)
		{
			Layer      out;
			const bool onNode = HasNode(a_player, a_node);

			Reader texture;
			if (ReadKey(a_player, a_node, onNode, a_female, kTexture, kFirstTexture, texture)) {
				out.texture = texture.asString;
			}
			Reader color;
			if (ReadKey(a_player, a_node, onNode, a_female, kTintColor, kNoIndex, color)) {
				out.color = static_cast<std::uint32_t>(color.asInt) & 0xFFFFFF;
			}
			Reader alpha;
			if (ReadKey(a_player, a_node, onNode, a_female, kAlpha, kNoIndex, alpha) &&
				std::isfinite(alpha.asFloat)) {
				out.alpha = std::clamp(alpha.asFloat, 0.0F, 1.0F);
			}
			Reader glow;
			if (ReadKey(a_player, a_node, onNode, a_female, kGlowColor, kNoIndex, glow)) {
				out.glow = static_cast<std::uint32_t>(glow.asInt) & 0xFFFFFF;
			}
			Reader multiple;
			if (ReadKey(a_player, a_node, onNode, a_female, kGlowMultiple, kNoIndex, multiple) &&
				std::isfinite(multiple.asFloat)) {
				// RaceMenu stores the strength as the alpha byte over ten;
				// the page is given the byte back, as a fraction.
				out.glowAlpha = std::clamp(multiple.asFloat * 10.0F / 255.0F, 0.0F, 1.0F);
			}
			return out;
		}

		const Area* Find(const std::string& a_key)
		{
			for (const auto& a : kAreas) {
				if (a_key == a.key) {
					return &a;
				}
			}
			return nullptr;
		}

		std::uint32_t LayerCount(const Area& a_area)
		{
			auto* iface = Overlay();
			return iface ? iface->GetOverlayCount(OverlayType::Normal, a_area.location) : 0;
		}

		void PushOverlays(const char* a_why)
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player || !Overlay() || !Override() || !player->Get3D(false)) {
				spdlog::info("[overlays] {}: no player skeleton or no SKEE - sending not ready", a_why);
				Panel::Bridge::Send("overlays", { { "ready", false }, { "areas", nlohmann::json::array() } });
				return;
			}

			bool wantCatalog = false;
			{
				std::scoped_lock lock(g_catalogLock);
				wantCatalog = !g_catalogRead;
			}
			if (wantCatalog) {
				ReadCatalog();
			}

			const bool female = IsFemale(player);
			auto       areas = nlohmann::json::array();
			std::string counts;
			std::string emptyRead;
			for (const auto& a : kAreas) {
				const auto n = LayerCount(a);
				if (n == 0) {
					// The player has no layers of this kind - RaceMenu does
					// not show the category either.
					counts += fmt::format("{}{} 0", counts.empty() ? "" : ", ", a.key);
					continue;
				}
				auto          layers = nlohmann::json::array();
				std::uint32_t paintedCount = 0;
				for (std::uint32_t i = 0; i < n; ++i) {
					const auto  node = NodeName(a, i);
					const Layer layer = ReadLayer(player, node, female);
					const bool  painted = Painted(layer.texture);
					paintedCount += painted ? 1 : 0;
					if (!painted && emptyRead.empty()) {
						emptyRead = fmt::format("{} layer {} '{}'", a.key, i, layer.texture);
					}
					layers.push_back({ { "i", i }, { "texture", painted ? layer.texture : "" },
						{ "name", painted ? DisplayName(layer.texture) : "" }, { "color", Hex(layer.color) },
						{ "alpha", layer.alpha }, { "glow", Hex(layer.glow) }, { "glowAlpha", layer.glowAlpha } });
				}
				counts += fmt::format("{}{} {} ({} painted)", counts.empty() ? "" : ", ", a.key, n, paintedCount);
				auto        catalog = nlohmann::json::array();
				std::string canonical;
				std::size_t catalogCount = 0;
				{
					std::scoped_lock lock(g_catalogLock);
					for (const auto& p : g_catalog[a.catalog]) {
						const auto name = Core::ListSig::Sanitize(p.name);
						const auto texture = Core::ListSig::Sanitize(p.texture);
						canonical += name + "\t" + texture + "\n";
						catalog.push_back({ { "name", name }, { "texture", texture } });
						++catalogCount;
					}
				}
				auto catalogSig = Core::ListSig::Stamp(std::string("overlay:") + a.key, canonical, catalogCount);
				areas.push_back({ { "k", a.key }, { "label", a.label }, { "catalog", std::move(catalog) },
					{ "catalogSig", std::move(catalogSig) }, { "layers", std::move(layers) } });
			}
			spdlog::info("[overlays] {}: sending {} areas ({}), female {}{}", a_why, areas.size(), counts, female,
				emptyRead.empty() ? std::string() : fmt::format("; first unpainted: {}", emptyRead));
			Panel::Bridge::Send("overlays", { { "ready", true }, { "areas", std::move(areas) } });
		}

		void Write(RE::PlayerCharacter* a_player, const std::string& a_node, bool a_female, Skee::skee_u16 a_key,
			Skee::skee_u8 a_index, Skee::IOverrideInterface::SetVariant& a_value)
		{
			Override()->AddNodeOverride(SkeeRef(a_player), a_female, a_node.c_str(), a_key, a_index, a_value);
		}

		// Clearing before a texture is written is RaceMenu's own order, and it
		// is not optional: an overlay's other maps (normal, specular) stay as
		// the previous paint left them, and the new diffuse would be worn over
		// them. Head overlays are cleared through a different call - the face
		// is a head part, not an armour addon.
		void ClearOverlay(RE::PlayerCharacter* a_player, const Area& a_area, const std::string& a_node)
		{
			auto* iface = Overlay();
			if (!iface) {
				return;
			}
			if (a_area.armorMask != 0) {
				iface->RevertOverlay(SkeeRef(a_player), a_node.c_str(), a_area.armorMask, a_area.armorMask, false);
			} else {
				// 1 is the face part, 4 the face tint shader - the pair
				// RaceMenu passes for a face overlay.
				iface->RevertHeadOverlay(SkeeRef(a_player), a_node.c_str(), 1, 4, false);
			}
		}

		void Apply(RE::PlayerCharacter* a_player, bool a_immediate)
		{
			auto* root = a_player->Get3D(false);
			if (root) {
				Override()->ApplyNodeOverrides(SkeeRef(a_player), SkeeNode(root), a_immediate);
			}
		}

		void OnOverlaysGet(const nlohmann::json&)
		{
			PushOverlays("the page asked");
		}

		void OnOverlaySet(const nlohmann::json& a_data)
		{
			if (!Session::MenuOpen()) {
				spdlog::warn("[overlays] overlay-set with the RaceSex Menu closed - ignored");
				return;
			}
			if (!a_data.is_object() || !a_data.contains("k") || !a_data["k"].is_string() || !a_data.contains("i") ||
				!a_data["i"].is_number_integer()) {
				spdlog::warn("[overlays] overlay-set without a string \"k\" and an integer \"i\" - ignored");
				return;
			}
			const auto  key = a_data["k"].get<std::string>();
			const Area* area = Find(key);
			if (!area) {
				spdlog::warn("[overlays] overlay-set for unknown area \"{}\" - ignored", key);
				return;
			}
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player || !Overlay() || !Override()) {
				spdlog::warn("[overlays] overlay-set {}: no player or no SKEE - ignored", key);
				return;
			}
			const auto index = a_data["i"].get<int>();
			const auto count = LayerCount(*area);
			if (index < 0 || static_cast<std::uint32_t>(index) >= count) {
				spdlog::warn("[overlays] overlay-set {}: layer {} is outside [0, {}) - ignored", key, index, count);
				return;
			}

			// Read and check everything before writing anything.
			std::uint32_t color = 0;
			const bool    hasColor = a_data.contains("color");
			if (hasColor && (!a_data["color"].is_string() || !ParseHex(a_data["color"].get<std::string>(), color))) {
				spdlog::warn("[overlays] overlay-set {}: \"color\" is not #RRGGBB - ignored", key);
				return;
			}
			std::uint32_t glow = 0;
			const bool    hasGlow = a_data.contains("glow");
			if (hasGlow && (!a_data["glow"].is_string() || !ParseHex(a_data["glow"].get<std::string>(), glow))) {
				spdlog::warn("[overlays] overlay-set {}: \"glow\" is not #RRGGBB - ignored", key);
				return;
			}
			const auto fraction = [&](const char* a_name, bool& a_has, float& a_out) {
				a_has = a_data.contains(a_name);
				if (!a_has) {
					return true;
				}
				if (!a_data[a_name].is_number() || !std::isfinite(a_data[a_name].get<float>())) {
					return false;
				}
				a_out = std::clamp(a_data[a_name].get<float>(), 0.0F, 1.0F);
				return true;
			};
			bool  hasAlpha = false;
			float alpha = 0.0F;
			bool  hasGlowAlpha = false;
			float glowAlpha = 0.0F;
			if (!fraction("alpha", hasAlpha, alpha) || !fraction("glowAlpha", hasGlowAlpha, glowAlpha)) {
				spdlog::warn("[overlays] overlay-set {}: an intensity is not a finite number - ignored", key);
				return;
			}
			std::string texture;
			const bool  hasTexture = a_data.contains("texture");
			if (hasTexture) {
				if (!a_data["texture"].is_string()) {
					spdlog::warn("[overlays] overlay-set {}: \"texture\" is not a string - ignored", key);
					return;
				}
				texture = a_data["texture"].get<std::string>();
			}
			if (!hasColor && !hasGlow && !hasAlpha && !hasGlowAlpha && !hasTexture) {
				spdlog::warn("[overlays] overlay-set {}: nothing to set - ignored", key);
				return;
			}

			const bool  female = IsFemale(player);
			const auto  node = NodeName(*area, static_cast<std::uint32_t>(index));
			const Layer before = ReadLayer(player, node, female);

			if (hasTexture) {
				ClearOverlay(player, *area, node);
				StringValue value(texture.empty() ? kNoTexture : texture);
				Write(player, node, female, kTexture, kFirstTexture, value);
			}
			if (hasColor || hasAlpha) {
				// The packed int and the float are one value written twice,
				// the way RaceMenu writes it - see the header. Whichever half
				// this message did not carry keeps what the layer had.
				const auto  rgb = hasColor ? color : before.color;
				const float a = hasAlpha ? alpha : before.alpha;
				const auto  packed = (static_cast<std::uint32_t>(std::lround(a * 255.0F)) << 24) | rgb;
				IntValue    packedValue(static_cast<Skee::skee_i32>(packed));
				FloatValue  alphaValue(a);
				Write(player, node, female, kTintColor, kNoIndex, packedValue);
				Write(player, node, female, kAlpha, kNoIndex, alphaValue);
			}
			if (hasGlow || hasGlowAlpha) {
				const auto  rgb = hasGlow ? glow : before.glow;
				const float a = hasGlowAlpha ? glowAlpha : before.glowAlpha;
				const auto  byte = static_cast<std::uint32_t>(std::lround(a * 255.0F));
				IntValue    packedValue(static_cast<Skee::skee_i32>((byte << 24) | rgb));
				FloatValue  multipleValue(static_cast<float>(byte) / 10.0F);
				Write(player, node, female, kGlowColor, kNoIndex, packedValue);
				Write(player, node, female, kGlowMultiple, kNoIndex, multipleValue);
			}

			const bool final = a_data.contains("final") && a_data["final"].is_boolean() && a_data["final"].get<bool>();
			Apply(player, final);
			++g_sets;

			if (final) {
				// RaceMenu layers its own paints over the tint, and re-applies
				// them when told the shaders are stale - which is what its own
				// script sends after a change of this kind.
				Core::RaceMenuSync::ShadersInvalidated();
				const Layer after = ReadLayer(player, node, female);
				spdlog::info(
					"[overlays] {} layer {} ({}) = texture '{}', colour {} alpha {:.2f}, glow {} alpha {:.2f} (edit ended)",
					key, index, node, after.texture, Hex(after.color), after.alpha, Hex(after.glow), after.glowAlpha);
				PushOverlays("after a final write");
			}
		}

		void OnShown() { PushOverlays("the panel came on screen"); }

		// How many entries the film's five paint lists hold right now, one
		// number: five GetArraySize calls and nothing read. -1 when the lists
		// are not there.
		int CatalogSize()
		{
			auto*        ui = RE::UI::GetSingleton();
			auto         menu = ui ? ui->GetMenu(RE::RaceSexMenu::MENU_NAME) : nullptr;
			auto*        movie = menu ? menu->uiMovie.get() : nullptr;
			RE::GFxValue lists;
			if (!movie || !movie->GetVariable(&lists, kCatalogList) || !lists.IsArray()) {
				return -1;
			}
			int          total = 0;
			const auto   count = std::min<std::uint32_t>(lists.GetArraySize(), kCatalogCount);
			for (std::uint32_t i = 0; i < count; ++i) {
				RE::GFxValue list;
				if (lists.GetElement(i, &list) && list.IsArray()) {
					total += static_cast<int>(list.GetArraySize());
				}
			}
			return total;
		}

		// The catalogs are filled by scripts after the menu opens, exactly as a
		// mod's tabs are (D091): the round that read them once, when the panel
		// came on screen, saw all five empty. So the film is watched, and the
		// catalogs are read again when their size moves.
		int                                   g_catalogSize = -2;
		std::chrono::steady_clock::time_point g_looked{};

		void OnTick()
		{
			if (!Session::MenuOpen()) {
				g_catalogSize = -2;
				return;
			}
			const auto now = std::chrono::steady_clock::now();
			if (now - g_looked < std::chrono::seconds(1)) {
				return;
			}
			g_looked = now;
			auto* tasks = SKSE::GetTaskInterface();
			if (!tasks) {
				return;
			}
			tasks->AddUITask([]() {
				const auto size = CatalogSize();
				if (size < 0 || size == g_catalogSize) {
					return;
				}
				g_catalogSize = size;
				spdlog::info("[overlays] the film's paint lists hold {} entries - reading them", size);
				ReadCatalogNow();
				PushOverlays("the paint catalog changed");
			});
		}

		void OnOpened()
		{
			g_sets = 0;
			g_catalogSize = -2;
			// The catalog is the film's, and a new menu means a new film.
			std::scoped_lock lock(g_catalogLock);
			g_catalogRead = false;
			for (auto& list : g_catalog) {
				list.clear();
			}
		}

		void OnClosed() { spdlog::info("[overlays] RaceSex Menu closed: {} values written", g_sets); }

		bool g_installed = false;
	}

	void Install()
	{
		if (g_installed) {
			return;
		}
		Session::OnPanelShown(&OnShown);
		Session::OnMenuOpened(&OnOpened);
		Session::OnTick(&OnTick);
		Session::OnMenuClosed(&OnClosed);
		Panel::Bridge::On("overlays-get", &OnOverlaysGet);
		Panel::Bridge::On("overlay-set", &OnOverlaySet);
		g_installed = true;
		spdlog::info("[overlays] installed");
	}

	std::vector<WarPaint> WarPaintCatalog()
	{
		std::vector<WarPaint> out;
		std::scoped_lock      lock(g_catalogLock);
		out.reserve(g_catalog[0].size());
		for (const auto& p : g_catalog[0]) {
			out.push_back({ p.name, p.texture });
		}
		return out;
	}

	std::vector<FilmRows> FilmOverlayRows()
	{
		std::vector<FilmRows> out;
		auto*                 player = RE::PlayerCharacter::GetSingleton();
		if (!player || !Overlay() || !Override()) {
			return out;
		}
		const bool female = IsFemale(player);
		for (const auto& a : kAreas) {
			FilmRows   area{ a.label, a.filmFunction, {} };
			const auto n = LayerCount(a);
			for (std::uint32_t i = 0; i < n; ++i) {
				// The script's reads, raw: the colour and glow ints as SKEE
				// holds them, a key that is not there reading as zero.
				const auto node = NodeName(a, i);
				const bool onNode = HasNode(player, node);
				Reader     rgb;
				Reader     glow;
				Reader     alpha;
				Reader     texture;
				Reader     multiple;
				ReadKey(player, node, onNode, female, kTintColor, kNoIndex, rgb);
				ReadKey(player, node, onNode, female, kGlowColor, kNoIndex, glow);
				ReadKey(player, node, onNode, female, kAlpha, kNoIndex, alpha);
				ReadKey(player, node, onNode, female, kTexture, kFirstTexture, texture);
				ReadKey(player, node, onNode, female, kGlowMultiple, kNoIndex, multiple);
				const auto truncated = [](float a_f) {
					return std::isfinite(a_f) ? static_cast<std::uint32_t>(static_cast<std::int32_t>(a_f)) : 0u;
				};
				const auto color = (static_cast<std::uint32_t>(rgb.asInt) & 0xFFFFFF) |
				                   (truncated(alpha.asFloat * 255.0F) << 24);
				const auto glowed = (truncated(multiple.asFloat * 10.0F) << 24) | static_cast<std::uint32_t>(glow.asInt);
				area.rows.push_back(fmt::format("{};;{};;{};;{}", a.tintType, static_cast<std::int32_t>(color),
					texture.asString.empty() ? kNoTexture : texture.asString, static_cast<std::int32_t>(glowed)));
			}
			out.push_back(std::move(area));
		}
		return out;
	}
}
