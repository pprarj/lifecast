// Appearance.cpp
// Lifecast

#include "Edit/Appearance.h"

#include "Core/ListSig.h"
#include "Core/RaceMenuSync.h"
#include "Core/SkeeLink.h"
#include "Edit/Session.h"
#include "Panel/Bridge.h"
#include "Panel/RaceMenuFilm.h"

namespace Lifecast::Edit::Appearance
{
	namespace
	{
		using HeadPart = RE::BGSHeadPart;
		using HPType = RE::BGSHeadPart::HeadPartType;
		using FaceData = RE::TESNPC::FaceData;
		using RaceFace = RE::TESRace::FaceRelatedData;

		constexpr float kMinWeight = 0.0F;
		constexpr float kMaxWeight = 100.0F;

		// A type list: which NAMA slot it writes, and which of the race's
		// preset counts gives its length. The race counts nose, brows, eyes,
		// lips; the NAMA slots are nose, (unused), eyes, mouth. Brow type is
		// NOT the second NAMA slot - it is the eyebrows head part.
		struct TypeList
		{
			const char* key;
			int         part;
			int         variant;
		};

		constexpr TypeList kTypeLists[] = {
			{ "nose-type", FaceData::Parts::kNose, RaceFace::kNose },
			{ "eye-shape", FaceData::Parts::kEyes, RaceFace::kEyes },
			{ "mouth-shape", FaceData::Parts::kMouth, RaceFace::kLips },
		};

		struct PartList
		{
			const char* key;
			HPType      type;
		};

		constexpr PartList kPartLists[] = {
			{ "hair", HPType::kHair },
			{ "facial-hair", HPType::kFacialHair },
			{ "eyes", HPType::kEyes },
			{ "scar", HPType::kScar },
			{ "brows", HPType::kEyebrows },
		};

		// The lists for one race and sex. Rebuilt whenever either changes,
		// because the page's indices point into them.
		struct Lists
		{
			RE::TESRace*                             race = nullptr;
			RE::SEX                                  sex = RE::SEX::kNone;
			std::map<std::string, std::vector<HeadPart*>> parts;
			std::vector<RE::BGSColorForm*>           hairColors;
			std::vector<RE::BGSTextureSet*>          complexions;
			std::map<std::string, int>               typeCounts;
			bool                                     fromMenu = false;
		} g_lists;

		std::uint32_t g_sets = 0;

		// Which build of the lists the page was last sent, and whether it asked for them
		// again (it does when it attaches). The lists go to the page once per build,
		// not with every look message: a pop-up needs every item, a slider needs one.
		std::uint32_t g_listsGen = 0;
		std::uint32_t g_listsSentGen = 0;
		bool          g_resendLists = false;

		RE::TESNPC* Npc()
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			return player ? player->GetActorBase() : nullptr;
		}

		RaceFace* RaceFaceOf(RE::TESNPC* a_npc)
		{
			auto* race = a_npc ? a_npc->GetRace() : nullptr;
			if (!race) {
				return nullptr;
			}
			const auto sex = static_cast<std::uint32_t>(a_npc->GetSex());
			return sex < RE::SEXES::kTotal ? race->faceRelatedData[sex] : nullptr;
		}

		// Whether the game offers this head part to the NPC's race and sex:
		// playable, not an extra part, made for the sex, and its valid races
		// include the race. The same test the lists below are built with, and
		// the one the race change checks what the player wears against.
		bool PartValidFor(RE::TESNPC* a_npc, const HeadPart* a_part)
		{
			auto* race = a_npc ? a_npc->GetRace() : nullptr;
			if (!race || !a_part) {
				return false;
			}
			const auto sexFlag = a_npc->GetSex() == RE::SEX::kFemale ? HeadPart::Flag::kFemale : HeadPart::Flag::kMale;
			if (!a_part->flags.any(HeadPart::Flag::kPlayable) || a_part->flags.any(HeadPart::Flag::kIsExtraPart)) {
				return false;
			}
			if (!a_part->flags.any(sexFlag)) {
				return false;
			}
			return a_part->validRaces && a_part->validRaces->HasForm(race);
		}

		std::vector<HeadPart*> PartsOf(RE::TESNPC* a_npc, HPType a_type)
		{
			std::vector<HeadPart*> out;
			auto*                  data = RE::TESDataHandler::GetSingleton();
			if (!data || !a_npc || !a_npc->GetRace()) {
				return out;
			}
			for (auto* part : data->GetFormArray<HeadPart>()) {
				if (part && part->type.get() == a_type && PartValidFor(a_npc, part)) {
					out.push_back(part);
				}
			}
			return out;
		}

