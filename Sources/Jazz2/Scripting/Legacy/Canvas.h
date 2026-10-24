#pragma once

#if defined(WITH_ANGELSCRIPT) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "Animations.h"

namespace Jazz2::Scripting
{
	namespace Legacy
	{
		/** @brief Describes how text is rendered (spacing, animation and special-character handling) */
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

			/** @brief Horizontal wave amplitude */
			std::int32_t xAmp;
			/** @brief Vertical wave amplitude */
			std::int32_t yAmp;
			/** @brief Extra spacing between characters */
			std::int32_t spacing;
			/** @brief Whether characters use fixed (monospace) width */
			bool monospace;
			/** @brief Whether a leading hash character is skipped */
			bool skipInitialHash;

			/** @brief How the at (@) character is handled */
			ch_ at = ch_HIDE;
			/** @brief How the caret (^) character is handled */
			ch_ caret = ch_HIDE;
			/** @brief How the hash (#) character is handled */
			ch_ hash = ch_HIDE;
			/** @brief How the newline character is handled */
			ch_ newline = ch_HIDE;
			/** @brief How the pipe (|) character is handled */
			ch_ pipe = ch_HIDE;
			/** @brief How the section (§) character is handled */
			ch_ section = ch_HIDE;
			/** @brief How the tilde (~) character is handled */
			ch_ tilde = ch_HIDE;
			/** @brief Horizontal text alignment */
			align_ align = align_DEFAULT;

			/** @brief Constructs a default appearance in place */
			static void constructor(void* self);
			/** @brief Constructs an appearance from a packed mode value */
			static void constructorMode(std::uint32_t mode, void* self);

			jjTEXTAPPEARANCE& operator=(std::uint32_t other);
		};

		/**
		 * @brief Drawing surface used by scripts to render onto the HUD or a layer
		 *
		 * Short-lived surface handed to draw callbacks, bound to a canvas - the HUD, or the level for `onDrawLayer#` - and
		 * an offset of its coordinates. Its methods draw pixels, rectangles, tiles, sprites (plain, resized, rotated, vine)
		 * and text there. Mirrors the original JJ2+ `jjCANVAS` passed to `onDraw*` hooks.
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

			/** @brief Draws a single colored pixel */
			void DrawPixel(std::int32_t xPixel, std::int32_t yPixel, std::uint8_t color, std::uint32_t mode, std::uint8_t param);
			/** @brief Draws a filled colored rectangle */
			void DrawRectangle(std::int32_t xPixel, std::int32_t yPixel, std::int32_t width, std::int32_t height, std::uint8_t color, std::uint32_t mode, std::uint8_t param);
			/** @brief Draws a sprite frame from an animation set */
			void DrawSprite(std::int32_t xPixel, std::int32_t yPixel, std::int32_t setID, std::uint8_t animation, std::uint8_t frame, int8_t direction, std::uint32_t mode, std::uint8_t param);
			/** @brief Draws the current frame of the given sprite */
			void DrawCurFrameSprite(std::int32_t xPixel, std::int32_t yPixel, std::uint32_t sprite, int8_t direction, std::uint32_t mode, std::uint8_t param);
			/** @brief Draws a sprite frame scaled by the given factors */
			void DrawResizedSprite(std::int32_t xPixel, std::int32_t yPixel, std::int32_t setID, std::uint8_t animation, std::uint8_t frame, float xScale, float yScale, std::uint32_t mode, std::uint8_t param);
			/** @brief Draws the current frame of the given sprite scaled by the given factors */
			void DrawResizedCurFrameSprite(std::int32_t xPixel, std::int32_t yPixel, std::uint32_t sprite, float xScale, float yScale, std::uint32_t mode, std::uint8_t param);
			/** @brief Draws a sprite frame rotated and scaled */
			void DrawTransformedSprite(std::int32_t xPixel, std::int32_t yPixel, std::int32_t setID, std::uint8_t animation, std::uint8_t frame, std::int32_t angle, float xScale, float yScale, std::uint32_t mode, std::uint8_t param);
			/** @brief Draws the current frame of the given sprite rotated and scaled */
			void DrawTransformedCurFrameSprite(std::int32_t xPixel, std::int32_t yPixel, std::uint32_t sprite, std::int32_t angle, float xScale, float yScale, std::uint32_t mode, std::uint8_t param);
			/** @brief Draws a swinging vine sprite */
			void DrawSwingingVine(std::int32_t xPixel, std::int32_t yPixel, std::uint32_t sprite, std::int32_t length, std::int32_t curvature, std::uint32_t mode, std::uint8_t param);

			/** @brief Draws a single tile from the tileset */
			void ExternalDrawTile(std::int32_t xPixel, std::int32_t yPixel, std::uint16_t tile, std::uint32_t tileQuadrant);
			/** @brief Draws text using the given built-in font size */
			void DrawTextBasicSize(std::int32_t xPixel, std::int32_t yPixel, const String& text, std::uint32_t size, std::uint32_t mode, std::uint8_t param);
			/** @brief Draws text with a custom appearance using the given built-in font size */
			void DrawTextExtSize(std::int32_t xPixel, std::int32_t yPixel, const String& text, std::uint32_t size, const jjTEXTAPPEARANCE& appearance, std::uint8_t param1, std::uint32_t mode, std::uint8_t param);
			/**
			 * @brief Draws text in a built-in font size, moving as `STRING::Mode` says (`BOUNCE`, `SPIN`), in a sprite mode
			 *
			 * In `SPIN`, `#` colors every letter on its own as in the texts of levels, as JJ2+ draws a selected menu item.
			 */
			void DrawScriptText(std::int32_t xPixel, std::int32_t yPixel, const String& text, std::uint32_t size, std::uint32_t stringMode,
				std::uint32_t spriteMode, std::uint8_t param);
			/** @brief Draws a frame of the font of the original that has no image as the character in the font of the game, returns `false` for any other frame */
			bool DrawMissingFontCharacter(std::int32_t xPixel, std::int32_t yPixel, std::int32_t setID, std::uint8_t animation, std::uint8_t frame,
				std::uint32_t mode, std::uint8_t param);

			/** @brief Draws text using the given animation as a font */
			void drawString(std::int32_t xPixel, std::int32_t yPixel, const String& text, const jjANIMATION& animation, std::uint32_t mode, std::uint8_t param);

			/** @brief Draws text with a custom appearance using the given animation as a font */
			void drawStringEx(std::int32_t xPixel, std::int32_t yPixel, const String& text, const jjANIMATION& animation, const jjTEXTAPPEARANCE& appearance, std::uint8_t param1, std::uint32_t spriteMode, std::uint8_t param2);

			/** @brief Draws text directly into the level using the given animation as a font */
			static void jjDrawString(float xPixel, float yPixel, const String& text, const jjANIMATION& animation, std::uint32_t mode, std::uint8_t param, std::int8_t layerZ, std::uint8_t layerXY, std::int8_t playerID);
			/** @brief Draws text with a custom appearance directly into the level using the given animation as a font */
			static void jjDrawStringEx(float xPixel, float yPixel, const String& text, const jjANIMATION& animation, const jjTEXTAPPEARANCE& appearance, std::uint8_t param1, std::uint32_t spriteMode, std::uint8_t param2, std::int8_t layerZ, std::uint8_t layerXY, std::int8_t playerID);
			/** @brief Returns the rendered width of the text in pixels */
			static int jjGetStringWidth(const String& text, const jjANIMATION& animation, const jjTEXTAPPEARANCE& style);
		};
	}
}

#endif
