// Panel.h
// Lifecast
//
// The panel's page, on whichever web UI framework this install has. The
// frameworks are backends (Panel/Backend.h); this module picks one, once, and
// every caller talks to it through the functions below - no framework type
// crosses this header.
//
// WHICH BACKEND. A table, in order: Prisma UI, then Meridian UI. The setting
// "backend" in Data\SKSE\Plugins\Lifecast\settings.json (Core/Settings.h)
// decides:
//
//   "auto"      the first of the table that answered - Prisma when both are
//               installed (the default)
//   "prisma"    Prisma UI
//   "meridian"  Meridian UI
//
// A forced backend that did not answer falls back to "auto", and the log says
// so in the same line that names the choice. With no backend at all the
// Lifecast stays out of the way: one log line, and RaceMenu opens as it always
// did.
//
// Lifetime: the page is created once, on first use, and then shown and hidden -
// never released and re-created while the game runs. The state of what the
// player has already edited lives in the page.

#pragma once

namespace Lifecast::Panel
{
	// Registers the panel's own bridge handlers. Call once from
	// SKSEPluginLoad; creates nothing.
	void Init();

	// Starts what a backend has to start before kDataLoaded (Prisma's API and
	// its exit-chord sink). Call once, from kInputLoaded.
	void OnInputLoaded();

	// Picks the backend from the table and the "backend" setting, and logs the
	// choice. Call once, at kDataLoaded, after Core::Settings::Install().
	void Choose();

	// "prisma", "meridian", or "none".
	const char* BackendName();

	// Creates the page if it does not exist yet, hidden. Returns false when
	// there is no backend, or it refused. Call on the game thread.
	bool Ensure();

	// Created and usable.
	bool Exists();

	// Shows or hides the page. Hiding also releases focus. Does nothing
	// without a page.
	void SetVisible(bool a_visible);
	bool IsVisible();

	// RaceMenu's film is in front: the page stays on screen but without focus,
	// so the keyboard and the mouse belong to the film, and the page draws only a
	// small card saying which key brings the panel back. Not "visible" in the
	// sense of IsVisible(): the panel itself is not on screen. SetVisible() ends
	// the card, either way.
	void ShowCard(const char* a_key);

	// The way out of a focused panel: up to two DirectInput scan codes, the
	// second one 0 for a single key. Pressed while the panel is focused, focus
	// goes back to the game and the key reaches neither the page nor the game.
	// Called again every time the panel is shown rather than once.
	void ArmFocusChord(std::uint32_t a_key1, std::uint32_t a_key2);

	// Removes the chord, so the keys reach the game again.
	void DisarmFocusChord();

	// Asks for focus. Applied later than the call - at page load, or on the
	// next frame - so IsFocused() right after it may still say false. Only call
	// this with a way out already armed.
	void RequestFocus();

	// Gives the focus up, so the keyboard and the mouse reach the game again.
	// Also applied late: the read-back right after the call may still say focused.
	void ReleaseFocus();

	// Whether the page holds the focus now, as the backend reports it.
	bool IsFocused();

	// The page can receive a message now.
	bool PageReady();

	// Delivers one DLL-to-page message as JSON text. Only with PageReady().
	void Post(const std::string& a_text);
}
