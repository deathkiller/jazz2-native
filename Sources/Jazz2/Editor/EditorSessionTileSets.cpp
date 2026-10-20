#include "EditorSessionPrivate.h"

#if defined(WITH_EDITOR)

// Tile sets read whole, the tiles the shown level overrides and what is learned of the tiles, of EditorSession

namespace Jazz2::Editor
{
	namespace
	{
		constexpr std::uint64_t HashSeed = TileSetDataCache::HashSeed;

		// FNV-1a, enough to tell whether what a tile shows changed
		inline std::uint64_t HashBytes(const void* data, std::size_t size, std::uint64_t hash = HashSeed)
		{
			return TileSetDataCache::HashBytes(data, size, hash);
		}

		template<class T>
		const T* FindByTileId(const SmallVectorImpl<T>& tiles, std::uint16_t tileId)
		{
			auto it = std::lower_bound(tiles.begin(), tiles.end(), tileId, [](const T& tile, std::uint16_t id) {
				return (tile.TileId < id);
			});
			return (it != tiles.end() && it->TileId == tileId ? &*it : nullptr);
		}

		template<class T>
		void SortByTileId(SmallVectorImpl<T>& tiles)
		{
			std::sort(tiles.begin(), tiles.end(), [](const T& a, const T& b) {
				return (a.TileId < b.TileId);
			});
		}
	}

	bool EditorSession::ApplyTileOverrides(bool palette)
	{
		Tiles::TileMap* tileMap = (_editHandler != nullptr ? _editHandler->TileMap() : nullptr);
		if (tileMap == nullptr) {
			return false;
		}

		// The tiles of the level itself, only when their bytes may have changed, and only those that did. A stroke of
		// the tile editor or moving the palette with ~1000 overridden tiles then gives the shown level only what changed.
		SmallVector<std::uint16_t, 0> changed;
		if (palette || _appliedLevelTilesGeneration != _tileSetData.GetLevelTilesGeneration()) {
			if (!SyncLevelTiles(tileMap, palette, changed)) {
				return false;
			}
		}
		std::sort(changed.begin(), changed.end());
		std::size_t levelTilesChanged = changed.size();
		// An override of a tile of the level is given again over the new tile
		auto isLevelTileChanged = [&changed, levelTilesChanged](std::uint16_t tileId) {
			return std::binary_search(changed.begin(), changed.begin() + levelTilesChanged, tileId);
		};

		SmallVector<AppliedTile, 0> diffuse, masks;
		CollectAppliedTiles(diffuse, masks);
		// A tile that isn't overridden anymore is as its tile set has it, which only loading the level again gives
		for (const AppliedTile& applied : _appliedDiffuseOverrides) {
			if (FindByTileId(diffuse, applied.TileId) == nullptr) {
				return false;
			}
		}
		for (const AppliedTile& applied : _appliedMaskOverrides) {
			if (FindByTileId(masks, applied.TileId) == nullptr) {
				return false;
			}
		}

		for (const Formats::TileDiffuseOverride& tile : _document.DiffuseOverrides) {
			const AppliedTile* applied = FindByTileId(_appliedDiffuseOverrides, tile.TileId);
			const AppliedTile* current = FindByTileId(diffuse, tile.TileId);
			// The colors of a tile set that isn't indexed were taken from the palette when the tile was given
			if (applied == nullptr || current == nullptr || applied->Hash != current->Hash || isLevelTileChanged(tile.TileId) ||
				(palette && !tileMap->IsTileSetIndexed(tile.TileId))) {
				ContentResolver::Get().ApplyTileDiffuseOverride(*tileMap, tile.TileId, tile.Indices);
				changed.push_back(tile.TileId);
			}
		}
		for (const Formats::TileMaskOverride& tile : _document.MaskOverrides) {
			const AppliedTile* applied = FindByTileId(_appliedMaskOverrides, tile.TileId);
			const AppliedTile* current = FindByTileId(masks, tile.TileId);
			if (applied == nullptr || current == nullptr || applied->Hash != current->Hash || isLevelTileChanged(tile.TileId)) {
				tileMap->OverrideTileMask(tile.TileId, tile.Mask);
				changed.push_back(tile.TileId);
			}
		}
		_appliedDiffuseOverrides = Death::move(diffuse);
		_appliedMaskOverrides = Death::move(masks);

		// Only the images that show the changed tiles are made again
		if (!changed.empty()) {
			std::sort(changed.begin(), changed.end());
			changed.erase(std::unique(changed.begin(), changed.end()), changed.end());
			_maskOverlay->InvalidateTileIds(_document, changed);
			_tileLod->InvalidateTileIds(_document, changed);
		}
		return true;
	}

