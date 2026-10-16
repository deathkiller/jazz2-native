#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "TerrainRules.h"
#include "../Document/LevelDocument.h"

#include <Containers/SmallVector.h>
#include <Containers/String.h>

#include <unordered_map>

namespace Jazz2::Editor
{
#ifndef DOXYGEN_GENERATING_OUTPUT
	using Death::Containers::SmallVector;
	using Death::Containers::String;
#endif

	/**
		@brief Learns how the tiles of a tile set fit together, from the levels that use it

		Nothing has to be set up: every level that uses the tile set as its default one is read, and for each tile of
		the tile set it's counted which of the 8 tiles around it weren't empty, and which tiles were next to it on the
		right and below. A cell to fill whose neighbors are known then gets the tile the levels used the most for the same
		neighbors, the one that also goes well with the tiles next to it --- grass at the top of ground, its edges at its
		sides, corners and slopes where the levels have them.

		The levels are read a few at a time from @ref Update(), so the editor doesn't stop for it.
	*/
	class SmartTileModel
	{
	public:
		/** @brief Bits of the neighbors of a cell that aren't empty */
		enum Neighbor : std::uint8_t
		{
			UpLeft = 0x01,
			Up = 0x02,
			UpRight = 0x04,
			Left = 0x08,
			Right = 0x10,
			DownLeft = 0x20,
			Down = 0x40,
			DownRight = 0x80
		};

		SmartTileModel();

		/** @brief Starts learning a tile set, unless it's the one learned already */
		void Request(StringView tileSet, ITileSetLibrary& library);
		/** @brief Reads some of the levels still to read, call it every frame */
		void Update();
		/** @brief Returns `true` if all levels were read */
		bool IsReady() const {
			return (_nextFile >= _files.size());
		}
		/** @brief Returns the tile set learned */
		StringView GetTileSet() const {
			return _tileSet;
		}
		/** @brief Returns the number of levels learned from */
		std::int32_t GetLevelCount() const {
			return _levelCount;
		}
		/** @brief Returns how far reading the levels is, from 0 to 1 */
		float GetProgress() const;
		/** @brief Learns from a level, the one being edited counts more */
		void AddLevel(const LevelDocument& document, std::uint32_t weight);

		/**
		 * @brief Picks a tile of the tile set for a cell
		 *
		 * @param neighbors  Neighbors that aren't empty, see @ref Neighbor
		 * @param spriteLayer Whether it's for the sprite layer, levels use other tiles there than elsewhere
		 * @param left       Tile on the left already decided, or `nullptr`
		 * @param up         Tile above already decided, or `nullptr`
		 * @param right      Tile on the right, or `nullptr`
		 * @param down       Tile below, or `nullptr`
		 * @param hash       Picks among tiles about as good, so large areas aren't all the same tile
		 * @param result     The tile
		 * @param keep       Tile there now, kept if the levels use it with the same neighbors too, or `nullptr`
		 *
		 * Returns `false` if nothing was learned yet.
		 */
		bool Pick(std::uint8_t neighbors, bool spriteLayer, const TileRef* left, const TileRef* up, const TileRef* right, const TileRef* down,
			std::uint32_t hash, TileRef& result, const TileRef* keep = nullptr);

		/** @brief Returns how many times the levels use a tile with nothing above it and something below it, as ground */
		std::uint32_t GetSurfaceUsage(const TileRef& tile);
		/** @brief Returns how many times the levels use a tile with given neighbors that aren't empty */
		std::uint32_t GetCount(std::uint8_t neighbors, bool spriteLayer, const TileRef& tile) const;
		/** @brief Returns whether a tile fits its neighbors, it's one of the rule for them or the levels use it so */
		bool Fits(std::uint8_t neighbors, bool spriteLayer, const TileRef& tile) const;
		/** @brief Returns the tiles chosen by hand, which are taken before those learned */
		TerrainRules& GetRules() {
			return _rules;
		}
		/** @brief Returns how many times the levels have a tile right of another one */
		std::uint32_t GetRightPair(const TileRef& left, const TileRef& right) const;
		/** @brief Returns how many times the levels have a tile below another one */
		std::uint32_t GetDownPair(const TileRef& up, const TileRef& down) const;

		/**
		 * @brief Suggests tiles for a cell by the tiles around it, the best first
		 *
		 * The tiles the levels have next to those around --- right of the one on the left, below the one above, and
		 * so on --- by how often, a tile next to more of them first. Neighbors that aren't tiles of the tile set are
		 * left out, the cell gets nothing if all of them are. Returns the number of tiles suggested.
		 */
		std::int32_t Suggest(const TileRef* left, const TileRef* up, const TileRef* right, const TileRef* down, std::int32_t maxCount,
			SmallVectorImpl<TileRef>& result);

		/**
		 * @brief Returns the tiles the levels use the most for a pattern of neighbors as rules know it, the most first
		 *
		 * Only what was learned, the rules aren't taken into account, see @ref TerrainRules::Reduce(). A tile used
		 * less than a part of how much the first one is, by @p minShare, is left out.
		 */
		void GetTopTiles(std::uint8_t pattern, bool spriteLayer, std::int32_t maxCount, float minShare, SmallVectorImpl<TileRef>& result);

		/** @brief Returns whether a tile is one the model picks from, of the tile set and not animated */
		static bool IsModelTile(const TileRef& tile);

	private:
		struct Candidate
		{
			std::uint32_t Tile;
			std::uint32_t Count;
		};

		String _tileSet;
		ITileSetLibrary* _library;
		SmallVector<String, 0> _files;
		std::size_t _nextFile;
		std::int32_t _levelCount;
		// Tiles seen with each pattern of neighbors, [0] other layers, [1] the sprite layer, the key is the pattern << 18 | tile
		std::unordered_map<std::uint32_t, std::uint32_t> _counts[2];
		// Tiles next to each other, the key is left or upper tile << 32 | the other one
		std::unordered_map<std::uint64_t, std::uint32_t> _rightPairs;
		std::unordered_map<std::uint64_t, std::uint32_t> _downPairs;
		// Sorted from the counts when they changed, by the pattern of 8 neighbors and of the 4 at the sides only
		SmallVector<Candidate, 0> _byPattern[2][256];
		SmallVector<Candidate, 0> _bySides[2][16];
		std::unordered_map<std::uint32_t, std::uint32_t> _surfaceUsage;
		// For each tile, the tiles the levels have right of it, left of it, below it and above it, the most often first
		std::unordered_map<std::uint32_t, SmallVector<Candidate, 0>> _neighborsOf[4];
		TerrainRules _rules;
		bool _dirty;
		// Of the levels read, so they don't have to be read again while none of them changed, see LoadCache()
		std::uint64_t _corpusSignature;
		bool _cacheWritten;

		static std::uint32_t ToKey(const TileRef& tile);
		static TileRef FromKey(std::uint32_t key);
		void Rebuild();
		String GetCachePath() const;
		bool LoadCache();
		void SaveCache();
	};
}

#endif
