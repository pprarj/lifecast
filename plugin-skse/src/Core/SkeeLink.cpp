// SkeeLink.cpp
// Lifecast

#include "SkeeLink.h"

#include <array>

namespace Lifecast::Core::SkeeLink
{
	namespace
	{
		Skee::IInterfaceMap* g_interfaceMap = nullptr;

		// The names SKEE registers its interfaces under. Derived two ways,
		// not guessed:
		//
		//  - the class names in the ModderResource header, minus the leading
		//    I and the trailing Interface (IBodyMorphInterface -> BodyMorph,
		//    IActorUpdateManager -> ActorUpdateManager);
		//  - each of the ten appears as a literal string in the installed
		//    skee64.dll (RaceMenu Anniversary Edition, 2026-09-22).
		//
		// Two are confirmed against a real round rather than by derivation:
		// "BodyMorph" answered non-null and "Preset" answered null, measured
		// in game with the RaceSex Menu closed.
		//
		// A null answer here therefore means one of two things and the log
		// cannot separate them: the interface is not reachable from outside,
		// or this is not the key it is registered under. Anything concluded
		// from a null is stated with that scope attached.
		// The lowest BodyMorph version this plugin calls into. The four
		// methods used here (SetMorph, VisitMorphValues, ApplyBodyMorphs,
		// UpdateModelWeight) sit in the same slots from version 4 to the
		// header's version 5, which only appended AddMorphShapeCallback at
		// the end of the table, and AddMorphShapeCallback is never called.
		// RaceMenu 0.4.19.16 answers 4 (read from its skee64.dll in game,
		// "QueryInterface("BodyMorph") ... version 4").
		constexpr std::uint32_t kBodyMorphFloor = Skee::IBodyMorphInterface::kPluginVersion4;

		// The BodyMorph interface when it is there and at or above the
		// floor; nullptr, with a warning that says what was read and what
		// was required, otherwise. The accepted version is logged once per
		// value, not at every call.
		Skee::IBodyMorphInterface* BodyMorph(const char* a_what, const RE::Actor* a_actor)
		{
			auto* map = GetInterfaceMap();
			auto* iface = map ? map->QueryInterface("BodyMorph") : nullptr;
			if (!a_actor || !iface) {
				spdlog::warn("[skee] body morphs NOT {}: actor {}, BodyMorph interface {}",
					a_what, static_cast<const void*>(a_actor), static_cast<const void*>(iface));
				return nullptr;
			}
			const auto version = iface->GetVersion();
			const auto header = static_cast<std::uint32_t>(Skee::IBodyMorphInterface::kCurrentPluginVersion);
			if (version < kBodyMorphFloor) {
				spdlog::warn("[skee] body morphs NOT {}: BodyMorph interface version {}, needs {} or newer (header describes {})",
					a_what, version, kBodyMorphFloor, header);
				return nullptr;
			}
			static std::uint32_t s_logged = 0;
			if (s_logged != version) {
				s_logged = version;
				spdlog::info("[skee] BodyMorph interface: version {} accepted (floor {}, header describes {})",
					version, kBodyMorphFloor, header);
			}
			return static_cast<Skee::IBodyMorphInterface*>(iface);
		}

		constexpr std::array kInterfaceNames{
			"BodyMorph",
			"NiTransform",
			"Attachment",
			"ItemData",
			"Command",
			"ActorUpdateManager",
			"Overlay",
			"Override",
			"Preset",
			"FormTag",
		};
	}

	Skee::IInterfaceMap* GetInterfaceMap()
	{
		if (g_interfaceMap) {
			return g_interfaceMap;
		}

		Skee::InterfaceExchangeMessage msg;
		msg.interfaceMap = nullptr;

		const bool dispatched = SKSE::GetMessagingInterface()->Dispatch(
			static_cast<std::uint32_t>(Skee::InterfaceExchangeMessage::kMessage_ExchangeInterface),
			&msg,
			sizeof(msg),
			"skee");

		spdlog::info(
			"[skee] Dispatch(kMessage_ExchangeInterface, \"skee\") returned {}, "
			"interfaceMap = {}",
			dispatched, static_cast<const void*>(msg.interfaceMap));

		g_interfaceMap = msg.interfaceMap;
		return g_interfaceMap;
	}

