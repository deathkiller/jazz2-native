#include "FilePickers.h"

#if defined(WITH_EDITOR)

#include "ProgressDialog.h"
#include "../Files/ContentTileSetLibrary.h"
#include "../Tools/EditorContext.h"
#include "../../UI/Windows/Dialogs.h"
#include "../../UI/Windows/Surface.h"
#include "../UI/EditorUi.h"
#include "../../ContentResolver.h"
#include "../../Formats/LevelPreview.h"
#include "../../Formats/TileSetFile.h"

#include "../../../nCine/I18n.h"
#if defined(WITH_THREADS)
#	include "../../../nCine/Threading/ThreadSync.h"
#endif

#include <Containers/DateTime.h>
#include <Containers/StringConcatenable.h>
#include <Containers/StringUtils.h>
#include <IO/FileSystem.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>
#include <unordered_map>

using namespace Death::Containers::Literals;
using namespace Death::IO;

namespace Jazz2::Editor
{
	namespace
	{
		// Close to the white of the other icons, only tinted
		const Colorf ScriptedLevelColor(0.72f, 0.83f, 0.98f, 1.0f);

		enum class FileType : std::uint8_t
		{
			Level = 1,
			TileSet,
			Music
		};

		enum class FileStatus : std::uint8_t
		{
			Unrecognized,
			Valid,
			// A level exported only to be played
			PlayOnly,
			// A tile set with true color tiles
			TrueColor
		};

		struct CachedStatus
		{
			String Path;
			std::int64_t Size;
			std::int64_t Time;
			FileType Type;
			FileStatus Status;
		};

		std::unordered_map<std::uint64_t, CachedStatus>& GetStatusCache()
		{
			static std::unordered_map<std::uint64_t, CachedStatus> cache;
			return cache;
		}

		// The cache is shared by the user interface and the thread of a file dialog reading the files
#if defined(WITH_THREADS)
		nCine::Mutex& GetStatusLock()
		{
			static nCine::Mutex lock;
			return lock;
		}

		struct StatusLockGuard
		{
			StatusLockGuard() { GetStatusLock().Lock(); }
			~StatusLockGuard() { GetStatusLock().Unlock(); }
			StatusLockGuard(const StatusLockGuard&) = delete;
			StatusLockGuard& operator=(const StatusLockGuard&) = delete;
		};
#else
		struct StatusLockGuard
		{
			StatusLockGuard() {}
		};
#endif

		std::uint64_t GetStatusKey(StringView path, FileType type)
		{
			// FNV-1a
			std::uint64_t hash = 14695981039346656037ull ^ std::uint64_t(type);
			for (char c : path) {
				hash = (hash ^ std::uint8_t(c)) * 1099511628211ull;
			}
			return hash;
		}

		void SetCachedStatus(StringView path, FileType type, FileStatus status)
		{
			std::int64_t size = fs::GetFileSize(path);
			std::int64_t time = fs::GetLastModificationTime(path).ToUnixMilliseconds();
			StatusLockGuard guard;
			auto& cache = GetStatusCache();
			if (cache.size() >= 4096) {
				cache.clear();
			}
			cache[GetStatusKey(path, type)] = { String(path), size, time, type, status };
		}

		// The status of a file read once, again only after the file changed
		template<class Func>
		FileStatus GetCachedStatus(StringView path, FileType type, Func&& read)
		{
			std::int64_t size = fs::GetFileSize(path);
			std::int64_t time = fs::GetLastModificationTime(path).ToUnixMilliseconds();
			{
				StatusLockGuard guard;
				auto& cache = GetStatusCache();
				auto it = cache.find(GetStatusKey(path, type));
				if (it != cache.end() && it->second.Type == type && it->second.Path == path && it->second.Size == size && it->second.Time == time) {
					return it->second.Status;
				}
			}
			FileStatus status = read(path);
			SetCachedStatus(path, type, status);
			return status;
		}

		String FormatFileSize(std::int64_t size)
		{
			return _fx("editor", "{} KB", (std::max<std::int64_t>(size, 0) + 1023) / 1024);
		}

