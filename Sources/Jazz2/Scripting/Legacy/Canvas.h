#pragma once

#if defined(WITH_ANGELSCRIPT) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "Animations.h"

namespace Jazz2::Scripting
{
	namespace Legacy
	{
		/**
		 * @brief Layout and styling options for drawn text
		 *
		 * Passed to @ref jjCANVAS::drawString(), @ref jjDrawString() and @ref jjGetStringWidth() to control spacing, alignment,
		 * movement of the letters and how special characters are treated. Text in one of the built-in sizes uses only
		 * @ref spacing, @ref align, @ref newline and whether @ref xAmp or @ref yAmp are non-zero; text drawn with a font
		 * animation uses @ref spacing, @ref align and the treatment of the special characters.
		 */
		struct jjTEXTAPPEARANCE {
			enum align_ {
				align_DEFAULT,
				align_LEFT,
				align_CENTER,
				align_RIGHT
			};

			enum ch_ {
				ch_HIDE,
				ch_DISPLAY,
				ch_SPECIAL
			};

			/**
			 * @brief Horizontal movement of the letters
			 *
			 * For text in a built-in size, a non-zero value together with a non-zero @ref yAmp makes the letters spin
			 * in place.
			 * @partialsupport The magnitude is currently not taken into account, and text drawn with a font animation ignores it.
			 */
			std::int32_t xAmp;
			/**
			 * @brief Vertical movement of the letters
			 *
			 * For text in a built-in size, a non-zero value makes the letters bounce up and down (or spin, if @ref xAmp
			 * is non-zero too).
			 * @partialsupport The magnitude is currently not taken into account, and text drawn with a font animation ignores it.
			 */
			std::int32_t yAmp;
			/**
			 * @brief Spacing between characters in pixels
			 *
			 * Added after each character. Defaults to 1.
			 */
			std::int32_t spacing;
			/** @brief Whether every character takes the same width (currently ignored) @partialsupport */
			bool monospace;
			/** @brief Whether a `#` at the very start of the text is left out (currently ignored) @partialsupport */
			bool skipInitialHash;

			/**
			 * @brief Treatment of the `@` character
			 *
			 * With @ref STRING::DISPLAYSIGN it is drawn as a regular character, otherwise it starts a new line. Applies to
			 * text drawn with a font animation.
			 * @partialsupport Ignored for text in a built-in size, and @ref STRING::HIDESIGN starts a new line too.
			 */
			ch_ at = ch_HIDE;
			/**
			 * @brief Treatment of the `^` character
			 *
			 * With @ref STRING::DISPLAYSIGN it is drawn as a regular character, otherwise it is left out. Applies to text
			 * drawn with a font animation.
			 * @partialsupport Ignored for text in a built-in size, and @ref STRING::SPECIALSIGN works as @ref STRING::HIDESIGN.
			 */
			ch_ caret = ch_HIDE;
			/**
			 * @brief Treatment of the `#` character
			 *
			 * With @ref STRING::DISPLAYSIGN it is drawn as a regular character, otherwise it is left out. Applies to text
			 * drawn with a font animation.
			 * @partialsupport Ignored for text in a built-in size, and @ref STRING::SPECIALSIGN works as @ref STRING::HIDESIGN.
			 */
			ch_ hash = ch_HIDE;
			/**
			 * @brief Treatment of the line feed character
			 *
			 * For text in a built-in size, @ref STRING::SPECIALSIGN makes `\n` start a new line. Text drawn with a font
			 * animation always breaks lines at it.
			 */
			ch_ newline = ch_HIDE;
			/**
			 * @brief Treatment of the `|` character
			 *
			 * With @ref STRING::DISPLAYSIGN it is drawn as a regular character, otherwise it is left out. Applies to text
			 * drawn with a font animation.
			 * @partialsupport Ignored for text in a built-in size, and @ref STRING::SPECIALSIGN works as @ref STRING::HIDESIGN.
			 */
			ch_ pipe = ch_HIDE;
			/**
			 * @brief Treatment of the `§` character
			 *
			 * With @ref STRING::DISPLAYSIGN it is drawn as a regular character, otherwise it is left out. Applies to text
			 * drawn with a font animation.
			 * @partialsupport Ignored for text in a built-in size, and @ref STRING::SPECIALSIGN works as @ref STRING::HIDESIGN.
			 */
			ch_ section = ch_HIDE;
			/**
			 * @brief Treatment of the `~` character
			 *
			 * With @ref STRING::DISPLAYSIGN it is drawn as a regular character, otherwise it is left out. Applies to text
			 * drawn with a font animation.
			 * @partialsupport Ignored for text in a built-in size, and @ref STRING::SPECIALSIGN works as @ref STRING::HIDESIGN.
			 */
			ch_ tilde = ch_HIDE;
			/**
			 * @brief Horizontal alignment of the text
			 *
			 * `LEFT`, @ref CENTER and `RIGHT` align the text against the given x coordinate. With `DEFAULT`, the x
			 * coordinate itself selects the alignment on the HUD: below `0x4000` the text starts there, from
			 * `0x4000` it is centered on the screen and shifted by `xPixel - 0x8000`, and from `0xC000` its right
			 * side is aligned to the right edge, moved left by `xPixel - 0x10000`.
			 */
			align_ align = align_DEFAULT;

