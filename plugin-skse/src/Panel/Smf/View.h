// View.h
// Lifecast - the panel drawn with Dear ImGui, for the SKSE Menu Framework
// backend (Panel/SmfBackend.h).
//
// It is driven by the bridge protocol only, as the web page is: it asks with
// the same messages ("hello", "settings-get", "sliders-get",
// "racemenu-categories-get", "stage-get") and draws what comes back. Nothing
// here calls Edit/, Core/ or the stage directly.
//
// 1.6 E2 draws one section whole - Sliders, a tab per RaceMenu category - and
// the stage: turn and zoom, by the "stage-*" messages stage.js sends.

#pragma once

namespace Lifecast::Panel::Smf
{
	// What the window drew this frame: where the panel is (screen pixels) and
	// whether a field wants the keyboard. Read by the input form.
	struct Frame
	{
		float x = 0.0f;
		float y = 0.0f;
		float w = 0.0f;
		float h = 0.0f;
		bool  drawn = false;
		bool  wantText = false;
	};

	// DLL -> view: one bridge message, as JSON text. Game thread.
	void Receive(const char* a_text);

	// The messages the view sends when it is created. Game thread.
	void Open();

	// Draws the panel or the card, from inside the SMF window's render
	// function. a_holds: the window holds the input this frame (the stage's
	// drag and wheel are read only then).
	Frame Draw(bool a_holds);

	// Hands what Draw queued to the bridge, on a later game-thread task.
	void Flush();

	// The "smfInput" setting as the view last heard it: true for "hybrid".
	bool Hybrid();

	// The card is up (the panel is away and shows only the way back).
	bool Card();

	// One line with the view's state, for the backend's LogState.
	void LogState(const char* a_what);
}