		String GetScriptPath(StringView levelPath)
		{
			String path = fs::CombinePath(fs::GetDirectoryName(levelPath), String(fs::GetFileNameWithoutExtension(levelPath) + ".j2as"_s));
			String found = fs::FindPathCaseInsensitive(path);
			return (!found.empty() && fs::FileExists(found) ? found : String());
		}

		FileStatus ReadLevelStatus(StringView path)
		{
			auto s = fs::Open(path, FileAccess::Read);
			Formats::LevelHeader header;
			if (!s->IsValid() || !Formats::LevelFile::ReadHeader(*s, header)) {
				return FileStatus::Unrecognized;
			}
			Formats::LevelReader reader(*s, header);
			Formats::LevelProperties properties;
			reader.ReadProperties(properties);
			if (!reader.IsValid()) {
				return FileStatus::Unrecognized;
			}
			return ((header.Flags & LevelFlags::NotEditable) == LevelFlags::NotEditable ? FileStatus::PlayOnly : FileStatus::Valid);
		}

		FileKind MakeLevelKind(FileStatus status, bool scripted)
		{
			FileKind kind;
			if (status == FileStatus::Unrecognized) {
				kind.Icon = EditorIcon::BrokenFile;
				kind.Color = Theme::Danger;
				// TRANSLATORS: Tooltip of a file in the list of the file dialog picking a level
				kind.Description = _x("editor", "Not a level of this version of the game, or it's damaged");
				return kind;
			}
			kind.Icon = (scripted ? EditorIcon::ScriptedLevelFile : EditorIcon::LevelFile);
			kind.Color = (scripted ? ScriptedLevelColor : Theme::IconColor);
			if (status == FileStatus::PlayOnly) {
				// TRANSLATORS: Tooltips of a file in the list of the file dialog picking a level
				kind.Description = (scripted ? _x("editor", "Level with a script, exported only to be played") : _x("editor", "Level exported only to be played"));
			} else {
				kind.Description = (scripted ? _x("editor", "Level with a script") : _x("editor", "Level"));
			}
			return kind;
		}

		bool GetLevelKind(StringView path, FileKind& kind)
		{
			FileStatus status = GetCachedStatus(path, FileType::Level, ReadLevelStatus);
			kind = MakeLevelKind(status, status != FileStatus::Unrecognized && !GetScriptPath(path).empty());
			return true;
		}

		// An empty value isn't shown at all
		void AddLevelRow(FileInfo& info, StringView label, StringView value)
		{
			if (!value.empty()) {
				info.Rows.push_back({ label, value });
			}
		}

		// Values of the next level that aren't levels, as the game takes them when a level ends (see GameEventHandler::ChangeLevel())
		void GetSpecialLevels(SmallVectorImpl<FileSpecialEntry>& entries)
		{
			// TRANSLATORS: Special value at the top of the file dialog picking the next level, the episode ends instead
			entries.push_back({ _x("editor", "End of the episode"), ":end"_s, EditorIcon::RaceFlag,
				_x("editor", "The episode ends here, the next episode follows if there's one") });
			// TRANSLATORS: Special value at the top of the file dialog picking the next level, the game ends with the ending and the credits
			entries.push_back({ _x("editor", "End of the game"), ":credits"_s, EditorIcon::ScreenFrame,
				_x("editor", "The game ends here, the ending and the credits are shown") });
		}

		// A level that follows, a special value by its name too
		void AddLevelReferenceRow(FileInfo& info, StringView label, StringView value)
		{
			if (value.empty()) {
				return;
			}
			SmallVector<FileSpecialEntry, 2> special;
			GetSpecialLevels(special);
			for (const FileSpecialEntry& entry : special) {
				if (entry.Value == value) {
					info.Rows.push_back({ label, String(entry.Label + " ("_s + value + ")"_s) });
					return;
				}
			}
			info.Rows.push_back({ label, value });
		}