		// RaceMenu's own lists. The RaceSex Menu builds one array of head
		// parts per type when it opens (runtime data +0x40, seven arrays
		// indexed like HeadPartType: misc, face, eyes, hair, facial hair,
		// scar, eyebrows - the layout skse64's GameMenus.h publishes), and
		// RaceMenu's sliders index into them (skee64's GetSliderPartData
		// reads the same arrays). Measured in the game: the panel's own list, built from
		// the data handler, had the same hair count in another order (the
		// player's hair was 36 here and 1119 there) and one eye fewer (348
		// against 349). Taking the menu's arrays makes both sides count the
		// same way. Empty without the menu open or before it built them.
		std::vector<HeadPart*> MenuPartsOf(HPType a_type)
		{
			std::vector<HeadPart*> out;
			auto*                  ui = RE::UI::GetSingleton();
			auto                   menu = ui ? ui->GetMenu<RE::RaceSexMenu>() : nullptr;
			const auto             slot = static_cast<std::size_t>(a_type);
			if (!menu || slot >= 7) {
				return out;
			}
			// CommonLib types these arrays as void*: every item is checked
			// to be a head part of the asked type, and one that is not
			// drops the whole list, falling back to the data handler's.
			for (auto* item : menu->GetRuntimeData().unk040[slot]) {
				auto* form = static_cast<RE::TESForm*>(item);
				auto* part = form ? form->As<HeadPart>() : nullptr;
				if (!part || part->type.get() != a_type) {
					spdlog::warn("[look] the RaceSex Menu's list {} holds a {:08X} that is not a head part of that "
								 "type - not used",
						slot, form ? form->GetFormID() : 0);
					return {};
				}
				out.push_back(part);
			}
			return out;
		}

		template <class T>
		std::vector<T*> Copy(const RE::BSTArray<T*>* a_list)
		{
			std::vector<T*> out;
			if (a_list) {
				for (auto* item : *a_list) {
					out.push_back(item);
				}
			}
			return out;
		}

		// Rebuilds the lists if the player's race or sex is not the one they
		// were built for. Returns false without a player.
		// The RaceSex Menu builds its head-part arrays when it opens. A race or sex
		// swap done from the panel does not rebuild them, so until the menu is
		// opened again they belong to the old race and sex (boot 147: the page
		// showed "--" in hair, eyes, brows and scar after the sex swap).
		bool g_menuListsStale = false;

		bool EnsureLists(const char* a_why)
		{
			auto* npc = Npc();
			if (!npc) {
				return false;
			}
			auto*      race = npc->GetRace();
			const auto sex = npc->GetSex();
			if (race && race == g_lists.race && sex == g_lists.sex &&
				(g_lists.fromMenu || g_menuListsStale || MenuPartsOf(HPType::kHair).empty())) {
				return true;
			}

			g_lists = {};
			++g_listsGen;
			g_lists.race = race;
			g_lists.sex = sex;
			g_lists.fromMenu = !g_menuListsStale && !MenuPartsOf(HPType::kHair).empty();
			for (const auto& p : kPartLists) {
				g_lists.parts[p.key] = g_lists.fromMenu ? MenuPartsOf(p.type) : PartsOf(npc, p.type);
			}
			spdlog::info("[look]   head parts from {}", g_lists.fromMenu ? "the RaceSex Menu's own lists (RaceMenu's order)"
			                                                               : "the data handler (the menu has no lists yet)");
			auto* rf = RaceFaceOf(npc);
			if (rf) {
				g_lists.hairColors = Copy(rf->availableHairColors);
				g_lists.complexions = Copy(rf->faceDetailsTextureSets);
			}
			for (const auto& t : kTypeLists) {
				g_lists.typeCounts[t.key] = rf ? static_cast<int>(rf->numFlagsSet[t.variant]) : 0;
			}

			spdlog::info("[look] lists rebuilt ({}): race {:08X}, sex {}", a_why,
				race ? race->GetFormID() : 0, static_cast<std::uint32_t>(sex));
			for (const auto& p : kPartLists) {
				spdlog::info("[look]   {} = {} head parts", p.key, g_lists.parts[p.key].size());
			}
			spdlog::info("[look]   hair-color = {}, complexion = {}", g_lists.hairColors.size(),
				g_lists.complexions.size());
			// The count per feature is the race's own; it matched what RaceMenu
			// shows for every race tried, while the morph flag words did not
			// (all bits set).
			spdlog::info("[look]   nose-type = {}, eye-shape = {}, mouth-shape = {} presets",
				g_lists.typeCounts["nose-type"], g_lists.typeCounts["eye-shape"], g_lists.typeCounts["mouth-shape"]);
			return true;
		}

		template <class T>
		int IndexOf(const std::vector<T*>& a_list, const T* a_item)
		{
			for (std::size_t i = 0; i < a_list.size(); ++i) {
				if (a_list[i] == a_item) {
					return static_cast<int>(i);
				}
			}
			return -1;
		}

		nlohmann::json List(int a_count, int a_index)
		{
			return { { "kind", "list" }, { "count", a_count }, { "index", a_index } };
		}

