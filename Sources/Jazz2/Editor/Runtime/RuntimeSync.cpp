#include "RuntimeSync.h"

#if defined(WITH_EDITOR)

#include <algorithm>

namespace Jazz2::Editor
{
	namespace
	{
		bool IsTexturedBackgroundType(Tiles::LayerRendererType type)
		{
			return (type == Tiles::LayerRendererType::Sky || type == Tiles::LayerRendererType::Circle);
		}
	}

	bool RuntimeSync::Apply(const LevelDocument& document, Tiles::TileMap& tileMap, const DocumentChanges& changes)
	{
		// Level properties are shown by the level handler, see EditorLevelHandler::ApplyLevelProperties(), the palette
		// by the session, all indexed tiles and sprites take their colors from it as they're drawn
		// Overridden tiles too, see EditorSession::ApplyTileOverrides()
		constexpr DocumentChangeKind ReloadKinds = DocumentChangeKind::LayerStructure | DocumentChangeKind::TileSets;

		if (changes.HasAny(ReloadKinds) || tileMap.GetLayerCount() != std::int32_t(document.Layers.size())) {
			return false;
		}

		if (changes.HasAny(DocumentChangeKind::AnimationStructure)) {
#if defined(TILEMAP_LAYER_EFFECTS)
			// X-ray layers make animated tiles of their own from those of the level when it's loaded
			if (tileMap.HasXRayLayers()) {
				return false;
			}
#endif
			SmallVector<Formats::AnimatedTileRecord, 0> animatedTiles;
			animatedTiles.reserve(document.Animations.size());
			for (std::int32_t i = 0; i < std::int32_t(document.Animations.size()); i++) {
				animatedTiles.push_back(document.MakeAnimatedTileRecord(i));
			}
			tileMap.SetAnimatedTiles(std::uint16_t(document.GetAnimatedTilesOffset()), animatedTiles);
			// The animated tiles take the top of the tile IDs, so tiles showing them do so by other IDs now
			for (std::int32_t i = 0; i < std::int32_t(document.Layers.size()); i++) {
				const DocumentLayer& layer = document.Layers[i];
				ApplyTiles(document, tileMap, i, Recti(0, 0, layer.GetWidth(), layer.GetHeight()));
			}
		} else if (changes.HasAny(DocumentChangeKind::AnimationTiming)) {
			// Timing and frames of animations that are all still there, they change in place
			if (tileMap.GetAnimatedTileCount() != std::int32_t(document.Animations.size())) {
				return false;
			}
			for (std::int32_t i = 0; i < std::int32_t(document.Animations.size()); i++) {
				tileMap.SetAnimatedTile(i, document.MakeAnimatedTileRecord(i));
			}
		}

		if (changes.HasAny(DocumentChangeKind::LayerProperties)) {
			for (std::int32_t i = 0; i < std::int32_t(document.Layers.size()); i++) {
				const DocumentLayer& layer = document.Layers[i];
				Tiles::LayerDescription description = Tiles::TileMap::MakeLayerDescription(layer.Record);
				const Tiles::LayerDescription* current = tileMap.GetLayerDescription(i);
				if (current != nullptr && !layer.IsSpriteLayer() &&
					IsTexturedBackgroundType(current->RendererType) != IsTexturedBackgroundType(description.RendererType)) {
					// The textured background needs its own pass, which is set up only with the viewports
					return false;
				}
				tileMap.SetLayerDescription(i, description, IsLayerShown(layer));
			}
		} else if (changes.HasAny(DocumentChangeKind::EditorState)) {
			ApplyLayerStates(document, tileMap);
		}

		if (changes.HasAny(DocumentChangeKind::Tiles)) {
			for (const DocumentTileArea& area : changes.GetTileAreas()) {
				ApplyTiles(document, tileMap, area.Layer, area.Area);
			}
		}

		return true;
	}

	void RuntimeSync::ApplyLayerStates(const LevelDocument& document, Tiles::TileMap& tileMap)
	{
		std::int32_t count = std::min(std::int32_t(document.Layers.size()), tileMap.GetLayerCount());
		for (std::int32_t i = 0; i < count; i++) {
			const Tiles::LayerDescription* description = tileMap.GetLayerDescription(i);
			if (description != nullptr && tileMap.IsLayerVisible(i) != IsLayerShown(document.Layers[i])) {
				tileMap.SetLayerDescription(i, *description, IsLayerShown(document.Layers[i]));
			}
		}
	}

	void RuntimeSync::ApplyTiles(const LevelDocument& document, Tiles::TileMap& tileMap, std::int32_t layerIndex, const Recti& area)
	{
		if (layerIndex < 0 || layerIndex >= std::int32_t(document.Layers.size())) {
			return;
		}

		const DocumentLayer& layer = document.Layers[layerIndex];
		std::int32_t x1 = std::max(area.X, 0);
		std::int32_t y1 = std::max(area.Y, 0);
		std::int32_t x2 = std::min(area.X + area.W, layer.GetWidth());
		std::int32_t y2 = std::min(area.Y + area.H, layer.GetHeight());
		for (std::int32_t y = y1; y < y2; y++) {
			for (std::int32_t x = x1; x < x2; x++) {
				tileMap.ReplaceLayerTile(layerIndex, x, y, MakeLayerTile(document, layer.GetTile(x, y)));
			}
		}
		if (IsTexturedBackgroundType(Tiles::LayerRendererType(layer.Record.RendererType))) {
			// Drawn once into a texture, which has to be drawn again
			tileMap.InvalidateTexturedBackground();
		}
	}

	bool RuntimeSync::IsTexturedBackground(const DocumentLayer& layer)
	{
		return (!layer.IsSpriteLayer() && IsTexturedBackgroundType(Tiles::LayerRendererType(layer.Record.RendererType)));
	}

	Tiles::LayerTile RuntimeSync::MakeLayerTile(const LevelDocument& document, const TileRef& tile)
	{
		return Tiles::TileMap::MakeLayerTile(tile.Flags, std::uint16_t(document.ToTileId(tile)));
	}

	bool RuntimeSync::IsLayerShown(const DocumentLayer& layer)
	{
		// A layer hidden in the game is still shown, so it can be edited
		return ((layer.EditorFlags & DocumentLayer::EditorHidden) == 0);
	}
}

#endif