		bool GetLevelInfo(StringView path, FileInfo& info)
		{
			String scriptPath = GetScriptPath(path);
			info.HasKind = true;

			auto s = fs::Open(path, FileAccess::Read);
			Formats::LevelHeader header;
			if (!s->IsValid() || !Formats::LevelFile::ReadHeader(*s, header)) {
				SetCachedStatus(path, FileType::Level, FileStatus::Unrecognized);
				info.Kind = MakeLevelKind(FileStatus::Unrecognized, false);
				info.Rows.push_back({ {}, _x("editor", "The file isn't a level of this version of the game"), Theme::TextDanger });
				return true;
			}
			Formats::LevelReader reader(*s, header);
			Formats::LevelProperties properties;
			reader.ReadProperties(properties);
			if (!reader.IsValid()) {
				SetCachedStatus(path, FileType::Level, FileStatus::Unrecognized);
				info.Kind = MakeLevelKind(FileStatus::Unrecognized, false);
				info.Rows.push_back({ {}, _x("editor", "The level is damaged"), Theme::TextDanger });
				return true;
			}
			// All of it is read, the preview is at its end, so a damaged level is told apart too
			SmallVector<Formats::ChunkRecord, 0> chunks;
			bool complete = reader.SkipToChunks();
			if (complete) {
				reader.ReadSelectedChunks(chunks, [](std::uint32_t tag, std::uint32_t) {
					return (tag == Formats::LevelFile::PreviewChunk);
				});
				complete = reader.IsValid();
			}
			for (const Formats::ChunkRecord& chunk : chunks) {
				if (chunk.Tag == Formats::LevelFile::PreviewChunk) {
					Formats::LevelPreview::Decode(chunk.Data, info.Rgba, info.ImageSize);
				}
			}

			bool playOnly = ((header.Flags & LevelFlags::NotEditable) == LevelFlags::NotEditable);
			FileStatus status = (!complete ? FileStatus::Unrecognized : (playOnly ? FileStatus::PlayOnly : FileStatus::Valid));
			SetCachedStatus(path, FileType::Level, status);
			info.Kind = MakeLevelKind(status, !scriptPath.empty());

			AddLevelRow(info, _x("editor", "Name"), properties.DisplayName);
			AddLevelRow(info, _x("editor", "Author"), properties.Author);
			AddLevelRow(info, _x("editor", "Music"), properties.Music);
			if (!properties.DefaultTileSet.empty() && ContentTileSetLibrary::FindTileSetFile(properties.DefaultTileSet).empty()) {
				// TRANSLATORS: Tile set of the selected level in the file dialog, {} is its name
				info.Rows.push_back({ _x("editor", "Tile set"), _fx("editor", "{} - not found", properties.DefaultTileSet), Theme::TextDanger });
			} else {
				AddLevelRow(info, _x("editor", "Tile set"), properties.DefaultTileSet);
			}
			AddLevelReferenceRow(info, _x("editor", "Next level"), properties.NextLevel);
			AddLevelReferenceRow(info, _x("editor", "Secret level"), properties.SecretLevel);
			AddLevelReferenceRow(info, _x("editor", "Bonus level"), properties.BonusLevel);
			// Only a level with a script has the row, as only its icon has the badge
			// TRANSLATORS: Label next to the selected level in the file dialog, the script file of the level
			AddLevelRow(info, _x("editor", "Script"), fs::GetFileName(scriptPath));
			if (!complete) {
				info.Rows.push_back({ {}, _x("editor", "The level is damaged"), Theme::TextDanger });
			} else if (playOnly) {
				info.Rows.push_back({ {}, _x("editor", "Exported only to be played"), Theme::TextWarning });
			}
			return true;
		}

		FileStatus ReadTileSetStatus(StringView path)
		{
			auto s = fs::Open(path, FileAccess::Read);
			Formats::TileSetHeader header;
			if (!s->IsValid() || !Formats::TileSetFile::ReadHeader(*s, header)) {
				return FileStatus::Unrecognized;
			}
			// The image has RGBA only if a tile is a true color one, see TileSetFile::Write()
			return (header.ChannelCount == 4 ? FileStatus::TrueColor : FileStatus::Valid);
		}

		FileKind MakeTileSetKind(FileStatus status)
		{
			FileKind kind;
			if (status == FileStatus::Unrecognized) {
				kind.Icon = EditorIcon::BrokenFile;
				kind.Color = Theme::Danger;
				// TRANSLATORS: Tooltip of a file in the list of the file dialog picking a tile set
				kind.Description = _x("editor", "Not a tile set of this version of the game, or it's damaged");
			} else if (status == FileStatus::TrueColor) {
				kind.Icon = EditorIcon::TrueColorTileSetFile;
				// TRANSLATORS: Tooltip of a file in the list of the file dialog picking a tile set, it has tiles of any colors, not only those of a palette
				kind.Description = _x("editor", "Tile set with true color tiles");
			} else {
				kind.Icon = EditorIcon::TileSetFile;
				// TRANSLATORS: Tooltip of a file in the list of the file dialog picking a tile set
				kind.Description = _x("editor", "Tile set");
			}
			return kind;
		}

