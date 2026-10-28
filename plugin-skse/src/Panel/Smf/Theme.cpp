// Theme.cpp
// Lifecast

#include "Panel/Smf/Theme.h"

#include <algorithm>
#include <cctype>
#include <array>
#include <cstring>
#include <string>

namespace Lifecast::Panel::Smf::Theme
{
	namespace
	{
		namespace IG = ImGuiMCP;

		constexpr IG::ImVec4 Rgba(int a_r, int a_g, int a_b, float a_a)
		{
			return IG::ImVec4{ a_r / 255.0f, a_g / 255.0f, a_b / 255.0f, a_a };
		}

		// panel.css :root, one constant per variable.
		constexpr IG::ImVec4 kPanelBg = Rgba(12, 22, 17, 0.94f);          // --panel-bg
		constexpr IG::ImVec4 kPanelBorder = Rgba(160, 230, 185, 0.14f);   // --panel-border
		constexpr IG::ImVec4 kText = Rgba(0xF1, 0xF5, 0xF2, 1.0f);        // --text
		constexpr IG::ImVec4 kTextDim = Rgba(0x9B, 0xB0, 0xA2, 1.0f);     // --text-dim
		constexpr IG::ImVec4 kAccent = Rgba(0x8F, 0xD9, 0xA0, 1.0f);      // --accent
		constexpr IG::ImVec4 kWarn = Rgba(0xD6, 0xB2, 0x5E, 1.0f);        // --warn
		constexpr IG::ImVec4 kBad = Rgba(0xD0, 0x70, 0x70, 1.0f);         // --bad
		constexpr IG::ImVec4 kFieldBg = Rgba(255, 255, 255, 0.04f);       // --field-bg
		constexpr IG::ImVec4 kRowHover = Rgba(255, 255, 255, 0.05f);      // --row-hover
		constexpr IG::ImVec4 kRowSelected = Rgba(255, 255, 255, 0.10f);   // --row-selected
		constexpr IG::ImVec4 kStageBorder = Rgba(160, 230, 185, 0.30f);   // --stage-border
		constexpr IG::ImVec4 kThumb = Rgba(255, 255, 255, 0.28f);         // --scrollbar-thumb
		constexpr IG::ImVec4 kTrack = Rgba(255, 255, 255, 0.04f);         // --scrollbar-track
		constexpr IG::ImVec4 kNone = Rgba(0, 0, 0, 0.0f);
		// No variable: the selected text of a field, the accent at a third.
		constexpr IG::ImVec4 kSelection = Rgba(0x8F, 0xD9, 0xA0, 0.35f);

		struct ColorItem
		{
			IG::ImGuiCol col;
			const char*  name;
			IG::ImVec4   v;
		};

