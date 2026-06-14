#pragma once

#include "../../Main.h"
#include "JJ2Level.h"

#include <memory>
#include <utility>

#include <Containers/String.h>
#include <Containers/StringView.h>

using namespace Death::Containers;

namespace Jazz2::Compatibility
{
	/** @brief Parses original `.j2e`/`.j2pe` episode files */
	class JJ2Episode
	{
	public:
		/** @brief Sort position of the episode */
		std::int32_t Position;
		/** @brief Internal name of the episode */
		String Name;
		/** @brief Display name of the episode */
		String DisplayName;
		/** @brief Name of the first level in the episode */
		String FirstLevel;
		/** @brief Name of the previous episode */
		String PreviousEpisode;
		/** @brief Name of the next episode */
		String NextEpisode;

		/** @brief Width of the episode image in pixels */
		std::int32_t ImageWidth;
		/** @brief Height of the episode image in pixels */
		std::int32_t ImageHeight;
		/** @brief Raw episode image data */
		std::unique_ptr<std::uint8_t[]> ImageData;

		/** @brief Width of the episode title image in pixels */
		std::int32_t TitleWidth;
		/** @brief Height of the episode title image in pixels */
		std::int32_t TitleHeight;
		/** @brief Raw episode title image data */
		std::unique_ptr<std::uint8_t[]> TitleData;

		/** @brief Creates a new instance */
		JJ2Episode();
		/** @brief Creates a new instance with the specified properties */
		JJ2Episode(StringView name, StringView displayName, StringView firstLevel, std::int32_t position);

		/** @brief Opens and parses the specified episode file */
		bool Open(StringView path);

		/** @brief Converts the episode and writes the result to the specified target path */
		void Convert(StringView targetPath, Function<JJ2Level::LevelToken(StringView)>&& levelTokenConversion = {}, Function<String(JJ2Episode*)>&& episodeNameConversion = {}, Function<Pair<String, String>(JJ2Episode*)>&& episodePrevNext = {});
	};
}