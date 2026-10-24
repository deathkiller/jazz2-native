#pragma once

#include "LevelFile.h"
#include "TileSetFile.h"
#include "../Tiles/LayerTypes.h"

#include <Containers/Function.h>

namespace Jazz2::Formats
{
	/**
		@brief Preview of a level shown in the list of custom levels, see @ref LevelFile::PreviewChunk

		What the game shows around the start of the level: all layers composited from the farthest one on, ordered by
		their depth as the game draws them, at the parallax of the game (see @ref Tiles::LayerDescription::GetGameplayOrigin()),
		a view of 384 × 216 pixels made @ref Scale times smaller, each pixel the average of a few points of the level.
		A layer of solid color is just its color, the layers of effects (x-ray and blur behind) aren't drawn, a
		reflection mirrors what is behind it below its horizon, without the ripples, over the view or where the layer
		is, and a palette-shifted layer takes the colors further in the palette, as the game draws it.

		The level comes through an @ref ISource, so the level editor, which keeps a level in a form of its own, makes
		it the same way as the converter of the original levels, which hands it a @ref RecordSource.
	*/
	struct LevelPreview
	{
		/** @brief Width of the preview in pixels */
		static constexpr std::int32_t Width = 128;
		/** @brief Height of the preview in pixels */
		static constexpr std::int32_t Height = 72;
		/** @brief Pixels of the level in a pixel of the preview, each way */
		static constexpr std::int32_t Scale = 3;

		/** @brief Pixels of a tile as stored in its tile set */
		struct TilePixels
		{
			/** @brief Pixels in the tile set */
			const std::uint8_t* Pixels = nullptr;
			/** @brief Palette indices of the level for those of the tile set, if it's remapped */
			const std::uint8_t* Remapping = nullptr;
			/** @brief Whether the pixels are RGBA instead of palette indices */
			bool TrueColor = false;
			/** @brief Flags of the tile, see @ref TileRecord */
			std::uint8_t Flags = 0;

			/** @brief Returns the pixel as `R | G << 8 | B << 16 | A << 24`, flipped as the tile is */
			std::uint32_t GetPixel(std::int32_t x, std::int32_t y, const std::uint32_t* palette) const;
		};

		/** @brief Layers of a level and the pixels of their tiles, as the preview takes them */
		class ISource
		{
		public:
			virtual ~ISource() = default;

			/** @brief Returns the number of layers */
			virtual std::int32_t GetLayerCount() = 0;
			/** @brief Returns the description of a layer as the level stores it, its kind, flags, size, renderer and color */
			virtual const LayerRecord& GetLayer(std::int32_t index) = 0;
			/**
			 * @brief Returns how the game places a layer
			 *
			 * As the level stores it by default, see @ref Tiles::LayerDescription::FromRecord().
			 */
			virtual Tiles::LayerDescription GetLayerDescription(std::int32_t index);
			/** @brief Finds the pixels of a tile of a layer, the first frame of an animated one, returns `false` for an empty or unknown one */
			virtual bool GetTilePixels(std::int32_t layer, std::int32_t x, std::int32_t y, TilePixels& result) = 0;
		};

		/**
		 * @brief A level in the form its file stores it, with its tile sets in memory
		 *
		 * The tiles are found the way the game finds them --- the tile IDs count the tiles of the tile sets in their
		 * order, an animated tile shows its first frame, and an overridden diffuse replaces the pixels of a tile. The
		 * tiles of a tile set that isn't there show nothing.
		 */
		class RecordSource : public ISource
		{
		public:
			/** @brief Layer with its tiles */
			struct Layer
			{
				/** @brief Description of the layer */
				LayerRecord Record;
				/** @brief Tiles row by row */
				ArrayView<const TileRecord> Tiles;
			};