		bool GetTileSetKind(StringView path, FileKind& kind)
		{
			kind = MakeTileSetKind(GetCachedStatus(path, FileType::TileSet, ReadTileSetStatus));
			return true;
		}

		// The first rows of tiles at half their size, ten in a row as the original tile sets have them
		void MakeTileSetPreview(const Formats::TileSetData& data, FileInfo& info)
		{
			constexpr std::int32_t Columns = 10, MaxRows = 6, Cell = 16;
			std::int32_t rows = std::min<std::int32_t>(MaxRows, (data.TileCount + Columns - 1) / Columns);
			if (rows <= 0) {
				return;
			}
			std::int32_t width = Columns * Cell, height = rows * Cell;
			info.ImageSize = Vector2i(width, height);
			info.Rgba.resize_for_overwrite(std::size_t(width) * std::size_t(height) * 4);
			for (std::int32_t y = 0; y < height; y++) {
				for (std::int32_t x = 0; x < width; x++) {
					// Transparent pixels on a dark checkerboard
					bool light = (((x / 4) + (y / 4)) & 1) != 0;
					float r = (light ? 48.0f : 38.0f), g = (light ? 44.0f : 34.0f), b = (light ? 60.0f : 48.0f);
					std::int32_t tile = (y / Cell) * Columns + (x / Cell);
					if (tile < data.TileCount) {
						const std::uint8_t* pixels = &data.Images[std::size_t(tile) * Formats::TileSetData::ImageBytesPerTile];
						bool trueColor = (data.TrueColor[tile] != 0);
						float sumR = 0.0f, sumG = 0.0f, sumB = 0.0f, sumA = 0.0f;
						for (std::int32_t j = 0; j < 2; j++) {
							for (std::int32_t i = 0; i < 2; i++) {
								std::int32_t p = ((y % Cell) * 2 + j) * 32 + (x % Cell) * 2 + i;
								std::uint32_t color;
								if (trueColor) {
									color = pixels[p * 4] | (pixels[p * 4 + 1] << 8) | (pixels[p * 4 + 2] << 16) | (std::uint32_t(pixels[p * 4 + 3]) << 24);
								} else {
									color = (pixels[p] != 0 ? (data.Palette[pixels[p]] | 0xFF000000u) : 0);
								}
								float a = (color >> 24) / 255.0f;
								sumR += (color & 0xFF) * a;
								sumG += ((color >> 8) & 0xFF) * a;
								sumB += ((color >> 16) & 0xFF) * a;
								sumA += a;
							}
						}
						float a = sumA / 4.0f;
						r = r * (1.0f - a) + sumR / 4.0f;
						g = g * (1.0f - a) + sumG / 4.0f;
						b = b * (1.0f - a) + sumB / 4.0f;
					}
					std::uint8_t* out = &info.Rgba[(std::size_t(y) * std::size_t(width) + std::size_t(x)) * 4];
					out[0] = std::uint8_t(std::clamp(r, 0.0f, 255.0f));
					out[1] = std::uint8_t(std::clamp(g, 0.0f, 255.0f));
					out[2] = std::uint8_t(std::clamp(b, 0.0f, 255.0f));
					out[3] = 255;
				}
			}
		}

