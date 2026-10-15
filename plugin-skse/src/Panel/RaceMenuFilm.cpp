// RaceMenuFilm.cpp
// Lifecast

#include "Panel/RaceMenuFilm.h"

#include "Edit/RacePresets.h"

namespace Lifecast::Panel::RaceMenuFilm
{
	namespace
	{
		using Clock = std::chrono::steady_clock;
		using HPType = RE::BGSHeadPart::HeadPartType;

		constexpr auto kList = "_root.RaceSexMenuBaseInstance.RaceSexPanelsInstance.itemList";

		struct Label
		{
			const char* text;
			int         index;
		};

		// The game's face sliders by RaceMenu's label, against the faceData
		// morph they show (TESNPC::FaceData::Morphs). Every pair was read in
		// game with both sides agreeing.
		constexpr Label kMorphs[] = {
			{ "Nose Length", 0 },
			{ "Nose Height", 1 },
			{ "Jaw Height", 2 },
			{ "Jaw Width", 3 },
			{ "Jaw Forward", 4 },
			{ "Cheekbone Height", 5 },
			{ "Cheekbone Width", 6 },
			{ "Eye Height", 7 },
			{ "Eye Width", 8 },
			{ "Brow Height", 9 },
			{ "Brow Width", 10 },
			{ "Brow Forward", 11 },
			{ "Mouth Height", 12 },
			{ "Mouth Forward", 13 },
			{ "Chin Width", 14 },
			{ "Chin Length", 15 },
			{ "Chin Forward", 16 },
			{ "Eye Depth", 17 },
		};

		// The same 18 sliders as the Russian game labels them (read from a
		// Russian game's film). Matched as a second table so a translated
		// game still finds them.
		constexpr Label kMorphsRu[] = {
			{ "\u0414\u043b\u0438\u043d\u0430 \u043d\u043e\u0441\u0430", 0 },
			{ "\u0412\u044b\u0441\u043e\u0442\u0430 \u043d\u043e\u0441\u0430", 1 },
			{ "\u0412\u044b\u0441\u043e\u0442\u0430 \u0447\u0435\u043b\u044e\u0441\u0442\u0438", 2 },
			{ "\u0428\u0438\u0440\u0438\u043d\u0430 \u0447\u0435\u043b\u044e\u0441\u0442\u0438", 3 },
			{ "\u0427\u0435\u043b\u044e\u0441\u0442\u044c \u0432\u043f\u0435\u0440\u0435\u0434", 4 },
			{ "\u0412\u044b\u0441\u043e\u0442\u0430 \u0441\u043a\u0443\u043b", 5 },
			{ "\u0428\u0438\u0440\u0438\u043d\u0430 \u0441\u043a\u0443\u043b", 6 },
			{ "\u0412\u044b\u0441\u043e\u0442\u0430 \u0433\u043b\u0430\u0437", 7 },
			{ "\u0420\u0430\u0441\u0441\u0442\u043e\u044f\u043d\u0438\u0435 \u043c\u0435\u0436\u0434\u0443 \u0433\u043b\u0430\u0437\u0430\u043c\u0438", 8 },
			{ "\u0412\u044b\u0441\u043e\u0442\u0430 \u0431\u0440\u043e\u0432\u0435\u0439", 9 },
			{ "\u0428\u0438\u0440\u0438\u043d\u0430 \u0431\u0440\u043e\u0432\u0435\u0439", 10 },
			{ "\u0411\u0440\u043e\u0432\u0438 \u0432\u043f\u0435\u0440\u0435\u0434", 11 },
			{ "\u0412\u044b\u0441\u043e\u0442\u0430 \u0433\u0443\u0431", 12 },
			{ "\u0413\u0443\u0431\u044b \u0432\u043f\u0435\u0440\u0435\u0434", 13 },
			{ "\u0428\u0438\u0440\u0438\u043d\u0430 \u043f\u043e\u0434\u0431\u043e\u0440\u043e\u0434\u043a\u0430", 14 },
			{ "\u0414\u043b\u0438\u043d\u0430 \u043f\u043e\u0434\u0431\u043e\u0440\u043e\u0434\u043a\u0430", 15 },
			{ "\u041f\u043e\u0434\u0431\u043e\u0440\u043e\u0434\u043e\u043a \u0432\u043f\u0435\u0440\u0435\u0434", 16 },
			{ "\u0413\u043b\u0443\u0431\u0438\u043d\u0430 \u0433\u043b\u0430\u0437\u043d\u0438\u0446", 17 },
		};

		// The race's preset types, against faceData->parts (nose 0, eyes 2,
		// mouth 3). Both sides count them the same way.
		constexpr Label kTypes[] = {
			{ "Nose Type", 0 },
			{ "Eye Shape", 2 },
			{ "Mouth Shape", 3 },
		};

		constexpr HPType kPartTypes[] = { HPType::kHair, HPType::kEyes, HPType::kEyebrows, HPType::kScar,
			HPType::kFacialHair };

		// The list's refresh, in the order tried. InvalidateData is the one
		// the film was seen to have; the others are its usual aliases.
		constexpr const char* kRefresh[] = { "InvalidateData", "UpdateList", "invalidateData" };

		std::string Str(const RE::GFxValue& a_obj, const char* a_name)
		{
			RE::GFxValue v;
			return a_obj.GetMember(a_name, &v) && v.IsString() ? std::string(v.GetString()) : std::string();
		}

		bool Num(const RE::GFxValue& a_obj, const char* a_name, double& a_out)
		{
			RE::GFxValue v;
			if (a_obj.GetMember(a_name, &v) && v.IsNumber()) {
				a_out = v.GetNumber();
				return true;
			}
			return false;
		}

