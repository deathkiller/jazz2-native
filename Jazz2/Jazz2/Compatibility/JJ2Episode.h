#pragma once

#include "../../Common.h"
#include "JJ2Level.h"

#include <functional>
#include <memory>
#include <utility>

#include <Containers/String.h>
#include <Containers/StringView.h>

using namespace Death::Containers;

namespace Jazz2::Compatibility
{
	class JJ2Episode // .j2e
	{
	public:
		int Position;
		String EpisodeToken;
		String EpisodeName;
		String FirstLevel;

		JJ2Episode(const String& episodeToken, const String& episodeName, const String& firstLevel, int position)
			: EpisodeToken(episodeToken), EpisodeName(episodeName), FirstLevel(firstLevel), Position(position), _isRegistered(false)
		{
		}

		static JJ2Episode Open(const StringView& path);

		void Convert(const String& targetPath, std::function<JJ2Level::LevelToken(const String&)> levelTokenConversion = nullptr, std::function<String(JJ2Episode&)> episodeNameConversion = nullptr, std::function<std::pair<String, String>(JJ2Episode&)> episodePrevNext = nullptr);

	private:
		bool _isRegistered;
		std::unique_ptr<uint8_t[]> _titleLight;

		JJ2Episode() { }

	};
}