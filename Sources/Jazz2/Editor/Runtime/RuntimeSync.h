#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../Document/DocumentChanges.h"
#include "../Document/LevelDocument.h"
#include "../../Tiles/TileMap.h"

namespace Jazz2::Editor
{
	/**
		@brief Brings the level shown by the editor up to date with the level being edited

		Most changes are applied to the shown level in place, layers that were only moved too. The rest --- other tile
		sets, added, removed or resized layers, changed animations --- need the level to be loaded again from the
		document, which the editor does when told so.
	*/
	class RuntimeSync
	{
	public:
		RuntimeSync() = delete;

		/** @brief Applies changes to the shown level, returns `false` if it has to be loaded again instead */
		static bool Apply(const LevelDocument& document, Tiles::TileMap& tileMap, const DocumentChanges& changes);
		/** @brief Applies the editor state of all layers, such as which are hidden */
		static void ApplyLayerStates(const LevelDocument& document, Tiles::TileMap& tileMap);
		/** @brief Copies tiles of an area of a layer from the document */
		static void ApplyTiles(const LevelDocument& document, Tiles::TileMap& tileMap, std::int32_t layerIndex, const Recti& area);
		/** @brief Makes the tile the shown level has for a tile of the document */
		static Tiles::LayerTile MakeLayerTile(const LevelDocument& document, const TileRef& tile);
		/** @brief Returns `true` if the editor shows a layer */
		static bool IsLayerShown(const DocumentLayer& layer);
		/** @brief Makes a layer of the shown level as opaque as the editor shows it, see @ref DocumentLayer::EditorOpacity */
		static void ApplyEditorOpacity(const DocumentLayer& layer, Tiles::LayerDescription& description);
		/** @brief Returns `true` if a layer is drawn from a texture of its tiles, a sky, a tunnel or a reflection */
		static bool IsTexturedBackground(const DocumentLayer& layer);
		/**
		 * @brief Returns `true` if a tile of a layer shows one of the tiles of @p sortedTileIds, an animated one by any of its frames
		 *
		 * The tile IDs have to be sorted.
		 */
		static bool ShowsAnyTile(const LevelDocument& document, const TileRef& tile, ArrayView<const std::uint16_t> sortedTileIds);

	private:
		// Sets how a layer is placed and drawn and whether it's shown, the sprite layer takes only how it's drawn
		static void SetLayerDescription(Tiles::TileMap& tileMap, std::int32_t layerIndex, const Tiles::LayerDescription& description,
			bool shown, bool isSpriteLayer);
	};
}

#endif