			/** @brief Tile set the tiles of the level are taken from, see @ref ExtraTileSetRecord */
			struct TileSet
			{
				/** @brief Tile set, `nullptr` if it isn't there */
				const TileSetData* Data;
				/** @brief First tile taken from the tile set */
				std::int32_t Offset;
				/** @brief Number of tiles taken from the tile set */
				std::int32_t Count;
				/** @brief Palette indices of the level for those of the 8-bit tiles, `nullptr` if they're shown as they are */
				const std::uint8_t* Remapping;
				/**
				 * @brief Tiles of the tile set that @ref Data has, sorted, in the order it has them
				 *
				 * Empty if it has all of them, each at its own index. A tile set loaded only in part needs no more than
				 * the tiles @ref CollectTiles() reports.
				 */
				ArrayView<const std::int32_t> Tiles;
			};

			/** @brief Layers in the order of the file */
			ArrayView<const Layer> Layers;
			/** @brief Tile sets in the order their tiles are numbered, the default one first */
			ArrayView<const TileSet> TileSets;
			/** @brief Tile ID of the first animated tile */
			std::int32_t AnimatedTilesOffset = 0;
			/** @brief Animated tiles */
			ArrayView<const AnimatedTileRecord> AnimatedTiles;
			/** @brief Overridden tile diffuse */
			ArrayView<const TileDiffuseOverride> DiffuseOverrides;

			std::int32_t GetLayerCount() override;
			const LayerRecord& GetLayer(std::int32_t index) override;
			bool GetTilePixels(std::int32_t layer, std::int32_t x, std::int32_t y, TilePixels& result) override;

			/**
			 * @brief Reports the tiles of the tile sets the preview around @p start takes
			 *
			 * Calls @p visit with the index of a tile set in @ref TileSets and the index of a tile in it, for every tile
			 * @ref Make() reads pixels of, possibly more than once, so the tile sets can be loaded only in part before.
			 * Doesn't need @ref TileSet::Data.
			 */
			void CollectTiles(nCine::Vector2f start, Function<void(std::int32_t, std::int32_t)>&& visit);

		private:
			// Whether DiffuseOverrides are sorted by their tile, so they can be searched, -1 until it's known
			std::int8_t _overridesSorted = -1;

			// The tile a tile of a layer shows, the first frame of an animated one, returns `false` for an empty one
			bool ResolveTile(std::int32_t layer, std::int32_t x, std::int32_t y, TileRecord& tile) const;
			// The overridden diffuse of a tile, `nullptr` if it isn't overridden
			const std::uint8_t* FindDiffuseOverride(std::uint16_t tileId);
			// The tile set of a tile ID and the index of the tile in it, returns `false` for one past all the tile sets
			bool FindTileSet(std::int32_t tileId, std::int32_t& tileSet, std::int32_t& index) const;
		};

		/**
		 * @brief Picks the start of a level the preview is made around
		 *
		 * The start of Jazz first, then any start, then a start of multiplayer, the first one in the grid of each.
		 */
		class StartPicker
		{
		public:
			/** @brief Takes an event of the grid into account, @p x and @p y are its tile */
			void Add(const EventRecord& event, std::int32_t x, std::int32_t y);
			/** @brief Returns where the level starts in pixels, the middle of a sprite layer of a size in tiles if no event was a start */
			nCine::Vector2f GetPosition(std::int32_t width, std::int32_t height) const;

		private:
			nCine::Vector2f _position;
			std::int32_t _priority = INT32_MAX;
		};

		LevelPreview() = delete;

		/**
		 * @brief Makes the preview, the payload of @ref LevelFile::PreviewChunk
		 *
		 * @p start is where the level starts in pixels (see @ref StartPicker), @p palette the 256 colors the 8-bit tiles
		 * are shown in. Leaves @p data empty if the level has no sprite layer.
		 */
		static void Make(ISource& source, nCine::Vector2f start, const std::uint32_t* palette, SmallVectorImpl<std::uint8_t>& data);

		/**
		 * @brief Reads the preview of a level file as RGBA pixels, row by row
		 *
		 * Returns `false` if the level has none, or one too large to be one of them.
		 */
		static bool Read(StringView path, SmallVectorImpl<std::uint8_t>& rgba, nCine::Vector2i& size);
	};
}
