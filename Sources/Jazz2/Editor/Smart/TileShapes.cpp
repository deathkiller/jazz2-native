#include "TileShapes.h"

#if defined(WITH_EDITOR)

#include "../Imaging/TileMasks.h"
#include "../../Formats/TileSetFile.h"

#include <Containers/StringUtils.h>
#include <Cryptography/xxHash.h>

#include <algorithm>
#include <cmath>
#include <cstring>

using namespace Death::Containers;
using Death::Cryptography::xxHash3;

namespace Jazz2::Editor
{
	namespace
	{
		constexpr std::int32_t Size = 32;

		bool EqualsIgnoreCase(StringView a, StringView b)
		{
			if (a.size() != b.size()) {
				return false;
			}
			for (std::size_t i = 0; i < a.size(); i++) {
				char ca = a[i], cb = b[i];
				if (ca >= 'A' && ca <= 'Z') ca = char(ca - 'A' + 'a');
				if (cb >= 'A' && cb <= 'Z') cb = char(cb - 'A' + 'a');
				if (ca != cb) {
					return false;
				}
			}
			return true;
		}

		template<class T>
		void MixValue(std::uint64_t& hash, const T& value)
		{
			hash = xxHash3(&value, sizeof(value), hash);
		}
	}

	std::uint64_t TileShapes::GetLevelKey(const LevelDocument* document, ArrayView<const LevelTiles> levelTiles)
	{
		std::uint64_t hash = 0;
		if (document == nullptr) {
			return hash;
		}
		// The masks the level overrides, by the tile IDs they're of, which move with the tile sets before them
		MixValue(hash, document->DefaultTileCount);
		for (const Formats::ExtraTileSetRecord& record : document->ExtraTileSets) {
			MixValue(hash, record.Count);
		}
		for (const Formats::TileMaskOverride& mask : document->MaskOverrides) {
			MixValue(hash, mask.TileId);
			hash = xxHash3(mask.Mask, sizeof(mask.Mask), hash);
		}
		// The tiles of the level, a change of them decodes them again
		for (const LevelTiles& tiles : levelTiles) {
			MixValue(hash, tiles.Source);
			MixValue(hash, tiles.Data);
			if (tiles.Data != nullptr) {
				MixValue(hash, tiles.Data->TileCount);
				hash = xxHash3(tiles.Data->Masks.data(), tiles.Data->Masks.size(), hash);
			}
		}
		return hash;
	}

	void TileShapes::Build(StringView tileSet, const Formats::TileSetData& data, const TerrainRules* rules,
		const LevelDocument* document, ArrayView<const LevelTiles> levelTiles)
	{
		// Only the slope tags of the rules matter, choosing another terrain or changing its rules analyzes nothing again
		std::uint32_t rulesRevision = (rules != nullptr ? rules->GetSlopeRevision() : 0);
		std::uint64_t levelKey = GetLevelKey(document, levelTiles);
		MixValue(levelKey, rules);
		MixValue(levelKey, &data);
		MixValue(levelKey, data.TileCount);
		if (!_tileSet.empty() && EqualsIgnoreCase(_tileSet, tileSet) && _rulesRevision == rulesRevision && _levelKey == levelKey) {
			return;
		}
		_tileSet = tileSet;
		_rulesRevision = rulesRevision;
		_levelKey = levelKey;
		_revision++;
		_surfaces.clear();
		_ceilings.clear();

		auto addCeiling = [this](TileRef tile, float left, float right, float error) {
			_ceilings.push_back(Surface{ tile, left, right, error });
			if (std::abs(left - right) > 0.5f) {
				tile.Flags ^= Formats::TileRecord::FlipX;
				_ceilings.push_back(Surface{ tile, right, left, error });
			}
		};

		auto addSurface = [this](TileRef tile, float left, float right, float error) {
			Surface surface;
			surface.Tile = tile;
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

		// The masks the level overrides, as bits like those of the tile sets
		struct OverriddenMask
		{
			std::int32_t TileId;
			std::uint8_t Bits[Formats::TileSetData::MaskBytesPerTile];
		};
		SmallVector<OverriddenMask, 0> overrides;
		if (document != nullptr) {
			for (const Formats::TileMaskOverride& mask : document->MaskOverrides) {
				OverriddenMask& converted = overrides.emplace_back();
				converted.TileId = mask.TileId;
				TileMasks::Pack(mask.Mask, converted.Bits);
			}
			std::sort(overrides.begin(), overrides.end(), [](const OverriddenMask& a, const OverriddenMask& b) { return a.TileId < b.TileId; });
		}
		auto findOverride = [&overrides](std::int32_t tileId) -> const std::uint8_t* {
			auto it = std::lower_bound(overrides.begin(), overrides.end(), tileId, [](const OverriddenMask& item, std::int32_t value) { return item.TileId < value; });
			return (it != overrides.end() && it->TileId == tileId ? it->Bits : nullptr);
		};

		// Each tile of a tile set, with its mask as the level has it, `tile` is how the level refers to it
		auto analyze = [&](const Formats::TileSetData& tiles, std::int32_t index, TileRef tile) {
			if (index < 0 || index >= tiles.TileCount || std::size_t(index + 1) * Formats::TileSetData::MaskBytesPerTile > tiles.Masks.size() ||
				std::size_t(index + 1) * Formats::TileSetData::ImageBytesPerTile > tiles.Images.size() || std::size_t(index) >= tiles.TrueColor.size()) {
				return;
			}
			const std::uint8_t* bits = &tiles.Masks[std::size_t(index) * Formats::TileSetData::MaskBytesPerTile];
			if (document != nullptr && !overrides.empty()) {
				if (const std::uint8_t* overridden = findOverride(document->ToTileId(tile))) {
					bits = overridden;
				}
			}
			const std::uint8_t* image = &tiles.Images[std::size_t(index) * Formats::TileSetData::ImageBytesPerTile];
			bool trueColor = (tiles.TrueColor[index] != 0);
			auto solid = [bits](std::int32_t x, std::int32_t y) {
				return TileMasks::GetBit(bits, y * Size + x);
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
				addSurface(tile, left, right, error);
			}
			if (fit(true, left, right, error)) {
				addCeiling(tile, left, right, error);
			}
		};

		for (std::int32_t index = 1; index < data.TileCount; index++) {
			TileRef tile{ std::uint16_t(index), TileRef::DefaultTileSet, 0 };
			// Tagged by hand, the heights are from the bottom of the tile
			if (const TerrainRules::SlopeTag* tag = (rules != nullptr ? rules->FindSlope(std::uint16_t(index)) : nullptr)) {
				if (!tag->Excluded) {
					addSurface(tile, float(Size) - tag->Left, float(Size) - tag->Right, 0.0f);
				}
				continue;
			}
			analyze(data, index, tile);
		}

		// The tiles the level has in itself, by their index in the slot the level gives them
		if (document != nullptr) {
			for (const LevelTiles& tiles : levelTiles) {
				if (tiles.Data == nullptr || tiles.Source == TileRef::DefaultTileSet || tiles.Source > document->ExtraTileSets.size()) {
					continue;
				}
				const Formats::ExtraTileSetRecord& record = document->ExtraTileSets[tiles.Source - 1];
				for (std::int32_t index = 0; index < std::int32_t(record.Count); index++) {
					analyze(*tiles.Data, std::int32_t(record.Offset) + index, TileRef{ std::uint16_t(index), tiles.Source, 0 });
				}
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