	void EditorSession::CollectAppliedTiles(SmallVectorImpl<AppliedTile>& diffuse, SmallVectorImpl<AppliedTile>& masks)
	{
		diffuse.clear();
		for (const Formats::TileDiffuseOverride& tile : _document.DiffuseOverrides) {
			diffuse.push_back(AppliedTile{ tile.TileId, HashBytes(tile.Indices, sizeof(tile.Indices)) });
		}
		SortByTileId(diffuse);
		masks.clear();
		for (const Formats::TileMaskOverride& tile : _document.MaskOverrides) {
			masks.push_back(AppliedTile{ tile.TileId, HashBytes(tile.Mask, sizeof(tile.Mask)) });
		}
		SortByTileId(masks);
	}

	bool EditorSession::SyncLevelTiles(Tiles::TileMap* tileMap, bool palette, SmallVectorImpl<std::uint16_t>& changed)
	{
		constexpr std::int32_t PixelCount = Formats::LevelFile::TileSize * Formats::LevelFile::TileSize;

		SmallVector<AppliedTile, 0> tiles;
		for (std::int32_t slot = 0; slot < std::int32_t(_document.ExtraTileSets.size()); slot++) {
			const Formats::ExtraTileSetRecord& record = _document.ExtraTileSets[slot];
			if ((record.Flags & Formats::ExtraTileSetRecord::Embedded) == 0) {
				continue;
			}
			const Formats::TileSetData* data = GetTileSetData(record.Name);
			if (data == nullptr) {
				return false;
			}
			// The 8-bit tiles of a remapped tile set are shown in the colors of the level, as the game loads them
			const std::uint8_t* remapping = ((record.Flags & (Formats::ExtraTileSetRecord::Remapped | Formats::ExtraTileSetRecord::TrueColor)) ==
				Formats::ExtraTileSetRecord::Remapped ? record.Remapping : nullptr);
			for (std::int32_t i = 0; i < record.Count && record.Offset + i < data->TileCount; i++) {
				std::int32_t index = record.Offset + i;
				std::int32_t tileId = _document.ToTileId(TileRef{ std::uint16_t(i), std::uint8_t(slot + 1), 0 });
				if (tileId < 0 || tileId > UINT16_MAX) {
					return false;
				}
				bool trueColor = (data->TrueColor[index] != 0);
				const std::uint8_t* image = &data->Images[std::size_t(index) * Formats::TileSetData::ImageBytesPerTile];
				const std::uint8_t* bits = &data->Masks[std::size_t(index) * Formats::TileSetData::MaskBytesPerTile];
				std::uint8_t remapped[PixelCount];
				if (!trueColor && remapping != nullptr) {
					for (std::int32_t p = 0; p < PixelCount; p++) {
						remapped[p] = remapping[image[p]];
					}
					image = remapped;
				}
				std::uint64_t hash = HashBytes(image, trueColor ? std::size_t(PixelCount * 4) : std::size_t(PixelCount));
				hash = HashBytes(bits, Formats::TileSetData::MaskBytesPerTile, hash);
				hash = HashBytes(&trueColor, sizeof(trueColor), hash);
				tiles.push_back(AppliedTile{ std::uint16_t(tileId), hash });
				if (tileMap == nullptr) {
					continue;
				}

				const AppliedTile* applied = FindByTileId(_appliedLevelTiles, std::uint16_t(tileId));
				if (applied != nullptr && applied->Hash == hash && !(palette && !tileMap->IsTileSetIndexed(tileId))) {
					continue;
				}
				if (trueColor) {
					if (!ContentResolver::Get().ApplyTileDiffuseOverrideRgba(*tileMap, tileId, image)) {
						return false;
					}
				} else {
					ContentResolver::Get().ApplyTileDiffuseOverride(*tileMap, tileId, image);
				}
				std::uint8_t mask[PixelCount];
				for (std::int32_t p = 0; p < PixelCount; p++) {
					mask[p] = ((bits[p / 8] >> (p % 8)) & 1);
				}
				tileMap->OverrideTileMask(tileId, mask);
				changed.push_back(std::uint16_t(tileId));
			}
		}
		SortByTileId(tiles);

		if (tileMap != nullptr) {
			// A tile of the level that's gone is as its tile set has it, which only loading the level again gives
			for (const AppliedTile& applied : _appliedLevelTiles) {
				if (FindByTileId(tiles, applied.TileId) == nullptr) {
					return false;
				}
			}
		}
		_appliedLevelTiles = Death::move(tiles);
		_appliedLevelTilesGeneration = _tileSetData.GetLevelTilesGeneration();
		return true;
	}

	void EditorSession::RememberAppliedOverrides()
	{
		// The level was loaded with them, so they're shown as they are now
		CollectAppliedTiles(_appliedDiffuseOverrides, _appliedMaskOverrides);
		SmallVector<std::uint16_t, 0> changed;
		if (!SyncLevelTiles(nullptr, false, changed)) {
			// All of them are given to the shown level the next time
			_appliedLevelTiles.clear();
			_appliedLevelTilesGeneration = 0;
		}
	}

