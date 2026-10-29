#include "AssetConverter.h"
#include "EventConverter.h"
#include "JJ2Anims.h"
#include "JJ2Data.h"
#include "JJ2Episode.h"
#include "JJ2Level.h"
#include "JJ2Strings.h"
#include "JJ2Tileset.h"

#include "../../nCine/Base/Algorithms.h"
#if defined(WITH_THREADS)
#	include "../../nCine/Threading/Thread.h"
#	include "../../nCine/Threading/ThreadSync.h"
#endif

#include <atomic>

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
		/** @brief Episodes the original game and its expansions came with */
		static const StringView OriginalEpisodes[] = {
			"prince"_s, "rescue"_s, "flash"_s, "monk"_s, "share"_s,
			"xmas98"_s, "xmas99"_s, "secretf"_s, "hh17"_s, "hh18"_s, "hh24"_s
		};

		/**
			@brief Levels of the original episodes that no other level of their episode leads to

			Every other level of an episode is found by following the exits from its first level, see
			@ref CollectEpisodeLevels(). The tutorial is started by the menu, and `psych3` isn't reachable in
			the original game either.
		*/
		static const Pair<StringView, StringView> UnreachableEpisodeLevels[] = {
			{ "trainer"_s, "prince"_s },
			{ "psych3"_s, "rescue"_s }
		};

		/** @brief Level names that are instructions to the game rather than levels */
		static const Pair<StringView, StringView> SpecialLevelTokens[] = {
			{ "end"_s, ":end"_s },
			{ "endepis"_s, ":end"_s },
			{ "endgame"_s, ":end"_s },
			{ "ending"_s, ":credits"_s }
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

#if defined(WITH_THREADS)
		using ConversionMutex = nCine::Mutex;
#else
		struct ConversionMutex
		{
			void Lock() {}
			void Unlock() {}
		};
#endif

		/** @brief Holds a @ref ConversionMutex until the end of the scope */
		class ConversionLock
		{
		public:
			explicit ConversionLock(ConversionMutex& mutex)
				: _mutex(mutex)
			{
				_mutex.Lock();
			}
			~ConversionLock()
			{
				_mutex.Unlock();
			}

			ConversionLock(const ConversionLock&) = delete;
			ConversionLock& operator=(const ConversionLock&) = delete;

		private:
			ConversionMutex& _mutex;
		};

		/**
			@brief Calls @p work with every index from 0 up to @p count, on as many threads as there are processors

			The levels and the tile sets don't depend on each other, so they're converted at once. What they share has to be
			locked by the caller.
		*/
		template<class Fn>
		void ForEachParallel(std::int32_t count, Fn&& work)
		{
#if defined(WITH_THREADS)
#	if defined(DEATH_TARGET_CONSTRAINED_MEMORY)
			// Each of them has a whole level or tile set in memory
			constexpr std::int32_t MaxThreads = 2;
#	else
			constexpr std::int32_t MaxThreads = 8;
#	endif
			std::int32_t threadCount = std::min(std::min(std::int32_t(nCine::Thread::GetProcessorCount()), MaxThreads), count);
			if (threadCount > 1) {
				std::atomic<std::int32_t> next{0};
				auto run = [&next, &work, count]() {
					for (std::int32_t i = next.fetch_add(1); i < count; i = next.fetch_add(1)) {
						work(i);
					}
				};
				SmallVector<nCine::Thread, MaxThreads> threads;
				for (std::int32_t i = 1; i < threadCount; i++) {
					threads.emplace_back(run);
				}
				run();
				for (nCine::Thread& thread : threads) {
					thread.Join();
				}
				return;
			}
#endif
			for (std::int32_t i = 0; i < count; i++) {
				work(i);
			}
		}

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

		bool IsSpecialLevelToken(StringView token)
		{
			for (const auto& special : SpecialLevelTokens) {
				if (token == special.first()) {
					return true;
				}
			}
			return false;
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
				String current = Death::move(pending.back());
				pending.pop_back();
				// A token starting with ':' is an instruction to the game ("end of episode", "credits"), not a level
				if (current.empty() || current[0] == ':' || IsSpecialLevelToken(current) || reachable.contains(current)) {
					continue;
				}

				auto file = levelFiles.find(current);
				if (file == levelFiles.end()) {
					continue;
				}
				reachable.emplace(current, true);

				String nextLevel, secretLevel, bonusLevel;
				if (!JJ2Level::ReadExits(file->second, nextLevel, secretLevel, bonusLevel)) {
					continue;
				}
				for (StringView exit : { StringView(nextLevel), StringView(secretLevel), StringView(bonusLevel) }) {
					if (!exit.empty()) {
						pending.push_back(NormalizeLevelToken(exit));
					}
				}
			}
		}

		/** @brief Episodes that lead to a level, see @ref AssignEpisodeLevels() */
		struct EpisodeClaim
		{
			String Original;
			String Custom;
			bool OriginalShared = false;
			bool CustomShared = false;

			void Add(StringView episodeName)
			{
				const bool isOriginal = IsOriginalEpisode(episodeName);
				String& claimed = (isOriginal ? Original : Custom);
				if (claimed.empty()) {
					claimed = episodeName;
				} else if (claimed != episodeName) {
					(isOriginal ? OriginalShared : CustomShared) = true;
				}
			}
		};

		/**
			@brief Decides the episode of every level that an episode leads to

			A level that more episodes lead to belongs to the original one of them, so a custom episode that continues
			into an original level doesn't take it. If that is still more than one, it belongs to none of them and the
			episode is left empty. The level is still a level of an episode, but its directory would otherwise depend on
			the order the files are found in.
		*/
		HashMap<String, String> AssignEpisodeLevels(const HashMap<String, EpisodeClaim>& claims)
		{
			HashMap<String, String> levelEpisodes;
			for (const auto& pair : claims) {
				const EpisodeClaim& claim = pair.second;
				const bool isShared = (!claim.Original.empty() ? claim.OriginalShared : claim.CustomShared);
				StringView episodeName = (!claim.Original.empty() ? StringView(claim.Original) : StringView(claim.Custom));
				if (isShared) {
					LOGW("Level \"{}\" is reached from more than one episode, it's converted as a level of no episode", pair.first);
					episodeName = {};
				}
				levelEpisodes.emplace(pair.first, String(episodeName));
			}
			return levelEpisodes;
		}

		/**
			@brief Tiles of the original tile sets the previews of the levels are drawn with, see JJ2Level::Convert()

			A preview takes only a few hundred tiles of a tile set. Where memory is scarce, only those are read from the
			file, a whole tile set wouldn't fit beside the level being converted. Elsewhere, many levels share a tile set,
			and reading one takes longer than converting a level, so the whole tile sets used last are kept for the levels
			after, as many as fit into a budget - a tile set of a few thousand tiles takes megabytes - and the tiles are
			copied from them. A tile set that isn't there is remembered as such.
		*/
		class PreviewTilesetCache
		{
		public:
			explicit PreviewTilesetCache(StringView sourcePath)
				: _sourcePath(sourcePath)
			{
			}

			/**
			 * @brief Returns the tiles of a tile set of a name in the order of @p tiles, or `nullptr` if the source directory
			 *        doesn't have it, and sets @p tileCount to the number of tiles of the whole tile set
			 */
			std::shared_ptr<const Formats::TileSetData> Get(StringView name, ArrayView<const std::int32_t> tiles, std::int32_t& tileCount)
			{
#if defined(DEATH_TARGET_CONSTRAINED_MEMORY)
				auto adjustedPath = fs::FindPathCaseInsensitive(fs::CombinePath(_sourcePath, String(name + ".j2t"_s)));
				if (!fs::IsReadableFile(adjustedPath)) {
					return nullptr;
				}
				auto data = std::make_shared<Formats::TileSetData>();
				if (!JJ2Tileset::ConvertTiles(adjustedPath, tiles, *data, tileCount)) {
					return nullptr;
				}
				return data;
#else
				std::shared_ptr<const Formats::TileSetData> whole = GetWhole(name);
				if (whole == nullptr) {
					return nullptr;
				}
				tileCount = whole->TileCount;

				auto data = std::make_shared<Formats::TileSetData>();
				std::memcpy(data->Palette, whole->Palette, sizeof(data->Palette));
				data->Resize(std::int32_t(tiles.size()));
				for (std::size_t i = 0; i < tiles.size(); i++) {
					std::int32_t tile = tiles[i];
					if (tile >= 0 && tile < whole->TileCount) {
						data->TrueColor[i] = whole->TrueColor[tile];
						std::memcpy(&data->Images[i * Formats::TileSetData::ImageBytesPerTile],
							&whole->Images[std::size_t(tile) * Formats::TileSetData::ImageBytesPerTile], Formats::TileSetData::ImageBytesPerTile);
					}
				}
				return data;
#endif
			}

		private:
#if !defined(DEATH_TARGET_CONSTRAINED_MEMORY)
			struct Entry
			{
				String Name;
				std::shared_ptr<Formats::TileSetData> Data;
				std::uint32_t LastUse;
			};

			static constexpr std::size_t Budget = 64 * 1024 * 1024;

			SmallVector<Entry, 0> _entries;
			std::size_t _size = 0;
			std::uint32_t _lastUse = 0;

			// The whole tile set of a name, or `nullptr` if the source directory doesn't have it
			std::shared_ptr<const Formats::TileSetData> GetWhole(StringView name)
			{
				_lastUse++;
				for (Entry& entry : _entries) {
					if (entry.Name == name) {
						entry.LastUse = _lastUse;
						return entry.Data;
					}
				}

				std::shared_ptr<Formats::TileSetData> data;
				auto adjustedPath = fs::FindPathCaseInsensitive(fs::CombinePath(_sourcePath, String(name + ".j2t"_s)));
				if (fs::IsReadableFile(adjustedPath)) {
					JJ2Tileset tileset;
					if (tileset.Open(adjustedPath, false)) {
						data = std::make_shared<Formats::TileSetData>();
						tileset.Convert(*data);
					}
				}

				// One that doesn't fit into the budget on its own isn't kept at all, it would only push out all the others
				// and then stay behind once the level that asked for it is done
				std::size_t size = GetDataSize(data.get());
				if (size > Budget) {
					return data;
				}

				// The ones used longest ago make room, a level still holds those it's drawn with
				while (!_entries.empty() && _size + size > Budget) {
					std::size_t oldest = 0;
					for (std::size_t i = 1; i < _entries.size(); i++) {
						if (_entries[i].LastUse < _entries[oldest].LastUse) {
							oldest = i;
						}
					}
					_size -= GetDataSize(_entries[oldest].Data.get());
					_entries.erase(_entries.begin() + oldest);
				}
				_entries.push_back({ String(name), data, _lastUse });
				_size += size;
				return data;
			}

			// The memory a tile set takes, its masks and the flags of its true color tiles included
			static std::size_t GetDataSize(const Formats::TileSetData* data)
			{
				return (data != nullptr ? data->Images.size() + data->TrueColor.size() + data->Masks.size() : 0);
			}
#endif

			String _sourcePath;
		};

		/**
			@brief Textures of the textured layers of JJ2+, see JJ2Level::Convert()

			`Plus.j2d` of a JJ2+ installation has them as `TextureBG.<n>`, 256×256 palette indices each, `n` being the
			value of `TEXTURE::Texture` that names it. The file is read only once a level asks for one of them, few levels
			do, and only the textures are kept of it.
		*/
		class PlusTextureCache
		{
		public:
			explicit PlusTextureCache(StringView sourcePath)
				: _sourcePath(sourcePath), _loaded(false)
			{
			}

			/** @brief Fills the pixels of a texture of a number, returns `false` if the source directory doesn't have it */
			bool Get(std::int32_t index, std::uint8_t* pixels)
			{
				if (!_loaded) {
					_loaded = true;
					auto path = fs::FindPathCaseInsensitive(fs::CombinePath(_sourcePath, "Plus.j2d"_s));
					JJ2Data data;
					if (fs::IsReadableFile(path) && data.Open(path, false)) {
						for (JJ2Data::Item& item : data.Items) {
							if (item.Filename.hasPrefix("TextureBG."_s) && item.Size == TextureBytes) {
								StringView number = item.Filename.exceptPrefix("TextureBG."_s);
								_textures.push_back({ stou32(number.data(), number.size()), Death::move(item.Blob) });
							}
						}
					}
				}
				for (const Entry& entry : _textures) {
					if (entry.Index == std::uint32_t(index)) {
						std::memcpy(pixels, entry.Pixels.get(), TextureBytes);
						return true;
					}
				}
				return false;
			}

		private:
			static constexpr std::int32_t TextureBytes = Formats::LayerPropertiesChunk::TextureSize * Formats::LayerPropertiesChunk::TextureSize;

			struct Entry
			{
				std::uint32_t Index;
				std::unique_ptr<std::uint8_t[]> Pixels;
			};

			String _sourcePath;
			SmallVector<Entry, 0> _textures;
			bool _loaded;
		};
	}

	AssetConverter::Result AssetConverter::ConvertSourceAssets(StringView animsPath, StringView sourcePath, StringView targetPath,
		JJ2Version& version, StringView packageName, ConversionProgress progress)
	{
		version = JJ2Version::Unknown;

		PakWriter pakWriter(fs::CombinePath(targetPath, packageName), true);
		if (!pakWriter.IsValid()) {
			return Result::CannotWriteTarget;
		}

		return ConvertSourceAssets(animsPath, sourcePath, pakWriter, version, progress);
	}

	AssetConverter::Result AssetConverter::ConvertSourceAssets(StringView animsPath, StringView sourcePath,
		PakWriter& pakWriter, JJ2Version& version, ConversionProgress progress)
	{
		// `Data.j2d` is small next to the animations, so the animations get all but the last sliver of the range
		version = JJ2Anims::Convert(animsPath, pakWriter, false, progress.Narrow(0.0f, 0.95f));
		if (version == JJ2Version::Unknown) {
			return Result::UnsupportedVersion;
		}

		JJ2Data data;
		if (data.Open(fs::CombinePath(sourcePath, "Data.j2d"_s), false)) {
			data.Convert(pakWriter, version);
		}

		progress.Report(1.0f);
		return Result::Success;
	}

	void AssetConverter::ConvertLevels(StringView sourcePath, StringView targetPath, bool recreateAll, ConversionProgress progress)
	{
		ConversionOptions everything;
		ConvertLevels(sourcePath, targetPath, recreateAll, everything, progress);
	}

	void AssetConverter::ConvertLevels(StringView sourcePath, StringView targetPath, bool recreateAll, const ConversionOptions& options, ConversionProgress progress)
	{
		LOGI("Searching for levels...");

		Compatibility::EventConverter eventConverter;

		bool hasChristmasChronicles = fs::IsReadableFile(fs::FindPathCaseInsensitive(fs::CombinePath(sourcePath, "xmas99.j2e"_s)));

		// A level goes into the directory of the episode that leads to it, under its own name, and a level of no episode
		// into "unknown". Which levels an episode leads to can only be answered once every level's file is known.
		HashMap<String, String> levelFiles;
		for (auto item : fs::Directory(fs::FindPathCaseInsensitive(sourcePath), fs::EnumerationOptions::SkipDirectories)) {
			if (fs::GetExtension(item) == "j2l"_s) {
				levelFiles.emplace(NormalizeLevelToken(item), String(item));
			}
		}

		const bool filtering = (options.OriginalsOnly || options.SharewareOnly || options.SkipNonEpisodeLevels);
		HashMap<String, EpisodeClaim> claims;
		HashMap<String, bool> allowedLevels;
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

			HashMap<String, bool> reachable;
			CollectEpisodeLevels(levelFiles, episode.FirstLevel, reachable);
			// "xmas98" is converted as "xmas99", see below
			StringView episodeName = (episode.Name == "xmas98"_s ? "xmas99"_s : StringView(episode.Name));
			const bool isAllowed = (filtering && IsAllowedEpisode(episode.Name, options));
			for (auto& level : reachable) {
				claims[level.first].Add(episodeName);
				if (isAllowed) {
					allowedLevels.emplace(level.first, true);
				}
			}
		}

		HashMap<String, String> levelEpisodes = AssignEpisodeLevels(claims);
		for (const auto& unreachable : UnreachableEpisodeLevels) {
			levelEpisodes.emplace(String(unreachable.first()), String(unreachable.second()));
			if (filtering && IsAllowedEpisode(unreachable.second(), options)) {
				allowedLevels.emplace(String(unreachable.first()), true);
			}
		}

		auto GetLevelEpisode = [&levelEpisodes](StringView levelName) -> StringView {
			auto it = levelEpisodes.find(levelName);
			return (it != levelEpisodes.end() && !it->second.empty() ? StringView(it->second) : "unknown"_s);
		};

		auto LevelTokenConversion = [&levelEpisodes](StringView levelToken) -> Compatibility::JJ2Level::LevelToken {
			for (const auto& special : SpecialLevelTokens) {
				if (levelToken == special.first()) {
					return { {}, special.second() };
				}
			}
			auto it = levelEpisodes.find(levelToken);
			if (it != levelEpisodes.end()) {
				return { it->second, levelToken };
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
		PreviewTilesetCache previewTilesets(sourcePath);
		PlusTextureCache plusTextures(sourcePath);
		auto textureLookup = [&plusTextures](std::int32_t index, std::uint8_t* pixels) {
			return plusTextures.Get(index, pixels);
		};

		// Every episode contributes to "belongs to an episode" (see above), only the allowed ones to "the original
		// game shipped this". Nothing in the data says a multiplayer level is original, so the lists are the only record.
		if (filtering) {
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

		// The tilesets are converted afterwards, once the levels have said which of them are actually used, and
		// a single one of them costs far more than a level does, so they get a good part of the range
		ConversionProgress levelProgress = progress.Narrow(0.0f, 0.6f);
		ConversionProgress tilesetProgress = progress.Narrow(0.6f, 1.0f);

		// The episodes are converted as they're found, the levels are collected and converted afterwards, several at once
		SmallVector<String, 0> pendingLevels;
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
					episode.Convert(fullPath, Death::move(LevelTokenConversion), Death::move(EpisodeNameConversion), Death::move(EpisodePrevNext));
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
							&& (!options.SkipNonEpisodeLevels || levelEpisodes.contains(token));
						if (!allowed) {
							if (options.SkippedLevels != nullptr) {
								options.SkippedLevels->push_back(Death::move(token));
							}
							continue;
						}
					}

					if (!recreateAll) {
						StringUtils::lowercaseInPlace(levelName);
						String fullPath = fs::CombinePath({ episodesPath, GetLevelEpisode(levelName), String(levelName + ".j2l"_s) });
						if (fs::FileExists(fullPath)) {
							continue;
						}
					}

					pendingLevels.push_back(String(item));
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

		// What the levels share is locked, the caches and the lists of what they use
		ConversionMutex sharedLock;
		std::int32_t levelsDone = 0;
		auto lockedTextureLookup = [&sharedLock, &textureLookup](std::int32_t index, std::uint8_t* pixels) {
			ConversionLock lock(sharedLock);
			return textureLookup(index, pixels);
		};
		ForEachParallel(std::int32_t(pendingLevels.size()), [&](std::int32_t index) {
			StringView item = pendingLevels[index];
			Compatibility::JJ2Level level;
			if (level.Open(item, false)) {
				StringView episodeName = GetLevelEpisode(level.LevelName);
				String fullPath = fs::CombinePath({ episodesPath, episodeName, String(level.LevelName + ".j2l"_s) });

				fs::CreateDirectories(fs::GetDirectoryName(fullPath));
				if (episodeName == "unknown"_s) {
					// The list of custom levels shows the levels of no episode, each with its preview
					level.Convert(fullPath, eventConverter, LevelTokenConversion, [&sharedLock, &previewTilesets](StringView name, ArrayView<const std::int32_t> tiles, std::int32_t& tileCount) {
						ConversionLock lock(sharedLock);
						return previewTilesets.Get(name, tiles, tileCount);
					}, lockedTextureLookup);
				} else {
					level.Convert(fullPath, eventConverter, LevelTokenConversion, {}, lockedTextureLookup);
				}

				// Also copy level script file if exists
				StringView foundDot = item.findLastOr('.', item.end());
				String scriptPath = item.prefix(foundDot.begin()) + ".j2as"_s;
				auto adjustedPath = fs::FindPathCaseInsensitive(scriptPath);
				if (fs::IsReadableFile(adjustedPath)) {
					foundDot = fullPath.findLastOr('.', fullPath.end());
					fs::Copy(adjustedPath, String(fullPath.prefix(foundDot.begin()) + ".j2as"_s));
				}

				ConversionLock lock(sharedLock);
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
					usedMusic.emplace(Death::move(music), true);
				}
				// The track a boss fight switches to is named only by the event that starts it
				for (const String& eventMusic : level.GetEventMusic()) {
					usedMusic.emplace(eventMusic, true);
				}
			}

			ConversionLock lock(sharedLock);
			levelProgress.ReportStep(++levelsDone, std::int32_t(pendingLevels.size()));
		});

		if (options.CopyUsedMusic) {
			// Not every track the game plays is named by a level. The main menu picks one of three, and each
			// cinematic plays the file beside its own name, and none of those appear in any ".j2l" - so a
			// tree built only from what the levels ask for left the menu and both cinematics silent on every
			// console.
			static const StringView musicOutsideLevels[] = {
				"menu.j2b"_s, "bonus2.j2b"_s, "bonus3.j2b"_s,	// See MainMenu::PlayMenuMusic()
				"intro.j2b"_s, "ending.j2b"_s					// See Cinematics::Initialize()
			};
			for (StringView music : musicOutsideLevels) {
				// Only what this data set actually ships. Unlike the tracks a level names - where a missing
				// file means the conversion is incomplete and the warning below is the point - these are
				// simply absent from some editions: the Shareware Demo has no ending cinematic and so no
				// "ending.j2b". Seeding them unconditionally warned about content that is correctly not
				// there, in a log people read to decide whether the conversion worked.
				if (fs::IsReadableFile(fs::FindPathCaseInsensitive(fs::CombinePath(sourcePath, music)))) {
					usedMusic.emplace(music, true);
				}
			}
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

			SmallVector<String, 0> tilesetNames;
			for (auto& pair : usedTilesets) {
				tilesetNames.push_back(pair.first);
			}
			std::int32_t tilesetsDone = 0;
			ForEachParallel(std::int32_t(tilesetNames.size()), [&](std::int32_t index) {
				const String& name = tilesetNames[index];
				String tilesetPath = fs::CombinePath(sourcePath, String(name + ".j2t"_s));
				auto adjustedPath = fs::FindPathCaseInsensitive(tilesetPath);
				if (fs::IsReadableFile(adjustedPath)) {
					Compatibility::JJ2Tileset tileset;
					if (tileset.Open(adjustedPath, false)) {
						tileset.Convert(fs::CombinePath({ tilesetsPath, String(name + ".j2t"_s) }));
					}
				}
				ConversionLock lock(sharedLock);
				tilesetProgress.ReportStep(++tilesetsDone, std::int32_t(tilesetNames.size()));
			});
		}

		progress.Report(1.0f);
	}
}
