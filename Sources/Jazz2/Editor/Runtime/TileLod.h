#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../Document/LevelDocument.h"
#include "../../Tiles/TileMap.h"
#include "../../../nCine/Graphics/Texture.h"

#include <Containers/SmallVector.h>

#include <memory>
#include <unordered_map>

namespace Jazz2::Editor
{
#ifndef DOXYGEN_GENERATING_OUTPUT
	using Death::Containers::SmallVector;
	using nCine::Texture;
#endif

	class DocumentChanges;
	class EditorSession;

	/**
		@brief Draws the tile layers from far away as images instead of tiles

		From far away, a tile is only a few pixels on the screen, and there are far too many of them to draw each one.
		Square chunks of tiles of a layer are drawn into images instead, 4 pixels for a tile, each the average of its
		part of the tile, and the images are drawn in place of the tiles (see @ref Tiles::ITileMapLayerOverride). An
		image is made again when its tiles change, at most a few of them a frame, and the least recently drawn ones are
		dropped. A layer that repeats shows the same images again where its pattern repeats.
	*/
	class TileLod : public Tiles::ITileMapLayerOverride
	{
	public:
		/** @brief Tiles on a side of a chunk */
		static constexpr std::int32_t ChunkTiles = 64;
		/** @brief Pixels of a tile on a side in a chunk image */
		static constexpr std::int32_t TilePixels = 4;
		/** @brief Scale of the view below which the images are drawn */
		static constexpr float MaxScale = 0.2f;

		explicit TileLod(EditorSession& session);
		~TileLod() override;

		/** @brief Drops all images */
		void Invalidate();
		/** @brief Makes the images that show changed tiles again */
		void OnDocumentChanged(const DocumentChanges& changes);
		/** @brief Called once a frame before the level is drawn */
		void BeginFrame();

		bool OverrideLayer(std::int32_t layerIndex, Vector2f origin, const Rectf& cullingRect, SmallVectorImpl<Tiles::TileMapLayerImage>& images) override;

	private:
		struct Chunk
		{
			std::int32_t Layer;
			Vector2i Start;
			std::unique_ptr<Texture> Image;
			std::uint32_t LastUsed;
			bool Dirty;
		};

		// Average colors of the 4 × 4 parts of a tile, as it's flipped
		struct TileBlock
		{
			std::uint32_t Colors[TilePixels * TilePixels];
		};

		EditorSession& _session;
		SmallVector<Chunk, 0> _chunks;
		std::unordered_map<std::uint32_t, TileBlock> _blocks;
		SmallVector<std::uint32_t, 0> _pixels;
		std::uint32_t _frame;
		std::int32_t _buildsLeft;

		Chunk* FindChunk(std::int32_t layer, Vector2i start);
		void BuildChunk(Chunk& chunk);
		const TileBlock& GetBlock(const TileRef& tile);
		void DropOldChunks();
	};
}

#endif