	const Formats::TileSetData* EditorSession::GetTileSetData(StringView name)
	{
		return _tileSetData.Get(_document, name);
	}

	void EditorSession::InvalidateTileSet(StringView name)
	{
		_tileSetData.Invalidate(name);
		_files->NoteTileSetWritten(name);
		_tileSets.Invalidate();
		_tileLod->Invalidate();
		_accentKey = 0;

		if (StringUtils::lowercase(name) == StringUtils::lowercase(_document.Properties.DefaultTileSet)) {
			// Its tiles may be in another order now, as rearranged in place, so what was learned of them and of their
			// shapes is learned again
			_smartTiles = SmartTileModel();
			_smartTilesUsed = false;
			_tileShapes = TileShapes();
		}
	}

	const TileShapes& EditorSession::GetTileShapes()
	{
		if (const Formats::TileSetData* data = GetTileSetData(_document.Properties.DefaultTileSet)) {
			// The slope tags of the rules are taken into account, tiles stored in the level are pieces of ground too
			SmallVector<TileShapes::LevelTiles, 2> levelTiles;
			for (std::size_t i = 0; i < _document.ExtraTileSets.size() && i < TileRef::LastExtraTileSet; i++) {
				const Formats::ExtraTileSetRecord& record = _document.ExtraTileSets[i];
				if ((record.Flags & Formats::ExtraTileSetRecord::Embedded) != 0) {
					if (const Formats::TileSetData* embedded = GetTileSetData(record.Name)) {
						levelTiles.push_back(TileShapes::LevelTiles{ std::uint8_t(i + 1), embedded });
					}
				}
			}
			_tileShapes.Build(_document.Properties.DefaultTileSet, *data, &GetSmartTiles().GetRules(), &_document, levelTiles);
		}
		return _tileShapes;
	}

	SmartTileModel& EditorSession::GetSmartTiles()
	{
		// Asked for many times a frame, the tile sets of the level change only with a new revision of it
		std::uint32_t revision = _history->GetRevision();
		if (!_smartTilesUsed || revision != _smartTilesRevision) {
			_smartTiles.Request(_document.Properties.DefaultTileSet, _tileSets);
			_smartTiles.GetRules().Resolve(_document);
			_smartTilesRevision = revision;
		}
		// The level being edited counts more than the others, as it is now
		_smartTiles.LearnDocument(_document, revision);
		_smartTilesUsed = true;
		return _smartTiles;
	}

	Colorf EditorSession::GetAccentColor()
	{
		const Formats::TileSetData* tileSetData = GetTileSetData(_document.Properties.DefaultTileSet);
		if (tileSetData == nullptr) {
			return UiTheme::Accent;
		}

		// Asked for at each start and end of a play-test, it stays the same as long as the level and its palette do
		bool levelPalette = (_document.Flags & LevelFlags::UseLevelPalette) == LevelFlags::UseLevelPalette;
		const std::uint32_t* palette = (levelPalette ? _document.LevelPalette : tileSetData->Palette);
		std::uint32_t revision = (_history != nullptr ? _history->GetRevision() : 0);
		std::uint64_t key = HashBytes(palette, Formats::LevelFile::ColorsPerPalette * sizeof(std::uint32_t),
			HashSeed ^ std::uint64_t(reinterpret_cast<std::uintptr_t>(tileSetData)));
		if (key == _accentKey && revision == _accentRevision) {
			return _accentColor;
		}
		_accentKey = key;
		_accentRevision = revision;
		_accentColor = UiTheme::Accent;

		// Only the tiles of the sprite layer, the player sees them the most, as often as they're there
		SmallVector<float, 0> weights;
		weights.resize(std::size_t(tileSetData->TileCount), 0.0f);
		auto count = [this, &weights](const TileRef& tile, float weight) {
			if (tile.Source == TileRef::DefaultTileSet && tile.Index > 0 && tile.Index < weights.size()) {
				weights[tile.Index] += weight;
			} else if (tile.Source == TileRef::Animated && tile.Index < _document.Animations.size()) {
				// The frames of an animated tile share its place
				const DocumentAnimation& animation = _document.Animations[tile.Index];
				for (const TileRef& frame : animation.Frames) {
					if (frame.Source == TileRef::DefaultTileSet && frame.Index > 0 && frame.Index < weights.size()) {
						weights[frame.Index] += weight / float(animation.Frames.size());
					}
				}
			}
		};
		std::int32_t spriteLayer = _document.GetSpriteLayerIndex();
		bool any = false;
		if (spriteLayer >= 0) {
			for (const TileRef& tile : _document.Layers[spriteLayer].Tiles) {
				if (!tile.IsEmpty()) {
					count(tile, 1.0f);
					any = true;
				}
			}
		}

		Colorf color;
		if (FindAccentColor(*tileSetData, palette, any ? ArrayView<const float>(weights) : ArrayView<const float>(), color)) {
			_accentColor = color;
		}
		return _accentColor;
	}
}

#endif
