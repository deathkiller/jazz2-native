#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "TerrainRules.h"
#include "../Document/LevelDocument.h"

#include "../../../nCine/Base/TimeStamp.h"

#include <Containers/SmallVector.h>
#include <Containers/String.h>

#include <memory>
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

		The levels are read on a thread of their own where there are threads, otherwise a few at a time from
		@ref Update(), so the editor doesn't stop for it either way; what they tell is taken in from @ref Update() a
		little at a time. What each level told is kept in a cache file, `Cache/Editor/Adjacency/<tile set>-<hash>.adj`, by
		the path, the size and the time of the level, so only levels changed, added or removed since are read again ---
		and so is everything if the tile set changes. The cache is written as the levels are read too, so the levels
		read already aren't read again if the editor is closed before all of them are.

		The level being edited counts more than the others, as it is in the editor (see @ref LearnDocument()), so the
		tiles it uses come first. It's learned a band of rows at a time, a few milliseconds a frame, and only the bands
		that changed are learned again.

		The lists the tiles are picked from are sorted again only for the neighbors a tile is picked for, when what was
		learned of them changed. Tiles past the end of the tile set, of levels saved with an older version of it, are
		never learned, so they're never picked.
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
		~SmartTileModel();

		SmartTileModel(const SmartTileModel&) = delete;
		SmartTileModel& operator=(const SmartTileModel&) = delete;
		SmartTileModel(SmartTileModel&& other);
		SmartTileModel& operator=(SmartTileModel&& other);

		/** @brief Starts learning a tile set, unless it's the one learned already */
		void Request(StringView tileSet, ITileSetLibrary& library);
		/** @brief Forgets everything learned, the next @ref Request() learns the tile set again, as after it changed */
		void Reset();
		/** @brief Takes in what the levels read tell and reads more of them without threads, call it every frame */
		void Update();
		/** @brief Returns `true` if all levels were read */
		bool IsReady() const {
			return _ready;
		}
		/** @brief Returns the tile set learned */
		StringView GetTileSet() const {
			return _tileSet;
		}
		/** @brief Returns the number of levels learned from, not counting the one being edited */
		std::int32_t GetLevelCount() const {
			return _levelCount;
		}
		/** @brief Returns how far reading the levels is, from 0 to 1 */
		float GetProgress() const;
		/** @brief Returns `true` if there's anything to pick tiles from, levels learned from or rules */
		bool CanPick() const;
		/** @brief Learns from a level, how many times it counts is given by @p weight */
		void AddLevel(const LevelDocument& document, std::uint32_t weight);
		/**
		 * @brief Learns from the level being edited, it counts more than the others
		 *
		 * Call it whenever the model is used, it's cheap while @p revision doesn't change. A changed level is learned
		 * again at most every few seconds, a few milliseconds of each frame until all of it was gone through, and only
		 * the bands of rows of its layers that changed are learned again.
		 */
		void LearnDocument(const LevelDocument& document, std::uint32_t revision);

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

		/**
		 * @brief Returns how each pattern of neighbors as rules know it is covered, see @ref TerrainRules::Reduce()
		 *
		 * 0 not at all, 1 by tiles learned from the levels, 2 by a rule of the current terrain. Only the 47 patterns rules
		 * can be for are filled in.
		 */
		void GetCoverage(bool spriteLayer, std::uint8_t (&coverage)[256]) const;

		/** @brief Returns whether a tile is one the model picks from, of the tile set and not animated */
		static bool IsModelTile(const TileRef& tile);

		/** @brief Returns a number that changes whenever the tiles picked can, with what's learned or with the rules */
		std::uint64_t GetRevision() const {
			return (std::uint64_t(_revision) << 32) | _rules.GetRevision();
		}

#ifndef DOXYGEN_GENERATING_OUTPUT
		// What a level tells, as the counts it adds, see AddLevel()
		struct Contribution
		{
			SmallVector<std::pair<std::uint32_t, std::uint32_t>, 0> Counts[2];
			SmallVector<std::pair<std::uint64_t, std::uint32_t>, 0> RightPairs;
			SmallVector<std::pair<std::uint64_t, std::uint32_t>, 0> DownPairs;
		};
		// Reading of the levels, on a thread of its own where there are threads
		struct Job;
		// Counts what rows of layers tell, see Collect()
		struct Collector;
