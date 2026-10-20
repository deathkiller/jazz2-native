#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../Document/LevelDocument.h"

#include <Containers/SmallVector.h>
#include <Containers/String.h>

namespace Jazz2::Editor
{
#ifndef DOXYGEN_GENERATING_OUTPUT
	using Death::Containers::SmallVector;
	using Death::Containers::String;
#endif

	/**
		@brief Tiles chosen by hand for ground with given neighbors, instead of those learned from levels

		The tools that choose tiles by their neighbors (see @ref SmartTileModel) take these first. A rule is for one of
		the 47 ways the 8 cells around can be solid, a corner counting only when both sides next to it are solid too, as
		a corner can't be seen otherwise. A rule has one tile or more, one of which is chosen by the position of the
		cell.

		A tile set can have several terrains --- ground, a wall of bricks, a cloud --- each with rules of its own, one
		of them is the one the tools use at a time. A rule may take tiles of an extra tile set too, they're kept by the
		name of the tile set and the index of the tile in it, so a level that has the tile set in another slot, or at
		another offset, gets the same tiles. Rules with tiles of a tile set the level doesn't have are kept, only not
		used in it.

		Tiles of ground can be tagged as slopes, with the heights of their surface at both edges, or left out of the
		ground the ground line tool lays (see @ref TileShapes). They're kept in the rules of the tile set (see
		@ref RulesFile), as `"terrains": [{ "name": ..., "patterns": { "<n>": [{ "i": ..., "f": ..., "s": ... }] } }]`
		and `"slopes": { "<i>": { "l": ..., "r": ... } | { "x": true } }`.
	*/
	class TerrainRules
	{
	public:
		/** @brief Slope tag of a tile of the default tile set */
		struct SlopeTag
		{
			/** @brief Height of the surface at the left edge, in pixels from the bottom of the tile */
			float Left = 0.0f;
			/** @brief Height of the surface at the right edge */
			float Right = 0.0f;
			/** @brief Whether the tile isn't ground at all, the heights don't matter then */
			bool Excluded = false;
		};

		/** @brief Returns the pattern of neighbors as rules know it, corners only with both sides next to them */
		static std::uint8_t Reduce(std::uint8_t neighbors);
		/** @brief Returns all 47 patterns rules can be for, ordered by how many neighbors are solid */
		static void GetPatterns(SmallVectorImpl<std::uint8_t>& patterns);

		TerrainRules();

		/** @brief Reads the rules of a tile set */
		void Load(StringView tileSet);
		/** @brief Writes the rules into the rules of a tile set in the user content, nothing if the rules there can't be read, returns `false` then */
		bool Save(StringView tileSet) const;
		/** @brief Finds the tiles of extra tile sets in a level, call it whenever the level is used with the rules */
		void Resolve(const LevelDocument& document);

		/** @brief Returns the number of terrains, there's always one */
		std::int32_t GetTerrainCount() const {
			return std::int32_t(_terrains.size());
		}
		/** @brief Returns the name of a terrain */
		StringView GetTerrainName(std::int32_t index) const;
		/** @brief Renames a terrain */
		void RenameTerrain(std::int32_t index, StringView name);
		/** @brief Adds a terrain without rules, returns its index */
		std::int32_t AddTerrain(StringView name);
		/** @brief Removes a terrain, the last one stays */
		void RemoveTerrain(std::int32_t index);
		/** @brief Returns the terrain the rules are used of */
		std::int32_t GetCurrentTerrain() const {
			return _current;
		}
		/** @brief Chooses the terrain the rules are used of */
		void SetCurrentTerrain(std::int32_t index);

		/** @brief Returns the tiles of the rule of the current terrain for neighbors, or `nullptr` if there's none */
		const SmallVector<TileRef, 0>* Find(std::uint8_t neighbors) const;
		/** @brief Sets the tiles of the rule of the current terrain for a pattern, no tiles remove it */
		void Set(std::uint8_t pattern, ArrayView<const TileRef> tiles);
		/** @brief Removes all rules of the current terrain */
		void Clear();
		/** @brief Returns the number of rules of the current terrain */
		std::int32_t GetCount() const;

		/** @brief Returns the slope tag of a tile of the default tile set, or `nullptr` */
		const SlopeTag* FindSlope(std::uint16_t index) const;
		/** @brief Tags a tile of the default tile set, or removes its tag with `nullptr` */
		void SetSlope(std::uint16_t index, const SlopeTag* tag);
		/** @brief Returns the tiles with a slope tag, sorted */
		void GetSlopeTiles(SmallVectorImpl<std::uint16_t>& tiles) const;

		/** @brief Returns a number that changes whenever the rules do */
		std::uint32_t GetRevision() const {
			return _revision;
		}
		/** @brief Returns a number that changes whenever the slope tags do, not with the rules of the terrains */
		std::uint32_t GetSlopeRevision() const {
			return _slopeRevision;
		}

		/** @brief Returns whether a tile can be in a rule, a tile of the default tile set or an extra one in a file, not animated */
		static bool IsRuleTile(const LevelDocument& document, const TileRef& tile);

	private:
		// A tile of a rule as it's kept, by the tile set it's in
		struct RuleTile
		{
			// Name of an extra tile set, empty for the default one
			String TileSet;
			// Index of the tile in its tile set file
			std::uint16_t Index;
			std::uint8_t Flags;
		};

		struct Terrain
		{
			String Name;
			SmallVector<SmallVector<RuleTile, 0>, 0> ByPattern;
			// The tiles as the level being edited has them, see Resolve()
			SmallVector<SmallVector<TileRef, 0>, 0> Resolved;
		};

		SmallVector<Terrain, 1> _terrains;
		std::int32_t _current;
		SmallVector<std::pair<std::uint16_t, SlopeTag>, 0> _slopes;
		std::uint32_t _revision;
		std::uint32_t _slopeRevision = 0;
		const LevelDocument* _document;
		// A hash of the extra tile sets of the level when it was resolved, a change makes it resolved again
		std::uint64_t _resolvedFor = 0;
		bool _resolved = false;

		void ResolveTerrain(Terrain& terrain) const;
		static std::uint64_t GetExtraTileSetsKey(const LevelDocument& document);
	};
}

#endif