		bool GetTileSetInfo(StringView path, FileInfo& info)
		{
			info.HasKind = true;
			Formats::TileSetHeader header;
			{
				auto s = fs::Open(path, FileAccess::Read);
				if (!s->IsValid() || !Formats::TileSetFile::ReadHeader(*s, header)) {
					SetCachedStatus(path, FileType::TileSet, FileStatus::Unrecognized);
					info.Kind = MakeTileSetKind(FileStatus::Unrecognized);
					info.Rows.push_back({ {}, _x("editor", "The file isn't a tile set of this version of the game"), Theme::TextDanger });
					return true;
				}
			}
			auto s = fs::Open(path, FileAccess::Read);
			Formats::TileSetData data;
			bool complete = (s->IsValid() && Formats::TileSetFile::Read(*s, data));
			bool trueColor = (complete ? data.HasTrueColorTiles() : header.ChannelCount == 4);
			FileStatus status = (!complete ? FileStatus::Unrecognized : (trueColor ? FileStatus::TrueColor : FileStatus::Valid));
			SetCachedStatus(path, FileType::TileSet, status);
			info.Kind = MakeTileSetKind(status);
			if (complete) {
				MakeTileSetPreview(data, info);
			}

			// TRANSLATORS: Label next to the selected tile set in the file dialog, the number of its tiles
			char count[16];
			std::int32_t length = std::snprintf(count, sizeof(count), "%d", std::int32_t(header.TileCount));
			info.Rows.push_back({ _x("editor", "Tiles"), String(count, std::size_t(std::max<std::int32_t>(length, 0))) });
			// TRANSLATORS: Label next to the selected tile set in the file dialog, and whether its tiles are 8-bit or true color
			info.Rows.push_back({ _x("editor", "Colors"), trueColor ? _x("editor", "True color") : _x("editor", "8-bit") });
			// TRANSLATORS: Label next to the selected file in the file dialog, how large the file is
			info.Rows.push_back({ _x("editor", "Size"), FormatFileSize(fs::GetFileSize(path)) });
			if (!complete) {
				info.Rows.push_back({ {}, _x("editor", "The tile set is damaged"), Theme::TextDanger });
			}
			return true;
		}

		// Name of the format and the title of a song from the start of its file, returns `false` if it's not of its format
		bool ReadMusicHeader(StringView path, StringView& format, String& title)
		{
			std::uint8_t data[1084];
			std::int64_t length = 0;
			{
				auto s = fs::Open(path, FileAccess::Read);
				if (!s->IsValid()) {
					return false;
				}
				length = s->Read(data, std::int64_t(sizeof(data)));
			}
			auto hasAt = [&](std::size_t offset, const char* signature) {
				std::size_t size = std::strlen(signature);
				return (length >= std::int64_t(offset + size) && std::memcmp(data + offset, signature, size) == 0);
			};
			auto readTitle = [&](std::size_t offset, std::size_t size) {
				if (length < std::int64_t(offset + size)) {
					return;
				}
				String result{NoInit, size};
				std::size_t end = 0;
				for (std::size_t i = 0; i < size && data[offset + i] != 0; i++) {
					std::uint8_t c = data[offset + i];
					// Old trackers wrote it in code pages of their own, only ASCII is kept
					result[i] = (c >= 32 && c < 127 ? char(c) : ' ');
					end = i + 1;
				}
				title = String(result.prefix(end).trimmed());
			};

			// Names of formats aren't translated
			String extension = fs::GetExtension(path);
			if (extension == "it"_s) {
				format = "Impulse Tracker"_s;
				readTitle(4, 26);
				return hasAt(0, "IMPM");
			} else if (extension == "xm"_s) {
				format = "FastTracker 2"_s;
				readTitle(17, 20);
				return hasAt(0, "Extended Module: ");
			} else if (extension == "s3m"_s) {
				format = "Scream Tracker 3"_s;
				readTitle(0, 28);
				return hasAt(44, "SCRM");
			} else if (extension == "mod"_s) {
				// The signature differs by the tracker, the oldest ones have none
				format = "ProTracker"_s;
				readTitle(0, 20);
				return (length >= 600);
			} else if (extension == "j2b"_s) {
				format = "Jazz Jackrabbit 2"_s;
				return hasAt(0, "MUSE");
			} else if (extension == "mo3"_s) {
				format = "MO3"_s;
				return hasAt(0, "MO3");
			} else if (extension == "ogg"_s) {
				format = "Ogg Vorbis"_s;
				return hasAt(0, "OggS");
			} else if (extension == "wav"_s) {
				format = "WAVE"_s;
				return (hasAt(0, "RIFF") && hasAt(8, "WAVE"));
			}
			return false;
		}