		std::string Hex(const RE::Color& a_c)
		{
			return fmt::format("#{:02X}{:02X}{:02X}", a_c.red, a_c.green, a_c.blue);
		}

		bool ParseHex(const std::string& a_s, RE::Color& a_out)
		{
			if (a_s.size() != 7 || a_s[0] != '#') {
				return false;
			}
			for (std::size_t i = 1; i < 7; ++i) {
				if (!std::isxdigit(static_cast<unsigned char>(a_s[i]))) {
					return false;
				}
			}
			const auto v = std::stoul(a_s.substr(1), nullptr, 16);
			a_out.red = static_cast<std::uint8_t>((v >> 16) & 0xFF);
			a_out.green = static_cast<std::uint8_t>((v >> 8) & 0xFF);
			a_out.blue = static_cast<std::uint8_t>(v & 0xFF);
			return true;
		}

		// What RaceMenu shows for a head part: the name, the editor ID, and the
		// plugin it comes from.
		std::string PartName(const HeadPart* a_part)
		{
			const std::string full = a_part->GetFullName() ? a_part->GetFullName() : "";
			const std::string id = a_part->GetFormEditorID() ? a_part->GetFormEditorID() : "";
			return full.empty() || full == id ? id : id.empty() ? full : full + " (" + id + ")";
		}

		std::string PartSource(HeadPart* a_part)
		{
			const auto* file = a_part->GetFile(0);
			return file ? std::string(file->GetFilename()) : std::string();
		}

		// The four head-part lists the pop-ups offer, whole: every item with its
		// name and its plugin of origin. The look message keeps sending only the
		// count and the current index - this is what the page chooses from.
		void SendLists(const char* a_why)
		{
			auto lists = nlohmann::json::object();
			for (const auto& p : kPartLists) {
				const std::string key = p.key;
				if (key == "scar") {
					continue;
				}
				const auto& list = g_lists.parts[key];
				auto        items = nlohmann::json::array();
				std::string canonical;
				for (auto* part : list) {
					const auto name = Core::ListSig::Sanitize(PartName(part));
					const auto source = Core::ListSig::Sanitize(PartSource(part));
					canonical += name + "\t" + source + "\n";
					items.push_back(nlohmann::json::array({ name, source }));
				}
				auto entry = Core::ListSig::Stamp("look:" + key, canonical, list.size());
				entry["items"] = std::move(items);
				lists[key] = std::move(entry);
			}
			const nlohmann::json message = { { "lists", std::move(lists) } };
			spdlog::info("[look] {}: sending the lists, {} bytes of payload", a_why, message.dump().size());
			Panel::Bridge::Send("look-lists", message);
			g_listsSentGen = g_listsGen;
		}

		void PushLook(const char* a_why)
		{
			auto* npc = Npc();
			if (!npc || !EnsureLists(a_why) || !npc->faceData) {
				spdlog::info("[look] {}: no player face yet - sending not ready", a_why);
				Panel::Bridge::Send("look", { { "ready", false }, { "controls", nlohmann::json::object() } });
				return;
			}

			auto controls = nlohmann::json::object();
			for (const auto& t : kTypeLists) {
				const int count = g_lists.typeCounts[t.key];
				const int value = npc->faceData->parts[t.part];
				controls[t.key] = List(count, value >= 0 && value < count ? value : -1);
			}
			for (const auto& p : kPartLists) {
				const auto& list = g_lists.parts[p.key];
				auto*       current = npc->GetCurrentHeadPartByType(p.type);
				auto        control = List(static_cast<int>(list.size()), IndexOf(list, current));
				// What RaceMenu shows under its slider: the name, the editor
				// ID, and the plugin it comes from.
				if (current) {
					control["name"] = PartName(current);
					control["source"] = PartSource(current);
				}
				controls[p.key] = std::move(control);
			}

			const auto* hrd = npc->headRelatedData;
			auto        hairColor = List(static_cast<int>(g_lists.hairColors.size()),
					   IndexOf(g_lists.hairColors, hrd ? hrd->hairColor : nullptr));
			auto        swatches = nlohmann::json::array();
			for (const auto* c : g_lists.hairColors) {
				swatches.push_back(c ? Hex(c->color) : "#000000");
			}
			hairColor["swatches"] = std::move(swatches);
			// The colour itself, for the picker: a free colour is in no list.
			if (hrd && hrd->hairColor) {
				hairColor["color"] = Hex(hrd->hairColor->color);
			}
			controls["hair-color"] = std::move(hairColor);

			controls["complexion"] = List(static_cast<int>(g_lists.complexions.size()),
				IndexOf(g_lists.complexions, hrd ? hrd->faceDetails : nullptr));

			controls["weight"] = { { "kind", "range" }, { "min", kMinWeight }, { "max", kMaxWeight },
				{ "value", npc->weight } };

			const nlohmann::json look = { { "ready", true }, { "controls", std::move(controls) } };
			spdlog::info("[look] {}: sending {} controls, {} bytes of payload", a_why, look["controls"].size(),
				look.dump().size());
			Panel::Bridge::Send("look", look);
			if (g_resendLists || g_listsSentGen != g_listsGen) {
				g_resendLists = false;
				SendLists(a_why);
			}
		}

