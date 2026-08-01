#include "AssetConverter.h"
#include "EventConverter.h"
#include "JJ2Anims.h"
#include "JJ2Data.h"
#include "JJ2Episode.h"
#include "JJ2Level.h"
#include "JJ2Strings.h"
#include "JJ2Tileset.h"

#include <Containers/GrowableArray.h>
#include <Containers/StringConcatenable.h>
#include <Containers/StringUtils.h>
#include <IO/FileSystem.h>
#include <IO/PakFile.h>

using namespace Death::Containers::Literals;
using namespace Death::IO;

namespace Jazz2::Compatibility
{
	AssetConverter::Result AssetConverter::ConvertSourceAssets(StringView animsPath, StringView sourcePath, StringView targetPath, JJ2Version& version)
	{
		version = JJ2Version::Unknown;

		std::unique_ptr<PakWriter> pakWriter = std::make_unique<PakWriter>(fs::CombinePath(targetPath, "Source.pak"_s), true);
		if (!pakWriter->IsValid()) {
			return Result::CannotWriteTarget;
		}

		version = JJ2Anims::Convert(animsPath, *pakWriter);
		if (version == JJ2Version::Unknown) {
			return Result::UnsupportedVersion;
		}

		JJ2Data data;
		if (data.Open(fs::CombinePath(sourcePath, "Data.j2d"_s), false)) {
			data.Convert(*pakWriter, version);
		}

		return Result::Success;
	}

