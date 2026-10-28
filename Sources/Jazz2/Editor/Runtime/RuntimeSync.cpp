#include "RuntimeSync.h"

#if defined(WITH_EDITOR)

#include <algorithm>
#include <cstring>

namespace Jazz2::Editor
{
	namespace
	{
		// Layers drawn from a texture of their tiles, which a pass of its own draws once, a reflection too
		bool IsTexturedBackgroundType(Tiles::LayerRendererType type)
		{
			return (Tiles::LayerDescription::IsTexturedBackground(type) || type == Tiles::LayerRendererType::Reflection);
		}
	}

	bool RuntimeSync::Apply(const LevelDocument& document, Tiles::TileMap& tileMap, const DocumentChanges& changes)
	{
		// Level properties are shown by the level handler, see EditorLevelHandler::ApplyLevelProperties(), the palette
		// by the session, all indexed tiles and sprites take their colors from it as they're drawn
		// Overridden tiles too, see EditorSession::ApplyTileOverrides()
		if (changes.HasAny(DocumentChangeKind::TileSets) || tileMap.GetLayerCount() != std::int32_t(document.Layers.size())) {
			return false;
		}
		if (changes.HasAny(DocumentChangeKind::LayerStructure)) {
			// Layers that were only moved are moved in the shown level too, with their tiles and the state they have there,
			// any other change of the layers needs the level loaded again. So do tiles changed along with a move, which
			// can't tell if their layer is counted from before the move or after it.
			ArrayView<const std::int32_t> order = changes.GetLayerOrder();
			if (order.empty() || !changes.GetTileAreas().empty() || !tileMap.ReorderLayers(order)) {
				return false;
			}
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

		if (changes.HasAny(DocumentChangeKind::LayerProperties | DocumentChangeKind::LayerStructure)) {
			std::int32_t spriteLayer = document.GetSpriteLayerIndex();
			for (std::int32_t i = 0; i < std::int32_t(document.Layers.size()); i++) {
				const DocumentLayer& layer = document.Layers[i];
				Tiles::LayerDescription description = Tiles::TileMap::MakeLayerDescription(layer.Record);
				const Tiles::LayerDescription* current = tileMap.GetLayerDescription(i);
				if (current != nullptr && !layer.IsSpriteLayer() &&
					IsTexturedBackgroundType(current->RendererType) != IsTexturedBackgroundType(description.RendererType)) {
					// The textured background needs its own pass, which is set up only with the viewports
					return false;
				}
				ApplyEditorOpacity(layer, description);
				SetLayerDescription(tileMap, i, description, IsLayerShown(layer), i == spriteLayer);

				// A texture of its own replaced by another one in place, one added or removed changes the size of what the
				// pass draws, which is set up only with the viewports
				ArrayView<const std::uint8_t> shownTexture = tileMap.GetLayerTexture(i);
				if (shownTexture.size() != layer.Texture.size()) {
					return false;
				}
				if (!layer.Texture.empty() && std::memcmp(shownTexture.data(), layer.Texture.data(), layer.Texture.size()) != 0) {
					tileMap.SetLayerTexture(i, layer.Texture);
				}
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
		std::int32_t spriteLayer = document.GetSpriteLayerIndex();
		for (std::int32_t i = 0; i < count; i++) {
			const Tiles::LayerDescription* current = tileMap.GetLayerDescription(i);
			if (current == nullptr) {
				continue;
			}
			// The rest of the description stays, the layer being edited is drawn otherwise than the game draws it
			Tiles::LayerDescription description = *current;
			ApplyEditorOpacity(document.Layers[i], description);
			if (tileMap.IsLayerVisible(i) != IsLayerShown(document.Layers[i]) || description.Color.W != current->Color.W) {
				SetLayerDescription(tileMap, i, description, IsLayerShown(document.Layers[i]), i == spriteLayer);
			}
		}
	}

	void RuntimeSync::SetLayerDescription(Tiles::TileMap& tileMap, std::int32_t layerIndex, const Tiles::LayerDescription& description,
		bool shown, bool isSpriteLayer)
	{
		tileMap.SetLayerDescription(layerIndex, description, shown);
		if (isSpriteLayer) {
			// It's always placed as the game places it, the editor opacity is a part of its color as of any other layer
			tileMap.SetSpriteLayerRenderer(description.RendererType, description.Color);
		}
	}

	void RuntimeSync::ApplyEditorOpacity(const DocumentLayer& layer, Tiles::LayerDescription& description)
	{
		// The color of the other renderers means something else, a tinted layer or a sky has no opacity of its own, a
		// palette-shifted, a single color, an alpha map or a palette-mapped one has it in alpha as an ordinary one
		const auto hasOpacity = [](Tiles::LayerRendererType type) {
			return (type == Tiles::LayerRendererType::Default || type == Tiles::LayerRendererType::PaletteShift ||
				type == Tiles::LayerRendererType::SingleColor ||
				type == Tiles::LayerRendererType::AlphaMap || type == Tiles::LayerRendererType::PaletteMapping);
		};
		if (!hasOpacity(description.RendererType)) {
			return;
		}
		// A sky being edited is drawn as tiles, its own color isn't an opacity then
		Tiles::LayerDescription original = Tiles::TileMap::MakeLayerDescription(layer.Record);
		float opacity = (hasOpacity(original.RendererType) ? original.Color.W : 1.0f);
		description.Color.W = opacity * (layer.EditorOpacity / 255.0f);
	}

	void RuntimeSync::ApplyTiles(const LevelDocument& document, Tiles::TileMap& tileMap, std::int32_t layerIndex, const Recti& area)
	{
		if (layerIndex < 0 || layerIndex >= std::int32_t(document.Layers.size())) {
			return;
		}

		const DocumentLayer& layer = document.Layers[layerIndex];
		std::int32_t x1 = std::max<std::int32_t>(area.X, 0);
		std::int32_t y1 = std::max<std::int32_t>(area.Y, 0);
		std::int32_t x2 = std::min<std::int32_t>(area.X + area.W, layer.GetWidth());
		std::int32_t y2 = std::min<std::int32_t>(area.Y + area.H, layer.GetHeight());
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

	bool RuntimeSync::ShowsAnyTile(const LevelDocument& document, const TileRef& tile, ArrayView<const std::uint16_t> sortedTileIds)
	{
		if (tile.IsEmpty() || sortedTileIds.empty()) {
			return false;
		}
		auto contains = [&document, sortedTileIds](const TileRef& ref) {
			std::int32_t tileId = document.ToTileId(ref);
			return (tileId >= 0 && tileId <= UINT16_MAX && std::binary_search(sortedTileIds.begin(), sortedTileIds.end(), std::uint16_t(tileId)));
		};
		if (tile.Source == TileRef::Animated) {
			if (tile.Index >= document.Animations.size()) {
				return false;
			}
			for (const TileRef& frame : document.Animations[tile.Index].Frames) {
				if (contains(TileRef{ frame.Index, frame.Source, 0 })) {
					return true;
				}
			}
			return false;
		}
		return contains(TileRef{ tile.Index, tile.Source, 0 });
	}

	bool RuntimeSync::IsLayerShown(const DocumentLayer& layer)
	{
		// As the game shows it, a layer hidden in the game isn't shown either, so what is seen is what is played; it's
		// still edited, the layer list and its properties tell it's hidden
		return ((layer.EditorFlags & DocumentLayer::EditorHidden) == 0 && (layer.Record.Flags & Formats::LayerRecord::Visible) != 0);
	}
}

#endif