		// Returns the index in [0, a_count), or -1 with a log line.
		int CheckIndex(const std::string& a_key, const nlohmann::json& a_v, int a_count)
		{
			if (!a_v.is_number_integer()) {
				spdlog::warn("[look] look-set {}: \"v\" is not an integer - ignored", a_key);
				return -1;
			}
			const int i = a_v.get<int>();
			if (i < 0 || i >= a_count) {
				spdlog::warn("[look] look-set {}: {} is outside [0, {}) - ignored", a_key, i, a_count);
				return -1;
			}
			return i;
		}

		// Head parts and weight are shown without the shared DoReset3D. With
		// physics (SMP) on the hair, every DoReset3D rebuilds the hair's
		// physics nodes without dropping the old ones: the actor grows by a
		// few hundred nodes per redraw, and each redraw costs more than the
		// last. Swapping a part with the game's own Actor::ChangeHeadPart
		// drops the old part's nodes, as RaceMenu's swap does; weight shows
		// with a skin and model update, which adds no node.
		//
		// Both are coalesced like the redraw: the latest value waits for a
		// gap after the last update. A DoReset3D in between shows them too,
		// and clears what was pending.
		using Clock = std::chrono::steady_clock;

		constexpr auto kLightGap = std::chrono::milliseconds(100);

		struct PendingPart
		{
			HeadPart* shown = nullptr;  // on screen, to be replaced
			HeadPart* wanted = nullptr;
		};

		std::map<HPType, PendingPart> g_pendingParts;
		bool                          g_pendingWeight = false;
		Clock::time_point             g_lastPartSwap{};
		Clock::time_point             g_lastWeight{};

		// The game's Actor::ChangeHeadPart(old, new). Not in the CommonLib
		// version this plugin pins; the IDs are the ones later CommonLibSSE-NG
		// releases declare for it.
		void ActorChangeHeadPart(RE::Actor* a_actor, HeadPart* a_old, HeadPart* a_new)
		{
			using func_t = void (*)(RE::Actor*, HeadPart*, HeadPart*);
			static REL::Relocation<func_t> func{ RELOCATION_ID(26468, 27063) };
			func(a_actor, a_old, a_new);
		}