	void ReportInterfaces(const char* a_moment)
	{
		auto* map = GetInterfaceMap();
		if (!map) {
			spdlog::error(
				"[skee] [{}] no interface map - SKEE did not answer the "
				"exchange. Either RaceMenu/skee64 is not installed, or it "
				"loads after the Lifecast in the plugin order.",
				a_moment);
			return;
		}

		for (const auto* name : kInterfaceNames) {
			auto* iface = map->QueryInterface(name);
			if (iface) {
				spdlog::info("[skee] [{}] QueryInterface(\"{}\") = {} version {}",
					a_moment, name, static_cast<const void*>(iface), iface->GetVersion());
			} else {
				spdlog::info("[skee] [{}] QueryInterface(\"{}\") = null",
					a_moment, name);
			}
		}
	}

	bool ClearSkinOverrides(RE::Actor* a_actor)
	{
		auto* map = GetInterfaceMap();
		auto* iface = map ? map->QueryInterface("Override") : nullptr;
		if (!a_actor || !iface) {
			spdlog::warn("[skee] skin overrides NOT cleared: actor {}, Override interface {}",
				static_cast<const void*>(a_actor), static_cast<const void*>(iface));
			return false;
		}
		// The vendored header describes version 2 of the interface; an older
		// one may lay its table out differently, so it is not called.
		const auto version = iface->GetVersion();
		if (version < Skee::IOverrideInterface::kPluginVersion2) {
			spdlog::warn("[skee] skin overrides NOT cleared: Override interface version {}, header describes {}",
				version, static_cast<std::uint32_t>(Skee::IOverrideInterface::kPluginVersion2));
			return false;
		}
		// The actor goes to SKEE as the address of the engine object. SKEE's
		// TESObjectREFR has no type relation to CommonLib's Actor, which is
		// why this is a reinterpret_cast - but what makes it VALID is that
		// TESObjectREFR is Actor's first base, at offset 000
		// (extern/commonlibsse-ng/include/RE/A/Actor.h:144): the Actor's
		// address is its TESObjectREFR's address. Actor has seven bases. For
		// any other class, recheck its header before copying this line - a
		// base that is not the first gives a wrong pointer, with no compile
		// error and no immediate crash.
		auto* refr = reinterpret_cast<Skee::TESObjectREFR*>(a_actor);
		static_cast<Skee::IOverrideInterface*>(iface)->RemoveAllSkinOverridesByReference(refr);
		return true;
	}

	bool ReadBodyMorphs(RE::Actor* a_actor, std::vector<MorphValue>& a_out)
	{
		a_out.clear();
		// VisitMorphValues is in the same slot from version 4 on (see
		// kBodyMorphFloor); an older interface may lay its table out
		// differently, so it is not called.
		auto* morphs = BodyMorph("read", a_actor);
		if (!morphs) {
			return false;
		}

		class Collector : public Skee::IBodyMorphInterface::MorphValueVisitor
		{
		public:
			explicit Collector(std::vector<MorphValue>& a_values) :
				_values(a_values)
			{}

			void Visit(Skee::TESObjectREFR*, const char* a_name, const char* a_key, float a_value) override
			{
				_values.push_back({ a_name ? a_name : "", a_key ? a_key : "", a_value });
			}

		private:
			std::vector<MorphValue>& _values;
		};

		// The same first-base cast as ClearSkinOverrides, for the same reason.
		auto*     refr = reinterpret_cast<Skee::TESObjectREFR*>(a_actor);
		Collector collector(a_out);
		morphs->VisitMorphValues(refr, collector);
		return true;
	}

