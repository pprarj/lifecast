// Backend.h
// Lifecast
//
// What the panel needs from a web UI framework, and nothing else. Panel.cpp
// talks to one of these and never to a framework; each framework's types stay
// inside its own .cpp (PrismaBackend.cpp, MagelightBackend.cpp,
// MeridianBackend.cpp). Another framework is another file and one more row in
// Panel.cpp's table - no caller of Panel:: changes.
//
// Every call below is made on the game thread, and every callback a backend
// receives from its framework reaches the rest of the Lifecast on the game
// thread too: page messages go to Bridge::Receive from a game-thread task.
//
// FOCUS IS READ, NOT ASSUMED. A backend may apply SetFocused() later than the
// call - at page load, or on the next frame. IsFocused() is the state the
// framework reports now, and the caller (Switcher's focus watch) tolerates the
// lag rather than the backend pretending it away.

#pragma once

namespace Lifecast::Panel
{
	class Backend
	{
	public:
		// "prisma", "magelight", "meridian": the value of the "backend" setting
		// that forces this one, and the tag its log lines carry.
		virtual const char* Name() const = 0;

		// The framework's name as the player knows it ("Prisma UI"), for the
		// log and the on-screen notice when it is missing.
		virtual const char* Framework() const = 0;

		// The page this backend opens, relative to the game folder. Present
		// only in the package made for this backend: its presence is what
		// makes the backend a candidate.
		virtual const char* PanelFile() const = 0;

		// The framework is loaded and answered its API. Read at kDataLoaded
		// and after; false means this backend is never asked to create.
		virtual bool Available() = 0;

		// Another backend was chosen: let go of whatever was registered with
		// the framework at startup. Called once, at the choice.
		virtual void StandDown() {}

		// Creates the page, hidden. Once: shown and hidden after that, never
		// destroyed and re-created while the game runs. False when refused.
		virtual bool Create() = 0;

		// Created, and the framework has not shut down since.
		virtual bool Exists() const = 0;

		virtual void SetVisible(bool a_visible) = 0;

		// true asks for the keyboard and the mouse; false gives them back.
		virtual void SetFocused(bool a_focused) = 0;
		virtual bool IsFocused() = 0;

		// The way out of a focused panel: up to two DirectInput scan codes
		// (the second 0 for one key). Pressed while the panel is focused, the
		// panel gives its focus up and the key goes no further - neither the
		// page nor the game sees it. Disarmed, the keys reach the game.
		virtual void ArmChord(std::uint32_t a_key1, std::uint32_t a_key2) = 0;
		virtual void DisarmChord() = 0;

		// The page has loaded far enough to receive a message.
		virtual bool PageReady() = 0;

		// Delivers one DLL-to-page message, as JSON text, to the page's
		// window.Lifecast.on("message") handler. Only called with PageReady().
		virtual void Post(const char* a_text) = 0;

		// One log line with the framework's own view of the page.
		virtual void LogState(const char* a_what) = 0;

	protected:
		~Backend() = default;
	};
}
