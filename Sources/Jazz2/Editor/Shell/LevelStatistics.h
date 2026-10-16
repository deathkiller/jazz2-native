#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include <cstdint>

namespace Jazz2::Editor
{
	class EditorSession;
	class LevelDocument;

	/**
		@brief What a level has in a difficulty, counted from its events

		Events only in multiplayer are left out, generators are counted on their own, what they spawn isn't counted,
		there's no end to it. Gems and coins are counted by their value, those in crates and barrels too.
	*/
	struct LevelStatistics
	{
		/** @brief Enemies, bosses not included */
		std::int32_t Enemies = 0;
		/** @brief Bosses */
		std::int32_t Bosses = 0;
		/** @brief Value of all gems, red ones are worth 1, green 5, blue 10 and purple 1 */
		std::int32_t Gems = 0;
		/** @brief Giant gems, each breaks into 5 to 12 red gems, which aren't counted in @ref Gems */
		std::int32_t GiantGems = 0;
		/** @brief Value of all coins, silver ones are worth 1, gold 5 */
		std::int32_t Coins = 0;
		/** @brief Food */
		std::int32_t Food = 0;
		/** @brief Carrots of all kinds */
		std::int32_t Carrots = 0;
		/** @brief Extra lives */
		std::int32_t ExtraLives = 0;
		/** @brief Ammo, and crates and barrels with ammo */
		std::int32_t Ammo = 0;
		/** @brief Crates, barrels and monitors */
		std::int32_t Containers = 0;
		/** @brief Checkpoints */
		std::int32_t Checkpoints = 0;
		/** @brief Tiles of warps */
		std::int32_t Warps = 0;
		/** @brief Tiles of exits */
		std::int32_t Exits = 0;
		/** @brief Starts of Jazz, Spaz and Lori */
		std::int32_t Starts[3] = {};
		/** @brief Generators */
		std::int32_t Generators = 0;
		/** @brief All events */
		std::int32_t Events = 0;

		/**
		 * @brief Counts what a level has in a difficulty
		 *
		 * @p difficultyBit is one of @ref Formats::EventRecord::Easy, @ref Formats::EventRecord::Normal and
		 * @ref Formats::EventRecord::Hard.
		 */
		static LevelStatistics Compute(const LevelDocument& document, std::uint8_t difficultyBit);
	};

	/** @brief Shows what the level has in each difficulty */
	void ShowLevelStatisticsDialog(EditorSession& session);
}

#endif
