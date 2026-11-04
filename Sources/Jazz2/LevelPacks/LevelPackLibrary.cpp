#include "LevelPackLibrary.h"

#if defined(WITH_LEVEL_PACKS)

#include "../ContentResolver.h"
#include "../PreferencesCache.h"
#include "../Compatibility/AssetConverter.h"
#include "../Compatibility/JJ2Anims.h"
#include "../Compatibility/JJ2Tileset.h"
#include "../Utils/ZipArchive.h"
#include "../../nCine/Base/HashMap.h"

#include <Containers/DateTime.h>
#include <Containers/StringConcatenable.h>
#include <Containers/StringUtils.h>
#include <Cryptography/Sha256.h>
#include <IO/FileSystem.h>

#include <algorithm>
#include <mutex>

using namespace Death::Containers;
using namespace Death::Containers::Literals;
using namespace Death::IO;
using namespace Jazz2::Compatibility;
using namespace Jazz2::Utils;

namespace Jazz2::LevelPacks
{
	namespace
	{
		constexpr std::uint32_t MaxArchiveEntries = 20000;
		constexpr std::uint64_t MaxFileSize = 128 * 1024 * 1024;
		constexpr std::uint64_t MaxTotalSize = 1024ull * 1024 * 1024;
		// A file that unpacks to this many times its packed size is suspicious, but only when it's large
		constexpr std::uint64_t MaxCompressionRatio = 200;
		constexpr std::uint64_t CompressionRatioThreshold = 16 * 1024 * 1024;
		constexpr std::size_t KeyHashLength = 16;
		constexpr std::size_t MaxIdLength = 48;

		struct AllowedExtension
		{
			StringView Extension;
			LevelPackFileKind Kind;
		};

		static const AllowedExtension AllowedExtensions[] = {
			{ "j2l"_s, LevelPackFileKind::Level }, { "j2t"_s, LevelPackFileKind::TileSet },
			{ "j2e"_s, LevelPackFileKind::Episode }, { "j2pe"_s, LevelPackFileKind::Episode },
			{ "j2as"_s, LevelPackFileKind::Script }, { "asc"_s, LevelPackFileKind::Script }, { "mut"_s, LevelPackFileKind::Script },
			{ "j2b"_s, LevelPackFileKind::Music }, { "it"_s, LevelPackFileKind::Music }, { "s3m"_s, LevelPackFileKind::Music },
			{ "mod"_s, LevelPackFileKind::Music }, { "xm"_s, LevelPackFileKind::Music }, { "mo3"_s, LevelPackFileKind::Music },
			{ "umx"_s, LevelPackFileKind::Music }, { "ogg"_s, LevelPackFileKind::Music }, { "mp3"_s, LevelPackFileKind::Music },
			{ "wav"_s, LevelPackFileKind::Other }, { "j2a"_s, LevelPackFileKind::Other }, { "j2p"_s, LevelPackFileKind::Other },
			{ "png"_s, LevelPackFileKind::Other }, { "bmp"_s, LevelPackFileKind::Other }, { "pal"_s, LevelPackFileKind::Other },
			{ "txt"_s, LevelPackFileKind::Other }
		};

		static const StringView ReservedNames[] = {
			"con"_s, "prn"_s, "aux"_s, "nul"_s,
			"com1"_s, "com2"_s, "com3"_s, "com4"_s, "com5"_s, "com6"_s, "com7"_s, "com8"_s, "com9"_s,
			"lpt1"_s, "lpt2"_s, "lpt3"_s, "lpt4"_s, "lpt5"_s, "lpt6"_s, "lpt7"_s, "lpt8"_s, "lpt9"_s
		};

		bool TryGetFileKind(StringView extension, LevelPackFileKind& kind)
		{
			for (const auto& allowed : AllowedExtensions) {
				if (extension == allowed.Extension) {
					kind = allowed.Kind;
					return true;
				}
			}
			return false;
		}

		// The name of a file of a pack is its last path component in lowercase, anything else is refused
		bool IsSafeFileName(StringView name)
		{
			if (name.empty() || name.size() > 200 || name[0] == '.' || name[0] == ' ' || name[name.size() - 1] == '.' || name[name.size() - 1] == ' ') {
				return false;
			}
			for (char c : name) {
				if (std::uint8_t(c) < 0x20 || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|' || c == '/' || c == '\\') {
					return false;
				}
			}
			StringView stem = name.partition('.')[0];
			for (StringView reserved : ReservedNames) {
				if (stem == reserved) {
					return false;
				}
			}
			return true;
		}

