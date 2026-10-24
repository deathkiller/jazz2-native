#pragma once

#if defined(WITH_ANGELSCRIPT) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "Common.h"
#include "../../UI/Canvas.h"

namespace Jazz2::Scripting::Legacy
{
	/**
		@brief Sprites and rectangles a level script draws in the level (`jjDrawSprite()` and the like)

		Attached to the scene of the level, so what's drawn is in the level, at the depth of a layer. The script draws in
		its updates, everything is drawn until the next update of the script clears it, as in JJ2+ where a draw lasts
		one frame.
	*/
	class ScriptWorldCanvas : public UI::Canvas
	{
	public:
		/** @brief Frame of a sprite drawn in the level */
		struct SpriteDraw {
			/** @brief Graphics with the frame */
			Resources::GenericGraphicResource* Graphics;
			/** @brief Frame of the graphics */
			std::int32_t Cell;
			/** @brief Position of the hotspot */
			float X, Y;
			/** @brief Direction, `SPRITE::Direction` */
			std::int8_t Direction;
			/** @brief Scale, negative flips */
			float ScaleX, ScaleY;
			/** @brief Angle, 1024 is a full turn */
			std::int32_t Angle;
			/** @brief Sprite mode (`SPRITE::Mode`) and its parameter */
			std::uint32_t Mode;
			std::uint8_t Param;
			/** @brief Depth */
			std::uint16_t Z;
			/** @brief Palette offset a palette-indexed frame is drawn with, see @ref LevelScriptLoader::GetSpritePaletteOffset() */
			std::int32_t PaletteOffset;
		};

		/** @brief Rectangle of a color of the palette drawn in the level */
		struct SolidDraw {
			/** @brief Position and size */
			float X, Y, Width, Height;
			/** @brief Index of the color in the palette */
			std::uint8_t Color;
			/** @brief Sprite mode (`SPRITE::Mode`) and its parameter */
			std::uint32_t Mode;
			std::uint8_t Param;
			/** @brief Depth */
			std::uint16_t Z;
		};

		/** @brief Draws a sprite in the level until the next @ref Clear() */
		void AddSprite(const SpriteDraw& draw) {
			_sprites.push_back(draw);
		}
		/** @brief Draws a rectangle in the level until the next @ref Clear() */
		void AddSolid(const SolidDraw& draw) {
			_solids.push_back(draw);
		}
		/** @brief Removes everything drawn */
		void Clear() {
			_sprites.clear();
			_solids.clear();
		}

		bool OnDraw(RenderQueue& renderQueue) override;

	private:
		SmallVector<SpriteDraw, 0> _sprites;
		SmallVector<SolidDraw, 0> _solids;
	};
}

#endif