	void AssetConverter::ConvertLevels(StringView sourcePath, StringView targetPath, bool recreateAll)
	{
	LOGI("Searching for levels...");

	Compatibility::EventConverter eventConverter;

	bool hasChristmasChronicles = fs::IsReadableFile(fs::FindPathCaseInsensitive(fs::CombinePath(sourcePath, "xmas99.j2e"_s)));
	const HashMap<String, Pair<String, String>> knownLevels = {
		{ "trainer"_s, { "prince"_s, {} } },
		{ "castle1"_s, { "prince"_s, "01"_s } },
		{ "castle1n"_s, { "prince"_s, "02"_s } },
		{ "carrot1"_s, { "prince"_s, "03"_s } },
		{ "carrot1n"_s, { "prince"_s, "04"_s } },
		{ "labrat1"_s, { "prince"_s, "05"_s } },
		{ "labrat2"_s, { "prince"_s, "06"_s } },
		{ "labrat3"_s, { "prince"_s, "bonus"_s } },

		{ "colon1"_s, { "rescue"_s, "01"_s } },
		{ "colon2"_s, { "rescue"_s, "02"_s } },
		{ "psych1"_s, { "rescue"_s, "03"_s } },
		{ "psych2"_s, { "rescue"_s, "04"_s } },
		{ "beach"_s, { "rescue"_s, "05"_s } },
		{ "beach2"_s, { "rescue"_s, "06"_s } },
		{ "psych3"_s, { "rescue"_s, "bonus"_s } },

		{ "diam1"_s, { "flash"_s, "01"_s } },
		{ "diam3"_s, { "flash"_s, "02"_s } },
		{ "tube1"_s, { "flash"_s, "03"_s } },
		{ "tube2"_s, { "flash"_s, "04"_s } },
		{ "medivo1"_s, { "flash"_s, "05"_s } },
		{ "medivo2"_s, { "flash"_s, "06"_s } },
		{ "garglair"_s, { "flash"_s, "bonus"_s } },
		{ "tube3"_s, { "flash"_s, "bonus"_s } },

		{ "jung1"_s, { "monk"_s, "01"_s } },
		{ "jung2"_s, { "monk"_s, "02"_s } },
		{ "hell"_s, { "monk"_s, "03"_s } },
		{ "hell2"_s, { "monk"_s, "04"_s } },
		{ "damn"_s, { "monk"_s, "05"_s } },
		{ "damn2"_s, { "monk"_s, "06"_s } },

		{ "share1"_s, { "share"_s, "01"_s } },
		{ "share2"_s, { "share"_s, "02"_s } },
		{ "share3"_s, { "share"_s, "03"_s } },

		{ "xmas1"_s, { "xmas99"_s, "01"_s } },
		{ "xmas2"_s, { "xmas99"_s, "02"_s } },
		{ "xmas3"_s, { "xmas99"_s, "03"_s } },

		{ "easter1"_s, { "secretf"_s, "01"_s } },
		{ "easter2"_s, { "secretf"_s, "02"_s } },
		{ "easter3"_s, { "secretf"_s, "03"_s } },
		{ "haunted1"_s, { "secretf"_s, "04"_s } },
		{ "haunted2"_s, { "secretf"_s, "05"_s } },
		{ "haunted3"_s, { "secretf"_s, "06"_s } },
		{ "town1"_s, { "secretf"_s, "07"_s } },
		{ "town2"_s, { "secretf"_s, "08"_s } },
		{ "town3"_s, { "secretf"_s, "09"_s } },

		// Holiday Hare '17
		{ "hh17_level00"_s, { "hh17"_s, {} } },
		{ "hh17_level01"_s, { "hh17"_s, {} } },
		{ "hh17_level01_save"_s, { "hh17"_s, {} } },
		{ "hh17_level02"_s, { "hh17"_s, {} } },
		{ "hh17_level02_save"_s, { "hh17"_s, {} } },
		{ "hh17_level03"_s, { "hh17"_s, {} } },
		{ "hh17_level03_save"_s, { "hh17"_s, {} } },
		{ "hh17_level04"_s, { "hh17"_s, {} } },
		{ "hh17_level04_save"_s, { "hh17"_s, {} } },
		{ "hh17_level05"_s, { "hh17"_s, {} } },
		{ "hh17_level05_save"_s, { "hh17"_s, {} } },
		{ "hh17_level06"_s, { "hh17"_s, {} } },
		{ "hh17_level06_save"_s, { "hh17"_s, {} } },
		{ "hh17_level07"_s, { "hh17"_s, {} } },
		{ "hh17_level07_save"_s, { "hh17"_s, {} } },
		{ "hh17_ending"_s, { "hh17"_s, {} } },
		{ "hh17_guardian"_s, { "hh17"_s, {} } },

		// Holiday Hare '18
		{ "hh18_level01"_s, { "hh18"_s, {} } },
		{ "hh18_level02"_s, { "hh18"_s, {} } },
		{ "hh18_level03"_s, { "hh18"_s, {} } },
		{ "hh18_level04"_s, { "hh18"_s, {} } },
		{ "hh18_level05"_s, { "hh18"_s, {} } },
		{ "hh18_level06"_s, { "hh18"_s, {} } },
		{ "hh18_level07"_s, { "hh18"_s, {} } },
		{ "hh18_save01"_s, { "hh18"_s, {} } },
		{ "hh18_save02"_s, { "hh18"_s, {} } },
		{ "hh18_save03"_s, { "hh18"_s, {} } },
		{ "hh18_save04"_s, { "hh18"_s, {} } },
		{ "hh18_save05"_s, { "hh18"_s, {} } },
		{ "hh18_save06"_s, { "hh18"_s, {} } },
		{ "hh18_save07"_s, { "hh18"_s, {} } },
		{ "hh18_ending"_s, { "hh18"_s, {} } },
		{ "hh18_guardian"_s, { "hh18"_s, {} } },

		// Special names
		{ "end"_s, { {}, ":end"_s } },
		{ "endepis"_s, { {}, ":end"_s } },
		{ "ending"_s, { {}, ":credits"_s } }
	};

	auto LevelTokenConversion = [&knownLevels](StringView levelToken) -> Compatibility::JJ2Level::LevelToken {
		auto it = knownLevels.find(levelToken);
		if (it != knownLevels.end()) {
			if (it->second.second().empty()) {
				return { it->second.first(), levelToken };
			}
			return { it->second.first(), (it->second.second()[0] == ':' ? it->second.second() : (it->second.second() + "_"_s + levelToken)) };
		}
		return { {}, levelToken };
	};

	auto EpisodeNameConversion = [](Compatibility::JJ2Episode* episode) -> String {
		if (episode->Name == "share"_s && episode->DisplayName == "#Shareware@Levels"_s) {
			return "Shareware Demo"_s;
		} else if (episode->Name == "xmas98"_s && episode->DisplayName == "#Xmas 98@Levels"_s) {
			return "Holiday Hare '98"_s;
		} else if (episode->Name == "xmas99"_s && episode->DisplayName == "#Xmas 99@Levels"_s) {
			return "The Christmas Chronicles"_s;
		} else if (episode->Name == "secretf"_s && episode->DisplayName == "#Secret@Files"_s) {
			return "The Secret Files"_s;
		} else if (episode->Name == "hh17"_s && episode->DisplayName == "Holiday Hare 17"_s) {
			return "Holiday Hare '17"_s;
		} else if (episode->Name == "hh18"_s && episode->DisplayName == "Holiday Hare 18"_s) {
			return "Holiday Hare '18"_s;
		} else if (episode->Name == "hh24"_s && episode->DisplayName == "HH24"_s) {
			return "Holiday Hare '24"_s;
		} else {
			// Strip formatting
			return Compatibility::JJ2Strings::RecodeString(episode->DisplayName, true);
		}
	};

	auto EpisodePrevNext = [](Compatibility::JJ2Episode* episode) -> Pair<String, String> {
		if (episode->Name == "prince"_s) {
			return { {}, "rescue"_s };
		} else if (episode->Name == "rescue"_s) {
			return { "prince"_s, "flash"_s };
		} else if (episode->Name == "flash"_s) {
			return { "rescue"_s, "monk"_s };
		} else if (episode->Name == "monk"_s) {
			return { "flash"_s, {} };
		} else {
			return { {}, {} };
		}
	};

	String episodesPath = fs::CombinePath(targetPath, "Episodes"_s);
	if (recreateAll) {
		fs::RemoveDirectoryRecursive(episodesPath);
		fs::CreateDirectories(episodesPath);
	}

	HashMap<String, bool> usedTilesets;

	for (auto item : fs::Directory(fs::FindPathCaseInsensitive(sourcePath), fs::EnumerationOptions::SkipDirectories)) {
		auto extension = fs::GetExtension(item);
		if (extension == "j2e"_s || extension == "j2pe"_s) {
			// Episode
			if (!recreateAll) {
				String episodeName = fs::GetFileNameWithoutExtension(item);
				StringUtils::lowercaseInPlace(episodeName);
				String fullPath = fs::CombinePath(episodesPath, String((episodeName == "xmas98"_s ? "xmas99"_s : StringView(episodeName)) + ".j2e"_s));
				if (fs::FileExists(fullPath)) {
					continue;
				}
			}

			Compatibility::JJ2Episode episode;
			if (episode.Open(item)) {
				if (hasChristmasChronicles && episode.Name == "xmas98"_s) {
					continue;
				}
				if (episode.Name == "home"_s) {
					episode.FirstLevel = ":custom-levels"_s;
					episode.Position = UINT16_MAX;
				} else if (episode.Position >= UINT32_MAX) {
					episode.Position = UINT16_MAX - 1;
				}

				String fullPath = fs::CombinePath(episodesPath, String((episode.Name == "xmas98"_s ? "xmas99"_s : StringView(episode.Name)) + ".j2e"_s));
				episode.Convert(fullPath, std::move(LevelTokenConversion), std::move(EpisodeNameConversion), std::move(EpisodePrevNext));
			}
		} else if (extension == "j2l"_s) {
			// Level
			String levelName = fs::GetFileNameWithoutExtension(item);
			if (levelName.find("-MLLE-Data-"_s) == nullptr) {
				if (!recreateAll) {
					StringUtils::lowercaseInPlace(levelName);

					String fullPath;
					auto it = knownLevels.find(levelName);
					if (it != knownLevels.end()) {
						if (it->second.second().empty()) {
							fullPath = fs::CombinePath({ episodesPath, it->second.first(), String(levelName + ".j2l"_s) });
						} else {
							fullPath = fs::CombinePath({ episodesPath, it->second.first(), String(it->second.second() + '_' + levelName + ".j2l"_s) });
						}
					} else {
						fullPath = fs::CombinePath({ episodesPath, "unknown"_s, String(levelName + ".j2l"_s) });
					}

					if (fs::FileExists(fullPath)) {
						continue;
					}
				}

				Compatibility::JJ2Level level;
				if (level.Open(item, false)) {
					String fullPath;
					auto it = knownLevels.find(level.LevelName);
					if (it != knownLevels.end()) {
						if (it->second.second().empty()) {
							fullPath = fs::CombinePath({ episodesPath, it->second.first(), String(level.LevelName + ".j2l"_s) });
						} else {
							fullPath = fs::CombinePath({ episodesPath, it->second.first(), String(it->second.second() + '_' + level.LevelName + ".j2l"_s) });
						}
					} else {
						fullPath = fs::CombinePath({ episodesPath, "unknown"_s, String(level.LevelName + ".j2l"_s) });
					}

					fs::CreateDirectories(fs::GetDirectoryName(fullPath));
					level.Convert(fullPath, eventConverter, LevelTokenConversion);

					usedTilesets.emplace(level.Tileset, true);
					for (auto& extraTileset : level.ExtraTilesets) {
						usedTilesets.emplace(extraTileset.Name, true);
					}

					// Also copy level script file if exists
					StringView foundDot = item.findLastOr('.', item.end());
					String scriptPath = item.prefix(foundDot.begin()) + ".j2as"_s;
					auto adjustedPath = fs::FindPathCaseInsensitive(scriptPath);
					if (fs::IsReadableFile(adjustedPath)) {
						foundDot = fullPath.findLastOr('.', fullPath.end());
						fs::Copy(adjustedPath, String(fullPath.prefix(foundDot.begin()) + ".j2as"_s));
					}
				}
			}
		}
#if defined(DEATH_DEBUG)
		/*else if (extension == "j2s"_s && recreateAll) {
			// Translations
			Compatibility::JJ2Strings strings;
			strings.Open(item);

			String fullPath = fs::CombinePath({ targetPath, "ExtractedTranslations"_s, String(strings.Name + ".h"_s) });
			fs::CreateDirectories(fs::GetDirectoryName(fullPath));
			strings.Convert(fullPath, LevelTokenConversion);
		}*/
#endif
	}

	if (recreateAll || !usedTilesets.empty()) {
		// Convert only used tilesets
		LOGI("Converting used tilesets...");
		String tilesetsPath = fs::CombinePath(targetPath, "Tilesets"_s);
		if (recreateAll) {
			fs::RemoveDirectoryRecursive(tilesetsPath);
			fs::CreateDirectories(tilesetsPath);
		}

		for (auto& pair : usedTilesets) {
			String tilesetPath = fs::CombinePath(sourcePath, String(pair.first + ".j2t"_s));
			auto adjustedPath = fs::FindPathCaseInsensitive(tilesetPath);
			if (fs::IsReadableFile(adjustedPath)) {
				Compatibility::JJ2Tileset tileset;
				if (tileset.Open(adjustedPath, false)) {
					tileset.Convert(fs::CombinePath({ tilesetsPath, String(pair.first + ".j2t"_s) }));
				}
			}
		}
	}
	}
}