		String ToHex(const std::uint8_t* data, std::size_t length)
		{
			static const char Digits[] = "0123456789abcdef";
			String hex(NoInit, length * 2);
			for (std::size_t i = 0; i < length; i++) {
				hex[i * 2] = Digits[data[i] >> 4];
				hex[i * 2 + 1] = Digits[data[i] & 0x0F];
			}
			return hex;
		}

		bool HashFile(StringView path, String& sha256)
		{
			auto s = fs::Open(path, FileAccess::Read);
			if (!s->IsValid()) {
				return false;
			}
			Death::Cryptography::Sha256 sha;
			std::unique_ptr<std::uint8_t[]> buffer = std::make_unique<std::uint8_t[]>(64 * 1024);
			std::int64_t bytesRead;
			while ((bytesRead = s->Read(buffer.get(), 64 * 1024)) > 0) {
				sha.Update(buffer.get(), std::size_t(bytesRead));
			}
			std::uint8_t digest[Death::Cryptography::Sha256::DigestLength];
			sha.Finish(digest);
			sha256 = ToHex(digest, sizeof(digest));
			return true;
		}

		String MakeKey(StringView name, StringView sha256)
		{
			const char* dot = name.findLastOr('.', name.end()).begin();
			return String(name.prefix(dot) + "~"_s + sha256.prefix(KeyHashLength) + name.suffix(dot));
		}

		// The key without its extension is the name a converted tile set or the music is referred to by
		StringView GetKeyStem(StringView key, StringView extension)
		{
			return (key.hasSuffix(extension) ? key.exceptSuffix(extension.size()) : key);
		}

		String SanitizeId(StringView id)
		{
			String result = StringUtils::lowercase(id);
			for (char& c : result) {
				if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) {
					c = '-';
				}
			}
			if (result.size() > MaxIdLength) {
				result = String(result.prefix(MaxIdLength));
			}
			return result;
		}

		bool CopyFileAtomic(StringView source, StringView target)
		{
			String tempPath = target + ".tmp"_s;
			if (!fs::Copy(source, tempPath, true)) {
				fs::RemoveFile(tempPath);
				return false;
			}
			fs::RemoveFile(target);
			if (!fs::Move(tempPath, target)) {
				fs::RemoveFile(tempPath);
				return false;
			}
			return true;
		}

		bool IsCancelled(const std::atomic<bool>* cancel)
		{
			return (cancel != nullptr && cancel->load(std::memory_order_relaxed));
		}

		// Whether a directory or an episode in the converted packs belongs to the pack, they're named "+<id>" or "+<id>_<episode>"
		bool BelongsToPack(StringView name, StringView id)
		{
			StringView stem = name.partition('.')[0];
			if (!stem.hasPrefix('+')) {
				return false;
			}
			stem = stem.exceptPrefix(1);
			return (stem == id || (stem.hasPrefix(id) && stem.size() > id.size() && stem[id.size()] == '_'));
		}

		// Clears "_busy" when the operation is over
		class BusyScope
		{
		public:
			explicit BusyScope(std::atomic<bool>& busy) : _busy(busy) {}
			~BusyScope() { _busy.store(false); }

