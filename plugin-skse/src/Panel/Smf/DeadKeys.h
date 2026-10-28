// DeadKeys.h
// Lifecast - dead keys for the SKSE Menu Framework view, under Media Keys Fix.
//
// Measured in 1.6 E1: with Media Keys Fix loaded, a dead key reaches the
// ImGui of SMF as nothing at all, and the letter after it comes plain
// ("a" for the acute and "a"); without it, Windows composes and the field
// gets the accented letter. So the view composes, and only then: when Media
// Keys Fix is loaded. Even then a letter that arrives composed already (code
// above ASCII) cancels the pending accent, so nothing is ever composed twice.
//
// How: the dead keys of the keyboard layout in use are found once (and again
// when the layout changes) with ToUnicodeEx in its no-side-effect mode, and
// their presses are watched with GetAsyncKeyState - the game's input and the
// SMF input callback are both silent while a field holds the keyboard. The
// next character typed into a field of the view is then changed by the
// field's character filter: the accented letter when there is one, the
// spacing accent for a space, and the letter alone otherwise (a filter
// cannot add a character).
//
// Measured in 1.6 E2 (boot E2-2): rewriting ImGui's input queue through the
// SMF header's ImGuiIO never found the letter there - every accent was
// pending and every letter reached the field plain. The filter goes through
// the field's own callback instead of a struct offset (D287).

#pragma once

#include "SKSEMenuFramework/SKSEMenuFramework.h"

namespace Lifecast::Panel::Smf::DeadKeys
{
	// Looks for Media Keys Fix and for the layout's dead keys. Game thread,
	// once, when the window is created.
	void Init();

	// Watches the dead keys and rewrites this frame's characters. Call at
	// the top of the render function, while the window holds the input.
	void Frame(ImGuiMCP::ImGuiIO* a_io);

	// The character filter of every text field of the view
	// (ImGuiInputTextFlags_CallbackCharFilter). Render thread.
	int Filter(ImGuiMCP::ImGuiInputTextCallbackData* a_data);
}
