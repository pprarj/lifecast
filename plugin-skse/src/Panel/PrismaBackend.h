// PrismaBackend.h
// Lifecast
//
// The panel on Prisma UI: one view, Lifecast/index.html, served from
// Data/PrismaUI/views/Lifecast/. What Prisma does not do for the panel is done
// here, each measured in the game before it was written:
//
//  - Focus() asked before the page's DOM is ready is dropped, while still
//    returning true. A focus request is held and issued at DOM ready.
//  - HasFocus() changes on the next frame, not inside the Focus()/Unfocus()
//    call. IsFocused() reports what Prisma reports; the focus watch in
//    Switcher.cpp already waits for focus to be SEEN before it counts a loss.
//  - Prisma has no exit chord. An input sink PREPENDED on BSInputDeviceManager
//    at kInputLoaded sees the key before anyone else; with the panel focused
//    it gives focus up and stops the event (kStop), so neither the page nor
//    the Switcher's own F4 handler fires for the same press.
//  - Page listeners are called on Prisma's thread. Each message is copied and
//    handed to the bridge from a game-thread task.
//  - A view without focus is click-through as a whole: nothing in it receives
//    a click. The panel's card (shown without focus) is text only, so it needs
//    nothing more.
//
// Prisma has no shutdown callback: the view lives as long as the process.

#pragma once

#include "Panel/Backend.h"

namespace Lifecast::Panel
{
	Backend& PrismaBackend();

	// Asks for Prisma's API and prepends the exit-chord sink. Call once, from
	// kInputLoaded: the sink has to be first in the list, before the
	// Switcher's own sink is added at kDataLoaded.
	void PrismaStart();
}
