#pragma once

#if defined(WITH_ANGELSCRIPT) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "Common.h"
#include "../../UI/Canvas.h"

namespace Jazz2::Actors
{
	class ActorBase;
}

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
			/** @brief Object the position is relative to, where it is when it's drawn, `nullptr` for the level */
			Actors::ActorBase* Anchor = nullptr;
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

		/** @brief Tile drawn in the level */
		struct TileDraw {
			/** @brief Tile map of the tile */
			Tiles::TileMap* TileMap;
			/** @brief Position of the top left corner */
			float X, Y;
			/** @brief Tile, as JJ2+ has it, and `TILE::Quadrant` */
			std::uint16_t Tile;
			std::uint8_t Quadrant;
			/** @brief Depth */
			std::uint16_t Z;
		};

		/** @brief Text drawn in the level */
		struct TextDraw {
			/** @brief Text, as the font of the game has it */
			String Text;
			/** @brief Position where the script puts it */
			float X, Y;
			/** @brief Size, `STRING::Size` */
			std::uint32_t Size;
			/** @brief Color the font is drawn in */
			Colorf Color;
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
		/** @brief Draws a tile in the level until the next @ref Clear() */
		void AddTile(const TileDraw& draw) {
			_tiles.push_back(draw);
		}
		/** @brief Draws text in the level until the next @ref Clear() */
		void AddText(TextDraw draw) {
			_texts.push_back(Death::move(draw));
		}
		/** @brief Removes everything drawn */
		void Clear() {
			_sprites.clear();
			_solids.clear();
			_tiles.clear();
			_texts.clear();
		}

		bool OnDraw(RenderQueue& renderQueue) override;

	private:
		SmallVector<SpriteDraw, 0> _sprites;
		SmallVector<SolidDraw, 0> _solids;
		SmallVector<TileDraw, 0> _tiles;
		SmallVector<TextDraw, 0> _texts;
	};

	/**
		@brief Where `onDrawLayer1()` to `onDrawLayer8()` of a level script draw

		Attached to the scene of the level, the functions are called whenever it's drawn for a view, so with the coordinates
		of each layer in that view, and what they draw is at the depth of the layer, in front of its tiles.
	*/
	class ScriptLayerCanvas : public UI::Canvas
	{
	public:
		/** @brief Creates a new instance of a script */
		explicit ScriptLayerCanvas(LevelScriptLoader* owner)
			: _owner(owner)
		{
		}

		bool OnDraw(RenderQueue& renderQueue) override;

	private:
		LevelScriptLoader* _owner;
	};
}

#endif
