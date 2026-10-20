#include "LevelThumbnail.h"

#if defined(WITH_EDITOR)

#include "../Common/TileSource.h"
#include "../Files/TileSetDataCache.h"
#include "../../ContentResolver.h"
#include "../../Formats/TileSetFile.h"
#include "../../Tiles/TileMap.h"

#include <algorithm>
#include <cstring>

#include <Cryptography/xxHash.h>

namespace Jazz2::Editor
{
	namespace
	{
		// The level as the document has it, its layers placed as the tile map has them
		class DocumentPreviewSource : public Formats::LevelPreview::ISource
		{
		public:
			DocumentPreviewSource(const LevelDocument& document, Tiles::TileMap* tileMap, TileSetDataCache& tileSets)
				: _document(document), _tileMap(tileMap), _tileSets(tileSets)
			{
			}

			std::int32_t GetLayerCount() override
			{
				return std::int32_t(_document.Layers.size());
			}

			const Formats::LayerRecord& GetLayer(std::int32_t index) override
			{
				return _document.Layers[index].Record;
			}

			Tiles::LayerDescription GetLayerDescription(std::int32_t index) override
			{
				const Tiles::LayerDescription* description = (_tileMap != nullptr ? _tileMap->GetLayerDescription(index) : nullptr);
				return (description != nullptr ? *description : ISource::GetLayerDescription(index));
			}

			bool GetTilePixels(std::int32_t layer, std::int32_t x, std::int32_t y, LevelThumbnail::TilePixels& result) override
			{
				return LevelThumbnail::GetTilePixels(_document, _tileSets, _document.Layers[layer].GetTile(x, y), result);
			}

		private:
			const LevelDocument& _document;
			Tiles::TileMap* _tileMap;
			TileSetDataCache& _tileSets;
		};
	}

	LevelThumbnail::LevelThumbnail(const LevelDocument& document, TileSetDataCache& tileSets)
		: _document(document), _tileSets(tileSets)
	{
	}

	void LevelThumbnail::Clear()
	{
		_colors.clear();
	}

	bool LevelThumbnail::Revalidate()
	{
		std::uint64_t hash = GetSourceHash(_document, _tileSets);
		if (hash == _sourceHash) {
			return false;
		}
		_sourceHash = hash;
		_colors.clear();
		return true;
	}

	std::uint64_t LevelThumbnail::GetSourceHash(const LevelDocument& document, TileSetDataCache& tileSets)
	{

		// The tiles stored in a level can be a few megabytes
		std::uint64_t hash = 0;
		auto addBytes = [&hash](const void* data, std::size_t size) {
			hash = Death::Cryptography::xxHash3(data, size, hash);
		};
		auto addValue = [&addBytes](std::uint64_t value) {
			addBytes(&value, sizeof(value));
		};

		addBytes(ContentResolver::Get().GetPalettes().data(), 256 * sizeof(std::uint32_t));

		// A tile set read again from its file, as after it's imported anew under the same name, is other data
		auto addTileSet = [&document, &tileSets, &addBytes, &addValue](StringView name) {
			addBytes(name.data(), name.size());
			const Formats::TileSetData* data = tileSets.Get(document, name);
			addValue(std::uint64_t(reinterpret_cast<std::uintptr_t>(data)));
			addValue(data != nullptr ? std::uint64_t(data->TileCount) : 0);
		};
		addTileSet(document.Properties.DefaultTileSet);
		for (const Formats::ExtraTileSetRecord& extra : document.ExtraTileSets) {
			addTileSet(extra.Name);
			addValue(std::uint64_t(extra.Offset) | (std::uint64_t(extra.Count) << 16) | (std::uint64_t(extra.Flags) << 32));
			if ((extra.Flags & Formats::ExtraTileSetRecord::Remapped) != 0) {
				addBytes(extra.Remapping, sizeof(extra.Remapping));
			}
			// The tiles stored in the level are changed in place
			if ((extra.Flags & Formats::ExtraTileSetRecord::Embedded) != 0) {
				addBytes(extra.Data.data(), extra.Data.size());
			}
		}

		// An animated tile stands for its first frame
		addValue(document.Animations.size());
		for (const DocumentAnimation& animation : document.Animations) {
			if (animation.Frames.empty()) {
				addValue(UINT64_MAX);
			} else {
				const TileRef& frame = animation.Frames[0];
				addValue(std::uint64_t(frame.Index) | (std::uint64_t(frame.Source) << 16) | (std::uint64_t(frame.Flags) << 24));
			}
		}
		return hash;
	}