		// The colour table (D292): every ImGui colour the view's widgets use.
		constexpr std::array kColors{
			ColorItem{ IG::ImGuiCol_Text, "Text", kText },
			ColorItem{ IG::ImGuiCol_TextDisabled, "TextDisabled", kTextDim },
			ColorItem{ IG::ImGuiCol_WindowBg, "WindowBg", kPanelBg },
			ColorItem{ IG::ImGuiCol_ChildBg, "ChildBg", kNone },
			ColorItem{ IG::ImGuiCol_PopupBg, "PopupBg", kPanelBg },
			ColorItem{ IG::ImGuiCol_Border, "Border", kPanelBorder },
			ColorItem{ IG::ImGuiCol_BorderShadow, "BorderShadow", kNone },
			ColorItem{ IG::ImGuiCol_FrameBg, "FrameBg", kFieldBg },
			ColorItem{ IG::ImGuiCol_FrameBgHovered, "FrameBgHovered", kRowHover },
			ColorItem{ IG::ImGuiCol_FrameBgActive, "FrameBgActive", kRowSelected },
			ColorItem{ IG::ImGuiCol_ScrollbarBg, "ScrollbarBg", kTrack },
			ColorItem{ IG::ImGuiCol_ScrollbarGrab, "ScrollbarGrab", kThumb },
			ColorItem{ IG::ImGuiCol_ScrollbarGrabHovered, "ScrollbarGrabHovered", kThumb },
			ColorItem{ IG::ImGuiCol_ScrollbarGrabActive, "ScrollbarGrabActive", kThumb },
			ColorItem{ IG::ImGuiCol_CheckMark, "CheckMark", kAccent },
			ColorItem{ IG::ImGuiCol_SliderGrab, "SliderGrab", kAccent },
			ColorItem{ IG::ImGuiCol_SliderGrabActive, "SliderGrabActive", kAccent },
			ColorItem{ IG::ImGuiCol_Button, "Button", kNone },
			ColorItem{ IG::ImGuiCol_ButtonHovered, "ButtonHovered", kRowHover },
			ColorItem{ IG::ImGuiCol_ButtonActive, "ButtonActive", kRowSelected },
			ColorItem{ IG::ImGuiCol_Header, "Header", kRowSelected },
			ColorItem{ IG::ImGuiCol_HeaderHovered, "HeaderHovered", kRowHover },
			ColorItem{ IG::ImGuiCol_HeaderActive, "HeaderActive", kRowSelected },
			ColorItem{ IG::ImGuiCol_Separator, "Separator", kPanelBorder },
			ColorItem{ IG::ImGuiCol_SeparatorHovered, "SeparatorHovered", kPanelBorder },
			ColorItem{ IG::ImGuiCol_SeparatorActive, "SeparatorActive", kPanelBorder },
			ColorItem{ IG::ImGuiCol_ResizeGrip, "ResizeGrip", kNone },
			ColorItem{ IG::ImGuiCol_ResizeGripHovered, "ResizeGripHovered", kNone },
			ColorItem{ IG::ImGuiCol_ResizeGripActive, "ResizeGripActive", kNone },
			ColorItem{ IG::ImGuiCol_Tab, "Tab", kNone },
			ColorItem{ IG::ImGuiCol_TabHovered, "TabHovered", kRowHover },
			ColorItem{ IG::ImGuiCol_TabActive, "TabActive", kRowSelected },
			ColorItem{ IG::ImGuiCol_TabUnfocused, "TabUnfocused", kNone },
			ColorItem{ IG::ImGuiCol_TabUnfocusedActive, "TabUnfocusedActive", kRowSelected },
			ColorItem{ IG::ImGuiCol_TextSelectedBg, "TextSelectedBg", kSelection },
			ColorItem{ IG::ImGuiCol_NavHighlight, "NavHighlight", kAccent },
		};

		enum class VarKind
		{
			kFloat,
			kVec2
		};

		struct VarItem
		{
			IG::ImGuiStyleVar var;
			const char*       name;
			VarKind           kind;
			float             x;      // CSS pixels
			float             y;      // CSS pixels (kVec2)
			bool              scaled; // a length that grows with the panel scale; radii and borders do not
		};

