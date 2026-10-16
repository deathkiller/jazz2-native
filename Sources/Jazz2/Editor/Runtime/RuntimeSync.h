#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../Document/DocumentChanges.h"
#include "../Document/LevelDocument.h"
#include "../../Tiles/TileMap.h"

namespace Jazz2::Editor
{
	/**
		@brief Brings the level shown by the editor up to date with the level being edited

		Most changes are applied to the shown level in place. The rest --- other tile sets, added layers, changed
		animations --- need the level to be loaded again from the document, which the editor does when told so.
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
		/** @brief Returns `true` if a layer is drawn as a textured background, a sky or a circle */
		static bool IsTexturedBackground(const DocumentLayer& layer);
	};
}

#endif