		private:
			std::atomic<bool>& _busy;
		};
	}

	LevelPackLibrary& LevelPackLibrary::Get()
	{
		static LevelPackLibrary instance;
		return instance;
	}

	LevelPackLibrary::LevelPackLibrary()
		: _busy(false), _loaded(false)
	{
	}

	void LevelPackLibrary::EnsureLoaded()
	{
		bool loaded;
		{
			std::unique_lock<Death::Threading::Spinlock> lock(_lock);
			loaded = _loaded;
		}
		if (!loaded) {
			Refresh();
		}
	}

	void LevelPackLibrary::Refresh()
	{
		String storePath = ContentResolver::Get().GetLevelPacksPath(false);

		SmallVector<std::shared_ptr<const LevelPackManifest>, 0> packs;
		String packsPath = fs::CombinePath(storePath, "Packs"_s);
		if (!storePath.empty() && fs::DirectoryExists(packsPath)) {
			for (auto item : fs::Directory(packsPath, fs::EnumerationOptions::SkipDirectories)) {
				if (fs::GetExtension(item) != "json"_s) {
					continue;
				}
				auto manifest = std::make_shared<LevelPackManifest>();
				if (manifest->Read(item) && manifest->Id == fs::GetFileNameWithoutExtension(item)) {
					packs.push_back(Death::move(manifest));
				} else {
					LOGW("Level pack \"{}\" cannot be read", item);
				}
			}
		}
		std::sort(packs.begin(), packs.end(), [](const std::shared_ptr<const LevelPackManifest>& a, const std::shared_ptr<const LevelPackManifest>& b) {
			return StringUtils::lowercase(a->Title) < StringUtils::lowercase(b->Title);
		});

		std::unique_lock<Death::Threading::Spinlock> lock(_lock);
		_packs = Death::move(packs);
		_storePath = Death::move(storePath);
		_loaded = true;
	}

	SmallVector<std::shared_ptr<const LevelPackManifest>, 0> LevelPackLibrary::GetPacks()
	{
		EnsureLoaded();
		std::unique_lock<Death::Threading::Spinlock> lock(_lock);
		SmallVector<std::shared_ptr<const LevelPackManifest>, 0> packs;
		packs.reserve(_packs.size());
		for (const auto& pack : _packs) {
			packs.push_back(pack);
		}
		return packs;
	}

	std::shared_ptr<const LevelPackManifest> LevelPackLibrary::FindPack(StringView id)
	{
		EnsureLoaded();
		std::unique_lock<Death::Threading::Spinlock> lock(_lock);
		for (const auto& pack : _packs) {
			if (pack->Id == id) {
				return pack;
			}
		}
		return nullptr;
	}

	std::shared_ptr<const LevelPackManifest> LevelPackLibrary::FindPackOfLevel(StringView levelName)
	{
		StringView id = GetPackId(levelName);
		return (id.empty() ? nullptr : FindPack(id));
	}

	StringView LevelPackLibrary::GetPackId(StringView name)
	{
		if (!name.hasPrefix('+')) {
			return {};
		}
		StringView rest = name.exceptPrefix(1);
		std::size_t length = 0;
		while (length < rest.size() && rest[length] != '/' && rest[length] != '\\' && rest[length] != '_') {
			length++;
		}
		return rest.prefix(length);
	}

	LevelPackResult LevelPackLibrary::Install(StringView archivePath, const LevelPackInfo& info, ConversionProgress progress,
		const std::atomic<bool>* cancel, String* installedId)
	{
		bool expected = false;
		if (!_busy.compare_exchange_strong(expected, true)) {
			return LevelPackResult::Busy;
		}
		BusyScope busy(_busy);

		auto& resolver = ContentResolver::Get();
		String storePath = resolver.GetLevelPacksPath(true);
		if (storePath.empty()) {
			LOGE("Level packs cannot be installed, there is no writable content directory");
			return LevelPackResult::Unavailable;
		}

		LOGI("Installing level pack from \"{}\"...", archivePath);
		ConversionProgress extractProgress = progress.Narrow(0.0f, 0.2f);
		ConversionProgress convertProgress = progress.Narrow(0.2f, 1.0f);

		String archiveSha256;
		if (!HashFile(archivePath, archiveSha256)) {
			LOGE("Archive \"{}\" cannot be read", archivePath);
			return LevelPackResult::InvalidArchive;
		}

		ZipArchive archive;
		ZipError zipError = archive.Open(archivePath, MaxArchiveEntries);
		if (zipError != ZipError::None) {
			LOGE("Archive \"{}\" cannot be opened: {}", archivePath, ZipArchive::GetErrorDescription(zipError));
			return (zipError == ZipError::TooLarge ? LevelPackResult::TooLarge : LevelPackResult::InvalidArchive);
		}

		LevelPackManifest manifest;
		manifest.Id = (!info.Id.empty() ? SanitizeId(info.Id) : String("local-"_s + archiveSha256.prefix(8)));
		manifest.Source = (!info.Source.empty() ? info.Source : (info.Id.empty() ? String("local"_s) : String("digiex"_s)));
		manifest.Title = (!info.Title.empty() ? info.Title : String(fs::GetFileNameWithoutExtension(archivePath)));
		manifest.Authors = info.Authors;
		manifest.Description = info.Description;
		manifest.Version = info.Version;
		manifest.ArchiveName = fs::GetFileName(archivePath);
		manifest.ArchiveSha256 = archiveSha256;
		manifest.InstalledAt = DateTime::UtcNow().ToUnixMilliseconds() / 1000;
		if (manifest.Id.empty()) {
			return LevelPackResult::InvalidArchive;
		}
		if (installedId != nullptr) {
			*installedId = manifest.Id;
		}

		String stagingPath = fs::CombinePath({ storePath, "Staging"_s, manifest.Id });
		fs::RemoveDirectoryRecursive(stagingPath);
		if (!fs::CreateDirectories(stagingPath)) {
			return LevelPackResult::CannotWrite;
		}
		auto cleanUpStaging = [&stagingPath]() {
			fs::RemoveDirectoryRecursive(stagingPath);
		};

		// Directories are left out, every file of a pack is next to its levels as the original game expects
		std::uint64_t totalSize = 0;
		bool hasLevels = false;
		auto entries = archive.GetEntries();
		for (std::size_t i = 0; i < entries.size(); i++) {
			const ZipEntry& entry = entries[i];
			extractProgress.ReportStep(std::int32_t(i), std::int32_t(entries.size()));
			if (IsCancelled(cancel)) {
				cleanUpStaging();
				return LevelPackResult::Cancelled;
			}
			if (entry.IsDirectory() || entry.Name.hasPrefix("__MACOSX/"_s) || entry.Name.contains("/__MACOSX/"_s)) {
				continue;
			}

			StringView entryName = entry.Name;
			StringView lastSlash = entryName.findLast('/');
			String name = StringUtils::lowercase(lastSlash != nullptr ? entryName.suffix(lastSlash.end()) : entryName);
			LevelPackFileKind kind;
			if (!IsSafeFileName(name) || !TryGetFileKind(fs::GetExtension(name), kind)) {
				LOGD("File \"{}\" of the archive is skipped", entry.Name);
				continue;
			}
			if (entry.Size > MaxFileSize || totalSize + entry.Size > MaxTotalSize ||
				(entry.Size > CompressionRatioThreshold && entry.Size > entry.CompressedSize * MaxCompressionRatio)) {
				LOGE("File \"{}\" of the archive is too large", entry.Name);
				cleanUpStaging();
				return LevelPackResult::TooLarge;
			}

			String targetPath = fs::CombinePath(stagingPath, name);
			String tempPath = targetPath + ".part"_s;
			ZipError error = archive.ExtractToFile(entry, tempPath, MaxFileSize);
			if (error != ZipError::None) {
				LOGE("File \"{}\" of the archive cannot be extracted: {}", entry.Name, ZipArchive::GetErrorDescription(error));
				cleanUpStaging();
				return (error == ZipError::CannotWrite ? LevelPackResult::CannotWrite : LevelPackResult::InvalidArchive);
			}

			String sha256;
			if (!HashFile(tempPath, sha256)) {
				cleanUpStaging();
				return LevelPackResult::CannotWrite;
			}

			if (const LevelPackFile* existing = manifest.FindFile(name)) {
				// The same file in two directories is fine, two different files of the same name aren't
				fs::RemoveFile(tempPath);
				if (existing->Sha256 != sha256) {
					LOGE("The archive has two different files named \"{}\"", name);
					cleanUpStaging();
					return LevelPackResult::NameConflict;
				}
				continue;
			}
			if (!fs::Move(tempPath, targetPath)) {
				cleanUpStaging();
				return LevelPackResult::CannotWrite;
			}

			LevelPackFile& file = manifest.Files.emplace_back();
			file.Name = Death::move(name);
			file.Sha256 = Death::move(sha256);
			file.Size = entry.Size;
			file.Kind = kind;
			totalSize += entry.Size;
			if (kind == LevelPackFileKind::Level && !file.Name.contains("-mlle-data-"_s)) {
				hasLevels = true;
			}
		}

		if (!hasLevels) {
			LOGE("Archive \"{}\" has no levels", archivePath);
			cleanUpStaging();
			return LevelPackResult::NoLevels;
		}

		// A file that the original game has too is used from there, only the rest is stored
		StringView sourcePath = resolver.GetSourcePath();
		for (LevelPackFile& file : manifest.Files) {
			bool sameAsOriginal = false;
			if (file.Kind == LevelPackFileKind::TileSet || file.Kind == LevelPackFileKind::Music) {
				String originalPath = fs::FindPathCaseInsensitive(fs::CombinePath(sourcePath, file.Name));
				if (fs::IsReadableFile(originalPath) && std::uint64_t(fs::GetFileSize(originalPath)) == file.Size) {
					String originalSha256;
					sameAsOriginal = (HashFile(originalPath, originalSha256) && originalSha256 == file.Sha256);
				}
				if (sameAsOriginal && file.Kind == LevelPackFileKind::TileSet) {
					// Only tile sets that a level of the original game uses are converted
					sameAsOriginal = !resolver.FindContentFile(fs::CombinePath("Tilesets"_s, file.Name)).empty();
				}
			}
			if (!sameAsOriginal) {
				file.Key = MakeKey(file.Name, file.Sha256);
			}
		}

		String filesPath = fs::CombinePath(storePath, "Files"_s);
		fs::CreateDirectories(filesPath);
		for (const LevelPackFile& file : manifest.Files) {
			if (file.Key.empty()) {
				continue;
			}
			String storedPath = fs::CombinePath(filesPath, file.Key);
			if (fs::IsReadableFile(storedPath) && std::uint64_t(fs::GetFileSize(storedPath)) == file.Size) {
				continue;
			}
			if (!CopyFileAtomic(fs::CombinePath(stagingPath, file.Name), storedPath)) {
				LOGE("File \"{}\" cannot be stored", storedPath);
				cleanUpStaging();
				return LevelPackResult::CannotWrite;
			}
		}

		LevelPackResult result = ConvertStaged(stagingPath, manifest, convertProgress, cancel);
		cleanUpStaging();
		if (result != LevelPackResult::Success) {
			CollectGarbage();
			return result;
		}

		String manifestPath = fs::CombinePath({ storePath, "Packs"_s, String(manifest.Id + ".json"_s) });
		fs::CreateDirectories(fs::GetDirectoryName(manifestPath));
		bool wasInstalled = fs::FileExists(manifestPath);
		if (!manifest.Write(manifestPath)) {
			RemoveConvertedFiles(manifest.Id);
			CollectGarbage();
			return LevelPackResult::CannotWrite;
		}

		Refresh();
		if (wasInstalled) {
			// Files only the previous version of the pack had
			CollectGarbage();
		}

		LOGI("Level pack \"{}\" ({}) installed with {} levels and {} episodes", manifest.Title, manifest.Id, manifest.Levels.size(), manifest.Episodes.size());
		progress.Report(1.0f);
		return LevelPackResult::Success;
	}

	LevelPackResult LevelPackLibrary::ConvertStaged(StringView stagingPath, LevelPackManifest& manifest, ConversionProgress progress,
		const std::atomic<bool>* cancel)
	{
		auto& resolver = ContentResolver::Get();
		String cachePath = resolver.GetLevelPacksCachePath();
		String convertedPath = fs::CombinePath(cachePath, String(".staging-"_s + manifest.Id));
		fs::RemoveDirectoryRecursive(convertedPath);
		if (!fs::CreateDirectories(convertedPath)) {
			return LevelPackResult::CannotWrite;
		}

		// Read-only while the levels are converted, they're converted on several threads
		HashMap<String, String> tileSetPaths, musicNames;
		for (const LevelPackFile& file : manifest.Files) {
			if (file.Key.empty()) {
				continue;
			}
			if (file.Kind == LevelPackFileKind::TileSet) {
				tileSetPaths.emplace(String(GetKeyStem(file.Name, ".j2t"_s)), String(GetKeyStem(file.Key, ".j2t"_s)));
			} else if (file.Kind == LevelPackFileKind::Music) {
				musicNames.emplace(file.Name, file.Key);
			}
		}

		AssetConverter::LevelPackConversion conversion;
		conversion.EpisodePrefix = manifest.GetEpisodePrefix();
		conversion.TileSetPath = [&tileSetPaths](StringView name) -> String {
			auto it = tileSetPaths.find(String::nullTerminatedView(name));
			return (it != tileSetPaths.end() ? it->second : String(name));
		};
		conversion.MusicPath = [&musicNames](StringView name) -> String {
			auto it = musicNames.find(String::nullTerminatedView(name));
			return (it != musicNames.end() ? it->second : String(name));
		};
#if defined(WITH_ANGELSCRIPT)
		conversion.CopyScripts = true;
#else
		conversion.CopyScripts = false;
#endif

		AssetConverter::ConvertLevelPack(stagingPath, convertedPath, conversion, progress.Narrow(0.0f, 0.6f));
		if (IsCancelled(cancel)) {
			fs::RemoveDirectoryRecursive(convertedPath);
			return LevelPackResult::Cancelled;
		}
		if (conversion.Levels.empty()) {
			fs::RemoveDirectoryRecursive(convertedPath);
			return LevelPackResult::NoLevels;
		}

		// Every tile set of the pack is converted, scripts may load the ones no level uses
		String tileSetsPath = fs::CombinePath(cachePath, "Tilesets"_s);
		fs::CreateDirectories(tileSetsPath);
		ConversionProgress tileSetProgress = progress.Narrow(0.6f, 1.0f);
		std::int32_t tileSetsDone = 0;
		for (const auto& pair : tileSetPaths) {
			if (IsCancelled(cancel)) {
				fs::RemoveDirectoryRecursive(convertedPath);
				return LevelPackResult::Cancelled;
			}
			String targetPath = fs::CombinePath(tileSetsPath, String(pair.second + ".j2t"_s));
			if (!fs::IsReadableFile(targetPath)) {
				JJ2Tileset tileSet;
				String tempPath = targetPath + ".tmp"_s;
				if (tileSet.Open(fs::CombinePath(stagingPath, String(pair.first + ".j2t"_s)), false)) {
					tileSet.Convert(tempPath);
					fs::RemoveFile(targetPath);
					if (!fs::Move(tempPath, targetPath)) {
						fs::RemoveFile(tempPath);
						LOGW("Tile set \"{}\" cannot be stored", targetPath);
					}
				} else {
					LOGW("Tile set \"{}\" of the pack cannot be read", pair.first);
				}
			}
			tileSetProgress.ReportStep(++tileSetsDone, std::int32_t(tileSetPaths.size()));
		}

		manifest.MissingAssets.clear();
		for (const String& name : conversion.UsedTileSets) {
			if (!tileSetPaths.contains(name) && resolver.FindContentFile(fs::CombinePath("Tilesets"_s, String(name + ".j2t"_s))).empty()) {
				manifest.MissingAssets.push_back(String(name + ".j2t"_s));
			}
		}
		StringView userContentPath = resolver.GetUserContentPath();
		for (const String& name : conversion.UsedMusic) {
			if (musicNames.contains(name) || manifest.FindFile(name) != nullptr) {
				continue;
			}
			bool found = (!userContentPath.empty() && fs::IsReadableFile(fs::CombinePath({ userContentPath, "Music"_s, name }))) ||
				fs::IsReadableFile(fs::CombinePath({ resolver.GetContentPath(), "Music"_s, name })) ||
				fs::IsReadableFile(fs::FindPathCaseInsensitive(fs::CombinePath(resolver.GetSourcePath(), name)));
			if (!found) {
				manifest.MissingAssets.push_back(name);
			}
		}
		for (const String& missing : manifest.MissingAssets) {
			LOGW("Level pack \"{}\" needs \"{}\", which isn't there", manifest.Id, missing);
		}

		// The levels are put in place only once all of them are converted
		RemoveConvertedFiles(manifest.Id);
		String episodesPath = fs::CombinePath(cachePath, "Episodes"_s);
		fs::CreateDirectories(episodesPath);
		String convertedEpisodesPath = fs::CombinePath(convertedPath, "Episodes"_s);
		for (auto item : fs::Directory(convertedEpisodesPath)) {
			if (!fs::Move(item, fs::CombinePath(episodesPath, fs::GetFileName(item)))) {
				LOGE("Converted level pack \"{}\" cannot be moved into \"{}\"", item, episodesPath);
				fs::RemoveDirectoryRecursive(convertedPath);
				RemoveConvertedFiles(manifest.Id);
				return LevelPackResult::CannotWrite;
			}
		}
		fs::RemoveDirectoryRecursive(convertedPath);

		manifest.Levels.clear();
		for (auto& level : conversion.Levels) {
			LevelPackLevel& converted = manifest.Levels.emplace_back();
			converted.Name = Death::move(level.Name);
			converted.DisplayName = Death::move(level.DisplayName);
		}
		manifest.Episodes.clear();
		for (auto& episode : conversion.Episodes) {
			LevelPackEpisode& converted = manifest.Episodes.emplace_back();
			converted.Name = Death::move(episode.Name);
			converted.DisplayName = Death::move(episode.DisplayName);
			converted.FirstLevel = Death::move(episode.FirstLevel);
		}
		manifest.CacheVersion = JJ2Anims::CacheVersion;
		return LevelPackResult::Success;
	}

	LevelPackResult LevelPackLibrary::Remove(StringView id)
	{
		bool expected = false;
		if (!_busy.compare_exchange_strong(expected, true)) {
			return LevelPackResult::Busy;
		}
		BusyScope busy(_busy);

		auto pack = FindPack(id);
		if (pack == nullptr) {
			return LevelPackResult::NotFound;
		}

		String storePath = ContentResolver::Get().GetLevelPacksPath(false);
		String manifestPath = fs::CombinePath({ storePath, "Packs"_s, String(pack->Id + ".json"_s) });
		if (!fs::RemoveFile(manifestPath) && fs::FileExists(manifestPath)) {
			return LevelPackResult::CannotWrite;
		}

		RemoveConvertedFiles(pack->Id);
		fs::RemoveFile(fs::CombinePath({ ContentResolver::Get().GetLevelPacksCachePath(), "Thumbnails"_s, String(pack->Id + ".png"_s) }));

		for (const LevelPackEpisode& episode : pack->Episodes) {
			PreferencesCache::RemoveEpisodeContinue(episode.Name);
			PreferencesCache::RemoveEpisodeEnd(episode.Name);
		}
		PreferencesCache::RemoveEpisodeContinue(pack->GetEpisodePrefix());
		PreferencesCache::Save();

		Refresh();
		CollectGarbage();
		LOGI("Level pack \"{}\" ({}) removed", pack->Title, pack->Id);
		return LevelPackResult::Success;
	}

	std::int32_t LevelPackLibrary::ReconvertStale(ConversionProgress progress)
	{
		auto& resolver = ContentResolver::Get();
		String cachePath = resolver.GetLevelPacksCachePath();

		SmallVector<std::shared_ptr<const LevelPackManifest>, 0> stale;
		for (const auto& pack : GetPacks()) {
			bool isStale = (pack->CacheVersion != JJ2Anims::CacheVersion || pack->Levels.empty());
			if (!isStale) {
				String path = fs::CombinePath({ cachePath, "Episodes"_s, String(pack->Levels[0].Name + ".j2l"_s) });
				isStale = !fs::IsReadableFile(path);
			}
			if (isStale) {
				stale.push_back(pack);
			}
		}
		if (stale.empty()) {
			return 0;
		}

		bool expected = false;
		if (!_busy.compare_exchange_strong(expected, true)) {
			return 0;
		}

		std::int32_t converted = 0;
		{
			BusyScope busy(_busy);
			String storePath = resolver.GetLevelPacksPath(false);
			String filesPath = fs::CombinePath(storePath, "Files"_s);
			StringView sourcePath = resolver.GetSourcePath();

			for (std::size_t i = 0; i < stale.size(); i++) {
				const LevelPackManifest& pack = *stale[i];
				LOGI("Converting level pack \"{}\" ({}) again...", pack.Title, pack.Id);

				String stagingPath = fs::CombinePath({ storePath, "Staging"_s, pack.Id });
				fs::RemoveDirectoryRecursive(stagingPath);
				fs::CreateDirectories(stagingPath);

				bool complete = true;
				for (const LevelPackFile& file : pack.Files) {
					String sourceFile = (file.Key.empty()
						? fs::FindPathCaseInsensitive(fs::CombinePath(sourcePath, file.Name))
						: fs::CombinePath(filesPath, file.Key));
					if (!fs::Copy(sourceFile, fs::CombinePath(stagingPath, file.Name), true)) {
						LOGW("File \"{}\" of level pack \"{}\" is gone", file.Name, pack.Id);
						complete = complete && (file.Kind != LevelPackFileKind::Level);
					}
				}

				LevelPackManifest manifest = pack;
				LevelPackResult result = (complete ? ConvertStaged(stagingPath, manifest, progress.Narrow(float(i) / stale.size(), float(i + 1) / stale.size()), nullptr) : LevelPackResult::NotFound);
				fs::RemoveDirectoryRecursive(stagingPath);
				if (result == LevelPackResult::Success &&
					manifest.Write(fs::CombinePath({ storePath, "Packs"_s, String(manifest.Id + ".json"_s) }))) {
					converted++;
				} else {
					LOGE("Level pack \"{}\" ({}) cannot be converted", pack.Title, pack.Id);
				}
			}
		}

		Refresh();
		CollectGarbage();
		return converted;
	}

	void LevelPackLibrary::CleanUp()
	{
		bool expected = false;
		if (!_busy.compare_exchange_strong(expected, true)) {
			return;
		}
		{
			BusyScope busy(_busy);
			String storePath = ContentResolver::Get().GetLevelPacksPath(false);
			if (!storePath.empty()) {
				fs::RemoveDirectoryRecursive(fs::CombinePath(storePath, "Staging"_s));
			}
			String cachePath = ContentResolver::Get().GetLevelPacksCachePath();
			if (fs::DirectoryExists(cachePath)) {
				for (auto item : fs::Directory(cachePath, fs::EnumerationOptions::SkipFiles)) {
					if (fs::GetFileName(item).hasPrefix(".staging-"_s)) {
						fs::RemoveDirectoryRecursive(item);
					}
				}
			}
		}
		Refresh();
		CollectGarbage();
	}

	void LevelPackLibrary::RemoveConvertedFiles(StringView id)
	{
		String episodesPath = fs::CombinePath(ContentResolver::Get().GetLevelPacksCachePath(), "Episodes"_s);
		if (!fs::DirectoryExists(episodesPath)) {
			return;
		}
		SmallVector<String, 0> toRemove;
		for (auto item : fs::Directory(episodesPath)) {
			if (BelongsToPack(fs::GetFileName(item), id)) {
				toRemove.push_back(String(item));
			}
		}
		for (const String& path : toRemove) {
			if (fs::DirectoryExists(path)) {
				fs::RemoveDirectoryRecursive(path);
			} else {
				fs::RemoveFile(path);
			}
		}
	}

	void LevelPackLibrary::CollectGarbage()
	{
		auto packs = GetPacks();
		HashMap<String, bool> referenced;
		for (const auto& pack : packs) {
			for (const LevelPackFile& file : pack->Files) {
				if (!file.Key.empty()) {
					referenced.emplace(file.Key, true);
				}
			}
		}

		std::int32_t removed = 0;
		auto& resolver = ContentResolver::Get();
		String storePath = resolver.GetLevelPacksPath(false);
		String cachePath = resolver.GetLevelPacksCachePath();
		// A converted tile set has the name of its original file
		for (String directory : { fs::CombinePath(storePath, "Files"_s), fs::CombinePath(cachePath, "Tilesets"_s) }) {
			if (storePath.empty() || !fs::DirectoryExists(directory)) {
				continue;
			}
			SmallVector<String, 0> toRemove;
			for (auto item : fs::Directory(directory, fs::EnumerationOptions::SkipDirectories)) {
				if (!referenced.contains(String(fs::GetFileName(item)))) {
					toRemove.push_back(String(item));
				}
			}
			for (const String& path : toRemove) {
				if (fs::RemoveFile(path)) {
					removed++;
				}
			}
		}

		// Converted levels of a pack that isn't installed, left by an interrupted installation
		String episodesPath = fs::CombinePath(cachePath, "Episodes"_s);
		if (fs::DirectoryExists(episodesPath)) {
			SmallVector<String, 0> toRemove;
			for (auto item : fs::Directory(episodesPath)) {
				StringView name = fs::GetFileName(item);
				bool owned = false;
				for (const auto& pack : packs) {
					if (BelongsToPack(name, pack->Id)) {
						owned = true;
						break;
					}
				}
				if (!owned) {
					toRemove.push_back(String(item));
				}
			}
			for (const String& path : toRemove) {
				if (fs::DirectoryExists(path)) {
					fs::RemoveDirectoryRecursive(path);
				} else {
					fs::RemoveFile(path);
				}
				removed++;
			}
		}

		if (removed > 0) {
			LOGI("Removed {} files of level packs that aren't needed anymore", removed);
		}
	}

	String LevelPackLibrary::FindOriginalFile(StringView levelName, StringView fileName)
	{
		auto pack = FindPackOfLevel(levelName);
		if (pack == nullptr) {
			return {};
		}
		const LevelPackFile* file = pack->FindFile(StringUtils::lowercase(fs::GetFileName(fileName)));
		if (file == nullptr) {
			return {};
		}
		if (file->Key.empty()) {
			return fs::FindPathCaseInsensitive(fs::CombinePath(ContentResolver::Get().GetSourcePath(), file->Name));
		}
		String storePath;
		{
			std::unique_lock<Death::Threading::Spinlock> lock(_lock);
			storePath = _storePath;
		}
		return fs::CombinePath({ storePath, "Files"_s, file->Key });
	}

	String LevelPackLibrary::FindTileSet(StringView levelName, StringView tileSetName)
	{
		auto pack = FindPackOfLevel(levelName);
		if (pack == nullptr) {
			return {};
		}
		String name = StringUtils::lowercase(tileSetName);
		if (!name.hasSuffix(".j2t"_s)) {
			name += ".j2t"_s;
		}
		const LevelPackFile* file = pack->FindFile(name);
		if (file == nullptr || file->Key.empty()) {
			return {};
		}
		return String(GetKeyStem(file->Key, ".j2t"_s));
	}

	StringView LevelPackLibrary::GetResultDescription(LevelPackResult result)
	{
		switch (result) {
			case LevelPackResult::Success: return "Success"_s;
			case LevelPackResult::Unavailable: return "Level packs cannot be stored on this device"_s;
			case LevelPackResult::Busy: return "Another level pack is being installed or removed"_s;
			case LevelPackResult::Cancelled: return "Cancelled"_s;
			case LevelPackResult::InvalidArchive: return "The archive cannot be read"_s;
			case LevelPackResult::NoLevels: return "The archive contains no levels"_s;
			case LevelPackResult::NameConflict: return "The archive contains different files of the same name"_s;
			case LevelPackResult::TooLarge: return "The archive is too large"_s;
			case LevelPackResult::CannotWrite: return "Files cannot be written"_s;
			case LevelPackResult::NotFound: return "The level pack is not installed"_s;
			default: return "Unknown error"_s;
		}
	}
}

#endif
