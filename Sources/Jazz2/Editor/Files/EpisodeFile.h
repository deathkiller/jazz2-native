#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../../../Main.h"

#include <Containers/ArrayView.h>
#include <Containers/SmallVector.h>
#include <Containers/String.h>
#include <Containers/StringView.h>

namespace Jazz2::Editor
{
#ifndef DOXYGEN_GENERATING_OUTPUT
	using Death::Containers::ArrayView;
	using Death::Containers::SmallVector;
	using Death::Containers::SmallVectorImpl;
	using Death::Containers::String;
	using Death::Containers::StringView;
#endif

	/** @brief Image of an episode, RGBA pixels row by row */
	struct EpisodeImage
	{
		/** @brief Width in pixels */
		std::int32_t Width = 0;
		/** @brief Height in pixels */
		std::int32_t Height = 0;
		/** @brief Four bytes for each pixel */
		SmallVector<std::uint8_t, 0> Pixels;

		/** @brief Returns `true` if there's no image */
		bool IsEmpty() const {
			return (Width <= 0 || Height <= 0);
		}
	};

	/**
		@brief An episode file (`.j2e`) as the editor reads and writes it

		The same layout as @ref ContentResolver::GetEpisodeByPath() reads and @ref Compatibility::JJ2Episode writes:
		the display name, the position in the list of episodes, the first level, the previous and the next episode,
		then the title image and the background image.
	*/
	struct EpisodeFile
	{
		/** @brief Flags as the file stores them, none are defined */
		std::uint16_t Flags = 0;
		/** @brief Name shown in the list of episodes */
		String DisplayName;
		/** @brief Position in the list of episodes */
		std::uint16_t Position = 0;
		/** @brief Name of the first level, without the episode */
		String FirstLevel;
		/** @brief Name of the episode before this one */
		String PreviousEpisode;
		/** @brief Name of the episode after this one */
		String NextEpisode;
		/** @brief Image with the name of the episode */
		EpisodeImage Title;
		/** @brief Image behind the list of episodes */
		EpisodeImage Background;

		/** @brief Reads an episode file, returns `false` if it isn't one */
		static bool Read(StringView path, EpisodeFile& episode);
		/** @brief Writes the episode file through a temporary file, with @p keepBackup the old one is kept as `.bak` */
		bool Write(StringView path, bool keepBackup) const;
	};

	/**
		@brief What the editor needs to know of a level of an episode without loading it
	*/
	struct EpisodeLevelInfo
	{
		/** @brief Name of the level, its file name without the extension, in lower case */
		String Name;
		/** @brief Path of the file */
		String Path;
		/** @brief Name shown */
		String DisplayName;
		/** @brief Next level, as the level refers to it */
		String NextLevel;
		/** @brief Secret level, as the level refers to it */
		String SecretLevel;
		/** @brief Bonus level, as the level refers to it */
		String BonusLevel;
		/** @brief Default tile set */
		String DefaultTileSet;
		/** @brief Extra tile sets that aren't stored in the level */
		SmallVector<String, 0> ExtraTileSets;
		/** @brief Music file */
		String Music;

		/** @brief Reads the start of a level file, returns `false` if it isn't a level of the game's own format */
		static bool Read(StringView path, EpisodeLevelInfo& info);
		/** @brief Reads all levels of the game's own format in a directory, sorted by their names */
		static void ReadDirectory(StringView directory, SmallVectorImpl<EpisodeLevelInfo>& levels);
	};

	/**
	 * @brief Splits a reference to a level, `episode/level` or just `level` within the same episode
	 *
	 * Returns the episode, empty if the reference has none, and the level in @p level, both in lower case.
	 */
	String SplitLevelReference(StringView reference, String& level);

	/**
	 * @brief Finds the file of a level that a level refers to as its next, secret or bonus level
	 *
	 * @p reference is `episode/level`, or just `level` within the episode of @p currentLevelName (`episode/level`).
	 * The file is looked for in @p episodeRoots, the `Episodes` directories in the order the game looks in them.
	 * Returns an empty string if the file doesn't exist, or if the reference is the end of the episode (`:end`).
	 */
	String FindLevelFile(StringView reference, StringView currentLevelName, ArrayView<const String> episodeRoots);
}

#endif
