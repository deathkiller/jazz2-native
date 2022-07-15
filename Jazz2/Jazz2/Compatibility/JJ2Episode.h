#pragma once

#include "../../Common.h"
#include "JJ2Level.h"

#include <functional>
#include <memory>
#include <string>
#include <utility>

namespace Jazz2::Compatibility
{
	class JJ2Episode // .j2e
	{
	public:
		int Position;
		std::string EpisodeToken;
		std::string EpisodeName;
		std::string FirstLevel;

		JJ2Episode(const std::string& episodeToken, const std::string& episodeName, const std::string& firstLevel, int position)
			: EpisodeToken(episodeToken), EpisodeName(episodeName), FirstLevel(firstLevel), Position(position), _isRegistered(false)
		{
		}

		static JJ2Episode Open(const std::string& path);

		void Convert(const std::string& targetPath, std::function<JJ2Level::LevelToken(const std::string&)> levelTokenConversion = nullptr, std::function<std::string(JJ2Episode&)> episodeNameConversion = nullptr, std::function<std::pair<std::string, std::string>(JJ2Episode&)> episodePrevNext = nullptr);

	private:
		bool _isRegistered;
		std::unique_ptr<uint8_t[]> _titleLight;

		JJ2Episode() { }

	};
}