	bool WriteBodyMorphs(RE::Actor* a_actor, const std::vector<MorphValue>& a_values, std::size_t& a_written)
	{
		a_written = 0;
		auto* morphs = BodyMorph("written", a_actor);
		if (!morphs) {
			return false;
		}
		auto* refr = reinterpret_cast<Skee::TESObjectREFR*>(a_actor);
		for (const auto& v : a_values) {
			morphs->SetMorph(refr, v.name.c_str(), v.key.c_str(), v.value);
			++a_written;
		}
		return true;
	}

	bool ApplyBodyMorphs(RE::Actor* a_actor)
	{
		auto* morphs = BodyMorph("applied", a_actor);
		if (!morphs) {
			return false;
		}
		auto* refr = reinterpret_cast<Skee::TESObjectREFR*>(a_actor);
		morphs->ApplyBodyMorphs(refr);
		morphs->UpdateModelWeight(refr, true);
		return true;
	}

	BodyWeight BodyMorphState(RE::Actor* a_actor)
	{
		// Not BodyMorph(): that one warns on every call, and this runs on every
		// weight step the panel sends.
		auto*      map = GetInterfaceMap();
		auto*      iface = map ? map->QueryInterface("BodyMorph") : nullptr;
		const auto version = iface ? iface->GetVersion() : 0u;
		if (!a_actor || !iface || version < kBodyMorphFloor) {
			static bool s_warned = false;
			if (!s_warned) {
				s_warned = true;
				spdlog::warn("[skee] body morph state NOT read: actor {}, BodyMorph interface {}, version {} (floor {}); written once",
					static_cast<const void*>(a_actor), static_cast<const void*>(iface), version, kBodyMorphFloor);
			}
			return BodyWeight::kNoInterface;
		}
		auto* morphs = static_cast<Skee::IBodyMorphInterface*>(iface);
		// The same first-base cast as the readers, for the same reason.
		auto* refr = reinterpret_cast<Skee::TESObjectREFR*>(a_actor);
		return morphs->HasMorphs(refr) ? BodyWeight::kHasMorphs : BodyWeight::kNoMorphs;
	}

	const char* ToString(BodyWeight a_state)
	{
		switch (a_state) {
		case BodyWeight::kNoInterface:
			return "no interface";
		case BodyWeight::kNoMorphs:
			return "no morphs";
		case BodyWeight::kHasMorphs:
			return "has morphs";
		}
		return "?";
	}

	namespace
	{
		Skee::IPresetInterface* PresetIface()
		{
			auto* map = GetInterfaceMap();
			auto* iface = map ? map->QueryInterface("Preset") : nullptr;
			if (!iface) {
				spdlog::warn("[skee] Preset interface: QueryInterface(\"Preset\") = null (map {})", static_cast<const void*>(map));
				return nullptr;
			}
			if (iface->GetVersion() < Skee::IPresetInterface::kCurrentPluginVersion) {
				spdlog::warn("[skee] Preset interface: version {} older than the header's", iface->GetVersion());
				return nullptr;
			}
			spdlog::info("[skee] Preset interface: {} version {}", static_cast<const void*>(iface), iface->GetVersion());
			return static_cast<Skee::IPresetInterface*>(iface);
		}
	}

	bool PresetReady()
	{
		return PresetIface() != nullptr;
	}

	// Skee::Actor is not RE::Actor; the address is the same because
	// TESObjectREFR is Actor's first base (Actor.h:144), as in the readers above.
	bool SavePreset(RE::Actor* a_actor, const char* a_file, const char* a_tint)
	{
		auto* iface = PresetIface();
		if (!a_actor || !iface) {
			return false;
		}
		return iface->SavePreset(a_file, a_tint, reinterpret_cast<Skee::Actor*>(a_actor));
	}

	bool LoadPreset(RE::Actor* a_actor, const char* a_file, const char* a_tint)
	{
		auto* iface = PresetIface();
		if (!a_actor || !iface) {
			return false;
		}
		return iface->LoadPreset(a_file, a_tint, reinterpret_cast<Skee::Actor*>(a_actor), Skee::IPresetInterface::kPresetApplyAll);
	}

