#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../Document/LevelDocument.h"
#include "../../Tiles/TileMap.h"
#include "../../../nCine/Base/TimeStamp.h"
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
	class TileSetDataCache;

#ifndef DOXYGEN_GENERATING_OUTPUT
	namespace Implementation
	{
		// See TileLod::GetLargerTileSize(), outside of the class, so the class can use it for the sizes of its members
		constexpr std::int32_t GetLargerLodTileSize(std::int32_t tilePixels)
		{
			return (tilePixels <= 1 ? 2 : ((tilePixels & (tilePixels - 1)) == 0 ? tilePixels * 3 / 2 : tilePixels * 4 / 3));
		}

		constexpr std::int32_t GetSmallerLodTileSize(std::int32_t tilePixels)
		{
			return (tilePixels <= 2 ? 1 : ((tilePixels & (tilePixels - 1)) == 0 ? tilePixels * 3 / 4 : tilePixels * 2 / 3));
		}

		// Where the colors of a tile of a size are in a block of all sizes, after those of all smaller sizes
		constexpr std::int32_t GetLodLevelOffset(std::int32_t tilePixels)
		{
			std::int32_t offset = 0;
			for (std::int32_t size = 1; size < tilePixels; size = GetLargerLodTileSize(size)) {
				offset += size * size;
			}
			return offset;
		}
	}