#endif

	private:
		struct Candidate
		{
			std::uint32_t Tile;
			std::uint32_t Count;
		};

		// What a band of rows of a layer of the level being edited told, by a hash of its tiles, see LearnDocument()
		struct DocumentBand
		{
			std::uint64_t Hash = 0;
			bool Learned = false;
			Contribution Data;
		};

		String _tileSet;
		bool _requested;
		bool _ready;
		ITileSetLibrary* _library;
		std::unique_ptr<Job> _job;
		float _progress;
		std::int32_t _levelCount;
		// Tiles of the tile set, tiles past them aren't learned, or -1 if it isn't known
		std::int32_t _tileCount;
		// Changes whenever anything is learned or forgotten
		std::uint32_t _revision;
		// Of the level being edited, by layers and bands of their rows, see LearnDocument()
		SmallVector<SmallVector<DocumentBand, 0>, 0> _documentBands;
		std::int32_t _documentTileBands;
		bool _documentLearned;
		bool _documentHasTiles;
		std::uint64_t _documentKey;
		TimeStamp _documentLearnedAt;
		// Learning it again goes on from here in the next frame, until all of it was gone through
		bool _documentPass;
		std::size_t _documentPassLayer;
		std::int32_t _documentPassBand;
		std::uint64_t _documentPassKey;
		std::uint32_t _documentFrame;
		float _documentFrameMs;
		// Tiles seen with each pattern of neighbors by the tile, [0] other layers, [1] the sprite layer
		std::unordered_map<std::uint32_t, std::uint32_t> _counts[2][256];
		// How often each tile is seen, with any neighbors, and as ground with nothing above it, kept as the counts change
		std::unordered_map<std::uint32_t, std::uint32_t> _tileTotals[2];
		std::unordered_map<std::uint32_t, std::uint32_t> _surfaceUsage;
		// Tiles next to each other, the key is left or upper tile << 32 | the other one
		std::unordered_map<std::uint64_t, std::uint32_t> _rightPairs;
		std::unordered_map<std::uint64_t, std::uint32_t> _downPairs;
		// Sorted from the counts when they're needed after they changed, by the pattern of 8 neighbors and of the 4 at
		// the sides only, see GetByPattern() and GetBySides()
		SmallVector<Candidate, 0> _byPattern[2][256];
		SmallVector<Candidate, 0> _bySides[2][16];
		bool _patternDirty[2][256];
		bool _sidesDirty[2][16];
		// The tile used the most, for neighbors nothing was learned for, [0] other layers, [1] the sprite layer
		std::uint32_t _mostUsed[2];
		bool _mostUsedDirty[2];
		// For each tile, the tiles the levels have right of it, left of it, below it and above it, the most often first,
		// made again at most once in a while when Suggest() needs them
		std::unordered_map<std::uint32_t, SmallVector<Candidate, 0>> _neighborsOf[4];
		bool _neighborsDirty;
		bool _neighborsBuilt;
		TimeStamp _neighborsBuiltAt;
		TerrainRules _rules;

		static std::uint32_t ToKey(const TileRef& tile);
		static TileRef FromKey(std::uint32_t key);
		static void Collect(const LevelDocument& document, std::uint32_t weight, Contribution& contribution);
		bool IsKnownTileKey(std::uint32_t tile) const;
		void Merge(const Contribution& contribution, bool subtract);
		const SmallVector<Candidate, 0>& GetByPattern(std::int32_t kind, std::uint8_t pattern);
		const SmallVector<Candidate, 0>& GetBySides(std::int32_t kind, std::uint8_t sides);
		std::uint32_t GetMostUsed(std::int32_t kind);
		void EnsureNeighbors();
		void LearnBand(const DocumentLayer& layer, std::int32_t band, DocumentBand& state);
		void ForgetBand(DocumentBand& state);
		void StopJob();
	};
}

#endif
