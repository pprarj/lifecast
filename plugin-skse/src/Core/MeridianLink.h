// MeridianLink.h
// Lifecast
//
// The handshake with Meridian UI, which is the only panel backend of this
// project. Two separate things happen here and they are easy to confuse:
//
//  1. the PLATFORM handshake - SKSE messages, version negotiation, and the
//     IUIPlatformAPI pointer. This is the flow MeridianUIAPI/SKSELoader.h
//     implements and the author guide recommends;
//  2. the EXTENSION queries - Meridian.View, Meridian.NifScene,
//     Meridian.Input. These do not go through SKSE messaging at all: they
//     resolve the QueryMeridianExtension export out of the already-loaded
//     MeridianUI.dll.
//
// The panel is a platform browser (IUIPlatformAPI::AddOrGetBrowser, see
// Panel/Panel.h), because that is the surface that carries the focus chord.
// The extensions are still queried: the side-by-side comparison would be a
// NifScene actor-appearance surface, and View and Input are measured so the
// choice can be revisited with numbers.

#pragma once

// API.h carries IUIPlatformAPI. None of the three extension loaders below
// pulls it in - they reach only as far as Settings.h.
#include "MeridianUIAPI/API.h"
#include "MeridianUIAPI/InputDllLoader.h"
#include "MeridianUIAPI/NifSceneDllLoader.h"
#include "MeridianUIAPI/RenderLayerDllLoader.h"
#include "MeridianUIAPI/ViewDllLoader.h"

namespace Lifecast::Core::MeridianLink
{
	// Logs the versions this build was compiled against. Call once from
	// SKSEPluginLoad. It registers nothing: see ListenToMeridian().
	void Init();

	// Registers the listener for Meridian's own messages and stores the
	// ready-callback. Call once, from the SKSE kPostLoad message: older SKSE
	// refuses a named listener for a plugin that has not loaded yet.
	void ListenToMeridian();

	// Hands an SKSE lifecycle message to the Meridian loader, which is what
	// dispatches RequestVersion at kPostPostLoad and RequestAPI at
	// kInputLoaded. The plugin owns its single SKSE listener and forwards
	// here; registering a second unnamed listener is not an option.
	void ForwardSKSEMessage(SKSE::MessagingInterface::Message* a_msg);

	// Queries the four extensions and writes one log line each, naming the
	// version asked for. Extensions that already answered are not asked
	// again.
	//
	// a_moment labels the round. Querying at more than one moment is the
	// point: the author guide states Meridian rejects a first API request
	// from a worker-thread lifecycle message later than kInputLoaded, and
	// whether that constrains the extension exports too is not documented
	// anywhere this project can read. A null with no moment next to it is
	// not a measurement.
	void QueryExtensions(const char* a_moment);

	// Null until the platform handshake completes, and stays null when the
	// installed Meridian is too old - the loader simply never invokes the
	// ready-callback in that case, which the author guide says to treat as
	// "MeridianUI unavailable" rather than as a retryable error.
	Meridian::UI::IUIPlatformAPI* Platform();

	Meridian::UI::View::IViewAPI* Views();
	Meridian::UI::NifScene::INifSceneAPI4* NifScene();
	Meridian::UI::Input::IInputAPI* Input();

	// Meridian.RenderLayer. It was added after the first round, which
	// asked for View, NifScene and Input only - so a null here is a
	// first measurement rather than a regression. NifScene draws into
	// a surface and this is what hands one out: the side-by-side
	// comparison needs both or neither.
	Meridian::UI::RenderLayer::IRenderLayerAPI* RenderLayer();
}
