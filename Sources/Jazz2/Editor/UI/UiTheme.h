#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../../../Main.h"
#include "../../../nCine/Primitives/Colorf.h"

#include <cmath>

using namespace nCine;

namespace Jazz2::Editor
{
	/** @brief Theme of the interface of the level editor */
	enum class UiThemeKind : std::int32_t
	{
		Default,		/**< The usual one */
		HighContrast	/**< Opaque panels, bright borders and text */
	};

	/**
		@brief Colors and sizes of the interface of the level editor

		Sizes are in units, which the interface scale turns into pixels. Text colors are as @ref UI::Font takes them:
		the default color keeps the colors of the font itself, the others tint it, 0.5 being neutral, and an alpha of
		0.5 is fully opaque.

		The colors change with the theme, see @ref Apply(). A color copied from them before --- an element keeps the
		color of its text, for example --- is still drawn in the color of the theme, @ref UiPainter looks it up by its
		default value, see @ref Resolve().

		Things shown over the level --- what is right or wrong, links, teams --- take the overlay colors.
	*/
	struct UiTheme
	{
		/** @{ @name Colors */

		static inline Colorf PanelFill = Colorf(0.10f, 0.09f, 0.20f, 0.94f);
		static inline Colorf PanelFillAlt = Colorf(0.13f, 0.12f, 0.25f, 0.94f);
		static inline Colorf PanelBorder = Colorf(0.62f, 0.48f, 0.20f, 0.85f);
		static inline Colorf PanelBorderDim = Colorf(0.32f, 0.27f, 0.40f, 0.9f);
		static inline Colorf PanelHeader = Colorf(0.18f, 0.15f, 0.32f, 0.96f);
		static inline Colorf Shadow = Colorf(0.0f, 0.0f, 0.0f, 0.45f);
		static inline Colorf Separator = Colorf(0.35f, 0.30f, 0.45f, 0.8f);

		static inline Colorf ButtonFill = Colorf(0.20f, 0.18f, 0.35f, 0.95f);
		static inline Colorf ButtonHover = Colorf(0.28f, 0.25f, 0.46f, 0.95f);
		static inline Colorf ButtonPressed = Colorf(0.14f, 0.12f, 0.26f, 0.95f);
		static inline Colorf ButtonChecked = Colorf(0.55f, 0.40f, 0.12f, 0.95f);
		static inline Colorf ButtonCheckedHover = Colorf(0.66f, 0.49f, 0.16f, 0.95f);
		// Of a button pointed at or pressed, lighter than its fill, softer than the accent of a checked one
		static inline Colorf ButtonOutline = Colorf(0.43f, 0.38f, 0.60f, 0.95f);

		static inline Colorf Accent = Colorf(0.95f, 0.74f, 0.25f, 1.0f);
		static inline Colorf AccentDim = Colorf(0.70f, 0.55f, 0.20f, 1.0f);
		static inline Colorf Selection = Colorf(0.95f, 0.74f, 0.25f, 0.28f);
		static inline Colorf Hover = Colorf(1.0f, 1.0f, 1.0f, 0.08f);
		static inline Colorf InputFill = Colorf(0.04f, 0.04f, 0.10f, 0.95f);
		static inline Colorf InputBorder = Colorf(0.38f, 0.33f, 0.52f, 1.0f);
		static inline Colorf FocusRing = Colorf(0.95f, 0.74f, 0.25f, 0.9f);

		static inline Colorf Play = Colorf(0.30f, 0.75f, 0.35f, 1.0f);
		static inline Colorf Danger = Colorf(0.85f, 0.30f, 0.28f, 1.0f);
		static inline Colorf IconColor = Colorf(0.86f, 0.84f, 0.92f, 1.0f);
		static inline Colorf IconDisabled = Colorf(0.86f, 0.84f, 0.92f, 0.3f);

		static inline Colorf GridLine = Colorf(1.0f, 1.0f, 1.0f, 0.13f);
		static inline Colorf GridLineStrong = Colorf(1.0f, 1.0f, 1.0f, 0.24f);
		static inline Colorf Cursor = Colorf(1.0f, 1.0f, 1.0f, 0.85f);

		/** @brief How much of the opacity of the background of a panel is kept over a blurred picture of the level, see @ref UiPainter::Backdrop() */
		static constexpr float BackdropFillOpacity = 0.8f;
		/** @brief How much of the opacity of the background of a window, a menu or a message is kept over a blurred picture of what is behind it */
		static constexpr float BackdropWindowFillOpacity = 0.93f;

		/** @} */

		/**
		 * @{ @name Text colors
		 *
		 * The font tints its gray glyphs by @cpp 1 + (color - 0.5) * 4 @ce, so 0.5 is the glyph as it is and opaque
		 * and anything much above 0.6 glares. The colors above are toned down when they're used for a text, see
		 * @ref UiPainter::Text(), these are made for it.
		 */

