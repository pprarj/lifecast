// SmfBackend.h
// Lifecast
//
// The panel on SKSE Menu Framework (SMF): no page. One SMF window, drawn in
// C++ with the Dear ImGui that SMF exports (Panel/Smf/View.h), speaking the
// same bridge protocol as the web page does - so Edit/, Core/ and the stage
// do not know which backend draws the panel. What is different here, each
// measured in the game before it was written (1.6 E1, D285):
//
//  - Create is SMF's AddWindow; the window is registered once and stays (SMF
//    has no call to remove one). Visible is the window's IsOpen.
//  - Focus is the window's BlockUserInput: while it is set, every key and
//    click goes to the window and none to the game - the Switcher's input
//    sink included. Two forms (the "smfInput" setting, 1.6 E2): "all" holds
//    the input for as long as the panel is focused, and the character is
//    turned by messages; "hybrid" holds it only while the cursor is over the
//    window, and a press keeps the owner it found until its release.
//  - The exit chord is heard by ImGui while the window holds the input (the
//    key SMF's own input callback goes silent on while a field is active),
//    and by a sink prepended to the game's input while it does not.
//  - The view runs inside the frame, on the game thread (measured). What it
//    sends is queued and handed to the bridge by a task, never from inside
//    the frame: a handler that answers would write into the model being
//    drawn.
//  - A press of a dead key is not composed under Media Keys Fix (E1); with
//    that mod loaded the view composes it (Panel/Smf/DeadKeys.h).
//
// Candidate: the SMF package ships a marker file instead of a page
// (PanelFile). Without SMF the panel is not drawn and the player is told
// once, as for the other backends.

#pragma once

#include "Panel/Backend.h"

namespace Lifecast::Panel
{
	Backend& SmfBackend();
}