			/**
			 * @brief Creates a default appearance
			 *
			 * Spacing is 1, the letters don't move, alignment is `DEFAULT` and every special character is hidden.
			 */
			static void constructor(void* self);
			/**
			 * @brief Creates an appearance that matches a @ref STRING::Mode
			 *
			 * Spacing is 1. @ref RIGHTALIGN aligns the text to the right, `BOUNCE` sets @ref yAmp, and @ref SPIN sets both
			 * amplitudes and lets line feeds start new lines. Everything else is as in the default appearance.
			 */
			static void constructorMode(std::uint32_t mode, void* self);

			/**
			 * @brief Applies the settings of a @ref STRING::Mode
			 *
			 * Sets `spacing`, @ref xAmp, @ref yAmp, `align` and @ref newline as the constructor taking a mode does, other
			 * properties keep their values.
			 */
			jjTEXTAPPEARANCE& operator=(std::uint32_t other);
		};

		/**
		 * @brief Drawing surface passed to the HUD and layer drawing hooks
		 *
		 * The HUD hooks (`onDraw*`) receive a canvas covering one player's view, the `onDrawLayer#` hooks one that draws
		 * into the level at the depth of that layer. A canvas is only valid while the hook runs. Its methods draw pixels,
		 * rectangles, tiles, sprites and text immediately; whatever is drawn later in the frame covers what was drawn
		 * before. Most methods take a @ref SPRITE::Mode and its parameter, which control how the colors are rendered.
		 * Nothing drawn this way interacts with the level.
		 */
		struct jjCANVAS {
			/** @brief The canvas this one draws onto */
			UI::Canvas* Target;
			/** @brief Added to every position */
			Vector2f Offset;
			/** @brief Width of the area, text is aligned in it */
			float Width;
			/** @brief Counts the draws in this frame, each is drawn over the ones before it, as in JJ2+ */
			std::uint16_t* DrawCount;
			/** @brief Layers of the draws, the draws after the last one stay on it */
			std::uint16_t FirstLayer, LastLayer;

			/** @brief Creates a new instance bound to a canvas, @p drawCount is shared by the canvases of the frame */
			jjCANVAS(UI::Canvas* target, Vector2f offset, float width, std::uint16_t* drawCount, std::uint16_t firstLayer, std::uint16_t lastLayer);

			/** @brief Returns the layer of the next draw, above every draw before it */
			std::uint16_t NextLayer();
			/**
			 * @brief Returns where text of a width starts, as JJ2 places it
			 *
			 * Around `0x8000` it's centered, moved by the difference, and around `0x10000` it ends that far from the
			 * right edge.
			 */
			float GetTextX(std::int32_t xPixel, float textWidth) const;

