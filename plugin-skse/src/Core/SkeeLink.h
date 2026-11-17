// SkeeLink.h
// Lifecast
//
// The handshake with SKEE, the plugin half of RaceMenu. The Lifecast dresses
// the RaceMenu chargen editor and hands the expensive work back to it, so
// what SKEE does and does not expose to an outside plugin is a load-bearing
// fact of the design, not a detail.
//
// The vendored header is included through Skee.h, inside a namespace. See the
// note there before touching the types.

#pragma once

#include "Skee.h"

namespace Lifecast::Core::SkeeLink
{
	// Dispatches InterfaceExchangeMessage to "skee" and caches the map.
	// Idempotent: only dispatches while it has no map.
	//
	// Returns nullptr when SKEE did not answer, which is the same answer as
	// "RaceMenu is not installed". The log line distinguishes the two as far
	// as anything here can.
	Skee::IInterfaceMap* GetInterfaceMap();

	// Queries every interface SKEE is known to register and writes one log
	// line per name saying whether it came back.
	//
	// a_moment labels the round in the log. The moment matters and is the
	// reason this takes a label at all: an earlier measurement got
	// QueryInterface("Preset") == 0x0 and never recorded whether the RaceSex
	// Menu was open at the time, which left the null unreadable - absent, or
	// merely not yet there. That can only be settled by asking at more than
	// one moment and writing the moment down next to the answer.
	void ReportInterfaces(const char* a_moment);

	// Removes every skin override SKEE holds for a_actor, through SKEE's
	// Override interface (RemoveAllSkinOverridesByReference). Loading a
	// preset does not do this: a preset without skin overrides, or with an
	// empty list, leaves the ones already on the character, and one with a
	// different index adds to them.
	//
	// False, and nothing removed, when the interface is not there or reports
	// a version older than the vendored header describes.
	bool ClearSkinOverrides(RE::Actor* a_actor);

	// One body morph value SKEE holds for an actor: the morph name, the key
	// it was written under (the mod that wrote it - "OBody", for instance)
	// and the value. The same pair a .jslot keeps under "bodyMorphs".
	struct MorphValue
	{
		std::string name;
		std::string key;
		float       value{ 0.0f };
	};

	// Every body morph value SKEE holds for a_actor, read through SKEE's
	// BodyMorph interface (VisitMorphValues) - nothing is written.
	//
	// False, and a_out left empty, when the interface is not there or reports
	// a version older than 4 (the floor; the vendored header describes 5).
	bool ReadBodyMorphs(RE::Actor* a_actor, std::vector<MorphValue>& a_out);

	// E21.2 - the second figure's body. Writes every value in a_values onto
	// a_actor under the name and the key it was READ with (SetMorph(refr,
	// name, key, value)), so the figure ends up holding the same table the
	// player holds. a_written counts the calls made.
	//
	// False, and nothing written, when the interface is not there or reports
	// a version older than 4 (the floor). The same first-base
	// cast as the readers, and for the same reason.
	bool WriteBodyMorphs(RE::Actor* a_actor, const std::vector<MorphValue>& a_values, std::size_t& a_written);

	// E21.2 - asks SKEE to apply a_actor's morph table to her loaded body:
	// ApplyBodyMorphs(refr) and then UpdateModelWeight(refr, immediate).
	// What was called is returned, not what changed on screen.
	bool ApplyBodyMorphs(RE::Actor* a_actor);

	// D330 - the weight slider, live. Whether a_actor carries SKEE body
	// morphs (HasMorphs). A morphed body keeps showing the geometry SKEE
	// morphed for the old weight until its 3D is rebuilt, so the caller picks
	// the rebuild only when this answers kHasMorphs. With no BodyMorph
	// interface (or one older than the floor) the warning is written once,
	// not on every slider step.
	enum class BodyWeight
	{
		kNoInterface,
		kNoMorphs,
		kHasMorphs,
	};

	BodyWeight BodyMorphState(RE::Actor* a_actor);
	const char* ToString(BodyWeight a_state);

	// E22.3 - the RaceMenu's own preset route (IPresetInterface, "Preset").
	// QueryInterface("Preset") answered null once in this tree, so every call
	// here reports whether the interface is there and does nothing when it is
	// not. LoadPreset may write the actor's TESNPC (the header says so): the
	// caller must prove the actor's base is not the player's BEFORE calling it.
	bool PresetReady();
	bool SavePreset(RE::Actor* a_actor, const char* a_file, const char* a_tint);
	bool LoadPreset(RE::Actor* a_actor, const char* a_file, const char* a_tint);

	// E23 - the node transforms (scale, position, rotation, scale mode) SKEE
	// holds for a_from, written onto a_to under the SAME node and key, then
	// UpdateNodeAllTransforms(a_to). Read by VisitNodes, the way that worked
	// where Has/Get read 1.000 (Edit/BodyScales.cpp:148-153). Counts what was
	// read and what was written; says nothing about what was drawn.
	struct NodeCopyCounts
	{
		std::size_t scales{ 0 }, positions{ 0 }, rotations{ 0 }, modes{ 0 };
	};
	bool CopyNodeTransforms(RE::Actor* a_from, RE::Actor* a_to, bool a_female, NodeCopyCounts& a_read, NodeCopyCounts& a_written);
}