		static inline Colorf Text = Colorf(0.54f, 0.53f, 0.56f, 0.5f);
		static inline Colorf TextDim = Colorf(0.46f, 0.45f, 0.50f, 0.5f);
		static inline Colorf TextDisabled = Colorf(0.45f, 0.45f, 0.48f, 0.345f);
		static inline Colorf TextHeader = Colorf(0.58f, 0.52f, 0.40f, 0.5f);
		static inline Colorf TextOnAccent = Colorf(0.58f, 0.56f, 0.52f, 0.5f);
		static inline Colorf TextAccent = Colorf(0.62f, 0.54f, 0.36f, 0.5f);
		static inline Colorf TextDanger = Colorf(0.60f, 0.40f, 0.38f, 0.5f);
		static inline Colorf TextPlay = Colorf(0.42f, 0.58f, 0.43f, 0.5f);

		/** @} */

		/**
		 * @{ @name Overlay colors
		 *
		 * For what is shown over the level.
		 */

		/** @brief What is right, reachable, allowed */
		static inline Colorf OverlayGood = Colorf(0.30f, 0.85f, 0.35f, 1.0f);
		/** @brief What is wrong, unreachable, blocked */
		static inline Colorf OverlayBad = Colorf(0.95f, 0.30f, 0.25f, 1.0f);
		/** @brief What needs attention */
		static inline Colorf OverlayCaution = Colorf(1.0f, 0.80f, 0.20f, 1.0f);
		/** @brief What is only pointed out, links between things */
		static inline Colorf OverlayInfo = Colorf(0.40f, 0.70f, 1.0f, 1.0f);
		/** @brief Colors of things told apart only by their color, such as the teams, in this order: blue, red, green, yellow */
		static inline Colorf OverlaySeries[4] = { Colorf(0.30f, 0.50f, 1.0f, 1.0f), Colorf(1.0f, 0.30f, 0.30f, 1.0f),
			Colorf(0.30f, 0.90f, 0.40f, 1.0f), Colorf(1.0f, 0.90f, 0.25f, 1.0f) };

		/** @} */

		/** @brief Whether animations of the interface are left out, set by @ref Apply() */
		static inline bool ReducedMotion = false;
		/**
		 * @brief Whether text is in the readable font instead of the pixel font of the game, set by @ref Apply()
		 *
		 * See @ref UiReadableFont. It's measured as the pixel font is, so the layout stays.
		 */
		static inline bool ReadableFont = true;
		/**
		 * @brief Changes whenever text is measured in another font, set by @ref Apply()
		 *
		 * Both fonts have the same height of text, so a width of a text kept for later is measured again when this changes.
		 */
		static inline std::uint32_t FontVersion = 0;

		/**
		 * @brief Sets the colors of a theme, whether animations are left out and the font
		 *
		 * The interface is drawn in them from the next frame.
		 */
		static void Apply(UiThemeKind kind, bool reducedMotion, bool readableFont);
		/** @brief Returns the theme in use */
		static UiThemeKind GetKind();
		/**
		 * @brief Returns the color of the current theme for a color of the default one
		 *
		 * A color made from one of the default theme with another alpha, such as a highlight faded in, gets the color of
		 * the current theme with its own alpha. A color that isn't one of the default theme is returned as it is. With
		 * the default theme, nothing is looked up.
		 */
		static Colorf Resolve(const Colorf& color);
		/** @brief Returns how far an animation of a number of frames moves in a frame, all of the way if motion is reduced */
		static float GetAnimationStep(float timeMult, float frames)
		{
			return (ReducedMotion || frames <= 0.0f ? 1.0f : timeMult / frames);
		}

		/** @{ @name Sizes in units */

		/** @brief Size of text relative to the interface scale */
		static constexpr float TextScale = 0.85f;

		static constexpr float Padding = 4.0f;
		static constexpr float Spacing = 3.0f;
		static constexpr float CornerRadius = 3.0f;
		static constexpr float RowHeight = 14.0f;
		static constexpr float ButtonHeight = 18.0f;
		static constexpr float IconSize = 16.0f;
		static constexpr float MenuBarHeight = 17.0f;
		static constexpr float ToolbarHeight = 26.0f;
		static constexpr float StatusBarHeight = 15.0f;
		static constexpr float ScrollBarWidth = 5.0f;
		static constexpr float ShadowSize = 6.0f;

		/** @} */

		/**
		 * @brief Returns the size text is drawn at for a scale of the interface
		 *
		 * It's @ref TextScale times the scale, unless a whole multiple of the size of the font is close to that ---
		 * the pixel font is crisp only at a whole multiple, so it's taken then. It's never more than a few percent
		 * larger than asked, larger text wouldn't fit the rows anymore, but it can be up to 15 % smaller. The readable
		 * font is drawn for the same scale, so the layout doesn't change with the font.
		 */
		static float GetTextScale(float scale)
		{
			float exact = scale * TextScale;
			float whole = std::round(exact);
			if (whole >= 1.0f && whole <= exact * 1.06f && whole >= exact * 0.85f) {
				return whole;
			}
			return exact;
		}
		/** @brief Returns `true` if text drawn at the scale of the interface is at a whole multiple of the size of the font */
		static bool IsTextScaleWhole(float scale)
		{
			float textScale = GetTextScale(scale);
			return (textScale == std::round(textScale));
		}
	};
}

#endif