#endif

	/**
		@brief Draws the tile layers from far away as images instead of tiles

		From far away, there are far too many tiles in view to draw each one. Square chunks of tiles of a layer are drawn
		into images instead, with at least as many pixels for a tile as a tile takes on the screen, so they're never
		stretched --- 1, 2, 3, 4, 6, 8, 12 or 16, each size at most half again as large as the one before, so they're
		never much larger either (see @ref GetLargerTileSize()) --- each the average of its part of the tile, and the
		images are drawn in place of the tiles (see @ref Tiles::ITileMapLayerOverride). An image has at most
		@ref MaxChunkPixels on a side, a chunk has fewer tiles the more pixels a tile has, always a power of two of them,
		so those in view take little more memory than the view itself, but there aren't too many of them from far away.

		The averages are kept for every tile in all sizes, so an image is put together by copying them. An image is made
		again when its tiles change or the view gets closer or farther, as many as fit in a few milliseconds of a frame,
		those missing first and at a lower resolution, then those of another resolution. A chunk not made yet is shown by
		the larger one around it, or by the smaller ones in it, made before, or by the image of the layer made ahead, and
		only where none of them is there yet are the tiles of the chunk drawn, so nothing is missing for a moment. The
		least recently drawn images are dropped once they take more memory than allowed, never those drawn in the frame,
		and the resolution is lowered when the images in view wouldn't fit; all but those made ahead are dropped a while
		after the view got closer again. A layer that repeats shows the same images again where its pattern repeats: its
		chunks start where its pattern does, and the last one of the pattern is shown only up to where it ends, or a chunk
		has the pattern a few times when it's shorter.
	*/
	class TileLod : public Tiles::ITileMapLayerOverride
	{
	public:
		/** @brief Most pixels of a chunk image on a side */
		static constexpr std::int32_t MaxChunkPixels = 512;
		/** @brief Fewest tiles on a side of a chunk, with the most pixels for a tile */
		static constexpr std::int32_t MinChunkTiles = 32;
		/** @brief Most tiles on a side of a chunk, with few pixels for a tile */
		static constexpr std::int32_t MaxChunkTiles = 128;
#if defined(DEATH_TARGET_CONSTRAINED_MEMORY)
		/** @brief Most pixels of a tile on a side in a chunk image, fewer when a tile is smaller on the screen */
		static constexpr std::int32_t TilePixels = 2;
		/** @brief Zoom below which the images are drawn, relative to how large the game shows the level, see @ref UpdateActive() */
		static constexpr float MaxZoom = 1.0f;
		/** @brief Most pixels a tile takes on the screen while the images are drawn, see @ref UpdateActive() */
		static constexpr float MaxScreenTilePixels = 6.4f;
#else
		static constexpr std::int32_t TilePixels = 16;
		static constexpr float MaxZoom = 0.15f;
		// Twice as large as the images have pixels for a tile at most, so the zoom alone decides it up to a 4K screen as
		// it does at 1080p (25.6 pixels a tile there), they're a little blurred then, but no larger
		static constexpr float MaxScreenTilePixels = float(TilePixels * 2);
#endif
		/** @brief How much closer the view has to get again before the tiles are drawn again, so it doesn't flicker there */
		static constexpr float Hysteresis = 1.1f;
		/** @brief Time a frame the level is edited in takes to prepare the colors of tiles, see @ref WarmUp() */
#if defined(DEATH_TARGET_CONSTRAINED_MEMORY)
		static constexpr float WarmUpMilliseconds = 1.0f;
		/** @brief Whether the images of the layers with a pixel for a tile are made ahead and kept, see @ref WarmUp() */
		static constexpr bool UseBaseChunks = false;
#else
		static constexpr float WarmUpMilliseconds = 2.0f;
		static constexpr bool UseBaseChunks = true;
#endif
		/**
		 * @brief Most pixels of a tile in the images made now for the images of the layers made ahead to stand in
		 *
		 * They show places of chunks not made yet. They're blurred, but drawing the tiles instead takes much longer from
		 * far away. With more pixels for a tile, they show only the chunks that can't be made in the frame anymore.
		 */
		static constexpr std::int32_t MaxBaseCoverTilePixels = 8;

		/** @brief Creates a new instance for a level, the pixels of its tiles are read from @p tileSets */
		TileLod(const LevelDocument& document, TileSetDataCache& tileSets);
		~TileLod() override;

		/**
		 * @brief Returns whether the images are drawn, at a zoom and with a tile so many pixels on the screen
		 *
		 * The cost of drawing the tiles grows with the number of them in view, which depends on the zoom relative to how
		 * large the game shows the level, so the images take over below @ref MaxZoom. Not while a tile is larger on the
		 * screen than @ref MaxScreenTilePixels though, the images would be blurred. Where memory is short, that's all that
		 * decides it. Once they're drawn, they're drawn until the view gets closer than that by @ref Hysteresis.
		 */
		bool UpdateActive(float zoom, float screenTilePixels);
		/**
		 * @brief Drops all images
		 *
		 * With @p keepBlocks, the colors of the tiles are kept, which depend only on the pixels of the tiles and the
		 * palette, so a level loaded again with the same tiles doesn't have them computed again.
		 */
		void Invalidate(bool keepBlocks = false);
		/** @brief Makes the images that show changed tiles again */
		void OnDocumentChanged(const DocumentChanges& changes);
		/** @brief Makes the images that show any of the tiles again, after they changed, the tile IDs have to be sorted */
		void InvalidateTileIds(const LevelDocument& document, ArrayView<const std::uint16_t> sortedTileIds);
		/** @brief Called once a frame before the level is drawn, with the scale of the view */
		void BeginFrame(float scale);
		/**
		 * @brief Prepares the colors of the tiles the level uses for some time, called in frames the level is edited in
		 *
		 * Computing the colors of a tile is what takes the time when a part of the level is first seen from far away, so
		 * they're computed ahead, a few at a time, after the level is opened and whenever the colors are dropped. All tiles
		 * of all layers are counted first, as they're flipped, then the colors of those used the most are computed first.
		 * Then images of whole layers with a pixel for a tile are made and kept, see @ref UseBaseChunks, they show places
		 * not made yet from far away, as the view gets farther fast, so the layer isn't drawn as tiles then. Those that
		 * change are made again later, once the level isn't changed for a moment. Only while the view is far enough for
		 * the images to be drawn soon, or nothing changed for a while, and not for layers hidden in the editor.
		 */
		void WarmUp(float milliseconds);
		/** @brief Returns the memory the images take in bytes */
		std::size_t GetMemoryUsage() const {
			return _bytes;
		}
		/** @brief Returns the pixels of a tile on a side in the images made now */
		std::int32_t GetTilePixels() const {
			return _tilePixels;
		}
		/** @brief Returns the pixels of a tile on a side the scale of the view asks for, more than @ref GetTilePixels() if memory is short */
		std::int32_t GetWantedTilePixels() const {
			return _wantedTilePixels;
		}
		/** @brief Returns the number of images drawn in the last frame, each once, and the memory they take in bytes */
		std::int32_t GetDrawnChunks(std::size_t* bytes = nullptr) const {
			if (bytes != nullptr) {
				*bytes = _lastDrawnBytes;
			}
			return _lastDrawnChunks;
		}
		/**
		 * @brief Returns the next size of a tile in the images, 1, 2, 3, 4, 6, 8, 12, 16 and so on
		 *
		 * A power of two and half again as much as it, so the sizes between them are made from those twice as large too.
		 */
		static constexpr std::int32_t GetLargerTileSize(std::int32_t tilePixels) {
			return Implementation::GetLargerLodTileSize(tilePixels);
		}
		/** @brief Returns the size of a tile in the images before @p tilePixels, see @ref GetLargerTileSize() */
		static constexpr std::int32_t GetSmallerTileSize(std::int32_t tilePixels) {
			return Implementation::GetSmallerLodTileSize(tilePixels);
		}
		/** @brief Returns the tiles on a side of a chunk with so many pixels for a tile, a power of two */
		static constexpr std::int32_t GetChunkTiles(std::int32_t tilePixels) {
			// By the power of two of the size or the next one, chunks of all sizes fit into each other
			std::int32_t pixels = 1;
			while (pixels < tilePixels) {
				pixels *= 2;
			}
			return (MaxChunkPixels / pixels < MinChunkTiles ? MinChunkTiles : (MaxChunkPixels / pixels > MaxChunkTiles ? MaxChunkTiles : MaxChunkPixels / pixels));
		}

		bool OverrideLayer(std::int32_t layerIndex, Vector2f origin, const Rectf& cullingRect, SmallVectorImpl<Tiles::TileMapLayerImage>& images,
			SmallVectorImpl<Recti>& tileAreas) override;

	private:
		struct Chunk
		{
			std::int32_t Layer;
			// Tiles on a side, and the first tile in the layer
			std::int32_t Size;
			Vector2i Start;
			std::unique_ptr<Texture> Image;
			// Keys of the blocks it shows, sorted, so the chunks that show changed tiles are found quickly, see InvalidateTileIds()
			SmallVector<std::uint32_t, 0> Keys;
			// Pixels of a tile on a side in the image, 0 until it's made, and the memory the image takes
			std::int32_t TilePixels = 0;
			std::size_t Bytes = 0;
			std::uint32_t LastUsed;
			// Frame it was counted as drawn in, a layer that repeats shows it many times, it takes the memory once
			std::uint32_t CountedFrame = 0;
			// Directions the layer repeated in when it was made, see GetRepeats()
			std::uint8_t Repeats = 0;
			// The palette the layer was drawn through when it was made, see GetPaletteVariant()
			std::uint32_t PaletteVariant = 0;
			bool Dirty;
			// Whether it's one of the images of a whole layer with a pixel for a tile, made ahead and kept, see WarmUp()
			bool Base = false;
		};

		// What shows the place of a chunk not made yet, see FindCover()
		enum class CoverKind : std::uint8_t
		{
			None,
			Larger,		// A larger chunk around it
			Smaller,	// All the smaller chunks in it
			Base,		// The image of the layer made ahead around it
			Tiles		// Nothing yet, its tiles are drawn
		};

		struct Cover
		{
			CoverKind Kind = CoverKind::None;
			std::int32_t Size = 0;
			std::int32_t X = 0, Y = 0;
		};

		// A chunk in view and what shows it, see OverrideLayer()
		struct ViewCell
		{
			std::int32_t X, Y;
			Cover By;
		};

		// How a layer is drawn, see OverrideLayer()
		struct DrawnLayer
		{
			std::int32_t Index;
			std::int32_t Width, Height;
			bool RepeatX, RepeatY;
			Vector2f Origin;
		};

		// Number of colors of all sizes of a tile, 1 × 1, 2 × 2, 3 × 3 and so on up to TilePixels × TilePixels
		static constexpr std::int32_t BlockColors = Implementation::GetLodLevelOffset(TilePixels) + TilePixels * TilePixels;

		// Average colors of the parts of a tile in all sizes, as it's flipped, the smallest first, see GetLargerTileSize()
		struct TileBlock
		{
			std::uint32_t Colors[BlockColors];
			// The last chunk made that it was counted in, see Chunk::Keys
			std::uint32_t BuildStamp = 0;
			// Whether any part of it isn't transparent
			bool Visible;
		};

		// An image of a chunk dropped, kept for another chunk of the same size, with the frame it was last drawn in
		struct PooledTexture
		{
			std::unique_ptr<Texture> Image;
			std::uint32_t LastDrawn;
		};

		// The blocks found last, by a part of their key, a chunk has thousands of tiles and the map is slow to look into
		struct BlockCacheEntry
		{
			std::uint32_t Key;
			TileBlock* Block;
		};
		static constexpr std::int32_t BlockCacheSize = 1024;

		const LevelDocument& _document;
		TileSetDataCache& _tileSets;
		SmallVector<Chunk, 0> _chunks;
		// Index of a chunk by its layer, size and start, see GetChunkKey()
		std::unordered_map<std::uint64_t, std::int32_t> _chunkIndex;
		std::unordered_map<std::uint32_t, TileBlock> _blocks;
		BlockCacheEntry _blockCache[BlockCacheSize];
		// Blocks of the layers drawn through a palette of their own, by the variant above the key of the tile
		std::unordered_map<std::uint64_t, TileBlock> _paletteBlocks;
		std::uint64_t _lastPaletteBlockKey = UINT64_MAX;
		TileBlock* _lastPaletteBlock = nullptr;
		SmallVector<std::uint32_t, 0> _pixels;
		SmallVector<PooledTexture, 0> _texturePool;
		std::size_t _pooledBytes;
		std::size_t _bytes;
		std::uint32_t _frame;
		// Counts the chunks made, see TileBlock::BuildStamp
		std::uint32_t _buildStamp;
		// The last frame any layer was drawn as images in, the images are dropped a while after
		std::uint32_t _lastOverrideFrame;
		bool _evicted;
		// The zoom of the view, and when the level or the view last changed, see WarmUp()
		float _zoom;
		float _lastScale;
		nCine::TimeStamp _lastChange;
		std::int32_t _buildsLeft;
		float _buildMilliseconds;
		// Pixels of a tile on a side in the images made in this frame, and as many as the scale of the view asks for
		std::int32_t _tilePixels;
		std::int32_t _wantedTilePixels;
		// Images drawn, each once, and images missing in this frame and the last one, which decide the resolution and what's
		// made first, with the tiles in view and the tiles on a side of the chunks drawn in the last frame
		std::int32_t _drawnChunks;
		std::int32_t _lastDrawnChunks;
		std::size_t _drawnBytes;
		std::size_t _lastDrawnBytes;
		std::int32_t _missingChunks;
		std::int32_t _lastMissingChunks;
		Vector2f _viewTiles;
		std::int32_t _drawnChunkTiles;
		bool _active;
		// The chunks in view of the layer being drawn, and where the chunks that show others are, see OverrideLayer()
		SmallVector<ViewCell, 0> _cells;
		SmallVector<Rectf, 0> _coverRects;
		// What WarmUp() does next: counts the tiles of the layers row by row, computes the colors in that order, then makes
		// the images of the layers with a pixel for a tile, or makes those that changed again
		enum class WarmUpState : std::uint8_t { Done, Counting, Computing, BaseChunks };
		WarmUpState _warmUpState;
		std::int32_t _warmUpLayer;
		std::int32_t _warmUpRow;
		std::int32_t _warmUpColumn;
		std::unordered_map<std::uint32_t, std::uint32_t> _warmUpCounts;
		SmallVector<std::uint32_t, 0> _warmUpKeys;
		std::size_t _warmUpNext;
		bool _baseChunksDirty;

		static std::uint64_t GetChunkKey(std::int32_t layer, std::int32_t size, Vector2i start, bool base);
		Chunk* FindChunk(std::int32_t layer, std::int32_t size, Vector2i start, bool base = false);
		Chunk& FindOrAddChunk(std::int32_t layer, std::int32_t size, Vector2i start, bool base = false);
		// Returns the first tile of the chunk of a size at a place in a layer, the same for every place a layer that repeats shows it
		static Vector2i GetChunkStart(const DrawnLayer& layer, std::int32_t size, std::int32_t cx, std::int32_t cy);
		// Returns where a chunk of a size at a place in a layer is drawn, and how much of its image is shown there
		static Rectf GetChunkRect(const DrawnLayer& layer, std::int32_t size, std::int32_t cx, std::int32_t cy, Vector2f* texScale = nullptr);
		// Returns the tiles of a chunk of a size at a place in a layer, from the tile (0, 0) of the layer without wrapping
		static Recti GetChunkTileArea(const DrawnLayer& layer, std::int32_t size, std::int32_t cx, std::int32_t cy);
		// Adds the image of a chunk made already, unless it's added already, returns false if it isn't made
		bool AddChunkImage(const DrawnLayer& layer, std::int32_t size, std::int32_t cx, std::int32_t cy, SmallVectorImpl<Tiles::TileMapLayerImage>& images,
			std::size_t firstImage, bool base = false);
		// Returns the chunks of other sizes made already that show the place of a chunk not made yet
		Cover FindCover(const DrawnLayer& layer, std::int32_t size, std::int32_t cx, std::int32_t cy);
		// Returns the image of the layer made ahead around a chunk, if it's made
		Cover FindBaseCover(const DrawnLayer& layer, std::int32_t size, std::int32_t cx, std::int32_t cy);
		// Returns an image of a size for a chunk, one of a chunk dropped before if there's one, and keeps one dropped now
		std::unique_ptr<Texture> AcquireTexture(std::int32_t size);
		void ReleaseTexture(std::unique_ptr<Texture> image, std::uint32_t lastDrawn);
		// Drops the chunks, their images are kept for others while they don't take too much memory
		void ClearChunks();
		// Forgets the colors of the animated tiles, after the animations changed
		void DropAnimatedBlocks();
		// Returns whether the colors of tiles or the images of the layers made ahead are made now, see WarmUp()
		bool IsWarmUpAllowed() const;
		// Returns whether a layer is drawn as images, layers of a single color or of effects aren't
		static bool IsDrawnAsImages(const DocumentLayer& layer);
		// Returns the directions a layer repeats in, 0x01 horizontally, 0x02 vertically, the sprite layer never repeats
		static std::uint8_t GetRepeats(const DocumentLayer& layer);
		// Makes the images of the layers with a pixel for a tile that are missing or changed, for some time, returns false
		// if it's not done yet
		bool BuildBaseChunks(const nCine::TimeStamp& start, float milliseconds);
		void BuildChunk(Chunk& chunk, std::int32_t tilePixels);
		TileBlock& GetBlock(const TileRef& tile);
		// The block of a tile through the palette of a layer, see GetPaletteVariant()
		TileBlock& GetPaletteBlock(const TileRef& tile, std::uint32_t variant);
		// Returns 0 for a layer drawn through the sprite palette, the renderer and its parameter otherwise
		static std::uint32_t GetPaletteVariant(const DocumentLayer& layer);
		// Fills a block of the colors of a tile, false if the tile has no pixels
		bool FillBlock(TileBlock& block, const TileRef& tile, const std::uint32_t* palette);
		// Returns the key a block is kept by, flips change the block, the rest of the flags don't
		static std::uint32_t GetBlockKey(const TileRef& tile);
		// Forgets the blocks found last, before any is removed from the map
		void ClearBlockCache();
		// Starts WarmUp() over, after blocks were dropped
		void RestartWarmUp();
		void DropOldChunks();
	};
}

#endif
