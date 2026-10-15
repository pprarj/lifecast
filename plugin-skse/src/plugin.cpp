// plugin.cpp
// Lifecast - a modern interface for RaceMenu.
//
// It puts its panel on screen every time the RaceSex Menu opens, swaps it with
// RaceMenu's own film on F4, owns the message bridge to the page (Panel/), and
// edits the face morphs live from the panel (Edit/) - the body too, through
// OBody, when OBody answers - and ends the menu with the character's name
// (Edit/Finish.h). At startup it also answers
// one question per log line: does Meridian UI answer, at which version, and
// which SKEE interfaces are reachable from outside RaceMenu.
//
// There is no console test here and there cannot be one - the Skyrim console
// does not call a global function of a plugin. What calls the code below is
// the engine, through SKSE lifecycle messages, and the only output is
// Documents/My Games/Skyrim Special Edition/SKSE/Lifecast.log.

#include "Core/ListSig.h"
#include "Core/Log.h"
#include "Core/MeridianLink.h"
#include "Core/ObodyLink.h"
#include "Core/Settings.h"
#include "Core/SkeeLink.h"
#include "Edit/Appearance.h"
#include "Edit/Body.h"
#include "Edit/Compare.h"
#include "Edit/FilmSliders.h"
#include "Edit/Finish.h"
#include "Edit/Session.h"
#include "Edit/Freeze.h"
#include "Edit/Light.h"
#include "Edit/Overlays.h"
#include "Edit/RacePresets.h"
#include "Edit/RaceSex.h"
#include "Edit/Tints.h"
#include "Edit/Undress.h"
#include "Panel/Pad.h"
#include "Panel/Panel.h"
#include "Panel/RaceMenuTabs.h"
#include "Panel/Stage.h"
#include "Panel/Switcher.h"
#include "Preset/Guard.h"
#include "Preset/Library.h"
#include "Preset/Load.h"
#include "Preset/Save.h"
#include "Version.h"

// Local-only instrumentation, absent from this repository by design. A
// measurement probe has to compile into this DLL, so it has to live in this
// source tree - but it is development tooling and is never shipped or
// published. It is kept outside this repository and dropped into src/_local/,
// which is gitignored and picked up by the OPTIONAL include at the bottom of
// CMakeLists.txt.
//
// __has_include is what makes this cost nothing: in a clean clone the folder
// is absent, the macro below is never defined, and the call site vanishes at
// preprocessing. Nothing here has to be remembered or undone before a commit.
#if __has_include("_local/Local.h")
#	include "_local/Local.h"
#	define LIFECAST_HAS_LOCAL 1
#endif

