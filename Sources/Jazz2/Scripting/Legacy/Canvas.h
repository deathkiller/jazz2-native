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
		 * @brief Drawing surface used by scripts to render onto the HUD
		 *
		 * Short-lived surface handed to draw callbacks, bound to a @ref UI::HUD and a view rectangle. Its methods draw
		 * pixels, rectangles, tiles, sprites (plain, resized, rotated, vine) and text into that frame's HUD layer.
		 * Mirrors the original JJ2+ `jjCANVAS` passed to `onDraw*` hooks.
		 */
		struct jjCANVAS {
			/** @brief The HUD this canvas draws onto */
			UI::HUD* Hud;
			/** @brief The visible view rectangle */
			Rectf View;
			/** @brief Counts the draws into the HUD in this frame, each is drawn over the ones before it, as in JJ2+ */
			std::uint16_t* DrawCount;

			/** @brief Creates a new instance bound to the given HUD and view, @p drawCount is shared by the canvases of the frame */
			jjCANVAS(UI::HUD* hud, const Rectf& view, std::uint16_t* drawCount);

			/** @brief Returns the HUD layer of the next draw, above every draw before it */
			std::uint16_t NextLayer();

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
