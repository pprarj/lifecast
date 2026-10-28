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
// the stage: turn and zoom, by the "stage-*" messages stage.js sends. 1.6 E3
// adds Look, Race & Sex and the race's preset faces (Face.h), each a tab.
// 1.6 E4 gives it the web panel's look and frame (Theme.h, D119): a small
// panel over a belt along the bottom, and a bar over the stage - three
// windows, and the screen between them is the game's.

#pragma once

namespace Lifecast::Panel::Smf
{
	// One of the view's windows on screen, in screen pixels.
	struct Box
	{
		const char* name = "";
		float       x0 = 0.0f;
		float       y0 = 0.0f;
		float       x1 = 0.0f;
		float       y1 = 0.0f;
	};

	// What the view drew this frame: its windows (the small panel, the belt,
	// the stage bar) and whether a field wants the keyboard. Read by the
	// input form, for which "inside" is inside any of the windows.
	struct Frame
	{
		static constexpr int kMaxBoxes = 3;

		Box  boxes[kMaxBoxes];
		int  count = 0;
		bool drawn = false;
		bool wantText = false;

		// The window the point is in, or nullptr when it is in none.
		const Box* At(float a_x, float a_y) const
		{
			for (int i = 0; i < count; ++i) {
				const auto& b = boxes[i];
				if (a_x >= b.x0 && a_x < b.x1 && a_y >= b.y0 && a_y < b.y1) {
					return &b;
				}
			}
			return nullptr;
		}
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