namespace
{
	void OnMessage(SKSE::MessagingInterface::Message* a_msg)
	{
		// Meridian's loader needs every SKSE lifecycle message: it dispatches
		// RequestVersion at kPostPostLoad and RequestAPI at kInputLoaded.
		// Forward first, then do our own work for the same message, so the
		// platform is asked to initialize before anything asks it for an
		// extension.
		Lifecast::Core::MeridianLink::ForwardSKSEMessage(a_msg);

#ifdef LIFECAST_HAS_LOCAL
		// Every lifecycle message, unfiltered: local instrumentation picks its
		// own moment, and this file must not have to know which one.
		Lifecast::Local::OnSKSEMessage(a_msg);
#endif

		switch (a_msg->type) {
		case SKSE::MessagingInterface::kPostLoad:
			// Every plugin has loaded, so a listener named for MeridianUI can
			// be registered on any SKSE version (see MeridianLink.cpp).
			Lifecast::Core::MeridianLink::ListenToMeridian();
			break;

		case SKSE::MessagingInterface::kPostPostLoad:
			// The OBody plugin interface is requested here, the moment OBody's
			// own header documents. Asking there is what makes a negative mean
			// "nobody answered" rather than "we asked early", and the answer
			// arrives through the written-through pointer, not through the
			// Dispatch return value. The capability calls are NOT safe until
			// the OBodyIsReady event has arrived - measured at 3min50s after
			// the pointer in one boot and 63 s in another, so the wait is an
			// event and never a timer. See Core/ObodyLink.h.
			Lifecast::Core::ObodyLink::Request();
			break;

		case SKSE::MessagingInterface::kPreLoadGame:
		case SKSE::MessagingInterface::kNewGame:
			// A freeze must never survive into another game.
			Lifecast::Edit::Freeze::Release("a game is loading");
			Lifecast::Edit::Compare::GameChanged();
			break;

		case SKSE::MessagingInterface::kSaveGame:
			// Sent before the save is written: the second figure must not be in it.
			Lifecast::Edit::Compare::BeforeSave();
			break;

		case SKSE::MessagingInterface::kInputLoaded:
			// The Meridian author guide is explicit that this is where an
			// extension is acquired: the platform initializes CEF on its own
			// application thread and rejects a first API request coming from
			// a later worker-thread message such as kDataLoaded.
			Lifecast::Core::MeridianLink::QueryExtensions("kInputLoaded");
			break;

		case SKSE::MessagingInterface::kDataLoaded:
			// Asked a second time on purpose, and only for whatever came
			// back null the first time. Two answers with their moments
			// written down separate "the extension is not there" from "it
			// was not there yet", which one round at one moment cannot.
			Lifecast::Core::MeridianLink::QueryExtensions("kDataLoaded");

			// Forms are loaded, so SKEE has had every chance to register.
			// This is the moment earlier measurements used, with the RaceSex
			// Menu closed - the same moment, so the answers are comparable.
			// Asking again with the menu OPEN is a separate round and does
			// not belong here.
			Lifecast::Core::SkeeLink::ReportInterfaces("kDataLoaded");

			// OBody's files, for the log only: they separate "not installed"
			// from "installed and did not answer", and decide nothing.
			Lifecast::Core::ObodyLink::ReportFiles();

			// The menu and input sinks need the UI and the input manager,
			// which exist from here on. The browser itself is not created
			// now: it is born the first time the RaceSex Menu opens.
			Lifecast::Panel::Switcher::Install();

			// The controller, while the panel has the focus: its events go to the
			// page as "pad" messages.
			Lifecast::Panel::Pad::Install();

			// The editing sections write the player's data from the page;
			// the session they share owns the menu and input sinks and the
			// redraw, so it goes first.
			Lifecast::Edit::Session::Install();
			Lifecast::Edit::FilmSliders::Install();
			Lifecast::Core::ListSig::Install();
			Lifecast::Edit::Appearance::Install();
			Lifecast::Edit::RacePresets::Install();
			Lifecast::Edit::RaceSex::Install();
			Lifecast::Panel::RaceMenuTabs::Install();
			Lifecast::Edit::Tints::Install();
			Lifecast::Edit::Overlays::Install();
			Lifecast::Edit::Light::Install();
			Lifecast::Edit::Freeze::Install();
			Lifecast::Edit::Undress::Install();

			// The body section, offered only while OBody answers.
			Lifecast::Edit::Body::Install();

			// The character's name and the button that ends the menu.
			Lifecast::Edit::Finish::Install();

			// The character view: the frame in the page and the camera moves
			// that put the character in it.
			Lifecast::Panel::Stage::Install();

			// The side by side: the character as she was, next to the
			// character as she is being edited.
			Lifecast::Edit::Compare::Install();

			// The panel's own preferences, kept across games.
			Lifecast::Core::Settings::Install();

			// The presets on disk, listed for the panel; saving and loading
			// one, which RaceMenu does at the panel's request; and the guard
			// that keeps a save from overwriting what was not loaded here.
			Lifecast::Preset::Library::Install();
			Lifecast::Preset::Guard::Install();
			Lifecast::Preset::Save::Install();
			Lifecast::Preset::Load::Install();
			break;

		default:
			break;
		}
	}
}

// SKSEPluginLoad is the modern CommonLibSSE-NG entry point. The version
// boilerplate (SKSEPlugin_Version) is auto-generated by
// add_commonlibsse_plugin.
SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	if (!Lifecast::Core::Log::Init()) {
		SKSE::stl::report_and_fail("SKSE log directory not provided.");
	}

	// a_log = false: SKSE::Init with the default (true) re-initializes
	// logging itself and overwrites the logger Log::Init just installed -
	// the level drops from trace to info in release and the pattern changes.
	// This log is the raw output of the phase, so it has to be ours.
	SKSE::Init(a_skse, false);

	spdlog::info("{} v{} loaded.",
		Lifecast::Version::kProjectName, Lifecast::Version::kString);

	Lifecast::Core::MeridianLink::Init();
	Lifecast::Panel::Init();

	if (!SKSE::GetMessagingInterface()->RegisterListener(OnMessage)) {
		spdlog::error("Failed to register the SKSE message listener.");
		return false;
	}

	spdlog::info("Message listener registered.");

	return true;
}
