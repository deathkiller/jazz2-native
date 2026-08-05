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
	namespace
	{
		/**
			@brief Episodes the original game and its expansions came with

			`hh24` has no levels in the table of known ones below, which is exactly why the filter follows each
			episode's chain instead of trusting that table to be complete.
		*/
		static const StringView OriginalEpisodes[] = {
			"prince"_s, "rescue"_s, "flash"_s, "monk"_s, "share"_s,
			"xmas98"_s, "xmas99"_s, "secretf"_s, "hh17"_s, "hh18"_s, "hh24"_s
		};

		/**
			@brief Levels the original game came with that belong to no episode

			The multiplayer levels, which no episode references, so nothing in the data says they are original -
			this list is the only record of it. It is drawn from the levels the retail game, the shareware demo,
			The Secret Files and the Christmas Chronicles install; a source directory that has collected levels
			from elsewhere will have many more, and those are what @ref AssetConverter::ConversionOptions::OriginalsOnly
			is meant to leave out. Worth reviewing against a clean installation - a name missing from here is
			silently dropped, and `ConversionOptions::SkippedLevels` reports everything that was.
		*/
		static const StringView StockStandaloneLevels[] = {
			// Battle
			"battle1"_s, "battle2"_s, "battle3"_s,
			// Race
			"race1"_s, "race2"_s, "race3"_s,
			// Treasure hunt
			"treasur1"_s, "treasur2"_s, "treasur3"_s,
			// Capture the flag
			"capture1"_s, "capture2"_s, "capture3"_s,
			// Shareware demo multiplayer
			"sharectf"_s, "sharect2"_s, "sharetrs"_s,
			// The Secret Files multiplayer
			"abattle1"_s, "arace1"_s, "arace2"_s, "battlea"_s,
			// Christmas Chronicles multiplayer
			"xmbattle1"_s, "xmbattle2"_s
		};

		/** @brief The one episode the Shareware Demo came with */
		static const StringView SharewareEpisode = "share"_s;

		/** @brief The multiplayer levels the Shareware Demo came with, a subset of the stock ones above */
		static const StringView SharewareStandaloneLevels[] = {
			"sharectf"_s, "sharect2"_s, "sharetrs"_s
		};

		bool IsOriginalEpisode(StringView name)
		{
			for (StringView episode : OriginalEpisodes) {
				if (name == episode) {
					return true;
				}
			}
			return false;
		}

		/** @brief Whether an episode survives the filter, and so whether its levels are worth following */
		bool IsAllowedEpisode(StringView name, const AssetConverter::ConversionOptions& options)
		{
			if (options.SharewareOnly) {
				return (name == SharewareEpisode);
			}
			if (options.OriginalsOnly) {
				return IsOriginalEpisode(name);
			}
			return true;
		}

		/** @brief Lowercased level token, with the extension and any directory dropped */
		String NormalizeLevelToken(StringView token)
		{
			String result = fs::GetFileNameWithoutExtension(token);
			StringUtils::lowercaseInPlace(result);
			return result;
		}

		/**
			@brief Collects every level an episode can reach, following each level's exits

			An episode names only its first level; the rest of it is discovered by opening that level and taking
			whatever it points at next, and so on. Levels are keyed by file name because that is what the exits
			refer to - a level's own recorded name is not always the same thing.
		*/
		void CollectEpisodeLevels(const HashMap<String, String>& levelFiles, StringView firstLevel,
			HashMap<String, bool>& reachable)
		{
			SmallVector<String, 0> pending;
			pending.push_back(NormalizeLevelToken(firstLevel));

			while (!pending.empty()) {
				String current = std::move(pending.back());
				pending.pop_back();
				// A token starting with ':' is an instruction to the game ("end of episode", "credits"), not a level
				if (current.empty() || current[0] == ':' || reachable.contains(current)) {
					continue;
				}

				auto file = levelFiles.find(current);
				if (file == levelFiles.end()) {
					continue;
				}
				reachable.emplace(current, true);

				JJ2Level level;
				if (!level.Open(file->second, false)) {
					continue;
				}
				for (StringView exit : { StringView(level.NextLevel), StringView(level.SecretLevel), StringView(level.BonusLevel) }) {
					if (!exit.empty()) {
						pending.push_back(NormalizeLevelToken(exit));
					}
				}
			}
		}
	}

	AssetConverter::Result AssetConverter::ConvertSourceAssets(StringView animsPath, StringView sourcePath, StringView targetPath,
		JJ2Version& version, StringView packageName)
	{
		version = JJ2Version::Unknown;

		PakWriter pakWriter(fs::CombinePath(targetPath, packageName), true);
		if (!pakWriter.IsValid()) {
			return Result::CannotWriteTarget;
		}

		return ConvertSourceAssets(animsPath, sourcePath, pakWriter, version);
	}

	AssetConverter::Result AssetConverter::ConvertSourceAssets(StringView animsPath, StringView sourcePath,
		PakWriter& pakWriter, JJ2Version& version)
	{
		version = JJ2Anims::Convert(animsPath, pakWriter);
		if (version == JJ2Version::Unknown) {
			return Result::UnsupportedVersion;
		}

		JJ2Data data;
		if (data.Open(fs::CombinePath(sourcePath, "Data.j2d"_s), false)) {
			data.Convert(pakWriter, version);
		}

		return Result::Success;
	}

	void AssetConverter::ConvertLevels(StringView sourcePath, StringView targetPath, bool recreateAll)
	{
		ConversionOptions everything;
		ConvertLevels(sourcePath, targetPath, recreateAll, everything);
	}

	void AssetConverter::ConvertLevels(StringView sourcePath, StringView targetPath, bool recreateAll, const ConversionOptions& options)
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

	HashMap<String, bool> usedTilesets, usedMusic;

	// What the filter allows through is decided up front, because it depends on the directory as a whole:
	// which levels each episode can reach can only be answered once every level's file is known
	const bool filtering = (options.OriginalsOnly || options.SharewareOnly || options.SkipNonEpisodeLevels);
	HashMap<String, bool> episodeLevels, allowedLevels;
	if (filtering) {
		HashMap<String, String> levelFiles;
		for (auto item : fs::Directory(fs::FindPathCaseInsensitive(sourcePath), fs::EnumerationOptions::SkipDirectories)) {
			if (fs::GetExtension(item) == "j2l"_s) {
				levelFiles.emplace(NormalizeLevelToken(item), String(item));
			}
		}

		for (auto item : fs::Directory(fs::FindPathCaseInsensitive(sourcePath), fs::EnumerationOptions::SkipDirectories)) {
			auto extension = fs::GetExtension(item);
			if (extension != "j2e"_s && extension != "j2pe"_s) {
				continue;
			}
			JJ2Episode episode;
			if (!episode.Open(item) || episode.Name == "home"_s) {
				// "home" is the custom-levels entry and reaches nothing
				continue;
			}
			// Every episode contributes to "belongs to an episode"; only the original ones contribute to
			// "the original game shipped this"
			CollectEpisodeLevels(levelFiles, episode.FirstLevel, episodeLevels);
			if (IsAllowedEpisode(episode.Name, options)) {
				CollectEpisodeLevels(levelFiles, episode.FirstLevel, allowedLevels);
			}
		}

		// The table of known levels covers the original episodes whether or not their episode file is present
		for (auto& knownLevel : knownLevels) {
			if (!knownLevel.second.first().empty()) {
				episodeLevels.emplace(knownLevel.first, true);
				if (IsAllowedEpisode(knownLevel.second.first(), options)) {
					allowedLevels.emplace(knownLevel.first, true);
				}
			}
		}
		// Nothing in the data says a multiplayer level is original, so the lists above are the only record
		if (options.SharewareOnly) {
			for (StringView stockLevel : SharewareStandaloneLevels) {
				allowedLevels.emplace(String(stockLevel), true);
			}
		} else if (options.OriginalsOnly) {
			for (StringView stockLevel : StockStandaloneLevels) {
				allowedLevels.emplace(String(stockLevel), true);
			}
		}
	}

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
				// "home" is how the game offers custom levels, so it is kept even when they are not converted
				if (episode.Name != "home"_s && !IsAllowedEpisode(episode.Name, options)) {
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
				if (filtering) {
					// Decided on the file name rather than the level's own recorded name, so a level that is
					// filtered out never has to be opened at all
					String token = NormalizeLevelToken(item);
					const bool allowed = ((!options.OriginalsOnly && !options.SharewareOnly) || allowedLevels.contains(token))
						&& (!options.SkipNonEpisodeLevels || episodeLevels.contains(token));
					if (!allowed) {
						if (options.SkippedLevels != nullptr) {
							options.SkippedLevels->push_back(std::move(token));
						}
						continue;
					}
				}

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
					if (!level.Music.empty()) {
						// Recorded exactly as the converted level will ask for it: the original data leaves the
						// extension off its own music, and JJ2Level::Convert fills in ".j2b" (see there)
						String music = StringUtils::lowercase(level.Music);
						if (music.find('.') == nullptr) {
							music += ".j2b"_s;
						}
						usedMusic.emplace(std::move(music), true);
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

	if (options.CopyUsedMusic && !usedMusic.empty()) {
		// The music is not converted, only carried over - but only what the levels that survived the filter
		// ask for, the same way the tilesets are. The game looks for it in a "Music" directory of its own,
		// which is not where the original data keeps it.
		LOGI("Copying used music...");
		String musicPath = fs::CombinePath(targetPath, "Music"_s);
		fs::CreateDirectories(musicPath);

		std::int32_t copied = 0;
		for (auto& pair : usedMusic) {
			auto adjustedPath = fs::FindPathCaseInsensitive(fs::CombinePath(sourcePath, pair.first));
			if (fs::IsReadableFile(adjustedPath)) {
				if (fs::Copy(adjustedPath, fs::CombinePath(musicPath, pair.first))) {
					copied++;
				} else {
					LOGW("Cannot copy \"{}\"", adjustedPath);
				}
			} else {
				LOGW("Cannot find music file \"{}\"", pair.first);
			}
		}
		LOGI("{} music files copied", copied);
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
