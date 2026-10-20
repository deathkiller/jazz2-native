#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "Commands.h"

#include <Containers/SmallVector.h>

#include <memory>

namespace Jazz2::Editor
{
#ifndef DOXYGEN_GENERATING_OUTPUT
	using Death::Containers::ArrayView;
	using Death::Containers::SmallVector;
	using Death::Containers::SmallVectorImpl;
#endif

	/** @brief What is laid along a line */
	enum class SplineKind : std::uint8_t
	{
		Ground,			/**< Ground below the line, down to the ground that's there */
		Ceiling,		/**< Ground above the line, its surface faces down */
		Band,			/**< A strip of ground as thick as the depth below the line, floating */
		Closed			/**< Ground inside of the line, which goes around back to its first point */
	};

	/**
		@brief Ground laid along a line, kept so it can be changed and laid again

		The line is kept in pixels of its layer, from its tile (0, 0), so it stays with the layer however the layer moves
		in the view. Each tile it wrote is remembered with the tile that was there before: when it's laid again, a tile
		that's still as it wrote it gets back what was there, and a tile changed by hand since is left as it is and isn't
		written anymore. They're kept in the editor-only chunk `spl2`, so the exported level has only the tiles.

		The chunk is @cpp u8 @ce version (1), @cpp varuint @ce count, then for each line a @cpp varuint @ce size of its
		record and the record: @cpp i32 @ce layer, @cpp u8 @ce depth, @cpp u8 @ce kind, @cpp u8 @ce flags (1 smooth),
		@cpp varuint @ce count of points and @cpp f32 @ce X, Y of each, @cpp varuint @ce count of tiles and
		@cpp i32 @ce X, Y, the tile written and the one before (@cpp u16 @ce index, @cpp u8 @ce source, @cpp u8 @ce
		flags) of each. A newer version may add fields at the end of a record, a reader skips those it doesn't know.
	*/
	struct TerrainSpline
	{
		/** @brief Tag of the chunk they're kept in */
		static constexpr std::uint32_t Chunk = Formats::LevelFile::MakeChunkTag('s', 'p', 'l', '2');
		/** @brief Tag of the chunk 3.9.0 kept them in, without a version, only read */
		static constexpr std::uint32_t LegacyChunk = Formats::LevelFile::MakeChunkTag('s', 'p', 'l', 'n');

		/** @brief Tile written by the ground, and what was there before */
		struct OwnedTile
		{
			/** @brief Position in the layer */
			std::int32_t X;
			/** @copydoc X */
			std::int32_t Y;
			/** @brief Tile written */
			TileRef Written;
			/** @brief Tile that was there before */
			TileRef Previous;
		};

		/** @brief Layer of the ground */
		std::int32_t Layer = 0;
		/** @brief Points of the surface in pixels of the layer, from left to right, in the order around the shape for a closed one */
		SmallVector<Vector2f, 0> Points;
		/** @brief Most tiles filled below the surface, above it for a ceiling, the thickness of a band */
		std::int32_t Depth = 6;
		/** @brief What is laid */
		SplineKind Kind = SplineKind::Ground;
		/** @brief Whether the line is a smooth curve through its points, not straight between them */
		bool Smooth = false;
		/** @brief Tiles written */
		SmallVector<OwnedTile, 0> Owned;

		/**
		 * @brief Reads all of them from a level, lines with points that make no sense are left out
		 *
		 * The legacy chunk wins over the current one if the level has both, it was written by 3.9.0 after this version
		 * saved the level, as this version never writes it.
		 */
		static void Read(const LevelDocument& document, SmallVectorImpl<TerrainSpline>& splines);
		/** @brief Writes all of them into bytes of the chunk */
		static void Write(const SmallVectorImpl<TerrainSpline>& splines, SmallVectorImpl<std::uint8_t>& data);
		/** @brief Returns the bytes of the chunk as the level has them now, in the current version, none without lines */
		static void GetChunkData(const LevelDocument& document, SmallVectorImpl<std::uint8_t>& data);
		/** @brief Replaces the chunk by the given bytes, the chunk of 3.9.0 is removed */
		static void SetChunkData(LevelDocument& document, ArrayView<const std::uint8_t> data);
		/** @brief Converts the chunk of 3.9.0 to the current one, if the level has it */
		static void UpgradeLegacyChunk(LevelDocument& document);

		/**
		 * @brief Follows tiles that moved to other tile references, as when the tile sets changed
		 *
		 * @p remap changes a reference in place, or returns `false` if its tile is gone, it's then the empty tile.
		 */
		static void RemapTiles(LevelDocument& document, Function<bool(TileRef&)>& remap);

		/**
		 * @brief Follows layers that were added, removed or moved
		 *
		 * @p newIndices has the new index of each layer as it was, -1 for a removed one, whose lines are gone.
		 */
		static void RemapLayers(LevelDocument& document, ArrayView<const std::int32_t> newIndices);
		/** @brief Follows a layer that was resized, its tiles moved by @p offset, tiles outside of it aren't its own anymore */
		static void ResizeLayer(LevelDocument& document, std::int32_t layer, Vector2i size, Vector2i offset);
	};

	/**
		@brief Changes the ground laid along lines and the tiles of its layer as one step
	*/
	class SetTerrainSplinesCommand : public EditCommand
	{
		DEATH_RUNTIME_OBJECT(EditCommand);

	public:
		/** @brief Creates a change to the given lines and tiles, the tiles may be `nullptr` */
		SetTerrainSplinesCommand(const LevelDocument& document, const SmallVectorImpl<TerrainSpline>& splines, std::unique_ptr<SetTilesCommand> tiles, String name);

		void Do(LevelDocument& document, DocumentChanges& changes) override;
		void Undo(LevelDocument& document, DocumentChanges& changes) override;
		String GetName() const override;
		std::size_t GetMemoryUsage() const override;

	private:
		friend class CommandArchive;

		String _name;
		SmallVector<std::uint8_t, 0> _oldData;
		SmallVector<std::uint8_t, 0> _newData;
		std::unique_ptr<SetTilesCommand> _tiles;

		explicit SetTerrainSplinesCommand(String name) : _name(Death::move(name)) {}
	};
}

#endif
