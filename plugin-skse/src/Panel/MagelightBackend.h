// MagelightBackend.h
// Lifecast
//
// The panel on Magelight UI (API v4): one fullscreen view of
// Data/Magelight/Lifecast/views/index.html, in the Panel layer, created
// hidden. What Magelight does differently from the other two backends, each
// measured in the game before it was written:
//
//  - Focus is Magelight's UI mode. RequestUIMode only queues the entry; the
//    UIModeEntered event confirms it. IsFocused() reports the confirmed
//    state, never the request.
//  - With UI mode on, no input sink of the game sees a key - the Switcher's
//    own F4 included. The exit chord is heard by the PAGE: bridge.js passes
//    F1-F12 to the listener "lifecastChord", and the DLL releases the mode
//    and hands the swap to the Switcher, the same swap the game's F4 makes.
//  - Escape: by default Magelight leaves UI mode on Escape. The panel sets
//    SetEscapeCapture at DOM ready, so Escape reaches the page as a key - as
//    it does on Meridian - and closes what the page has open.
//  - Without UI mode the view takes no click: the clicks reach RaceMenu,
//    which is what the card needs.
//  - Page listeners run on Magelight's render thread, inside the frame. Each
//    message is copied and handed to the bridge through PostGameTask - never
//    SKSE's own task queue from there (Magelight's threading contract).
//  - Events (DOM ready, UI mode entered and left) are delivered on the game
//    thread (CallbackThread::GameThread).
//
// The mod is registered early (kInputLoaded, again at kDataLoaded if that
// failed) and unregistered when another backend is chosen.

#pragma once

#include "Panel/Backend.h"

namespace Lifecast::Panel
{
	Backend& MagelightBackend();

	// Asks for Magelight's API v4 and registers the mod. Call from
	// kInputLoaded and again from kDataLoaded: the second call only acts if
	// the first did not register.
	void MagelightStart(const char* a_moment);
}
