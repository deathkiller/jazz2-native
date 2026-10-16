#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../../../Main.h"
#include "../../../nCine/Primitives/Colorf.h"

using namespace nCine;

namespace Jazz2::Editor
{
	/**
		@brief Colors and sizes of the interface of the level editor

		Sizes are in units, which the interface scale turns into pixels. Text colors are as @ref UI::Font takes them:
		the default color keeps the colors of the font itself, the others tint it, 0.5 being neutral, and an alpha of
		0.5 is fully opaque.
	*/
	struct UiTheme
	{
		/** @{ @name Colors */

		static constexpr Colorf PanelFill = Colorf(0.10f, 0.09f, 0.20f, 0.94f);
		static constexpr Colorf PanelFillAlt = Colorf(0.13f, 0.12f, 0.25f, 0.94f);
		static constexpr Colorf PanelBorder = Colorf(0.62f, 0.48f, 0.20f, 0.85f);
		static constexpr Colorf PanelBorderDim = Colorf(0.32f, 0.27f, 0.40f, 0.9f);
		static constexpr Colorf PanelHeader = Colorf(0.18f, 0.15f, 0.32f, 0.96f);
		static constexpr Colorf Shadow = Colorf(0.0f, 0.0f, 0.0f, 0.45f);
		static constexpr Colorf Separator = Colorf(0.35f, 0.30f, 0.45f, 0.8f);

		static constexpr Colorf ButtonFill = Colorf(0.20f, 0.18f, 0.35f, 0.95f);
		static constexpr Colorf ButtonHover = Colorf(0.28f, 0.25f, 0.46f, 0.95f);
		static constexpr Colorf ButtonPressed = Colorf(0.14f, 0.12f, 0.26f, 0.95f);
		static constexpr Colorf ButtonChecked = Colorf(0.55f, 0.40f, 0.12f, 0.95f);
		static constexpr Colorf ButtonCheckedHover = Colorf(0.66f, 0.49f, 0.16f, 0.95f);

		static constexpr Colorf Accent = Colorf(0.95f, 0.74f, 0.25f, 1.0f);
		static constexpr Colorf AccentDim = Colorf(0.70f, 0.55f, 0.20f, 1.0f);
		static constexpr Colorf Selection = Colorf(0.95f, 0.74f, 0.25f, 0.28f);
		static constexpr Colorf Hover = Colorf(1.0f, 1.0f, 1.0f, 0.08f);
		static constexpr Colorf InputFill = Colorf(0.04f, 0.04f, 0.10f, 0.95f);
		static constexpr Colorf InputBorder = Colorf(0.38f, 0.33f, 0.52f, 1.0f);
		static constexpr Colorf FocusRing = Colorf(0.95f, 0.74f, 0.25f, 0.9f);

		static constexpr Colorf Play = Colorf(0.30f, 0.75f, 0.35f, 1.0f);
		static constexpr Colorf Danger = Colorf(0.85f, 0.30f, 0.28f, 1.0f);
		static constexpr Colorf IconColor = Colorf(0.86f, 0.84f, 0.92f, 1.0f);
		static constexpr Colorf IconDisabled = Colorf(0.86f, 0.84f, 0.92f, 0.3f);

		static constexpr Colorf GridLine = Colorf(1.0f, 1.0f, 1.0f, 0.13f);
		static constexpr Colorf GridLineStrong = Colorf(1.0f, 1.0f, 1.0f, 0.24f);
		static constexpr Colorf Cursor = Colorf(1.0f, 1.0f, 1.0f, 0.85f);

		/** @} */

		/**
		 * @{ @name Text colors
		 *
		 * The font tints its gray glyphs by @cpp 1 + (color - 0.5) * 4 @ce, so 0.5 is the glyph as it is and opaque
		 * and anything much above 0.6 glares. The colors above are toned down when they're used for a text, see
		 * @ref UiPainter::Text(), these are made for it.
		 */

		static constexpr Colorf Text = Colorf(0.54f, 0.53f, 0.56f, 0.5f);
		static constexpr Colorf TextDim = Colorf(0.46f, 0.45f, 0.50f, 0.5f);
		static constexpr Colorf TextDisabled = Colorf(0.45f, 0.45f, 0.48f, 0.345f);
		static constexpr Colorf TextHeader = Colorf(0.58f, 0.52f, 0.40f, 0.5f);
		static constexpr Colorf TextOnAccent = Colorf(0.58f, 0.56f, 0.52f, 0.5f);
		static constexpr Colorf TextAccent = Colorf(0.62f, 0.54f, 0.36f, 0.5f);
		static constexpr Colorf TextDanger = Colorf(0.60f, 0.40f, 0.38f, 0.5f);
		static constexpr Colorf TextPlay = Colorf(0.42f, 0.58f, 0.43f, 0.5f);

		/** @} */

		/** @{ @name Sizes in units */

		/** @brief Size of text relative to the interface scale */
		static constexpr float TextScale = 0.85f;

		static constexpr float Padding = 4.0f;
		static constexpr float Spacing = 3.0f;
		static constexpr float CornerRadius = 3.0f;
		static constexpr float RowHeight = 14.0f;
		static constexpr float ButtonHeight = 16.0f;
		static constexpr float IconSize = 16.0f;
		static constexpr float MenuBarHeight = 15.0f;
		static constexpr float ToolbarHeight = 22.0f;
		static constexpr float StatusBarHeight = 13.0f;
		static constexpr float ScrollBarWidth = 5.0f;
		static constexpr float ShadowSize = 6.0f;

		/** @} */
	};
}

#endif