	bool CopyNodeTransforms(RE::Actor* a_from, RE::Actor* a_to, bool a_female, NodeCopyCounts& a_read, NodeCopyCounts& a_written)
	{
		a_read = {};
		a_written = {};
		auto* map = GetInterfaceMap();
		auto* raw = map ? map->QueryInterface("NiTransform") : nullptr;
		if (!a_from || !a_to || !raw) {
			spdlog::warn("[skee] node transforms NOT copied: NiTransform interface {}", static_cast<const void*>(raw));
			return false;
		}
		if (raw->GetVersion() < Skee::INiTransformInterface::kCurrentPluginVersion) {
			spdlog::warn("[skee] node transforms NOT copied: NiTransform interface version {}", raw->GetVersion());
			return false;
		}
		auto* iface = static_cast<Skee::INiTransformInterface*>(raw);
		// Same first-base cast as the readers above.
		auto* src = reinterpret_cast<Skee::TESObjectREFR*>(a_from);
		auto* dst = reinterpret_cast<Skee::TESObjectREFR*>(a_to);

		class Collector : public Skee::INiTransformInterface::NodeVisitor
		{
		public:
			struct Pos { std::string node, key; Skee::INiTransformInterface::Position v; };
			struct Rot { std::string node, key; Skee::INiTransformInterface::Rotation v; };
			struct Sca { std::string node, key; float v; };
			struct Mod { std::string node, key; Skee::skee_u32 v; };
			std::vector<Pos> pos;
			std::vector<Rot> rot;
			std::vector<Sca> sca;
			std::vector<Mod> mod;

			bool VisitPosition(const char* n, const char* k, Skee::INiTransformInterface::Position& p) override
			{
				pos.push_back({ n ? n : "", k ? k : "", p });
				return false;
			}
			bool VisitRotation(const char* n, const char* k, Skee::INiTransformInterface::Rotation& r) override
			{
				rot.push_back({ n ? n : "", k ? k : "", r });
				return false;
			}
			bool VisitScale(const char* n, const char* k, float s) override
			{
				sca.push_back({ n ? n : "", k ? k : "", s });
				return false;
			}
			bool VisitScaleMode(const char* n, const char* k, Skee::skee_u32 m) override
			{
				mod.push_back({ n ? n : "", k ? k : "", m });
				return false;
			}
		};

		Collector c;
		iface->VisitNodes(src, false, a_female, c);
		a_read.scales = c.sca.size();
		a_read.positions = c.pos.size();
		a_read.rotations = c.rot.size();
		a_read.modes = c.mod.size();
		for (auto& s : c.sca) {
			spdlog::info("[skee] node transform read: scale {} / {} = {:.4f}", s.node, s.key, s.v);
			iface->AddNodeTransformScale(dst, false, a_female, s.node.c_str(), s.key.c_str(), s.v);
			++a_written.scales;
		}
		for (auto& p : c.pos) {
			spdlog::info("[skee] node transform read: position {} / {} = ({:.3f}, {:.3f}, {:.3f})", p.node, p.key, p.v.x, p.v.y, p.v.z);
			iface->AddNodeTransformPosition(dst, false, a_female, p.node.c_str(), p.key.c_str(), p.v);
			++a_written.positions;
		}
		for (auto& r : c.rot) {
			spdlog::info("[skee] node transform read: rotation {} / {}", r.node, r.key);
			iface->AddNodeTransformRotation(dst, false, a_female, r.node.c_str(), r.key.c_str(), r.v);
			++a_written.rotations;
		}
		for (auto& m : c.mod) {
			spdlog::info("[skee] node transform read: scale mode {} / {} = {}", m.node, m.key, m.v);
			iface->AddNodeTransformScaleMode(dst, false, a_female, m.node.c_str(), m.key.c_str(), m.v);
			++a_written.modes;
		}
		iface->UpdateNodeAllTransforms(dst);
		return true;
	}
}