		// The size table (D292). --gap is 8 px; the radii are panel.css's 5 (scrollbar), 4 (chips, bars,
		// popups), 3 (fields); the panel and the belt are flush (0), as the page draws them.
		constexpr std::array kVars{
			VarItem{ IG::ImGuiStyleVar_WindowPadding, "WindowPadding", VarKind::kVec2, 12.0f, 8.0f, true },
			VarItem{ IG::ImGuiStyleVar_WindowRounding, "WindowRounding", VarKind::kFloat, 0.0f, 0.0f, false },
			VarItem{ IG::ImGuiStyleVar_WindowBorderSize, "WindowBorderSize", VarKind::kFloat, 0.0f, 0.0f, false },
			VarItem{ IG::ImGuiStyleVar_ChildRounding, "ChildRounding", VarKind::kFloat, 3.0f, 0.0f, false },
			VarItem{ IG::ImGuiStyleVar_ChildBorderSize, "ChildBorderSize", VarKind::kFloat, 1.0f, 0.0f, false },
			VarItem{ IG::ImGuiStyleVar_PopupRounding, "PopupRounding", VarKind::kFloat, 4.0f, 0.0f, false },
			VarItem{ IG::ImGuiStyleVar_PopupBorderSize, "PopupBorderSize", VarKind::kFloat, 1.0f, 0.0f, false },
			VarItem{ IG::ImGuiStyleVar_FramePadding, "FramePadding", VarKind::kVec2, 8.0f, 4.0f, true },
			VarItem{ IG::ImGuiStyleVar_FrameRounding, "FrameRounding", VarKind::kFloat, 3.0f, 0.0f, false },
			VarItem{ IG::ImGuiStyleVar_FrameBorderSize, "FrameBorderSize", VarKind::kFloat, 1.0f, 0.0f, false },
			VarItem{ IG::ImGuiStyleVar_ItemSpacing, "ItemSpacing", VarKind::kVec2, 8.0f, 4.0f, true },
			VarItem{ IG::ImGuiStyleVar_ItemInnerSpacing, "ItemInnerSpacing", VarKind::kVec2, 6.0f, 4.0f, true },
			VarItem{ IG::ImGuiStyleVar_IndentSpacing, "IndentSpacing", VarKind::kFloat, 16.0f, 0.0f, true },
			VarItem{ IG::ImGuiStyleVar_ScrollbarSize, "ScrollbarSize", VarKind::kFloat, 10.0f, 0.0f, true },
			VarItem{ IG::ImGuiStyleVar_ScrollbarRounding, "ScrollbarRounding", VarKind::kFloat, 5.0f, 0.0f, false },
			VarItem{ IG::ImGuiStyleVar_GrabMinSize, "GrabMinSize", VarKind::kFloat, 10.0f, 0.0f, true },
			VarItem{ IG::ImGuiStyleVar_GrabRounding, "GrabRounding", VarKind::kFloat, 3.0f, 0.0f, false },
			VarItem{ IG::ImGuiStyleVar_TabRounding, "TabRounding", VarKind::kFloat, 4.0f, 0.0f, false },
			VarItem{ IG::ImGuiStyleVar_TabBorderSize, "TabBorderSize", VarKind::kFloat, 0.0f, 0.0f, false },
		};

		// Per window, what differs from the table: the stage bar is a framed
		// chip (1 px border, 4 px radius), the card is the same.
		struct Override
		{
			float rounding;
			float border;
		};

		Override Of(Kind a_kind)
		{
			switch (a_kind) {
			case Kind::kStageBar:
			case Kind::kCard:
				return { 4.0f, 1.0f };
			default:
				return { 0.0f, 0.0f };
			}
		}

		const char* Name(Kind a_kind)
		{
			switch (a_kind) {
			case Kind::kPanel:
				return "panel";
			case Kind::kBelt:
				return "belt";
			case Kind::kStageBar:
				return "stagebar";
			default:
				return "card";
			}
		}

		float g_scale = 1.0f;
		float g_loggedScale[4] = { -1.0f, -1.0f, -1.0f, -1.0f };

		// The balance (cell O).
		IG::ImGuiStyle g_before{};
		bool           g_haveBefore = false;
		std::uint64_t  g_frames = 0;
		std::uint64_t  g_changed = 0;
		std::uint64_t  g_pushedColors = 0;
		std::uint64_t  g_poppedColors = 0;
		std::uint64_t  g_pushedVars = 0;
		std::uint64_t  g_poppedVars = 0;
		bool           g_saidChange = false;

		std::uint32_t Hex(const IG::ImVec4& a_c)
		{
			auto b = [](float a_v) { return static_cast<std::uint32_t>((std::clamp)(a_v, 0.0f, 1.0f) * 255.0f + 0.5f); };
			return (b(a_c.x) << 24) | (b(a_c.y) << 16) | (b(a_c.z) << 8) | b(a_c.w);
		}