		int IndexOf(const Label (&a_table)[18], const std::string& a_text)
		{
			for (const auto& l : a_table) {
				if (a_text == l.text) {
					return l.index;
				}
			}
			return -1;
		}

		int TypeOf(const std::string& a_text)
		{
			for (const auto& l : kTypes) {
				if (a_text == l.text) {
					return l.index;
				}
			}
			return -1;
		}

		// A translated game renames the three type sliders; their tab does not
		// change: nose on FACE (16), eyes on EYES (32), mouth on MOUTH (128).
		int TypeOfFlag(const RE::GFxValue& a_entry)
		{
			double f = 0;
			RE::GFxValue v;
			if (!a_entry.GetMember("filterFlag", &v) || !v.IsNumber()) {
				return -1;
			}
			f = v.GetNumber();
			const auto flag = static_cast<std::uint32_t>(f);
			if (flag & 32) {
				return 2;
			}
			if (flag & 128) {
				return 3;
			}
			if (flag & 16) {
				return 0;
			}
			return -1;
		}

		// The entry's own part list, asked for with its sliderID - the one
		// argument that call takes (without it skee64 asserts and the game
		// crashes). The index of the part whose name carries a_editorID,
		// either alone (eyes) or in brackets after a display name (hair).
		int PartIndex(const RE::GFxValue& a_entry, const std::vector<std::string>& a_editorIDs)
		{
			double id = -1;
			if (a_editorIDs.empty() || !Num(a_entry, "sliderID", id)) {
				return -1;
			}
			RE::GFxValue                      result;
			RE::GFxValue                      parts;
			const std::array<RE::GFxValue, 1> arg{ RE::GFxValue(id) };
			if (!const_cast<RE::GFxValue&>(a_entry).Invoke("GetSliderPartData", &result, arg) || !result.IsObject() ||
				!result.GetMember("parts", &parts) || !parts.IsArray()) {
				return -1;
			}
			const auto n = parts.GetArraySize();
			for (std::uint32_t i = 0; i < n; ++i) {
				RE::GFxValue part;
				if (!parts.GetElement(i, &part)) {
					continue;
				}
				const auto name = Str(part, "name");
				for (const auto& editorID : a_editorIDs) {
					if (name == editorID || name.find("[" + editorID + "]") != std::string::npos) {
						double index = i;
						Num(part, "index", index);
						return static_cast<int>(index);
					}
				}
			}
			return -1;
		}

		// The player's face detail set among the race's.
		int ComplexionIndex(RE::TESNPC* a_npc)
		{
			auto*      race = a_npc->GetRace();
			const auto sex = static_cast<std::uint32_t>(a_npc->GetSex());
			auto*      rf = race && sex < RE::SEXES::kTotal ? race->faceRelatedData[sex] : nullptr;
			auto*      hrd = a_npc->headRelatedData;
			if (!rf || !rf->faceDetailsTextureSets || !hrd || !hrd->faceDetails) {
				return -1;
			}
			const auto& list = *rf->faceDetailsTextureSets;
			for (std::uint32_t i = 0; i < list.size(); ++i) {
				if (list[i] == hrd->faceDetails) {
					return static_cast<int>(i);
				}
			}
			return -1;
		}

		// The last preset index handed to ChangeHeadPreset, and the film it was handed to.
		RE::GFxMovieView* g_presetMovie = nullptr;
		int               g_presetPos = -1;

		// What the Presets entry's "position" held the last time the Lifecast was done
		// with the film (SyncNow reads it back after writing), and the film it was
		// read from. If the entry holds something else when the panel comes back,
		// the player moved RaceMenu's own slider meanwhile (ReadNativePreset).
		RE::GFxMovieView* g_baseMovie = nullptr;
		double            g_presetBase = -1.0;

		// RaceMenu's own Presets slider: the entry, and its position.
		bool PresetEntry(RE::GFxMovieView* a_movie, RE::GFxValue& a_entry)
		{
			RE::GFxValue list;
			RE::GFxValue entries;
			if (!a_movie->GetVariable(&list, kList) || !list.IsObject() || !list.GetMember("_entryList", &entries) ||
				!entries.IsArray()) {
				return false;
			}
			const auto n = entries.GetArraySize();
			for (std::uint32_t i = 0; i < n; ++i) {
				RE::GFxValue entry;
				if (entries.GetElement(i, &entry) && entry.IsObject() && Str(entry, "callbackName") == "ChangeHeadPreset") {
					a_entry = entry;
					return true;
				}
			}
			return false;
		}

		bool Set(RE::GFxValue& a_entry, double a_value, int& a_written)
		{
			double pos = 0;
			if (Num(a_entry, "position", pos) && std::abs(pos - a_value) < 0.0005) {
				return false;
			}
			if (a_entry.SetMember("position", RE::GFxValue(a_value))) {
				++a_written;
				return true;
			}
			return false;
		}

