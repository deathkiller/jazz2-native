#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../Common/LayerView.h"
#include "../Document/LevelDocument.h"
#include "../../../nCine/Graphics/Texture.h"

#include <Containers/SmallVector.h>

#include <memory>
#include <unordered_map>

namespace Jazz2::Tiles
{
	class TileMap;
}

namespace Jazz2::Rendering
{
	class PolylineRenderer;
}

namespace Jazz2::Editor
{
#ifndef DOXYGEN_GENERATING_OUTPUT
	using Death::Containers::SmallVector;
	using nCine::Texture;
#endif

	class UiPainter;

	/**
		@brief Shows the collision masks of a layer over the level

		Solid parts are darkened and outlined, the outline goes around whole shapes, across the edges of tiles. The
		masks are put together into textures of square chunks of tiles, kept until the tiles of a chunk change, and
		drawn with fewer pixels when the view is far. The outline is traced from the pixels of a chunk once, as lines
		between the middles of the edges of its pixels, so a slope is a straight line instead of steps, and it's drawn
		antialiased at the same thickness however close the view is, but not when a tile is only a few pixels large.

		A layer that repeats has the same chunks wherever its pattern repeats. The chunks drawn the longest time ago are
		dropped once they take more memory than allowed, never those drawn in the frame, whose images are still to be
		drawn.
	*/
	class MaskOverlay
	{
	public:
		/** @brief Tiles on a side of a chunk */
		static constexpr std::int32_t ChunkTiles = 16;

		MaskOverlay();
		~MaskOverlay();

		/** @brief Drops all chunks */
		void Invalidate();
		/** @brief Drops all chunks and the memory used to make them, while the masks aren't shown */
		void Release();
		/** @brief Drops the chunks with tiles of an area of a layer, and those next to it */
		void InvalidateTiles(std::int32_t layer, const Recti& area);
		/** @brief Drops the chunks that show any of the tiles, after their masks changed, the tile IDs have to be sorted */
		void InvalidateTileIds(const LevelDocument& document, ArrayView<const std::uint16_t> sortedTileIds);
		/**
		 * @brief Draws the masks of a layer
		 *
		 * @param painter    Painter
		 * @param tileMap    Shown level
		 * @param document   The level
		 * @param layer      Index of the layer
		 * @param view       How the layer is shown
		 * @param viewRect   Part of the screen the level is seen in
		 * @param alone      Whether the masks are shown instead of the level, not over it
		 */
		void Draw(UiPainter& painter, Tiles::TileMap& tileMap, const LevelDocument& document, std::int32_t layer,
			const LayerView& view, const Rectf& viewRect, bool alone);

	private:
		struct Chunk
		{
			// First tile of the chunk, wrapped into the layer along a direction it repeats in
			Vector2i Start;
			std::int32_t Step = 1;
			std::unique_ptr<Texture> Image;
			// Outline in pixels of the chunk image, the polylines end before the points in OutlineEnds
			SmallVector<Vector2f, 0> Outline;
			SmallVector<std::int32_t, 0> OutlineEnds;
			// Memory the image and the outline take
			std::size_t Bytes = 0;
			std::uint32_t LastUsed = 0;
			bool Empty = true;
		};

		// By the start and the step, see MakeKey()
		std::unordered_map<std::uint64_t, Chunk> _chunks;
		std::size_t _bytes;
		SmallVector<std::uint8_t, 0> _solid;
		SmallVector<std::uint8_t, 0> _pixels;
		// Tracing the outline, the two lines that meet at the middle of an edge between pixels
		SmallVector<std::int32_t, 0> _edgeLinks;
		SmallVector<std::int32_t, 0> _segments;
		SmallVector<Vector2f, 0> _traced;
		// Outlines of the visible chunks on the screen, drawn at once, kept while the view and the chunks in it stay the
		// same, with how the chunks were shown then
		SmallVector<Vector2f, 0> _screenOutline;
		SmallVector<std::int32_t, 0> _screenOutlineEnds;
		bool _outlineValid;
		std::int32_t _outlineStep;
		float _outlineScale;
		Vector2f _outlineOrigin;
		Rectf _outlineViewRect;
		std::unique_ptr<Rendering::PolylineRenderer> _outlineRenderer;
		std::int32_t _layer;
		bool _repeatX;
		bool _repeatY;
		std::uint32_t _frame;

		static std::uint64_t MakeKey(Vector2i start, std::int32_t step);
		void BuildChunk(Chunk& chunk, Tiles::TileMap& tileMap, const LevelDocument& document, const LayerView& view);
		void TraceOutline(Chunk& chunk, std::int32_t size, std::int32_t stride, std::int32_t apron);
		void DropOldChunks();
	};
}

#endif
