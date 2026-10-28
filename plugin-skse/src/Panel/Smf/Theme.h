// Theme.h
// Lifecast - the web panel's look (panel.css :root) for the ImGui view.
//
// Every colour and size is pushed around Lifecast's own windows and popped
// after them, on the same frame; the framework's global style is never
// written, so another mod's window keeps its own. FrameStart / FrameEnd
// bracket the view's render callback and count any frame whose global style
// differs at the end from what it was at the start.

#pragma once

#include "SKSEMenuFramework/SKSEMenuFramework.h"

namespace Lifecast::Panel::Smf::Theme
{
	// The panel's colours, the :root variables of panel.css.
	namespace Color
	{
		ImGuiMCP::ImVec4 PanelBg();
		ImGuiMCP::ImVec4 PanelBorder();
		ImGuiMCP::ImVec4 Text();
		ImGuiMCP::ImVec4 TextDim();
		ImGuiMCP::ImVec4 Accent();
		ImGuiMCP::ImVec4 Warn();
		ImGuiMCP::ImVec4 Bad();
		ImGuiMCP::ImVec4 RowSelected();
		ImGuiMCP::ImVec4 StageBorder();
	}

	// The player's panel scale ("panelScale" in the settings, 0.8 to 2.5);
	// 1 until the view hears it.
	void  SetScale(float a_scale);
	float Scale();

	// A size of panel.css in screen pixels at the current scale.
	float Px(float a_cssPx);

	// What the windows are, for the log line.
	enum class Kind
	{
		kPanel,
		kBelt,
		kStageBar,
		kCard
	};

	// Pushed before Begin, popped after End: the colours and sizes of one of
	// the view's windows.
	class Scope
	{
	public:
		explicit Scope(Kind a_kind);
		~Scope();
		Scope(const Scope&) = delete;
		Scope& operator=(const Scope&) = delete;

	private:
		int  colors_ = 0;
		int  vars_ = 0;
		bool font_ = false;
	};

	// The view's own typeface, when the package carries it: a TTF the
	// framework loads from Data\SKSE\Plugins\Fonts at startup, found in the
	// atlas by its file name. Null without it, and the framework's font is used.
	ImGuiMCP::ImFont* Font();

	// The text size of the window or child just begun: panel.css's body text
	// (--text-size) unless another size is given, in CSS pixels. It is set on
	// every window and child, because a child's text scale multiplies its
	// parent's.
	void Body(float a_cssPx = 12.5f);

	// A heading at its own size and colour, laid out like a line of text:
	// the title (22 px, accent), a section title (14 px, text) and a group
	// heading (12 px, accent).
	void Title(const char* a_text);
	void SectionTitle(const char* a_text);
	void Group(const char* a_text);
	void Heading(const char* a_text, float a_cssPx, const ImGuiMCP::ImVec4& a_color);

	// The log line of one window (cell T), once per window and again when the
	// scale changes.
	void LogWindow(Kind a_kind);

	// Around the view's render callback: the global style must be the same
	// before and after it.
	void FrameStart();
	void FrameEnd();

	// The balance numbers, for the backend's state line.
	void LogBalance(const char* a_what);
}