		void ShowParts()
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player || !player->Get3D(false)) {
				g_pendingParts.clear();
				Session::RequestRedraw(true);
				return;
			}
			for (const auto& [type, p] : g_pendingParts) {
				if (p.shown == p.wanted) {
					continue;
				}
				const auto start = Clock::now();
				ActorChangeHeadPart(player, p.shown, p.wanted);
				const auto us = std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - start).count();
				spdlog::info("[look] head part {} shown: Actor::ChangeHeadPart({} -> {}) took {} us",
					static_cast<int>(type), p.shown->GetFormEditorID(), p.wanted->GetFormEditorID(), us);
			}
			g_pendingParts.clear();
			g_lastPartSwap = Clock::now();
		}

		void ShowWeight()
		{
			g_pendingWeight = false;
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* process = player ? player->GetActorRuntimeData().currentProcess : nullptr;
			if (!process) {
				Session::RequestRedraw(true);
				return;
			}
			const auto start = Clock::now();
			// D330: a body with SKEE morphs keeps the geometry SKEE morphed for
			// the old weight through the skin and model update; only a rebuilt
			// 3D takes the new weight, and SKEE morphs it again when it attaches.
			// A body without morphs keeps the lighter update it always had.
			const auto morphs = Core::SkeeLink::BodyMorphState(player);
			const char* how = "skin and model update";
			if (morphs == Core::SkeeLink::BodyWeight::kHasMorphs) {
				player->DoReset3D(true);
				how = "3D reset";
			} else {
				process->Set3DUpdateFlag(static_cast<RE::RESET_3D_FLAGS>(
					static_cast<std::uint32_t>(RE::RESET_3D_FLAGS::kSkin) |
					static_cast<std::uint32_t>(RE::RESET_3D_FLAGS::kModel)));
				process->Update3DModel(player);
			}
			g_lastWeight = Clock::now();
			const auto us = std::chrono::duration_cast<std::chrono::microseconds>(g_lastWeight - start).count();
			spdlog::info("[look] weight shown: {} took {} us (body morphs: {})", how, us, Core::SkeeLink::ToString(morphs));
		}

		void OnTick()
		{
			if (!Session::MenuOpen()) {
				return;
			}
			const auto now = Clock::now();
			if (!g_pendingParts.empty() && now - g_lastPartSwap >= Session::kMinRedrawGap) {
				ShowParts();
			}
			if (g_pendingWeight && now - g_lastWeight >= kLightGap) {
				ShowWeight();
			}
		}

		// The nine values a race or sex change has to leave valid: seven head
		// parts, the hair colour and the complexion. Each is read from the game,
		// and the line says where it was found - the FormID and the size of the
		// list - because "it changed" is not what is asked, "the race accepts it"
		// is. Head parts are checked twice: against what the game offers the race
		// and sex (the data handler) and against the list the panel shows. A part
		// that nobody wears (no scar) is absent, not wrong. Returns how many of
		// the nine belong.
		struct FitType
		{
			const char* key;
			HPType      type;
		};

		constexpr FitType kFitTypes[] = {
			{ "misc", HPType::kMisc },
			{ "face", HPType::kFace },
			{ "eyes", HPType::kEyes },
			{ "hair", HPType::kHair },
			{ "facial-hair", HPType::kFacialHair },
			{ "scar", HPType::kScar },
			{ "brows", HPType::kEyebrows },
		};

		bool g_fitPending = false;

		int FitReport(const char* a_tag)
		{
			auto* npc = Npc();
			if (!npc) {
				return -1;
			}
			int belong = 0;
			for (const auto& t : kFitTypes) {
				auto*       current = npc->GetCurrentHeadPartByType(t.type);
				const auto  valid = PartsOf(npc, t.type);
				const auto  it = g_lists.parts.find(t.key);
				const bool  hasPanel = it != g_lists.parts.end();
				const int   inGame = current ? IndexOf(valid, current) : -1;
				const int   inPanel = current && hasPanel ? IndexOf(it->second, current) : -1;
				const bool  ok = !current || (inGame >= 0 && (!hasPanel || inPanel >= 0));
				belong += ok ? 1 : 0;
				auto* raw = npc->GetHeadPartByType(t.type);
				spdlog::info("[fit] {} {}: {:08X} '{}' | game list {} idx {} | panel list {} idx {} | {} | raw {:08X} overlays {}",
					a_tag, t.key, current ? current->GetFormID() : 0u,
					current && current->GetFormEditorID() ? current->GetFormEditorID() : "",
					valid.size(), inGame, hasPanel ? static_cast<int>(it->second.size()) : -1, inPanel,
					!current ? "ABSENT (none worn)" : ok ? "BELONGS" : "DOES NOT BELONG", raw ? raw->GetFormID() : 0u,
					npc->HasOverlays());
			}
			const auto* hrd = npc->headRelatedData;
			auto*       color = hrd ? hrd->hairColor : nullptr;
			const int   colorIdx = color ? IndexOf(g_lists.hairColors, color) : -1;
			belong += colorIdx >= 0 ? 1 : 0;
			spdlog::info("[fit] {} hair-color: {:08X} {} | list {} idx {} | {}", a_tag, color ? color->GetFormID() : 0u,
				color ? Hex(color->color) : "-", g_lists.hairColors.size(), colorIdx,
				colorIdx >= 0 ? "BELONGS" : "DOES NOT BELONG");
			auto*     set = hrd ? hrd->faceDetails : nullptr;
			const int setIdx = set ? IndexOf(g_lists.complexions, set) : -1;
			belong += setIdx >= 0 ? 1 : 0;
			spdlog::info("[fit] {} complexion: {:08X} | list {} idx {} | {}", a_tag, set ? set->GetFormID() : 0u,
				g_lists.complexions.size(), setIdx, setIdx >= 0 ? "BELONGS" : "DOES NOT BELONG");
			spdlog::info("[fit] {}: {} of 9 belong", a_tag, belong);
			return belong;
		}

		// A DoReset3D rebuilt the actor from its data: what was pending is on
		// screen already. After a race or sex change this is also where the nine
		// values are read again, from the game and not from what was written.
		void OnRedrawn()
		{
			g_pendingParts.clear();
			g_pendingWeight = false;
			if (g_fitPending) {
				g_fitPending = false;
				FitReport("after the redraw");
			}
		}

		// A nose, eye or mouth type or the complexion goes to RaceMenu's own
		// slider callback, which reshapes the head in place. Without it the
		// value is written here and the actor redrawn. A finished edit sends
		// the page what the game holds once the callback has run.
		void ThroughRaceMenu(Panel::RaceMenuFilm::Slider a_slider, int a_which, int a_value, bool a_final,
			std::function<void()> a_write)
		{
			Panel::RaceMenuFilm::Apply(a_slider, a_which, a_value, a_final,
				[write = std::move(a_write)](bool a_delivered, bool a_final) {
					if (!Npc()) {
						return;
					}
					if (!a_delivered) {
						write();
						Session::RequestRedraw(a_final);
					}
					if (a_final) {
						PushLook(a_delivered ? "after RaceMenu applied it" : "after a written value and full redraw");
					}
				});
		}

		// Applies one value. Returns whether it was taken; a_redraw says
		// whether it needs the shared DoReset3D.
		bool Apply(RE::TESNPC* a_npc, const std::string& a_key, const nlohmann::json& a_v, bool a_final,
			bool& a_redraw)
		{
			for (const auto& t : kTypeLists) {
				if (a_key != t.key) {
					continue;
				}
				const int i = CheckIndex(a_key, a_v, g_lists.typeCounts[t.key]);
				if (i < 0) {
					return false;
				}
				const int part = t.part;
				ThroughRaceMenu(Panel::RaceMenuFilm::Slider::kType, part, i, a_final, [part, i]() {
					if (auto* npc = Npc(); npc && npc->faceData) {
						npc->faceData->parts[part] = i;
					}
				});
				return true;
			}

			for (const auto& p : kPartLists) {
				if (a_key != p.key) {
					continue;
				}
				const auto& list = g_lists.parts[p.key];
				const int   i = CheckIndex(a_key, a_v, static_cast<int>(list.size()));
				if (i < 0 || !list[i]) {
					return false;
				}
				// A part that is missing on either side (no scar worn) goes
				// through the full redraw: the game's swap wants both.
				auto* worn = a_npc->GetCurrentHeadPartByType(p.type);
				a_npc->ChangeHeadPart(list[i]);
				if (!worn) {
					a_redraw = true;
					return true;
				}
				auto& pending = g_pendingParts[p.type];
				if (!pending.shown) {
					pending.shown = worn;
				}
				pending.wanted = list[i];
				return true;
			}

			if (a_key == "hair-color") {
				const int i = CheckIndex(a_key, a_v, static_cast<int>(g_lists.hairColors.size()));
				if (i < 0 || !g_lists.hairColors[i]) {
					return false;
				}
				a_npc->SetHairColor(g_lists.hairColors[i]);
				if (auto* player = RE::PlayerCharacter::GetSingleton()) {
					player->UpdateHairColor();
				}
				return true;
			}

			// A free colour. The race's colour forms are shared by every NPC
			// that uses them, so the colour goes to RaceMenu's own hair colour
			// form, which is there for this: the live preview writes it here,
			// and the final value is handed to RaceMenu's script (OnLookSet).
			if (a_key == "hair-rgb") {
				RE::Color color;
				if (!a_v.is_string() || !ParseHex(a_v.get<std::string>(), color)) {
					spdlog::warn("[look] look-set hair-rgb: \"v\" is not #RRGGBB - ignored");
					return false;
				}
				auto* form = Core::RaceMenuSync::HairColorForm();
				if (!form) {
					spdlog::warn("[look] look-set hair-rgb: RaceMenu's hair colour form (000801:RaceMenu.esp) "
								 "is not loaded - ignored");
					return false;
				}
				form->color.red = color.red;
				form->color.green = color.green;
				form->color.blue = color.blue;
				a_npc->SetHairColor(form);
				if (auto* player = RE::PlayerCharacter::GetSingleton()) {
					player->UpdateHairColor();
				}
				return true;
			}

			if (a_key == "complexion") {
				const int i = CheckIndex(a_key, a_v, static_cast<int>(g_lists.complexions.size()));
				if (i < 0 || !g_lists.complexions[i]) {
					return false;
				}
				auto* set = g_lists.complexions[i];
				ThroughRaceMenu(Panel::RaceMenuFilm::Slider::kComplexion, 0, i, a_final, [set]() {
					if (auto* npc = Npc()) {
						npc->SetFaceTexture(set);
					}
				});
				return true;
			}

			if (a_key == "weight") {
				if (!a_v.is_number() || !std::isfinite(a_v.get<float>())) {
					spdlog::warn("[look] look-set weight: \"v\" is not a finite number - ignored");
					return false;
				}
				a_npc->weight = std::clamp(a_v.get<float>(), kMinWeight, kMaxWeight);
				g_pendingWeight = true;
				return true;
			}

			spdlog::warn("[look] look-set for unknown control \"{}\" - ignored", a_key);
			return false;
		}

		void OnLookGet(const nlohmann::json&)
		{
			g_resendLists = true;
			PushLook("the page asked");
		}

		void OnLookSet(const nlohmann::json& a_data)
		{
			if (!Session::MenuOpen()) {
				spdlog::warn("[look] look-set with the RaceSex Menu closed - ignored");
				return;
			}
			if (!a_data.is_object() || !a_data.contains("k") || !a_data["k"].is_string() || !a_data.contains("v")) {
				spdlog::warn("[look] look-set without a string \"k\" and a \"v\" - ignored");
				return;
			}
			auto* npc = Npc();
			if (!npc || !npc->faceData) {
				spdlog::warn("[look] look-set: the player has no face data - ignored");
				return;
			}

			const auto key = a_data["k"].get<std::string>();
			const bool final = a_data.contains("final") && a_data["final"].is_boolean() && a_data["final"].get<bool>();

			// A head part may come with who it is (name and plugin): a value the
			// page put back from an older reading (undo) holds an index of the
			// list as it was then, and the list is built in RaceMenu's own order
			// while the menu is fresh and in the data handler's once a race or
			// sex swap has settled (E31). The part is what the page means.
			const bool byPart = a_data.contains("n") && a_data["n"].is_string();

			// The page's indices point into the lists it was last sent. If
			// RaceMenu changed race or sex since, they point into the wrong
			// lists: refuse, and send the new ones. A part that says who it is
			// does not need the old list, so the lists are rebuilt and it goes on.
			if (npc->GetRace() != g_lists.race || npc->GetSex() != g_lists.sex) {
				if (!byPart || !EnsureLists("look-set by part") || npc->GetRace() != g_lists.race ||
					npc->GetSex() != g_lists.sex) {
					spdlog::warn("[look] look-set: race or sex changed since the lists were sent - ignored, resending");
					PushLook("race or sex changed");
					return;
				}
			}

			nlohmann::json value = a_data["v"];
			if (byPart) {
				const auto name = a_data["n"].get<std::string>();
				const auto source = a_data.contains("s") && a_data["s"].is_string() ? a_data["s"].get<std::string>() :
				                                                                       std::string();
				for (const auto& p : kPartLists) {
					if (key != p.key) {
						continue;
					}
					const auto& list = g_lists.parts[p.key];
					int         found = -1;
					for (std::size_t i = 0; i < list.size(); ++i) {
						if (list[i] && PartName(list[i]) == name && PartSource(list[i]) == source) {
							found = static_cast<int>(i);
							break;
						}
					}
					if (found < 0) {
						spdlog::warn("[look] look-set {}: '{}' ({}) is not in the list this race and sex have - ignored",
							key, name, source);
						return;
					}
					if (!a_data["v"].is_number_integer() || a_data["v"].get<int>() != found) {
						spdlog::info("[look] look-set {}: '{}' is index {} here, the page said {} - went by the part", key,
							name, found, a_data["v"].dump());
					}
					value = found;
					break;
				}
			}

			bool redraw = false;
			if (!Apply(npc, key, value, final, redraw)) {
				return;
			}
			++g_sets;
			if (redraw) {
				Session::RequestRedraw(final);
			}
			if (final) {
				spdlog::info("[look] {} = {} (drag ended{})", key, a_data["v"].dump(),
					redraw ? "" : ", no full redraw needed");
				// RaceMenu keeps its own copy of the hair colour; without this
				// its controls show the old one and a reload brings it back.
				if (key == "hair-rgb") {
					if (const auto* hrd = npc->headRelatedData; hrd && hrd->hairColor) {
						Core::RaceMenuSync::HairColorChanged(hrd->hairColor->color);
					}
				} else if (key == "hair-color") {
					Core::RaceMenuSync::HairPresetChanged();
				}
				PushLook("after a final write");
			}
		}

		void OnShown() { PushLook("the panel came on screen"); }

		void OnOpened()
		{
			g_sets = 0;
			g_menuListsStale = false;
			g_lists = {};
			g_pendingParts.clear();
			g_pendingWeight = false;
		}

		void OnClosed()
		{
			// A value written in the last moments must not stay invisible.
			if (!g_pendingParts.empty()) {
				ShowParts();
			}
			if (g_pendingWeight) {
				ShowWeight();
			}
			spdlog::info("[look] RaceSex Menu closed: {} values written", g_sets);
		}

		// A loaded preset replaced the head parts and the weight; a swap or
		// a weight still waiting for its redraw would put the old ones back.
		void OnReplaced()
		{
			if (!g_pendingParts.empty() || g_pendingWeight) {
				spdlog::info("[look] preset replaced the character: {} part swaps and {} weight dropped",
					g_pendingParts.size(), g_pendingWeight ? 1 : 0);
			}
			g_pendingParts.clear();
			g_pendingWeight = false;
		}

		bool g_installed = false;
	}

	void ListsChanged()
	{
		g_menuListsStale = true;
		g_lists = {};
	}

	void FitToRace(const char* a_why)
	{
		auto* npc = Npc();
		auto* player = RE::PlayerCharacter::GetSingleton();
		auto* race = npc ? npc->GetRace() : nullptr;
		if (!npc || !player || !race) {
			return;
		}
		// The lists are the new race's and sex's own, never the ones the page was
		// last sent.
		g_menuListsStale = true;
		g_lists = {};
		if (!EnsureLists(a_why)) {
			return;
		}
		spdlog::info("[fit] {}: race {:08X}, sex {} - what the race accepts stays, what it does not becomes the "
					 "race's default, or the first of its list when it declares none",
			a_why, race->GetFormID(), static_cast<std::uint32_t>(npc->GetSex()));
		FitReport("before the fit");

		int changed = 0;
		for (const auto& t : kFitTypes) {
			auto* current = npc->GetCurrentHeadPartByType(t.type);
			if (!current) {
				continue;
			}
			const auto valid = PartsOf(npc, t.type);
			const auto it = g_lists.parts.find(t.key);
			const bool inPanel = it == g_lists.parts.end() || IndexOf(it->second, current) >= 0;
			if (IndexOf(valid, current) >= 0 && inPanel) {
				continue;
			}
			HeadPart* pick = nullptr;
			if (it != g_lists.parts.end()) {
				for (auto* p : it->second) {
					if (p && IndexOf(valid, p) >= 0) {
						pick = p;
						break;
					}
				}
			}
			if (!pick && !valid.empty()) {
				pick = valid.front();
			}
			if (!pick) {
				spdlog::warn("[fit] {}: {:08X} is not valid for this race and sex, and the race offers none - left", t.key,
					current->GetFormID());
				continue;
			}
			// A type can hold several parts (misc does) and ChangeHeadPart replaces
			// the first of its type: only a part that does not belong is touched.
			spdlog::info("[fit] {}: {:08X} '{}' -> {:08X} '{}'{}", t.key, current->GetFormID(),
				current->GetFormEditorID() ? current->GetFormEditorID() : "", pick->GetFormID(),
				pick->GetFormEditorID() ? pick->GetFormEditorID() : "",
				t.type == HPType::kMisc ? " (misc: a type that holds several parts - check the mouth)" : "");
			npc->ChangeHeadPart(pick);
			// With overlays (RaceMenu's skee) the game reads the worn part from the
			// base-overlay array, which ChangeHeadPart leaves alone: measured in boot
			// 147, the part list changed and GetCurrentHeadPartByType still said the
			// old one. The array's entry of that type takes the new part too.
			if (npc->HasOverlays()) {
				auto*      overlays = npc->GetBaseOverlays();
				const auto count = npc->GetNumBaseOverlays();
				for (std::uint32_t i = 0; overlays && i < count; ++i) {
					if (overlays[i] && overlays[i]->type == t.type) {
						overlays[i] = pick;
						break;
					}
				}
			}
			++changed;
		}

		auto* rf = RaceFaceOf(npc);
		const auto* hrd = npc->headRelatedData;
		auto*       color = hrd ? hrd->hairColor : nullptr;
		if (!g_lists.hairColors.empty() && (!color || IndexOf(g_lists.hairColors, color) < 0)) {
			auto* pick = rf && rf->defaultHairColor ? rf->defaultHairColor : g_lists.hairColors.front();
			spdlog::info("[fit] hair-color: {:08X} -> {:08X} ({}; default of the race {:08X}, in its list at {})",
				color ? color->GetFormID() : 0u, pick->GetFormID(),
				rf && rf->defaultHairColor ? "the race's default" : "first of the list",
				rf && rf->defaultHairColor ? rf->defaultHairColor->GetFormID() : 0u,
				rf && rf->defaultHairColor ? IndexOf(g_lists.hairColors, rf->defaultHairColor) : -1);
			npc->SetHairColor(pick);
			player->UpdateHairColor();
			Core::RaceMenuSync::HairPresetChanged();
			++changed;
		}
		const auto* hrd2 = npc->headRelatedData;
		auto*       set = hrd2 ? hrd2->faceDetails : nullptr;
		if (!g_lists.complexions.empty() && (!set || IndexOf(g_lists.complexions, set) < 0)) {
			auto* pick = rf && rf->defaultFaceDetailsTextureSet ? rf->defaultFaceDetailsTextureSet
			                                                    : g_lists.complexions.front();
			spdlog::info("[fit] complexion: {:08X} -> {:08X} ({}; default of the race {:08X}, in its list at {})",
				set ? set->GetFormID() : 0u, pick->GetFormID(),
				rf && rf->defaultFaceDetailsTextureSet ? "the race's default" : "first of the list",
				rf && rf->defaultFaceDetailsTextureSet ? rf->defaultFaceDetailsTextureSet->GetFormID() : 0u,
				rf && rf->defaultFaceDetailsTextureSet ? IndexOf(g_lists.complexions, rf->defaultFaceDetailsTextureSet) : -1);
			npc->SetFaceTexture(pick);
			++changed;
		}
		spdlog::info("[fit] {} values replaced; the caller redraws, and the nine are read again after it", changed);
		FitReport("after the write");
		g_fitPending = true;
	}

	void Install()
	{
		if (g_installed) {
			return;
		}
		Session::OnPanelShown(&OnShown);
		Session::OnMenuOpened(&OnOpened);
		Session::OnMenuClosed(&OnClosed);
		Session::OnReplaced(&OnReplaced);
		Session::OnTick(&OnTick);
		Session::OnRedrawn(&OnRedrawn);
		Panel::Bridge::On("look-get", &OnLookGet);
		Panel::Bridge::On("look-set", &OnLookSet);
		g_installed = true;
		spdlog::info("[look] installed");
	}
}