	std::uint32_t LevelThumbnail::GetTileColor(TileRef tile)
	{
		std::uint32_t key = (std::uint32_t(tile.Source) << 16) | tile.Index;
		auto it = _colors.find(key);
		if (it == _colors.end()) {
			it = _colors.emplace(key, ComputeTileColor(tile)).first;
		}
		return it->second;
	}

	bool LevelThumbnail::GetTilePixels(const LevelDocument& document, TileSetDataCache& tileSets, TileRef tile, TilePixels& result)
	{
		if (tile.IsAnimated()) {
			// The first frame stands for the animation
			if (tile.Index >= document.Animations.size() || document.Animations[tile.Index].Frames.empty()) {
				return false;
			}
			std::uint8_t flags = tile.Flags;
			tile = document.Animations[tile.Index].Frames[0];
			tile.Flags ^= (flags & (Formats::TileRecord::FlipX | Formats::TileRecord::FlipY));
			// A modifier of the tile in the layer, translucent or invisible, is what the layer shows the animation with
			if ((flags & Formats::TileRecord::ModifierMask) != 0) {
				tile.Flags = std::uint8_t((tile.Flags & ~Formats::TileRecord::ModifierMask) | (flags & Formats::TileRecord::ModifierMask));
			}
			if (tile.IsAnimated()) {
				return false;
			}
		}
		if (tile.IsEmpty() || tile.Source == TileRef::Raw) {
			return false;
		}

		TileSource source;
		if (!ResolveTileSource(document, tile.Source, source)) {
			return false;
		}
		const Formats::TileSetData* tileSet = tileSets.Get(document, source.Name);
		std::int32_t index = source.Offset + tile.Index;
		if (tileSet == nullptr || index >= tileSet->TileCount) {
			return false;
		}

		result.Pixels = &tileSet->Images[std::size_t(index) * Formats::TileSetData::ImageBytesPerTile];
		result.TrueColor = (tileSet->TrueColor[index] != 0);
		result.Remapping = source.Remapping;
		result.Flags = tile.Flags;
		return true;
	}

	std::uint32_t LevelThumbnail::ComputeTileColor(TileRef tile)
	{
		TilePixels source;
		if (!GetTilePixels(_document, _tileSets, tile, source)) {
			return 0;
		}

		const std::uint32_t* palette = ContentResolver::Get().GetPalettes().data();
		std::uint32_t r = 0, g = 0, b = 0, count = 0;
		for (std::int32_t i = 0; i < 32 * 32; i++) {
			std::uint32_t color = source.GetPixel(i % 32, i / 32, palette);
			if ((color >> 24) < 128) {
				continue;
			}
			r += color & 0xFF;
			g += (color >> 8) & 0xFF;
			b += (color >> 16) & 0xFF;
			count++;
		}
		if (count == 0) {
			return 0;
		}
		// Half covered counts as covered, a tile with a little of something on it as a little
		std::uint32_t alpha = std::min<std::uint32_t>(255, count * 255 / 512);
		return (r / count) | ((g / count) << 8) | ((b / count) << 16) | (alpha << 24);
	}

	void LevelThumbnail::DrawLayer(std::int32_t layerIndex, SmallVectorImpl<std::uint32_t>& pixels)
	{
		const LevelDocument& document = _document;
		pixels.clear();
		if (layerIndex < 0 || layerIndex >= std::int32_t(document.Layers.size())) {
			return;
		}
		const DocumentLayer& layer = document.Layers[layerIndex];
		pixels.reserve(layer.Tiles.size());
		for (const TileRef& tile : layer.Tiles) {
			pixels.push_back(GetTileColor(tile));
		}
	}

	void LevelThumbnail::MakePreview(const LevelDocument& document, Tiles::TileMap* tileMap, TileSetDataCache& tileSets, SmallVectorImpl<std::uint8_t>& data)
	{
		data.clear();
		std::int32_t spriteLayer = document.GetSpriteLayerIndex();
		if (spriteLayer < 0) {
			return;
		}

		Formats::LevelPreview::StartPicker start;
		std::int32_t gridWidth = std::max<std::int32_t>(document.GetEventGridSize().X, 1);
		for (const DocumentEvent& event : document.Events) {
			start.Add(event.Record, std::int32_t(event.Cell % std::uint32_t(gridWidth)), std::int32_t(event.Cell / std::uint32_t(gridWidth)));
		}

		DocumentPreviewSource source(document, tileMap, tileSets);
		const DocumentLayer& spriteLayerData = document.Layers[spriteLayer];
		Formats::LevelPreview::Make(source, start.GetPosition(spriteLayerData.GetWidth(), spriteLayerData.GetHeight()),
			ContentResolver::Get().GetPalettes().data(), data);
	}
}

#endif