		float BaseFontSize()
		{
			auto* io = IG::GetIO();
			auto* font = IG::GetFont();
			if (!font) {
				return 0.0f;
			}
			return (io ? io->FontGlobalScale : 1.0f) * font->FontSize * font->Scale;
		}
	}

	namespace Color
	{
		IG::ImVec4 PanelBg() { return kPanelBg; }
		IG::ImVec4 PanelBorder() { return kPanelBorder; }
		IG::ImVec4 Text() { return kText; }
		IG::ImVec4 TextDim() { return kTextDim; }
		IG::ImVec4 Accent() { return kAccent; }
		IG::ImVec4 Warn() { return kWarn; }
		IG::ImVec4 Bad() { return kBad; }
		IG::ImVec4 RowSelected() { return kRowSelected; }
		IG::ImVec4 StageBorder() { return kStageBorder; }
	}

	void SetScale(float a_scale)
	{
		g_scale = (std::clamp)(a_scale, 0.8f, 2.5f);
	}

	float Scale()
	{
		return g_scale;
	}

	float Px(float a_cssPx)
	{
		return a_cssPx * g_scale;
	}

	Scope::Scope(Kind a_kind)
	{
		for (const auto& c : kColors) {
			IG::PushStyleColor(c.col, c.v);
			++colors_;
		}
		const auto o = Of(a_kind);
		for (const auto& v : kVars) {
			const float k = v.scaled ? g_scale : 1.0f;
			if (v.kind == VarKind::kVec2) {
				IG::PushStyleVar(v.var, IG::ImVec2{ v.x * k, v.y * k });
			} else if (v.var == IG::ImGuiStyleVar_WindowRounding) {
				IG::PushStyleVar(v.var, o.rounding);
			} else if (v.var == IG::ImGuiStyleVar_WindowBorderSize) {
				IG::PushStyleVar(v.var, o.border);
			} else {
				IG::PushStyleVar(v.var, v.x * k);
			}
			++vars_;
		}
		g_pushedColors += colors_;
		g_pushedVars += vars_;
		if (auto* f = Font()) {
			IG::PushFont(f);
			font_ = true;
		}
	}

	IG::ImFont* Font()
	{
		// The framework names a TTF it loaded "<file>, <size>px" (D293).
		constexpr char kFile[] = "lifecast-sans.ttf";
		auto*          io = IG::GetIO();
		if (!io || !io->Fonts) {
			return nullptr;
		}
		for (int i = 0; i < io->Fonts->Fonts.Size; ++i) {
			auto* f = io->Fonts->Fonts.Data[i];
			if (!f || !f->ConfigData) {
				continue;
			}
			const char* n = f->ConfigData->Name;
			std::size_t k = 0;
			while (k + 1 < sizeof(kFile) && n[k] != 0 &&
				   std::tolower(static_cast<unsigned char>(n[k])) == static_cast<unsigned char>(kFile[k])) {
				++k;
			}
			if (k + 1 == sizeof(kFile)) {
				return f;
			}
		}
		return nullptr;
	}

	Scope::~Scope()
	{
		if (font_) {
			IG::PopFont();
		}
		IG::PopStyleVar(vars_);
		IG::PopStyleColor(colors_);
		g_poppedColors += colors_;
		g_poppedVars += vars_;
	}

	void Body(float a_cssPx)
	{
		// The window's own scale multiplies its parent's: measure at 1 first.
		IG::SetWindowFontScale(1.0f);
		const float now = IG::GetFontSize();
		if (now > 0.0f) {
			IG::SetWindowFontScale(Px(a_cssPx) / now);
		}
	}

	void Heading(const char* a_text, float a_cssPx, const IG::ImVec4& a_color)
	{
		auto* font = IG::GetFont();
		if (!font || !a_text) {
			return;
		}
		const float size = Px(a_cssPx);
		const auto  dim = IG::ImFontManger::CalcTextSizeA(font, size, 1.0e9f, 0.0f, a_text, nullptr, nullptr);
		const auto  at = IG::GetCursorScreenPos();
		IG::Dummy(IG::ImVec2{ dim.x, dim.y });
		IG::ImDrawListManager::AddText(IG::GetWindowDrawList(), font, size, at, IG::ColorConvertFloat4ToU32(a_color), a_text);
	}