		void SyncNow()
		{
			const auto start = Clock::now();
			auto*      ui = RE::UI::GetSingleton();
			auto       menu = ui ? ui->GetMenu(RE::RaceSexMenu::MENU_NAME) : nullptr;
			auto*      movie = menu ? menu->uiMovie.get() : nullptr;
			auto*      player = RE::PlayerCharacter::GetSingleton();
			auto*      npc = player ? player->GetActorBase() : nullptr;
			auto*      face = npc ? npc->faceData : nullptr;
			if (!movie || !face) {
				spdlog::warn("[film] RaceMenu's sliders not updated: film {}, face data {}", movie != nullptr,
					face != nullptr);
				return;
			}

			RE::GFxValue list;
			RE::GFxValue entries;
			if (!movie->GetVariable(&list, kList) || !list.IsObject() || !list.GetMember("_entryList", &entries) ||
				!entries.IsArray()) {
				spdlog::warn("[film] RaceMenu's sliders not updated: no {}._entryList", kList);
				return;
			}

			std::vector<std::string> worn;
			for (const auto type : kPartTypes) {
				auto* part = npc->GetCurrentHeadPartByType(type);
				const std::string editorID = part ? part->GetFormEditorID() : "";
				if (!editorID.empty()) {
					worn.push_back(editorID);
				}
			}

			int           written = 0;
			int           seen = 0;
			std::string   changed;
			std::array<bool, 18> morphsDone{};
			const auto    n = entries.GetArraySize();
			for (std::uint32_t i = 0; i < n; ++i) {
				RE::GFxValue entry;
				if (!entries.GetElement(i, &entry) || !entry.IsObject()) {
					continue;
				}
				const auto callback = Str(entry, "callbackName");
				const auto text = Str(entry, "text");
				double     want = 0;
				if (callback == "ChangeDoubleMorph") {
					int morph = IndexOf(kMorphs, text);
					if (morph < 0) {
						morph = IndexOf(kMorphsRu, text);
					}
					// RaceMenu's own sliders come after the game's, and some
					// repeat a label ("Brow Width"): the first one is the game's.
					if (morph < 0 || morphsDone[morph]) {
						continue;
					}
					morphsDone[morph] = true;
					want = face->morphs[morph];
					// A new game's face data holds FLT_MAX for a moment: not a value.
					if (!(std::abs(want) <= 10.0)) {
						continue;
					}
				} else if (callback == "ChangePreset") {
					int type = TypeOf(text);
					if (type < 0) {
						type = TypeOfFlag(entry);
					}
					if (type < 0) {
						continue;
					}
					want = face->parts[type];
				} else if (callback == "ChangeHeadPart") {
					const int index = PartIndex(entry, worn);
					if (index < 0) {
						continue;
					}
					want = index;
				} else if (callback == "ChangeWeight") {
					// The weight slider runs from 0 to 1; the game keeps 0 to 100.
					want = npc->weight / 100.0;
				} else if (callback == "ChangeHeadPreset") {
					if (g_presetMovie != movie || g_presetPos < 0) {
						continue;
					}
					want = g_presetPos;
				} else if (callback == "ChangeFaceDetails") {
					const int index = ComplexionIndex(npc);
					if (index < 0) {
						continue;
					}
					want = index;
				} else {
					continue;
				}
				++seen;
				if (Set(entry, want, written)) {
					changed += fmt::format("{}'{}' {}", changed.empty() ? "" : ", ", text, want);
				}
			}

			const char* refreshed = "-";
			if (written > 0) {
				refreshed = "none found";
				for (const char* name : kRefresh) {
					RE::GFxValue fn;
					if (list.GetMember(name, &fn) && !fn.IsUndefined()) {
						refreshed = list.Invoke(name) ? name : "failed";
						break;
					}
				}
			}
			RE::GFxValue presets;
			double       presetPos = 0;
			if (PresetEntry(movie, presets) && Num(presets, "position", presetPos)) {
				g_baseMovie = movie;
				g_presetBase = presetPos;
			}
			const auto us = std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - start).count();
			spdlog::info("[film] RaceMenu's sliders: {} checked, {} updated, refresh {}, {} us{}{}", seen, written,
				refreshed, us, written > 0 ? ": " : "", changed);
		}

		struct Target
		{
			const char* callback = nullptr;
			const char* text = nullptr;  // nullptr: the first entry with that callback
		};

		Target TargetOf(Slider a_slider, int a_which)
		{
			switch (a_slider) {
			case Slider::kType:
				for (const auto& l : kTypes) {
					if (l.index == a_which) {
						return { "ChangePreset", l.text };
					}
				}
				break;
			case Slider::kComplexion:
				return { "ChangeFaceDetails", nullptr };
			case Slider::kHeadPreset:
				return { "ChangeHeadPreset", nullptr };
			}
			return {};
		}

		// The sliderID of the first entry with that callback (and label).
		bool FindSliderID(RE::GFxMovieView* a_movie, const Target& a_target, double& a_id)
		{
			RE::GFxValue list;
			RE::GFxValue entries;
			if (!a_movie->GetVariable(&list, kList) || !list.IsObject() || !list.GetMember("_entryList", &entries) ||
				!entries.IsArray()) {
				return false;
			}
			const auto n = entries.GetArraySize();
			for (std::uint32_t i = 0; i < n; ++i) {
				RE::GFxValue entry;
				if (!entries.GetElement(i, &entry) || !entry.IsObject() || Str(entry, "callbackName") != a_target.callback) {
					continue;
				}
				if (a_target.text && Str(entry, "text") != a_target.text) {
					// A translated game: the type sliders are found by their tab.
					const int want = std::string_view(a_target.callback) == "ChangePreset" ? TypeOf(a_target.text) : -1;
					if (want < 0 || TypeOfFlag(entry) != want) {
						continue;
					}
				}
				return Num(entry, "sliderID", a_id);
			}
			return false;
		}

		struct Pending
		{
			double value = 0;
			bool   final = false;
			Done   done;
		};

		using Key = std::pair<Slider, int>;

		std::mutex                 g_lock;
		std::map<Key, Pending>     g_pending;
		RE::GFxMovieView*          g_idsFor = nullptr;
		std::map<Key, double>      g_ids;
		std::map<Key, int>         g_calls;

