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
		written anymore. They're kept in the editor-only chunk `spln`, so the exported level has only the tiles.
	*/
	struct TerrainSpline
	{
		/** @brief Tag of the chunk they're kept in */
		static constexpr std::uint32_t Chunk = Formats::LevelFile::MakeChunkTag('s', 'p', 'l', 'n');

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

		/** @brief Reads all of them from a level */
		static void Read(const LevelDocument& document, SmallVectorImpl<TerrainSpline>& splines);
		/** @brief Writes all of them into bytes of the chunk */
		static void Write(const SmallVectorImpl<TerrainSpline>& splines, SmallVectorImpl<std::uint8_t>& data);

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
		String _name;
		SmallVector<std::uint8_t, 0> _oldData;
		SmallVector<std::uint8_t, 0> _newData;
		std::unique_ptr<SetTilesCommand> _tiles;
	};
}

#endif
