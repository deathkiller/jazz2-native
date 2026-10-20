#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../Document/LevelDocument.h"
#include "../../../nCine/Primitives/Vector2.h"

#include <cstdlib>

namespace Jazz2::Editor
{
	/**
		@brief Small helpers shared by the tools of the level editor and the smart tiles
	*/
	namespace ToolMath
	{
		/** @brief Remainder of a division that's never negative, for positions wrapped around a layer that repeats */
		inline std::int32_t PositiveModulo(std::int32_t value, std::int32_t divisor)
		{
			std::int32_t result = value % divisor;
			return (result < 0 ? result + divisor : result);
		}

		/** @brief Division rounded down, not towards zero, for chunks of a grid that goes into negative positions */
		inline std::int32_t FloorDivide(std::int32_t value, std::int32_t divisor)
		{
			return (value >= 0 ? value / divisor : -((-value + divisor - 1) / divisor));
		}

		/** @brief Calls a function for every cell on a line between two cells, both included, so a fast drag doesn't leave gaps */
		template<class TFunc>
		void WalkLine(Vector2i from, Vector2i to, TFunc&& func)
		{
			// Bresenham
			std::int32_t dx = std::abs(to.X - from.X), sx = (from.X < to.X ? 1 : -1);
			std::int32_t dy = -std::abs(to.Y - from.Y), sy = (from.Y < to.Y ? 1 : -1);
			std::int32_t error = dx + dy;
			Vector2i cell = from;
			while (true) {
				func(cell);
				if (cell == to) {
					break;
				}
				std::int32_t e2 = error * 2;
				if (e2 >= dy) {
					error += dy;
					cell.X += sx;
				}
				if (e2 <= dx) {
					error += dx;
					cell.Y += sy;
				}
			}
		}

		/** @brief Returns a point of the Catmull-Rom curve between @p p1 and @p p2, @p t from 0 to 1 */
		inline Vector2f CatmullRom(Vector2f p0, Vector2f p1, Vector2f p2, Vector2f p3, float t)
		{
			float t2 = t * t, t3 = t2 * t;
			return (p1 * 2.0f + (p2 - p0) * t + (p0 * 2.0f - p1 * 5.0f + p2 * 4.0f - p3) * t2 + (p1 * 3.0f - p0 - p2 * 3.0f + p3) * t3) * 0.5f;
		}

		/** @brief Returns a key of a cell for a hash map */
		inline std::uint64_t ToCellKey(Vector2i cell)
		{
			return (std::uint64_t(std::uint32_t(cell.Y)) << 32) | std::uint32_t(cell.X);
		}

		/** @brief Returns the cell of a key made by @ref ToCellKey() */
		inline Vector2i FromCellKey(std::uint64_t key)
		{
			return Vector2i(std::int32_t(std::uint32_t(key)), std::int32_t(std::uint32_t(key >> 32)));
		}

		/** @brief Returns a hash of a cell, which picks among tiles about as good so large areas aren't all the same tile */
		inline std::uint32_t HashCell(Vector2i cell)
		{
			std::uint32_t h = std::uint32_t(cell.X) * 0x8DA6B343u ^ std::uint32_t(cell.Y) * 0xD8163841u;
			h ^= h >> 15;
			return h * 0x2C1B3C6Du;
		}

		/** @brief Offsets of the 8 neighbors of a cell, in the order of the bits of @ref SmartTileModel::Neighbor */
		constexpr std::int32_t NeighborOffsets[8][2] = { { -1, -1 }, { 0, -1 }, { 1, -1 }, { -1, 0 }, { 1, 0 }, { -1, 1 }, { 0, 1 }, { 1, 1 } };

		/** @brief Returns whether a tile is solid as the smart tools see it, not empty and not invisible */
		inline bool IsSolidTile(const TileRef& tile)
		{
			return (!tile.IsEmpty() && (tile.Flags & Formats::TileRecord::ModifierMask) != Formats::TileRecord::ModifierInvisible);
		}
	}
}

#endif