		// Calls the menu's callback for one slider. Returns whether it was called.
		bool Deliver(const Key& a_key, double a_value, bool a_final)
		{
			const auto target = TargetOf(a_key.first, a_key.second);
			auto*      ui = RE::UI::GetSingleton();
			auto       menu = ui ? ui->GetMenu(RE::RaceSexMenu::MENU_NAME) : nullptr;
			auto*      movie = menu ? menu->uiMovie.get() : nullptr;
			if (!target.callback || !movie || !menu->fxDelegate) {
				spdlog::warn("[film] {} for slider {}: no target, film or GameDelegate - not handed to RaceMenu",
					target.callback ? target.callback : "no callback", a_key.second);
				return false;
			}

			if (movie != g_idsFor) {
				g_ids.clear();
				g_calls.clear();
				g_idsFor = movie;
			}
			// The id is read from the film on every delivery. The film keeps
			// ONE movie while the menu is open but rebuilds its slider list when
			// the character's sex changes, and the same callback and label can
			// then sit under another sliderID: a cached id made "Nose Length"
			// land on the Nose Height morph after a sex change. The walk stops
			// at the first match, so it is short for the callbacks that go
			// through here.
			double id = 0;
			if (!FindSliderID(movie, target, id)) {
				spdlog::warn("[film] {} '{}': no such entry in RaceMenu's list - not handed to RaceMenu",
					target.callback, target.text ? target.text : "(first)");
				return false;
			}
			auto found = g_ids.find(a_key);
			if (found == g_ids.end()) {
				found = g_ids.emplace(a_key, id).first;
			} else if (found->second != id) {
				spdlog::info("[film] {} '{}': sliderID {} -> {} (the film's list was rebuilt)", target.callback,
					target.text ? target.text : "(first)", found->second, id);
				found->second = id;
			}

			auto* defn = menu->fxDelegate->callbacks.Get(RE::GString(target.callback));
			if (!defn || !defn->callback) {
				spdlog::warn("[film] {} is not registered in RaceMenu's GameDelegate - not handed to RaceMenu",
					target.callback);
				return false;
			}

			// The two arguments RaceMenu's own slider sends: value, sliderID.
			const std::array<RE::GFxValue, 2> args{ RE::GFxValue(a_value), RE::GFxValue(found->second) };
			const RE::FxDelegateArgs          params(RE::GFxValue(), defn->handler.get(), movie, args.data(),
						 static_cast<std::uint32_t>(args.size()));
			const auto queuedBefore = RE::MessageBoxMenu::GetQueueSize();
			const auto start = Clock::now();
			defn->callback(params);
			const auto us = std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - start).count();
			if (a_key.first == Slider::kHeadPreset && RE::MessageBoxMenu::GetQueueSize() > queuedBefore) {
				// With edits made since the last preset, the game asks "Change preset and
				// lose your changes?" before it applies one. That box is a native menu: the
				// panel holds the browser focus, so the keyboard never reaches it and the
				// player has no way to answer. The panel already tells the player what a
				// preset replaces (the change is one Undo entry), so the question is
				// answered Ok for them, which runs the same callback the button would.
				//
				// Only the game's own question is answered: a box that another mod opened
				// right after the call (the FSMP's, V140) is the player's, and the
				// Switcher lets the panel go of the focus so it can be answered.
				const auto* box = RE::MessageBoxMenu::GetCurrentMessageBoxData();
				const char* confirm = nullptr;
				if (auto* gmst = RE::GameSettingCollection::GetSingleton()) {
					if (auto* setting = gmst->GetSetting("sRSMConfirmDestruction")) {
						confirm = setting->GetString();
					}
				}
				const bool ours = box && confirm && std::string_view(box->bodyText.c_str()) == confirm;
				spdlog::info("[film] the Presets callback queued a box ('{}', {} bytes) against the game's sRSMConfirmDestruction ('{}'): {}",
					box ? box->bodyText.c_str() : "?", box ? std::string_view(box->bodyText.c_str()).size() : 0,
					confirm ? confirm : "(none)", ours ? "the game's own confirmation, answered Ok" : "NOT the game's confirmation, left to the player");
				if (ours) {
					RE::MessageBoxMenu::SelectOption(0);
				}
			}
			if (a_key.first == Slider::kHeadPreset) {
				// The game does not hand the new position back to the film, and the
				// face the preset set changes other sliders: show both.
				g_presetMovie = movie;
				g_presetPos = static_cast<int>(a_value);
				SyncNow();
			}