			/**
			 * @brief Draws a single pixel
			 *
			 * The pixel at the given position gets the palette color with index `color`, rendered in the sprite mode.
			 * For many pixels, a sprite of your own is usually the better choice.
			 */
			void DrawPixel(std::int32_t xPixel, std::int32_t yPixel, std::uint8_t color, std::uint32_t mode, std::uint8_t param);
			/**
			 * @brief Draws a filled rectangle
			 *
			 * The top left corner is at the given position, the rectangle is `width` by `height` pixels in the palette
			 * color with index `color`. In most `BLEND_` sprite modes, `param` acts as the opacity (255 is opaque).
			 */
			void DrawRectangle(std::int32_t xPixel, std::int32_t yPixel, std::int32_t width, std::int32_t height, std::uint8_t color, std::uint32_t mode, std::uint8_t param);
			/**
			 * @brief Draws a frame of an animation set
			 *
			 * `setID` is an @ref ANIM::Set value, `animation` the index of the animation in that set and `frame` the
			 * index of the frame in the animation, all counted from 0. The frame's hotspot lands on the given position.
			 *
			 * `direction` flips the sprite: negative values flip it horizontally, values from 64 to 127 vertically and
			 * values from -128 to -65 both ways. The @ref SPRITE::Direction constants name these cases.
			 */
			void DrawSprite(std::int32_t xPixel, std::int32_t yPixel, std::int32_t setID, std::uint8_t animation, std::uint8_t frame, int8_t direction, std::uint32_t mode, std::uint8_t param);
			/**
			 * @brief Draws a frame given by its index in @ref jjAnimFrames
			 *
			 * Same as @ref drawSprite(), but the frame is a single index, such as the `curFrame` of an object or a player.
			 */
			void DrawCurFrameSprite(std::int32_t xPixel, std::int32_t yPixel, std::uint32_t sprite, int8_t direction, std::uint32_t mode, std::uint8_t param);
			/**
			 * @brief Draws a scaled frame of an animation set
			 *
			 * Like @ref drawSprite(), but the frame is stretched by `xScale` and `yScale` instead of taking a direction.
			 * A negative scale flips the sprite on that axis. No smoothing is applied.
			 */
			void DrawResizedSprite(std::int32_t xPixel, std::int32_t yPixel, std::int32_t setID, std::uint8_t animation, std::uint8_t frame, float xScale, float yScale, std::uint32_t mode, std::uint8_t param);
			/**
			 * @brief Draws a scaled frame given by its index in @ref jjAnimFrames
			 *
			 * Same as @ref drawResizedSprite(), but the frame is a single index.
			 */
			void DrawResizedCurFrameSprite(std::int32_t xPixel, std::int32_t yPixel, std::uint32_t sprite, float xScale, float yScale, std::uint32_t mode, std::uint8_t param);
			/**
			 * @brief Draws a rotated and scaled frame of an animation set
			 *
			 * `angle` uses 1024 units per full turn (as @ref jjSin() does) and turns the sprite counter-clockwise about its
			 * hotspot; 0 leaves it unrotated. The sprite is scaled by `xScale` and `yScale` before it is rotated.
			 */
			void DrawTransformedSprite(std::int32_t xPixel, std::int32_t yPixel, std::int32_t setID, std::uint8_t animation, std::uint8_t frame, std::int32_t angle, float xScale, float yScale, std::uint32_t mode, std::uint8_t param);
			/**
			 * @brief Draws a rotated and scaled frame given by its index in @ref jjAnimFrames
			 *
			 * Same as @ref drawRotatedSprite(), but the frame is a single index.
			 */
			void DrawTransformedCurFrameSprite(std::int32_t xPixel, std::int32_t yPixel, std::uint32_t sprite, std::int32_t angle, float xScale, float yScale, std::uint32_t mode, std::uint8_t param);
			/**
			 * @brief Draws a frame bent like a swinging vine
			 *
			 * The frame is drawn `length` rows down from the given position, repeating from its top if it is shorter.
			 * Each row is shifted sideways by `curvature * row * row / 4194304` pixels, and the vine gets one row shorter
			 * for every 2048 of the curvature. Pixels with palette index 128 are treated as transparent, as vine
			 * graphics expect.
			 */
			void DrawSwingingVine(std::int32_t xPixel, std::int32_t yPixel, std::uint32_t sprite, std::int32_t length, std::int32_t curvature, std::uint32_t mode, std::uint8_t param);

			/**
			 * @brief Draws a tile of the tile set
			 *
			 * The tile's top left corner is at the given position. `tile` may include the @ref TILE::HFLIPPED,
			 * @ref TILE::VFLIPPED and @ref TILE::ANIMATED flags. With a `tileQuadrant` other than @ref TILE::ALLQUADRANTS, only
			 * that quarter of the tile is drawn, at its place within the tile. This only draws; it doesn't add
			 * anything solid to the level.
			 */
			void ExternalDrawTile(std::int32_t xPixel, std::int32_t yPixel, std::uint16_t tile, std::uint32_t tileQuadrant);
			/**
			 * @brief Draws text in one of the built-in font sizes
			 *
			 * `size` is a @ref STRING::Size and `mode` a @ref STRING::Mode: @ref DARK draws dimmed text, @ref RIGHTALIGN ends the
			 * text at the given position, `BOUNCE` and @ref SPIN move the letters, and `PALSHIFT` shifts the font colors
			 * by `param` palette entries. The x coordinate can also select alignment, see @ref jjTEXTAPPEARANCE::align.
			 * Characters whose frames a script replaced in the font animations are drawn with those frames.
			 * @partialsupport `PALSHIFT` shifts only the replaced frames and `^` drawn as ∞, not the letters of the game
			 * font. `DARK` is a fixed color, and `BOUNCE` and `SPIN` move the letters as the menus of the game do.
			 */
			void DrawTextBasicSize(std::int32_t xPixel, std::int32_t yPixel, const String& text, std::uint32_t size, std::uint32_t mode, std::uint8_t param);
			/**
			 * @brief Draws text in one of the built-in font sizes with a custom appearance
			 *
			 * Uses `spacing`, `align` and @ref newline of the appearance, and lets the letters bounce or spin when its
			 * amplitudes are non-zero.
			 * @partialsupport The text is drawn in `spriteMode` with `param2`; `param1` is ignored. Of the appearance, the
			 * other treatments of characters are ignored and the amplitudes only count as zero or non-zero. Only
			 * translucent, single color, single hue, shadow, brightness and the `BLEND_*` modes (as opacity) change the
			 * letters of the game font, `PALSHIFT` doesn't.
			 */
			void DrawTextExtSize(std::int32_t xPixel, std::int32_t yPixel, const String& text, std::uint32_t size, const jjTEXTAPPEARANCE& appearance, std::uint8_t param1, std::uint32_t mode, std::uint8_t param);
			/**
			 * @brief Draws text in a built-in font size, moving as `STRING::Mode` says (`BOUNCE`, `SPIN`), in a sprite mode
			 *
			 * In `SPIN`, `#` colors every letter on its own as in the texts of levels, as JJ2+ draws a selected menu item.
			 * Elsewhere `\n` breaks the line only with @p newLines. @p spacing and @p align are of `jjTEXTAPPEARANCE`,
			 * `RIGHTALIGN` aligns right too.
			 */
			void DrawScriptText(std::int32_t xPixel, std::int32_t yPixel, const String& text, std::uint32_t size, std::uint32_t stringMode,
				std::uint32_t spriteMode, std::uint8_t param, bool newLines = false, std::int32_t spacing = 1,
				jjTEXTAPPEARANCE::align_ align = jjTEXTAPPEARANCE::align_DEFAULT);
			/** @brief Draws a frame of the font of the original that has no image as the character in the font of the game, returns `false` for any other frame */
			bool DrawMissingFontCharacter(std::int32_t xPixel, std::int32_t yPixel, std::int32_t setID, std::uint8_t animation, std::uint8_t frame,
				std::uint32_t mode, std::uint8_t param);