		FileKind MakeMusicKind(FileStatus status)
		{
			FileKind kind;
			if (status == FileStatus::Unrecognized) {
				kind.Icon = EditorIcon::BrokenFile;
				kind.Color = Theme::Danger;
				// TRANSLATORS: Tooltip of a file in the list of the file dialog picking music
				kind.Description = _x("editor", "Not music of the format its name tells, or it's damaged");
			} else {
				kind.Icon = EditorIcon::MusicFile;
				// TRANSLATORS: Tooltip of a file in the list of the file dialog picking music
				kind.Description = _x("editor", "Music");
			}
			return kind;
		}

		bool GetMusicKind(StringView path, FileKind& kind)
		{
			// Told by the name only, the dialog lists only known extensions; the selected file is looked into
			static_cast<void>(path);
			kind = MakeMusicKind(FileStatus::Valid);
			return true;
		}

		bool GetMusicInfo(StringView path, FileInfo& info)
		{
			StringView format;
			String title;
			bool valid = ReadMusicHeader(path, format, title);
			FileStatus status = (valid ? FileStatus::Valid : FileStatus::Unrecognized);
			SetCachedStatus(path, FileType::Music, status);
			info.HasKind = true;
			info.Kind = MakeMusicKind(status);
			// TRANSLATORS: Labels next to the selected music in the file dialog, its format and the title of the song
			info.Rows.push_back({ _x("editor", "Format"), format });
			if (!title.empty()) {
				info.Rows.push_back({ _x("editor", "Title"), title });
			}
			info.Rows.push_back({ _x("editor", "Size"), FormatFileSize(fs::GetFileSize(path)) });
			if (!valid) {
				info.Rows.push_back({ {}, _x("editor", "The file isn't of this format, or it's damaged"), Theme::TextDanger });
			}
			return true;
		}

		// A level of the same episode is written without the episode, as the original levels have it
		String MakeLevelReference(StringView levelName, StringView currentLevelName)
		{
			auto picked = levelName.partition('/');
			auto current = currentLevelName.partition('/');
			if (!picked[1].empty() && !current[1].empty() && picked[0] == current[0]) {
				return picked[2];
			}
			return levelName;
		}

		// Places tile sets are looked for, the first one that has a tile set is the one the game uses
#if defined(WITH_LEVEL_PACKS)
		// Files of level packs are stored as "<name>~<hash>.<extension>", shown by their own name
		String GetStoredFileLabel(StringView path, bool isDirectory)
		{
			StringView name = fs::GetFileName(path);
			StringView tilde = name.findLast('~');
			if (isDirectory || tilde == nullptr) {
				return {};
			}
			StringView dot = name.findLastOr('.', name.end());
			return String(name.prefix(tilde.begin()) + name.suffix(dot.begin()));
		}
#endif

		void GetTileSetRoots(SmallVectorImpl<FileRoot>& roots)
		{
			auto& resolver = ContentResolver::Get();
			StringView userContent = resolver.GetUserContentPath();
			if (!userContent.empty()) {
				// TRANSLATORS: Name of a place tile sets are picked from, the folder of the content added by the user
				roots.push_back({ _x("editor", "User content"), fs::CombinePath(userContent, "Tilesets"_s), false });
			}
			// TRANSLATORS: Name of a place tile sets are picked from, the folder of the content of the game
			roots.push_back({ _x("editor", "Content"), fs::CombinePath(resolver.GetContentPath(), "Tilesets"_s), false });
			// TRANSLATORS: Name of a place tile sets are picked from, the folder of the files converted from the original game
			roots.push_back({ _x("editor", "Cache"), fs::CombinePath(resolver.GetCachePath(), "Tilesets"_s), false });
#if defined(WITH_LEVEL_PACKS)
			// TRANSLATORS: Name of a place tile sets are picked from, the tile sets of the installed level packs
			FileRoot levelPacks{ _x("editor", "Level packs"), fs::CombinePath(resolver.GetLevelPacksCachePath(), "Tilesets"_s), false };
			levelPacks.LabelFunc = GetStoredFileLabel;
			roots.push_back(Death::move(levelPacks));
#endif
			for (std::size_t i = 0; i < roots.size(); ) {
				if (!fs::DirectoryExists(roots[i].Path)) {
					roots.erase(roots.begin() + i);
				} else {
					i++;
				}
			}
		}
	}

