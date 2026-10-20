#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../Document/LevelDocument.h"

#include <Containers/SmallVector.h>
#include <Containers/String.h>
#include <Containers/StringView.h>

#include <memory>

namespace Jazz2::Formats
{
	struct TileSetData;
}

namespace Jazz2::Editor
{
#ifndef DOXYGEN_GENERATING_OUTPUT
	using Death::Containers::SmallVector;
	using Death::Containers::String;
	using Death::Containers::StringView;
#endif

	/**
		@brief Tile sets read whole, for the pixels and the masks of their tiles

		A tile set file is read once, also when it can't be read, so it isn't tried again and again, until it's
		invalidated after the file was written. A tile set the level stores in itself, whose name starts with `:`, is
		read from its bytes in the document, and read again when they changed. A change of the same size is copied into
		the same bytes, so after @ref MarkLevelTilesChanged() a hash of the bytes tells whether they really changed.
	*/
	class TileSetDataCache
	{
	public:
		/** @brief Seed of @ref HashBytes() */
		static constexpr std::uint64_t HashSeed = 14695981039346656037ull;
		/** @brief Most tile sets read from their files that the level doesn't use kept at once, the least recently used are forgotten */
		static constexpr std::size_t MaxUnusedTileSets = 4;

		TileSetDataCache();
		~TileSetDataCache();

		TileSetDataCache(const TileSetDataCache&) = delete;
		TileSetDataCache& operator=(const TileSetDataCache&) = delete;

		/**
		 * @brief Returns a whole tile set, or `nullptr` if it can't be read
		 *
		 * A name starting with `:` is a tile set @p document stores in itself. The pointer is valid until the tile set
		 * is invalidated, or for a tile set of the level, until its bytes changed. Only a few tile sets that @p document
		 * doesn't use are kept, so the pointer to such one is valid only until @ref MaxUnusedTileSets others are read.
		 */
		const Formats::TileSetData* Get(const LevelDocument& document, StringView name);
		/** @brief Forgets what was read of a tile set, after its file was written */
		void Invalidate(StringView name);
		/** @brief Forgets the tile sets a level stores in itself, before another level is edited */
		void InvalidateLevelTileSets();
		/** @brief Tells that the bytes of the tile sets the level stores in itself may have changed */
		void MarkLevelTilesChanged() {
			_levelTilesGeneration++;
		}
		/** @brief Returns a number that changes with each @ref MarkLevelTilesChanged() */
		std::uint32_t GetLevelTilesGeneration() const {
			return _levelTilesGeneration;
		}

		/** @brief Hashes bytes by FNV-1a, enough to tell whether they changed */
		static std::uint64_t HashBytes(const void* data, std::size_t size, std::uint64_t hash = HashSeed);

	private:
		struct CachedTileSet
		{
			String Name;
			std::unique_ptr<Formats::TileSetData> Data;
			// Bytes it was read from, for a tile set the level stores in itself, it's read again when they change. The
			// bytes can change in place, so their hash is compared once they might have, see _levelTilesGeneration.
			const std::uint8_t* Bytes = nullptr;
			std::size_t ByteCount = 0;
			std::uint64_t Hash = 0;
			std::uint32_t Generation = 0;
			// When it was asked for the last time, of a tile set read from its file, see _useCounter
			std::uint64_t LastUsed = 0;
		};

		SmallVector<CachedTileSet, 0> _entries;
		// Of the tile sets the level stores in itself, counts their changes
		std::uint32_t _levelTilesGeneration;
		// Counts the tile sets asked for, so the least recently used can be forgotten
		std::uint64_t _useCounter;

		// Forgets the tile sets read from their files that the document doesn't use but the most recently used ones
		void TrimUnused(const LevelDocument& document);
	};
}

#endif