			/**
			 * @brief Draws text using the frames of an animation as the font
			 *
			 * Frame 0 of the animation is the space and each further frame the next character code, as in the built-in
			 * font animations. A space is 7 pixels wide, other characters as wide as their frames. `@` and line feeds
			 * start a new line, and `|`, `#`, `~`, `^` and `§` are left out. `mode` is a @ref STRING::Mode: @ref RIGHTALIGN
			 * ends the text at the given position and `PALSHIFT` shifts the colors by `param` palette entries.
			 * @partialsupport @ref DARK draws the letters translucent, and the letters of `BOUNCE` and @ref SPIN don't move.
			 */
			void drawString(std::int32_t xPixel, std::int32_t yPixel, const String& text, const jjANIMATION& animation, std::uint32_t mode, std::uint8_t param);

			/**
			 * @brief Draws text using the frames of an animation as the font, with a custom appearance
			 *
			 * Uses `spacing` and `align` of the appearance, and draws the special characters it sets to
			 * @ref STRING::DISPLAYSIGN as regular characters. The text is drawn in `spriteMode` with `param2`.
			 * @partialsupport `param1`, @ref jjTEXTAPPEARANCE::xAmp, @ref jjTEXTAPPEARANCE::yAmp, `monospace` and
			 * `skipInitialHash` are ignored, @ref STRING::SPECIALSIGN works as @ref STRING::HIDESIGN.
			 */
			void drawStringEx(std::int32_t xPixel, std::int32_t yPixel, const String& text, const jjANIMATION& animation, const jjTEXTAPPEARANCE& appearance, std::uint8_t param1, std::uint32_t spriteMode, std::uint8_t param2);

			/**
			 * @brief Draws text into the level using the frames of an animation as the font
			 *
			 * @partialsupport Currently has no effect.
			 */
			static void jjDrawString(float xPixel, float yPixel, const String& text, const jjANIMATION& animation, std::uint32_t mode, std::uint8_t param, std::int8_t layerZ, std::uint8_t layerXY, std::int8_t playerID);
			/**
			 * @brief Draws text into the level using the frames of an animation as the font, with a custom appearance
			 *
			 * @partialsupport Currently has no effect.
			 */
			static void jjDrawStringEx(float xPixel, float yPixel, const String& text, const jjANIMATION& animation, const jjTEXTAPPEARANCE& appearance, std::uint8_t param1, std::uint32_t spriteMode, std::uint8_t param2, std::int8_t layerZ, std::uint8_t layerXY, std::int8_t playerID);
			/**
			 * @brief Returns the width of text drawn with an animation as the font
			 *
			 * Measured in pixels as @ref jjCANVAS::drawString() would lay the text out with the appearance; for text of
			 * several lines, the width of the widest one.
			 */
			static int jjGetStringWidth(const String& text, const jjANIMATION& animation, const jjTEXTAPPEARANCE& style);

		private:
			// Draws text in the font of an animation, laid out by the appearance if any
			void DrawAnimationText(std::int32_t xPixel, std::int32_t yPixel, const String& text, const jjANIMATION& animation,
				const jjTEXTAPPEARANCE* appearance, std::uint32_t mode, std::uint8_t param);
		};
	}
}

#endif