	void SetupLevelFileDialog(FileDialog& dialog)
	{
		dialog.KindFunc = [](StringView path, FileKind& kind) { return GetLevelKind(path, kind); };
		dialog.InfoFunc = [](StringView path, FileInfo& info) { return GetLevelInfo(path, info); };		// The whole level is read for its details
		dialog.Asynchronous = true;
	}

	void SetupTileSetFileDialog(FileDialog& dialog)
	{
		dialog.KindFunc = [](StringView path, FileKind& kind) { return GetTileSetKind(path, kind); };
		dialog.InfoFunc = [](StringView path, FileInfo& info) { return GetTileSetInfo(path, info); };		// The whole tile set is read for its preview
		dialog.Asynchronous = true;
	}

	void SetupMusicFileDialog(FileDialog& dialog)
	{
		dialog.KindFunc = [](StringView path, FileKind& kind) { return GetMusicKind(path, kind); };
		dialog.InfoFunc = [](StringView path, FileInfo& info) { return GetMusicInfo(path, info); };	}

	void ShowLevelPicker(IEditorContext& session, StringView title, StringView current, Function<void(StringView)>&& onPicked)
	{
		Surface* surface = session.GetSurface();
		if (surface == nullptr) {
			return;
		}
		IEditorContext* s = &session;
		SmallVector<FileRoot, 4> roots;
		session.GetFileRoots(false, roots);
		// Opens where the level is now, or next to the level being edited, the next level is usually there
		StringView filePath = session.GetFilePath();
		String directory = (!filePath.empty() ? String(fs::GetDirectoryName(filePath)) : String());
		String fileName;
		StringView episode = session.GetLevelName().partition('/')[0];
		if (!current.empty() && current[0] != ':' && (current.contains('/') || !episode.empty())) {
			String levelName = (current.contains('/') ? String(current) : String(episode + "/"_s + current));
			for (const FileRoot& root : roots) {
				String path = fs::FindPathCaseInsensitive(fs::CombinePath(root.Path, String(levelName + ".j2l"_s)));
				if (fs::IsReadableFile(path)) {
					directory = fs::GetDirectoryName(path);
					fileName = fs::GetFileName(path);
					break;
				}
			}
		}
		auto sharedOnPicked = std::make_shared<Function<void(StringView)>>(Death::move(onPicked));
		FileDialog* dialog = FileDialog::Show(*surface, title, FileDialogMode::Open, Death::move(roots), ".j2l"_s, directory, fileName,
			[s, sharedOnPicked](StringView path) {
				// The level is looked up by its name, so it doesn't matter which root it was picked from
				(*sharedOnPicked)(MakeLevelReference(IEditorContext::MakeLevelName(path), s->GetLevelName()));
			}, false);
		SetupLevelFileDialog(*dialog);
		// The values the game takes instead of a level, stored as they are
		SmallVector<FileSpecialEntry, 2> special;
		GetSpecialLevels(special);
		dialog->SetSpecialEntries(Death::move(special), current, [sharedOnPicked](StringView value) {
			(*sharedOnPicked)(value);
		});
	}

	void ShowTileSetPicker(IEditorContext& session, StringView title, StringView current, Function<void(StringView)>&& onPicked)
	{
		Surface* surface = session.GetSurface();
		if (surface == nullptr) {
			return;
		}
		SmallVector<FileRoot, 4> roots;
		GetTileSetRoots(roots);
		SmallVector<String, 4> rootPaths;
		for (const FileRoot& root : roots) {
			rootPaths.push_back(root.Path);
		}
		String directory, fileName;
		String currentPath = ContentTileSetLibrary::FindTileSetFile(current);
		if (!current.empty() && !currentPath.empty()) {
			directory = fs::GetDirectoryName(currentPath);
			fileName = fs::GetFileName(currentPath);
		}
		FileDialog* dialog = FileDialog::Show(*surface, title, FileDialogMode::Open, Death::move(roots), ".j2t"_s, directory, fileName,
			[rootPaths = Death::move(rootPaths), onPicked = Death::move(onPicked)](StringView path) mutable {
				// Tile sets are referred to by their path relative to "Tilesets", without the extension
				StringView name = fs::GetFileName(path);
				for (const String& rootPath : rootPaths) {
					// As the file system compares paths, the dialog may give the path in another case
					if (IsPathInside(path, rootPath) && path.size() > rootPath.size()) {
						name = path.exceptPrefix(rootPath.size());
						while (!name.empty() && (name[0] == '/' || name[0] == '\\')) {
							name = name.exceptPrefix(1);
						}
						break;
					}
				}
				String result = fs::FromNativeSeparators(name);
				if (StringUtils::lowercase(result).hasSuffix(".j2t"_s)) {
					result = result.exceptSuffix(std::size_t(4));
				}
				onPicked(result);
			}, false);
		SetupTileSetFileDialog(*dialog);
	}