	void Title(const char* a_text)
	{
		Heading(a_text, 22.0f, kAccent);  // --title-size, --accent (.panel-title)
	}

	void SectionTitle(const char* a_text)
	{
		Heading(a_text, 14.0f, kText);  // --section-title-size
	}

	void Group(const char* a_text)
	{
		Heading(a_text, 12.0f, kAccent);  // --group-size, the accent (.group-heading)
	}

	void LogWindow(Kind a_kind)
	{
		auto& last = g_loggedScale[static_cast<int>(a_kind)];
		if (last == g_scale) {
			return;
		}
		last = g_scale;
		const auto* style = IG::GetStyle();
		if (!style) {
			return;
		}
		// What ImGui holds while this window draws: the pushed values read back.
		std::string colors;
		for (const auto& c : kColors) {
			colors += fmt::format("{}{} {:08X}", colors.empty() ? "" : " ", c.name, Hex(style->Colors[c.col]));
		}
		auto* font = IG::GetFont();
		std::string fontName = "?";
		if (font && font->ConfigData) {
			fontName = font->ConfigData->Name;
		}
		spdlog::info("[smf] theme {}: scale {:.2f} | colors {} | WindowPadding {:.1f}x{:.1f} WindowRounding {:.0f} "
					 "WindowBorderSize {:.0f} FramePadding {:.1f}x{:.1f} FrameRounding {:.0f} FrameBorderSize {:.0f} "
					 "ItemSpacing {:.1f}x{:.1f} ScrollbarSize {:.1f} ScrollbarRounding {:.0f} GrabRounding {:.0f} "
					 "TabRounding {:.0f} PopupRounding {:.0f} ChildRounding {:.0f} | text {:.1f} px (font '{}' {:.1f} px, "
					 "window scale {:.3f})",
			Name(a_kind), g_scale, colors, style->WindowPadding.x, style->WindowPadding.y, style->WindowRounding,
			style->WindowBorderSize, style->FramePadding.x, style->FramePadding.y, style->FrameRounding,
			style->FrameBorderSize, style->ItemSpacing.x, style->ItemSpacing.y, style->ScrollbarSize,
			style->ScrollbarRounding, style->GrabRounding, style->TabRounding, style->PopupRounding, style->ChildRounding,
			IG::GetFontSize(), fontName, BaseFontSize(), BaseFontSize() > 0.0f ? IG::GetFontSize() / BaseFontSize() : 0.0f);
	}

	void FrameStart()
	{
		const auto* style = IG::GetStyle();
		g_haveBefore = style != nullptr;
		if (style) {
			std::memcpy(&g_before, style, sizeof(IG::ImGuiStyle));
		}
	}

	void FrameEnd()
	{
		const auto* style = IG::GetStyle();
		if (!g_haveBefore || !style) {
			return;
		}
		++g_frames;
		if (std::memcmp(&g_before, style, sizeof(IG::ImGuiStyle)) != 0) {
			++g_changed;
			if (!g_saidChange) {
				g_saidChange = true;
				spdlog::warn("[smf] theme balance: the global style differs after the view's frame {} - pushes {}/{} "
							 "pops {}/{}",
					g_frames, g_pushedColors, g_pushedVars, g_poppedColors, g_poppedVars);
			}
		}
	}

	void LogBalance(const char* a_what)
	{
		spdlog::info("[smf] theme balance {}: frames {}, global style changed across a frame {}, colors pushed {} "
					 "popped {}, vars pushed {} popped {}",
			a_what, g_frames, g_changed, g_pushedColors, g_poppedColors, g_pushedVars, g_poppedVars);
	}
}