			const int n = ++g_calls[a_key];
			if (n == 1 || a_final) {
				spdlog::info("[film] {}({}, {}) for '{}' took {} us (call {} for this slider{})", target.callback,
					a_value, found->second, target.text ? target.text : "(first)", us, n, a_final ? ", edit ended" : "");
			}
			return true;
		}

		void DeliverPending()
		{
			std::map<Key, Pending> batch;
			{
				std::scoped_lock lock(g_lock);
				batch.swap(g_pending);
			}
			for (auto& [key, p] : batch) {
				const bool delivered = Deliver(key, p.value, p.final);
				if (p.done) {
					p.done(delivered, p.final);
				}
			}
		}
	}

	void Apply(Slider a_slider, int a_which, double a_value, bool a_final, Done a_done)
	{
		bool schedule = false;
		{
			std::scoped_lock lock(g_lock);
			schedule = g_pending.empty();
			auto& p = g_pending[{ a_slider, a_which }];
			p.value = a_value;
			p.final = p.final || a_final;
			p.done = std::move(a_done);
		}
		if (!schedule) {
			return;
		}
		auto* tasks = SKSE::GetTaskInterface();
		if (!tasks) {
			spdlog::warn("[film] no task interface - RaceMenu's callback called at once");
			DeliverPending();
			return;
		}
		tasks->AddUITask([]() { DeliverPending(); });
	}

	namespace
	{
		constexpr auto kRaceList = "_root.RaceSexMenuBaseInstance.RaceSexPanelsInstance.racePanel.itemList";

		// Calls a GameDelegate callback of the menu with two numbers. UI thread.
		bool CallNative(RE::GFxMovieView* a_movie, RE::IMenu* a_menu, const char* a_callback, double a_first,
			double a_second)
		{
			if (!a_movie || !a_menu || !a_menu->fxDelegate) {
				return false;
			}
			auto* defn = a_menu->fxDelegate->callbacks.Get(RE::GString(a_callback));
			if (!defn || !defn->callback) {
				spdlog::warn("[film] {} is not registered in RaceMenu's GameDelegate", a_callback);
				return false;
			}
			const std::array<RE::GFxValue, 2> args{ RE::GFxValue(a_first), RE::GFxValue(a_second) };
			const RE::FxDelegateArgs          params(RE::GFxValue(), defn->handler.get(), a_movie, args.data(),
						 static_cast<std::uint32_t>(args.size()));
			defn->callback(params);
			return true;
		}

		// E31 - RaceMenu's extended face sliders (the "$EFM_*", "$CME_*", "$EXPR_*"... ChangeDoubleMorph
		// entries) after a sex change. Read from RaceMenu's public skee64 source, and confirmed against the
		// log: the callback carries no name. skee resolves the slider's baked index against ONE list per
		// race, the names of the sliders of the sex it loaded LAST, sorted without case. The game loads a
		// sex's sliders the first time that sex is shown in a menu and keeps them after; so going back to
		// the sex that was loaded first leaves its baked indices against the other sex's list, and every
		// value the player's edits (or RaceMenu's own ChangeSex) send lands on the name that sits at the same
		// position in that list: same numbers, other names (a lip value on EXPR_Aah, a chin value on the
		// eyeballs). The film keeps showing the right values, which is why nothing looks wrong in the panel.
		// Measured: the 17 names a test save holds are reproduced by this rule, all 17 (Docs/MEDIDO-E31).
		//
		// What this puts back: after the change, for every position i of the list skee holds, the film's
		// slider at position i of the target sex's sorted names is sent the value that belongs to the name
		// skee holds at i. The write reaches that name by skee's own rule, so the values end under the names
		// the film shows them under.
		struct ExtEntry
		{
			std::string  name;  // the label without its '$': skee's own name for the morph
			double       id = -1;
			double       position = 0;
			double       max = 0;
			RE::GFxValue entry;  // only while the film that holds it is the one in hand
		};

		struct CiLess
		{
			bool operator()(const std::string& a_a, const std::string& a_b) const
			{
				return _stricmp(a_a.c_str(), a_b.c_str()) < 0;
			}
		};

		bool NameLess(const ExtEntry& a_a, const ExtEntry& a_b)
		{
			return _stricmp(a_a.name.c_str(), a_b.name.c_str()) < 0;
		}

		bool ReadExtended(RE::GFxMovieView* a_movie, std::vector<ExtEntry>& a_out, double& a_idSum)
		{
			a_out.clear();
			a_idSum = 0;
			RE::GFxValue list;
			RE::GFxValue entries;
			if (!a_movie || !a_movie->GetVariable(&list, kList) || !list.IsObject() ||
				!list.GetMember("_entryList", &entries) || !entries.IsArray()) {
				return false;
			}
			const auto n = entries.GetArraySize();
			for (std::uint32_t i = 0; i < n; ++i) {
				RE::GFxValue entry;
				if (!entries.GetElement(i, &entry) || !entry.IsObject() || Str(entry, "callbackName") != "ChangeDoubleMorph") {
					continue;
				}
				const auto text = Str(entry, "text");
				if (text.size() < 2 || text[0] != '$') {
					continue;  // the game's 18 carry their plain labels
				}
				ExtEntry e;
				e.name = text.substr(1);
				if (!Num(entry, "sliderID", e.id) || e.id < 0) {
					continue;
				}
				Num(entry, "position", e.position);
				Num(entry, "sliderMax", e.max);
				a_idSum += e.id;
				e.entry = entry;
				a_out.push_back(std::move(e));
			}
			std::stable_sort(a_out.begin(), a_out.end(), NameLess);
			return true;
		}

		// A slider that is a head-part chooser (RM_FacePart, RM_MouthPart: 0..N, N past the morphs' 3) is
		// not a morph: skee sends it through another path, and it is never written here.
		bool IsPartSlider(const ExtEntry& a_e)
		{
			return a_e.name.rfind("RM_", 0) == 0 || a_e.max > 3.0001;
		}

		// What the menu session knows about skee's list: which sexes of this race were loaded, and the
		// names of the list skee holds now. Reset when the film (the menu) or the race is not the one it
		// was built for.
		struct ListModel
		{
			RE::GFxMovieView*        movie = nullptr;
			RE::FormID               race = 0;
			std::set<int>            seen;  // sexes whose sliders the game has loaded in this menu
			std::vector<std::string> internal;
		} g_model;

		struct Source
		{
			bool                  valid = false;
			bool                  female = false;
			std::vector<ExtEntry> entries;
		} g_src;

		std::vector<std::string> Names(const std::vector<ExtEntry>& a_entries)
		{
			std::vector<std::string> out;
			out.reserve(a_entries.size());
			for (const auto& e : a_entries) {
				out.push_back(e.name);
			}
			return out;
		}

		void RememberExtended(RE::GFxMovieView* a_movie, bool a_targetFemale)
		{
			double sum = 0;
			g_src.valid = ReadExtended(a_movie, g_src.entries, sum);
			g_src.female = a_targetFemale;
			for (auto& e : g_src.entries) {
				e.entry = RE::GFxValue();
			}
			auto*      player = RE::PlayerCharacter::GetSingleton();
			auto*      npc = player ? player->GetActorBase() : nullptr;
			const auto race = npc && npc->GetRace() ? npc->GetRace()->GetFormID() : 0;
			const int  sourceSex = npc ? static_cast<int>(npc->GetSex()) : -1;
			if (g_model.movie != a_movie || g_model.race != race) {
				g_model = {};
				g_model.movie = a_movie;
				g_model.race = race;
				g_model.seen.insert(sourceSex);
				g_model.internal = Names(g_src.entries);
				spdlog::info("[film] extended sliders: list model built from the film in front ({} names, sex {}, race {:08X})",
					g_model.internal.size(), sourceSex, race);
			}
			std::size_t nonzero = 0;
			for (const auto& e : g_src.entries) {
				nonzero += std::abs(e.position) > 0.0005 ? 1 : 0;
			}
			spdlog::info("[film] extended sliders before the sex change: {} ({} not at 0), film {}", g_src.entries.size(), nonzero,
				g_src.valid ? "read" : "NOT read");
		}

		struct Reassert
		{
			bool                  active = false;
			std::function<void()> done;
			Clock::time_point     start;
			std::size_t           lastAll = 0;
			double                lastSum = -1.0;
			int                   stable = 0;
		} g_re;

		void ScheduleUi(std::chrono::milliseconds a_after, std::function<void()> a_fn)
		{
			std::thread([a_after, fn = std::move(a_fn)]() {
				std::this_thread::sleep_for(a_after);
				if (auto* tasks = SKSE::GetTaskInterface()) {
					tasks->AddUITask(fn);
				}
			}).detach();
		}

		void FinishReassert()
		{
			auto done = std::move(g_re.done);
			g_re.active = false;
			g_re.done = nullptr;
			if (done) {
				done();
			}
		}

		void ReassertStep()
		{
			if (!g_re.active) {
				return;
			}
			auto*      ui = RE::UI::GetSingleton();
			auto       menu = ui ? ui->GetMenu(RE::RaceSexMenu::MENU_NAME) : nullptr;
			auto*      movie = menu ? menu->uiMovie.get() : nullptr;
			const auto waited = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - g_re.start);
			if (!movie || !menu->fxDelegate) {
				spdlog::warn("[film] extended sliders not put back: the RaceSex Menu is gone after {} ms", waited.count());
				FinishReassert();
				return;
			}
			std::vector<ExtEntry> now;
			double                sum = 0;
			const bool            read = ReadExtended(movie, now, sum);
			if (read && !now.empty() && now.size() == g_re.lastAll && sum == g_re.lastSum) {
				++g_re.stable;
			} else {
				g_re.stable = 0;
			}
			g_re.lastAll = now.size();
			g_re.lastSum = sum;
			// Papyrus registers the sliders again after the change: wait until the list stops moving.
			const bool settled = g_re.stable >= 2 && waited >= std::chrono::milliseconds(900);
			if (!settled && waited < std::chrono::milliseconds(8000)) {
				ScheduleUi(std::chrono::milliseconds(300), []() { ReassertStep(); });
				return;
			}
			if (!read || now.empty() || !g_src.valid || g_model.movie != movie) {
				spdlog::info("[film] extended sliders: nothing put back (film {}, {} now, source {}, model {})",
					read ? "read" : "NOT read", now.size(), g_src.valid ? "kept" : "NOT kept",
					g_model.movie == movie ? "this menu" : "another");
				FinishReassert();
				return;
			}

			// What the player set is what the film showed BEFORE the change: RaceMenu's own change can hand the
			// film back with the values moved to other labels (measured, second round trip), so the values
			// are taken from that reading, and the film is put back to it.
			std::map<std::string, double, CiLess> film;
			for (const auto& e : g_src.entries) {
				film[e.name] = e.position;
			}
			for (const auto& e : now) {
				film.emplace(e.name, e.position);
			}
			int moved = 0;
			for (auto& e : now) {
				const auto truth = std::find_if(g_src.entries.begin(), g_src.entries.end(),
					[&](const ExtEntry& s) { return _stricmp(s.name.c_str(), e.name.c_str()) == 0; });
				if (truth == g_src.entries.end() || IsPartSlider(e)) {
					continue;
				}
				Set(e.entry, truth->position, moved);
			}
			if (moved > 0) {
				RE::GFxValue list;
				if (movie->GetVariable(&list, kList) && list.IsObject()) {
					for (const char* name : kRefresh) {
						RE::GFxValue fn;
						if (list.GetMember(name, &fn) && !fn.IsUndefined()) {
							list.Invoke(name);
							break;
						}
					}
				}
			}
			spdlog::info("[film] extended sliders: {} film positions put back to what they showed before the change", moved);

			const int  target = g_src.female ? 1 : 0;
			const auto targetNames = Names(now);
			if (g_model.seen.insert(target).second) {
				// First time this sex is shown in this menu: the game has just loaded its sliders and skee
				// built its list from them. Indices and list agree.
				g_model.internal = targetNames;
				spdlog::info("[film] extended sliders: sex {} loaded now ({} names) - list and indices agree, nothing to put "
							 "back, settled after {} ms",
					target, targetNames.size(), waited.count());
				FinishReassert();
				return;
			}
			if (g_model.internal == targetNames) {
				spdlog::info("[film] extended sliders: skee's list is this sex's ({} names) - nothing to put back, settled "
							 "after {} ms",
					targetNames.size(), waited.count());
				FinishReassert();
				return;
			}

			// skee's list is the other sex's: position i of its names is where a slider at position i of
			// the target sex's names will write.
			const auto  start = Clock::now();
			const auto  count = (std::min)(g_model.internal.size(), now.size());
			std::size_t sent = 0;
			std::size_t skipped = 0;
			std::string first;
			for (std::size_t i = 0; i < count; ++i) {
				const auto& holder = now[i];  // the slider that writes to position i
				const auto& name = g_model.internal[i];  // the name skee holds at position i
				if (IsPartSlider(holder) || name.rfind("RM_", 0) == 0) {
					++skipped;
					continue;
				}
				const auto it = film.find(name);
				const double value = it != film.end() ? it->second : 0.0;
				if (!CallNative(movie, menu.get(), "ChangeDoubleMorph", value, holder.id)) {
					break;
				}
				if (first.empty() && std::abs(value) > 0.0005) {
					first = fmt::format("{} = {} via '{}' (id {})", name, value, holder.name, holder.id);
				}
				++sent;
			}
			const auto us = std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - start).count();
			spdlog::info("[film] extended sliders put back under their names after the sex change: skee's list held {} names, "
						 "the film shows {} ({} sent, {} part sliders skipped), settled after {} ms, {} us; first non-zero: {}",
				g_model.internal.size(), now.size(), sent, skipped, waited.count(), us, first.empty() ? "-" : first);
			FinishReassert();
		}

		std::string Lower(std::string a_s)
		{
			for (auto& c : a_s) {
				c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			}
			return a_s;
		}

		// The race list marks the player's race with the entry kept in
		// listState.activeEntry: RaceMenu's own click on a race sets it, then asks
		// the list to redraw (onItemPress), and only then calls ChangeRace. Reading
		// it back says which row the film is drawing as the player's.
		void ReadRaceMarker(RE::GFxMovieView* a_movie, const char* a_why)
		{
			RE::GFxValue list;
			RE::GFxValue state;
			RE::GFxValue active;
			RE::GFxValue entries;
			if (!a_movie || !a_movie->GetVariable(&list, kRaceList) || !list.IsObject() ||
				!list.GetMember("listState", &state) || !state.IsObject()) {
				spdlog::info("[film] race marker ({}): no race list state", a_why);
				return;
			}
			std::uint32_t count = 0;
			if ((list.GetMember("entryList", &entries) || list.GetMember("_entryList", &entries)) && entries.IsArray()) {
				count = entries.GetArraySize();
			}
			double selected = -2;
			Num(list, "_selectedIndex", selected);
			if (!state.GetMember("activeEntry", &active) || !active.IsObject()) {
				spdlog::info("[film] race marker ({}): no active entry; list selected index {}, {} entries", a_why, selected,
					count);
				return;
			}
			double raceID = -1;
			Num(active, "raceID", raceID);
			spdlog::info("[film] race marker ({}): active entry raceID {} '{}'; list selected index {}, {} entries", a_why,
				raceID, Str(active, "text"), selected, count);
		}

		// What RaceMenu's click does before ChangeRace: the entry becomes the list's
		// active one and the list is told to redraw. ChangeRace called straight
		// moves the player and leaves the old row marked.
		void MarkRace(RE::GFxValue& a_list, const RE::GFxValue& a_entry)
		{
			RE::GFxValue state;
			if (!a_list.GetMember("listState", &state) || !state.IsObject()) {
				spdlog::warn("[film] ChangeRace: the race list has no listState - the marker stays where it was");
				return;
			}
			const bool set = state.SetMember("activeEntry", a_entry);
			const bool redrawn = a_list.Invoke("requestUpdate");
			spdlog::info("[film] race marker moved to the new race: set {}, redraw asked {}", set, redrawn);
		}


		void DoChangeSex(bool a_female, const std::function<void(bool)>& a_done)
		{
			auto* ui = RE::UI::GetSingleton();
			auto  menu = ui ? ui->GetMenu(RE::RaceSexMenu::MENU_NAME) : nullptr;
			auto* movie = menu ? menu->uiMovie.get() : nullptr;
			double id = -1;
			bool   called = false;
			Edit::RacePresets::LogPlayerFields("sex-before");
			RememberExtended(movie, a_female);
			if (movie && FindSliderID(movie, { "ChangeSex", nullptr }, id)) {
				called = CallNative(movie, menu.get(), "ChangeSex", a_female ? 1.0 : 0.0, id);
			}
			Edit::RacePresets::LogPlayerFields("sex-after");
			// The reset the game runs for a sex change may finish after the callback returns.
			std::thread([]() {
				std::this_thread::sleep_for(std::chrono::seconds(3));
				if (auto* tasks = SKSE::GetTaskInterface()) {
					tasks->AddUITask([]() { Edit::RacePresets::LogPlayerFields("sex-3s-later"); });
				}
			}).detach();
			spdlog::info("[film] ChangeSex({}, {}): {}", a_female ? 1 : 0, id, called ? "called" : "not called");
			if (a_done) {
				a_done(called);
			}
		}

		void DoChangeRace(const std::string& a_name, const std::function<void(bool)>& a_done)
		{
			auto* ui = RE::UI::GetSingleton();
			auto  menu = ui ? ui->GetMenu(RE::RaceSexMenu::MENU_NAME) : nullptr;
			auto* movie = menu ? menu->uiMovie.get() : nullptr;
			bool  called = false;
			RE::GFxValue list;
			RE::GFxValue entries;
			if (movie && movie->GetVariable(&list, kRaceList) && list.IsObject() &&
				((list.GetMember("entryList", &entries) && entries.IsArray()) ||
					(list.GetMember("_entryList", &entries) && entries.IsArray()))) {
				const auto n = entries.GetArraySize();
				const auto wanted = Lower(a_name);
				int        found = -1;
				for (std::uint32_t i = 0; i < n; ++i) {
					RE::GFxValue entry;
					if (!entries.GetElement(i, &entry) || !entry.IsObject()) {
						continue;
					}
					const auto text = Str(entry, "text");
					double     raceID = i;
					Num(entry, "raceID", raceID);
					spdlog::info("[film]   race list [{}] '{}' raceID {}", i, text, raceID);
					if (Lower(text) == wanted) {
						found = static_cast<int>(i);
						MarkRace(list, entry);
						called = CallNative(movie, menu.get(), "ChangeRace", raceID, -1.0);
						spdlog::info("[film] ChangeRace({}, -1) for '{}': {}", raceID, a_name,
							called ? "called" : "not called");
						break;
					}
				}
				if (found < 0) {
					spdlog::warn("[film] ChangeRace: no entry named '{}' among the film's {} races", a_name, n);
				}
			} else {
				spdlog::warn("[film] ChangeRace: no race list at {}", kRaceList);
			}
			if (a_done) {
				a_done(called);
			}
		}
	}

	void ChangeSex(bool a_female, std::function<void(bool)> a_done)
	{
		auto* tasks = SKSE::GetTaskInterface();
		if (!tasks) {
			DoChangeSex(a_female, a_done);
			return;
		}
		tasks->AddUITask([a_female, a_done = std::move(a_done)]() { DoChangeSex(a_female, a_done); });
	}

	void ReassertExtendedSliders(std::function<void()> a_done)
	{
		auto begin = [a_done = std::move(a_done)]() {
			if (g_re.active) {
				spdlog::warn("[film] extended sliders: a put-back is already running");
				if (a_done) {
					a_done();
				}
				return;
			}
			g_re.active = true;
			g_re.done = a_done;
			g_re.start = Clock::now();
			g_re.lastAll = 0;
			g_re.lastSum = -1.0;
			g_re.stable = 0;
			ReassertStep();
		};
		auto* tasks = SKSE::GetTaskInterface();
		if (!tasks) {
			begin();
			return;
		}
		tasks->AddUITask(std::move(begin));
	}

	void ChangeRace(std::string a_raceName, std::function<void(bool)> a_done)
	{
		auto* tasks = SKSE::GetTaskInterface();
		if (!tasks) {
			DoChangeRace(a_raceName, a_done);
			return;
		}
		tasks->AddUITask(
			[name = std::move(a_raceName), a_done = std::move(a_done)]() { DoChangeRace(name, a_done); });
	}

	void LogRaceMarker(std::string a_why)
	{
		auto* tasks = SKSE::GetTaskInterface();
		if (!tasks) {
			return;
		}
		tasks->AddUITask([why = std::move(a_why)]() {
			auto* ui = RE::UI::GetSingleton();
			auto  menu = ui ? ui->GetMenu(RE::RaceSexMenu::MENU_NAME) : nullptr;
			ReadRaceMarker(menu ? menu->uiMovie.get() : nullptr, why.c_str());
		});
	}

	void ReadNativePreset(std::function<void(int)> a_found)
	{
		auto* tasks = SKSE::GetTaskInterface();
		if (!tasks) {
			return;
		}
		tasks->AddUITask([a_found = std::move(a_found)]() {
			auto* ui = RE::UI::GetSingleton();
			auto  menu = ui ? ui->GetMenu(RE::RaceSexMenu::MENU_NAME) : nullptr;
			auto* movie = menu ? menu->uiMovie.get() : nullptr;
			RE::GFxValue entry;
			double       pos = 0;
			if (!movie || !PresetEntry(movie, entry) || !Num(entry, "position", pos)) {
				spdlog::info("[film] the native Presets slider could not be read (film {})", movie != nullptr);
				return;
			}
			// With a baseline for this film, any change is the player's. Without
			// one (the panel's first showing, nothing handed over yet), only a
			// slider off its resting end can be told from an untouched one.
			const bool known = g_baseMovie == movie && g_presetBase >= 0.0;
			const bool moved = known ? std::abs(pos - g_presetBase) > 0.0005 : pos > 0.0005;
			spdlog::info("[film] the native Presets slider reads {} (baseline {}, Lifecast's last {}): {}", pos,
				known ? fmt::format("{}", g_presetBase) : "none", g_presetPos, moved ? "moved by the player" : "unchanged");
			if (!moved) {
				return;
			}
			const int index = static_cast<int>(std::lround(pos));
			g_presetMovie = movie;
			g_presetPos = index;
			g_baseMovie = movie;
			g_presetBase = pos;
			if (a_found) {
				a_found(index);
			}
		});
	}

	void Sync()
	{
		auto* tasks = SKSE::GetTaskInterface();
		if (!tasks) {
			spdlog::warn("[film] no task interface - RaceMenu's sliders keep their old values");
			return;
		}
		tasks->AddUITask([]() { SyncNow(); });
	}
}