	void ShowMusicPicker(IEditorContext& session, StringView title, StringView current, Function<void(StringView)>&& onPicked)
	{
		Surface* surface = session.GetSurface();
		if (surface == nullptr) {
			return;
		}
		// As ContentResolver::GetMusic() looks for it, the value is the path relative to one of them
		auto& resolver = ContentResolver::Get();
		SmallVector<FileRoot, 4> roots;
		StringView userContent = resolver.GetUserContentPath();
		if (!userContent.empty()) {
			// TRANSLATORS: Place in the file dialog choosing the music of the level, the folder of content added by the user
			roots.push_back({ _x("editor", "User content"), fs::CombinePath(userContent, "Music"_s), false });
		}
		StringView content = resolver.GetContentPath();
		if (!content.empty()) {
			// TRANSLATORS: Place in the file dialog choosing the music of the level, the folder of content of the game
			roots.push_back({ _x("editor", "Content"), fs::CombinePath(content, "Music"_s), false });
		}
		StringView source = resolver.GetSourcePath();
		if (!source.empty()) {
			// Without the trailing separator, so the paths of files in it can be made relative to it
			// TRANSLATORS: Place in the file dialog choosing the music of the level, the folder of the original game
			roots.push_back({ _x("editor", "Original game"), String(source.trimmedSuffix("/\\"_s)), false });
		}
#if defined(WITH_LEVEL_PACKS)
		String levelPacksPath = resolver.GetLevelPacksPath(false);
		if (!levelPacksPath.empty()) {
			// TRANSLATORS: Place in the file dialog choosing the music of the level, the music of the installed level packs
			FileRoot levelPacks{ _x("editor", "Level packs"), fs::CombinePath(levelPacksPath, "Files"_s), false };
			levelPacks.LabelFunc = GetStoredFileLabel;
			roots.push_back(Death::move(levelPacks));
		}
#endif
		// Opens where the music is now, the first place that has it is the one that is played
		String directory, fileName;
		SmallVector<String, 4> rootPaths;
		for (std::size_t i = 0; i < roots.size(); ) {
			if (!fs::DirectoryExists(roots[i].Path)) {
				roots.erase(roots.begin() + i);
				continue;
			}
			if (directory.empty() && !current.empty()) {
				String path = fs::FindPathCaseInsensitive(fs::CombinePath(roots[i].Path, current));
				if (fs::IsReadableFile(path)) {
					directory = fs::GetDirectoryName(path);
					fileName = fs::GetFileName(path);
				}
			}
			rootPaths.push_back(roots[i].Path);
			i++;
		}
		FileDialog* dialog = FileDialog::Show(*surface, title, FileDialogMode::Open, Death::move(roots), ".j2b;.it;.mo3;.mod;.s3m;.xm;.ogg;.wav"_s, directory, fileName,
			[rootPaths = Death::move(rootPaths), onPicked = Death::move(onPicked)](StringView path) mutable {
				String value = fs::GetFileName(path);
				for (const String& rootPath : rootPaths) {
					if (path.hasPrefix(rootPath)) {
						StringView rest = path.exceptPrefix(rootPath.size());
						while (!rest.empty() && (rest[0] == '/' || rest[0] == '\\')) {
							rest = rest.exceptPrefix(1);
						}
						value = fs::FromNativeSeparators(rest);
						break;
					}
				}
				onPicked(value);
			}, false);
		SetupMusicFileDialog(*dialog);
	}
}

#endif
