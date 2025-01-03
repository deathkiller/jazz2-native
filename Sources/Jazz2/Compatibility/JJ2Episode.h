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
		std::int32_t Position;
		String Name;
		String DisplayName;
		String FirstLevel;
		String PreviousEpisode;
		String NextEpisode;

		std::int32_t ImageWidth;
		std::int32_t ImageHeight;
		std::unique_ptr<std::uint8_t[]> ImageData;

		std::int32_t TitleWidth;
		std::int32_t TitleHeight;
		std::unique_ptr<std::uint8_t[]> TitleData;

		JJ2Episode();
		JJ2Episode(StringView name, StringView displayName, StringView firstLevel, std::int32_t position);

		bool Open(StringView path);

		void Convert(StringView targetPath, Function<JJ2Level::LevelToken(StringView)>&& levelTokenConversion = {}, Function<String(JJ2Episode*)>&& episodeNameConversion = {}, Function<Pair<String, String>(JJ2Episode*)>&& episodePrevNext = {});
	};
}