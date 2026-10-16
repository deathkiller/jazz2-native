#include "TileShapes.h"

#if defined(WITH_EDITOR)

#include "../../Formats/TileSetFile.h"

#include <Containers/StringUtils.h>

#include <cmath>

using namespace Death::Containers;

namespace Jazz2::Editor
{
	namespace
	{
		constexpr std::int32_t Size = 32;
	}

	void TileShapes::Build(StringView tileSet, const Formats::TileSetData& data, const TerrainRules* rules)
	{
		std::uint32_t rulesRevision = (rules != nullptr ? rules->GetRevision() : 0);
		if (!_tileSet.empty() && StringUtils::lowercase(_tileSet) == StringUtils::lowercase(tileSet) && _rulesRevision == rulesRevision) {
			return;
		}
		_tileSet = tileSet;
		_rulesRevision = rulesRevision;
		_surfaces.clear();
		_ceilings.clear();

		auto addCeiling = [this](TileRef tile, float left, float right, float error) {
			_ceilings.push_back(Surface{ tile, left, right, error });
			if (std::abs(left - right) > 0.5f) {
				tile.Flags ^= Formats::TileRecord::FlipX;
				_ceilings.push_back(Surface{ tile, right, left, error });
			}
		};

		auto addSurface = [this](std::int32_t index, float left, float right, float error) {
			Surface surface;
			surface.Tile = TileRef{ std::uint16_t(index), TileRef::DefaultTileSet, 0 };
			surface.Left = left;
			surface.Right = right;
			surface.Error = error;
			_surfaces.push_back(surface);
			// The same slope the other way
			if (std::abs(surface.Left - surface.Right) > 0.5f) {
				Surface flipped = surface;
				flipped.Tile.Flags = Formats::TileRecord::FlipX;
				flipped.Left = surface.Right;
				flipped.Right = surface.Left;
				_surfaces.push_back(flipped);
			}
		};

		for (std::int32_t index = 1; index < data.TileCount; index++) {
			// Tagged by hand, the heights are from the bottom of the tile
			if (const TerrainRules::SlopeTag* tag = (rules != nullptr ? rules->FindSlope(std::uint16_t(index)) : nullptr)) {
				if (!tag->Excluded) {
					addSurface(index, float(Size) - tag->Left, float(Size) - tag->Right, 0.0f);
				}
				continue;
			}

			const std::uint8_t* bits = &data.Masks[std::size_t(index) * Formats::TileSetData::MaskBytesPerTile];
			const std::uint8_t* image = &data.Images[std::size_t(index) * Formats::TileSetData::ImageBytesPerTile];
			bool trueColor = (data.TrueColor[index] != 0);
			auto solid = [bits](std::int32_t x, std::int32_t y) {
				std::int32_t i = y * Size + x;
				return ((bits[i / 8] >> (i % 8)) & 1) != 0;
			};

			// A straight line where the solid part ends, below it for ground, above it for a ceiling, by least squares
			auto fit = [&](bool ceiling, float& left, float& right, float& error) {
				// The depth of the surface in each column, the column on the solid side of it has to be solid
				float heights[Size];
				std::int32_t holes = 0, emptyColumns = 0;
				for (std::int32_t x = 0; x < Size; x++) {
					std::int32_t y = 0;
					while (y < Size && solid(x, y) == ceiling) {
						y++;
					}
					heights[x] = float(y);
					if (y == (ceiling ? 0 : Size)) {
						emptyColumns++;
					}
					// Past the surface ground has to be solid, a ceiling empty
					for (std::int32_t other = y; other < Size; other++) {
						holes += ((solid(x, other) == ceiling) ? 1 : 0);
					}
				}
				// Mostly solid on its side of the surface, and at least some of it solid
				if (emptyColumns == Size || holes > Size * 2) {
					return false;
				}
				// And drawn there, not a tile with a mask only, or a thing with gaps standing on the ground
				std::int32_t inside = 0, drawn = 0;
				for (std::int32_t x = 0; x < Size; x++) {
					std::int32_t from = (ceiling ? 0 : std::int32_t(heights[x])), to = (ceiling ? std::int32_t(heights[x]) : Size);
					for (std::int32_t y = from; y < to; y++) {
						std::int32_t i = y * Size + x;
						inside++;
						drawn += ((trueColor ? image[i * 4 + 3] >= 128 : image[i] != 0) ? 1 : 0);
					}
				}
				if (inside == 0 || drawn < inside * 9 / 10) {
					return false;
				}

				float sumX = 0.0f, sumY = 0.0f, sumXX = 0.0f, sumXY = 0.0f;
				for (std::int32_t x = 0; x < Size; x++) {
					float cx = float(x) + 0.5f;
					sumX += cx;
					sumY += heights[x];
					sumXX += cx * cx;
					sumXY += cx * heights[x];
				}
				float slope = (Size * sumXY - sumX * sumY) / (Size * sumXX - sumX * sumX);
				float offset = (sumY - slope * sumX) / Size;
				error = 0.0f;
				for (std::int32_t x = 0; x < Size; x++) {
					error += std::abs(heights[x] - (offset + slope * (float(x) + 0.5f)));
				}
				error /= Size;
				left = offset;
				right = offset + slope * Size;
				return (error <= 2.0f);
			};

			float left, right, error;
			if (fit(false, left, right, error)) {
				addSurface(index, left, right, error);
			}
			if (fit(true, left, right, error)) {
				addCeiling(TileRef{ std::uint16_t(index), TileRef::DefaultTileSet, 0 }, left, right, error);
			}
		}

		// Ground upside down, for a tile set without pieces of ceiling, a little worse than those made for it
		std::size_t groundCount = _surfaces.size();
		for (std::size_t i = 0; i < groundCount; i++) {
			const Surface& ground = _surfaces[i];
			TileRef flipped = ground.Tile;
			flipped.Flags |= Formats::TileRecord::FlipY;
			_ceilings.push_back(Surface{ flipped, float(Size) - ground.Left, float(Size) - ground.Right, ground.Error + 2.0f });
		}
	}
}

#endif
