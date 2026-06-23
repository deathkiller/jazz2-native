#include "ContentResolver.h"
#include "ContentResolver.Shaders.h"
#include "Compatibility/JJ2Anims.Palettes.h"
#include "LevelFlags.h"
#include "LevelHandler.h"
#include "Tiles/TileSet.h"
#if defined(DEATH_DEBUG)
#	include "Compatibility/JJ2Anims.h"
#endif

#include "../nCine/Application.h"
#include "../nCine/AppConfiguration.h"
#include "../nCine/ServiceLocator.h"
#include "../nCine/tracy.h"
#include "../nCine/Graphics/ITextureLoader.h"
#include "../nCine/Graphics/RenderResources.h"
#include "../nCine/Graphics/RenderCommand.h"
#include "../nCine/Base/Random.h"

#if defined(DEATH_TARGET_ANDROID)
#	include "../nCine/Backends/Android/AndroidJniHelper.h"
#	include <IO/AndroidAssetStream.h>
#elif defined(DEATH_TARGET_WINDOWS_RT)
#	include <Environment.h>
#endif

#include <cmath>

#include <Containers/StringConcatenable.h>
#include <Containers/StringStlView.h>
#include <IO/MemoryStream.h>
#include <IO/Compression/DeflateStream.h>

#include <jsoncpp/json.h>

using namespace Death::IO::Compression;
using namespace Jazz2::Tiles;

static Vector2i GetVector2iFromJson(const Json::Value& value, Vector2i defaultValue = Vector2i::Zero)
{
	if (value.isArray()) {
		std::int64_t x = 0, y = 0;
		if (value[0].get(x) == Json::SUCCESS && value[1].get(y) == Json::SUCCESS) {
			return Vector2i((std::int32_t)x, (std::int32_t)y);
		}
	}
	return defaultValue;
}

namespace Jazz2
{
	ContentResolver& ContentResolver::Get()
	{
		static ContentResolver current;
		return current;
	}

	ContentResolver::ContentResolver()
		: _isHeadless(false), _isLoading(false), _cachedMetadata(64), _cachedGraphics(256),
#if defined(WITH_AUDIO)
			_cachedSounds(192),
#endif
			_palettes{}, _paletteDirtyFirstRow(0), _paletteDirtyLastRow(PaletteCount - 1), _paletteRowRefCount{},
			_paletteRowColor{}, _paletteRowScheme{}
	{
		InitializePaths();
	}

	ContentResolver::~ContentResolver()
	{
	}

	void ContentResolver::Release()
	{
		_cachedMetadata.clear();
		_cachedGraphics.clear();
#if defined(WITH_AUDIO)
		_cachedSounds.clear();
#endif

		for (std::int32_t i = 0; i < (std::int32_t)FontType::Count; i++) {
			_fonts[i] = nullptr;
		}

		for (std::int32_t i = 0; i < (std::int32_t)PrecompiledShader::Count; i++) {
			_precompiledShaders[i] = nullptr;
		}
	}

	StringView ContentResolver::GetContentPath() const
	{
#if defined(DEATH_TARGET_UNIX) || defined(DEATH_TARGET_WINDOWS_RT)
		return _contentPath;
#elif defined(DEATH_TARGET_ANDROID)
		return "assets:/"_s;
#elif defined(DEATH_TARGET_SWITCH)
		return "romfs:/"_s;
#elif defined(DEATH_TARGET_WINDOWS)
		return "Content\\"_s;
#else
		return "Content/"_s;
#endif
	}

	StringView ContentResolver::GetCachePath() const
	{
#if defined(DEATH_TARGET_ANDROID) || defined(DEATH_TARGET_APPLE) || defined(DEATH_TARGET_UNIX) || defined(DEATH_TARGET_WINDOWS_RT)
		return _cachePath;
#elif defined(DEATH_TARGET_SWITCH)
		// Switch has some issues with UTF-8 characters, so use "Jazz2" instead
		return "sdmc:/Games/Jazz2/Cache/"_s;
#elif defined(DEATH_TARGET_WINDOWS)
		return "Cache\\"_s;
#else
		return "Cache/"_s;
#endif
	}

	StringView ContentResolver::GetSourcePath() const
	{
#if defined(DEATH_TARGET_ANDROID) || defined(DEATH_TARGET_APPLE) || defined(DEATH_TARGET_UNIX) || defined(DEATH_TARGET_WINDOWS_RT)
		return _sourcePath;
#elif defined(DEATH_TARGET_SWITCH)
		// Switch has some issues with UTF-8 characters, so use "Jazz2" instead
		return "sdmc:/Games/Jazz2/Source/"_s;
#elif defined(DEATH_TARGET_WINDOWS)
		return "Source\\"_s;
#else
		return "Source/"_s;
#endif
	}

	bool ContentResolver::IsHeadless() const
	{
		return _isHeadless;
	}

	void ContentResolver::SetHeadless(bool value)
	{
		_isHeadless = value;
	}

	void ContentResolver::InitializePaths()
	{
#if defined(DEATH_TARGET_ANDROID)
		// If `MANAGE_EXTERNAL_STORAGE` permission is granted, try to use also alternative paths
		if (Backends::AndroidJniWrap_Activity::hasExternalStoragePermission()) {
			constexpr StringView ExternalPaths[] = {
				"Games/Jazz² Resurrection/"_s,
				"Games/Jazz2 Resurrection/"_s,
				"Download/Jazz² Resurrection/"_s,
				"Download/Jazz2 Resurrection/"_s
			};

			bool found = false;
			String externalStorage = fs::GetExternalStorage();
			for (std::size_t i = 0; i < arraySize(ExternalPaths); i++) {
				String externalPath = fs::CombinePath(externalStorage, ExternalPaths[i]);
				_sourcePath = fs::CombinePath(externalPath, "Source/"_s);
				if (fs::DirectoryExists(_sourcePath)) {
					_cachePath = fs::CombinePath(externalPath, "Cache/"_s);
					found = true;
					break;
				}
			}

			if (!found) {
				String deviceBrand = Backends::AndroidJniClass_Version::deviceBrand();
				String deviceModel = Backends::AndroidJniClass_Version::deviceModel();
				if (deviceBrand == "Windows"_s && deviceModel == "Subsystem for Android(TM)"_s) {
					// Set special paths if Windows Subsystem for Android™ is used
					String externalStorageWindows = fs::CombinePath(externalStorage, "Windows/Saved Games/"_s);
					if (fs::DirectoryExists(externalStorageWindows)) {
						if (fs::DirectoryExists(fs::CombinePath(externalStorageWindows, "Jazz2 Resurrection/Source/"_s)) &&
							!fs::DirectoryExists(fs::CombinePath(externalStorageWindows, "Jazz² Resurrection/Source/"_s))) {
							_sourcePath = fs::CombinePath(externalStorageWindows, "Jazz2 Resurrection/Source/"_s);
							_cachePath = fs::CombinePath(externalStorageWindows, "Jazz2 Resurrection/Cache/"_s);
						} else {
							_sourcePath = fs::CombinePath(externalStorageWindows, "Jazz² Resurrection/Source/"_s);
							_cachePath = fs::CombinePath(externalStorageWindows, "Jazz² Resurrection/Cache/"_s);
						}
						found = true;
					}
				}

				if (!found) {
					String externalPath = fs::CombinePath(externalStorage, ExternalPaths[0]);
					_sourcePath = fs::CombinePath(externalPath, "Source/"_s);
					_cachePath = fs::CombinePath(externalPath, "Cache/"_s);
				}
			}
		} else {
			StringView dataPath = AndroidAssetStream::GetExternalDataPath();
			_sourcePath = fs::CombinePath(dataPath, "Source/"_s);
			_cachePath = fs::CombinePath(dataPath, "Cache/"_s);
		}
#elif defined(DEATH_TARGET_APPLE)
		// Returns local application data directory on Apple
		const String& appData = fs::GetSavePath("Jazz² Resurrection"_s);
		_sourcePath = fs::CombinePath(appData, "Source/"_s);
		_cachePath = fs::CombinePath(appData, "Cache/"_s);
#elif defined(DEATH_TARGET_UNIX)
#	if defined(NCINE_PACKAGED_CONTENT_PATH)
		_contentPath = "Content/"_s;
#	elif defined(NCINE_OVERRIDE_CONTENT_PATH)
		_contentPath = NCINE_OVERRIDE_CONTENT_PATH;
#	else
		_contentPath = NCINE_INSTALL_PREFIX "/share/" NCINE_LINUX_PACKAGE "/Content/";
#	endif
#	if defined(NCINE_PACKAGED_CONTENT_PATH)
		// If Content is packaged with binaries, prefer a local Source/ for portable use and
		// fall back to standard XDG paths otherwise.
		// AppRun cds into the AppImage mount point before launching, so CWD is useless —
		// use dirname($APPIMAGE) to resolve paths relative to the .AppImage file itself.
		String localBase;
		StringView appImageEnv = ::getenv("APPIMAGE");
		if (!appImageEnv.empty()) {
			localBase = fs::GetDirectoryName(appImageEnv);
		}

		String localSourcePath = fs::CombinePath(localBase, "Source/"_s);
		if (fs::FindPathCaseInsensitive(fs::CombinePath(localSourcePath, "Anims.j2a"_s)) ||
			fs::FindPathCaseInsensitive(fs::CombinePath(localSourcePath, "AnimsSw.j2a"_s))) {
			_sourcePath = std::move(localSourcePath);
			_cachePath = fs::CombinePath(localBase, "Cache/"_s);
		} else {
			auto localStorage = fs::GetLocalStorage();
			if (!localStorage.empty()) {
				auto appData = fs::CombinePath(localStorage, NCINE_LINUX_PACKAGE);
				_sourcePath = fs::CombinePath(appData, "Source/"_s);
				_cachePath = fs::CombinePath(appData, "Cache/"_s);
			} else {
				_sourcePath = "Source/"_s;
				_cachePath = "Cache/"_s;
			}
		}
#	else
		if (fs::DirectoryExists(_contentPath)) {
			// Shared Content exists, try to use standard XDG paths
			auto localStorage = fs::GetLocalStorage();
			if (!localStorage.empty()) {
				// Use "$XDG_DATA_HOME/Jazz² Resurrection/" if exists (for backward compatibility), otherwise "$XDG_DATA_HOME/{NCINE_LINUX_PACKAGE}/"
				_cachePath = fs::CombinePath(localStorage, "Jazz² Resurrection/Cache/"_s);
				if (!fs::DirectoryExists(_cachePath)) {
					auto appData = fs::CombinePath(localStorage, NCINE_LINUX_PACKAGE);
					_cachePath = fs::CombinePath(appData, "Cache/"_s);
				}
			} else {
				_cachePath = "Cache/"_s;
			}

			// Prefer system-wide Source only if it exists and the local one doesn't exist
			_sourcePath = fs::CombinePath(fs::GetDirectoryName(_cachePath), "Source/"_s);
			if (!fs::FindPathCaseInsensitive(fs::CombinePath(_sourcePath, "Anims.j2a"_s)) &&
				!fs::FindPathCaseInsensitive(fs::CombinePath(_sourcePath, "AnimsSw.j2a"_s))) {
				auto systemWideSource = NCINE_INSTALL_PREFIX "/share/" NCINE_LINUX_PACKAGE "/Source/";
				if (fs::FindPathCaseInsensitive(fs::CombinePath(systemWideSource, "Anims.j2a"_s)) ||
					fs::FindPathCaseInsensitive(fs::CombinePath(systemWideSource, "AnimsSw.j2a"_s))) {
					_sourcePath = systemWideSource;
				}
			}
		} else {
			// Fallback to relative paths
			_contentPath = "Content/"_s;
			_sourcePath = "Source/"_s;
			_cachePath = "Cache/"_s;
		}
#	endif
#elif defined(DEATH_TARGET_WINDOWS_RT)
		bool found = false;
		if (Environment::CurrentDeviceType == DeviceType::Xbox) {
			// Try to use external drives (D:, E:, F:) on Xbox, "\\?\" path prefix is required on Xbox
			String PathTemplate1 = "\\\\?\\X:\\Games\\Jazz² Resurrection\\"_s;
			String PathTemplate2 = "\\\\?\\X:\\Games\\Jazz2 Resurrection\\"_s;

			for (char letter = 'D'; letter <= 'G'; letter++) {
				PathTemplate1[4] = letter;
				_sourcePath = fs::CombinePath(PathTemplate1, "Source\\"_s);
				if (fs::DirectoryExists(_sourcePath)) {
					_cachePath = fs::CombinePath(PathTemplate1, "Cache\\"_s);
					found = true;
					break;
				}

				PathTemplate2[4] = letter;
				_sourcePath = fs::CombinePath(PathTemplate2, "Source\\"_s);
				if (fs::DirectoryExists(_sourcePath)) {
					_cachePath = fs::CombinePath(PathTemplate2, "Cache\\"_s);
					found = true;
					break;
				}
			}
		}

		if (!found) {
			// Returns local application data directory on Windows RT
			const String& appData = fs::GetSavePath("Jazz² Resurrection"_s);
			_sourcePath = fs::CombinePath(appData, "Source\\"_s);
			_cachePath = fs::CombinePath(appData, "Cache\\"_s);
		}
		_contentPath = "Content\\"_s;
#endif

#if !defined(DEATH_TARGET_EMSCRIPTEN)
		RemountPaks();
#endif
	}

#if !defined(DEATH_TARGET_EMSCRIPTEN)
	void ContentResolver::RemountPaks()
	{
		// Unload all already loaded .paks
		_mountedPaks.clear();

		// Load all .paks from `Content` and `Cache` directory
		for (auto item : fs::Directory(GetContentPath(), fs::EnumerationOptions::SkipDirectories)) {
			auto extension = fs::GetExtension(item);
			if (extension != "pak"_s) {
				continue;
			}

			auto& pak = _mountedPaks.emplace_back(std::make_unique<PakFile>(item));
			if (pak->IsValid()) {
				LOGI("File \"{}\" mounted successfully", item);
			} else {
				LOGE("Failed to mount file \"{}\"", item);
				_mountedPaks.pop_back();
			}
		}

		for (auto item : fs::Directory(GetCachePath(), fs::EnumerationOptions::SkipDirectories)) {
			auto extension = fs::GetExtension(item);
			if (extension != "pak"_s) {
				continue;
			}

			auto& pak = _mountedPaks.emplace_back(std::make_unique<PakFile>(item));
			if (pak->IsValid()) {
				LOGI("File \"{}\" mounted successfully", item);
			} else {
				LOGE("Failed to mount file \"{}\"", item);
				_mountedPaks.pop_back();
			}
		}
	}
#endif

	std::unique_ptr<Stream> ContentResolver::OpenContentFile(StringView path, std::int32_t bufferSize)
	{
		// Search .paks first, then Content directory and Cache directory
#if !defined(DEATH_TARGET_EMSCRIPTEN)
		for (std::size_t i = 0; i < _mountedPaks.size(); i++) {
			auto mountPoint = _mountedPaks[i]->GetMountPoint();
			if (path.hasPrefix(mountPoint)) {
				auto packedFile = _mountedPaks[i]->OpenFile(path.exceptPrefix(mountPoint.size()), bufferSize);
				if (packedFile != nullptr && packedFile->IsValid()) {
					return packedFile;
				}
			}
		}
#endif

		String fullPath = fs::CombinePath(GetContentPath(), path);
		if (fs::IsReadableFile(fullPath)) {
			auto realFile = fs::Open(fullPath, FileAccess::Read, bufferSize);
			if (realFile->IsValid()) {
				return realFile;
			}
		}

		fullPath = fs::CombinePath(GetCachePath(), path);
		return fs::Open(fullPath, FileAccess::Read, bufferSize);
	}

	std::unique_ptr<Stream> ContentResolver::OpenSourceFile(StringView path, std::int32_t bufferSize)
	{
		String fullPath = fs::FindPathCaseInsensitive(fs::CombinePath(GetSourcePath(), path));
		return fs::Open(fullPath, FileAccess::Read, bufferSize);
	}

	void ContentResolver::BeginLoading()
	{
		_isLoading = true;

		// Reset Referenced flag
		for (auto& resource : _cachedMetadata) {
			resource.second->Flags &= ~MetadataFlags::Referenced;
		}
		for (auto& resource : _cachedGraphics) {
			resource.second->Flags &= ~GenericGraphicResourceFlags::Referenced;
		}
#if defined(WITH_AUDIO)
		for (auto& resource : _cachedSounds) {
			resource.second->Flags &= ~GenericSoundResourceFlags::Referenced;
		}
#endif
	}

	void ContentResolver::EndLoading()
	{
#if defined(DEATH_DEBUG)
		std::int32_t metadataKept = 0, metadataReleased = 0;
		std::int32_t animationsKept = 0, animationsReleased = 0;
		std::int32_t soundsKept = 0, soundsReleased = 0;
#endif

		// Release unreferenced metadata
		{
			auto it = _cachedMetadata.begin();
			while (it != _cachedMetadata.end()) {
				if ((it->second->Flags & MetadataFlags::Referenced) != MetadataFlags::Referenced) {
					it = _cachedMetadata.erase(it);
#if defined(DEATH_DEBUG)
					metadataReleased++;
#endif
				} else {
					++it;
#if defined(DEATH_DEBUG)
					metadataKept++;
#endif
				}
			}
		}

		// Released unreferenced graphics
		{
			auto it = _cachedGraphics.begin();
			while (it != _cachedGraphics.end()) {
				if ((it->second->Flags & GenericGraphicResourceFlags::Referenced) != GenericGraphicResourceFlags::Referenced) {
					it = _cachedGraphics.erase(it);
#if defined(DEATH_DEBUG)
					animationsReleased++;
#endif
				} else {
					++it;
#if defined(DEATH_DEBUG)
					animationsKept++;
#endif
				}
			}
		}

#if defined(WITH_AUDIO)
		// Released unreferenced sounds
		{
			auto it = _cachedSounds.begin();
			while (it != _cachedSounds.end()) {
				if ((it->second->Flags & GenericSoundResourceFlags::Referenced) != GenericSoundResourceFlags::Referenced) {
					it = _cachedSounds.erase(it);
#	if defined(DEATH_DEBUG)
					soundsReleased++;
#	endif
				} else {
					++it;
#	if defined(DEATH_DEBUG)
					soundsKept++;
#	endif
				}
			}
		}
#endif

#if defined(DEATH_DEBUG)
		LOGW("Metadata: {}|{}, Animations: {}|{}, Sounds: {}|{}", metadataKept, metadataReleased,
			animationsKept, animationsReleased, soundsKept, soundsReleased);
#endif

		_isLoading = false;
	}

	void ContentResolver::OverridePathHandler(Function<String(StringView)>&& callback)
	{
		_pathHandler = std::move(callback);
	}

	void ContentResolver::PreloadMetadataAsync(StringView path)
	{
		// TODO: Reimplement async preloading
		RequestMetadata(path);
	}

	Metadata* ContentResolver::RequestMetadata(StringView path, bool forceIndexed)
	{
		auto pathNormalized = fs::ToNativeSeparators(path);
		// Indexed metadata (animations loaded with keepIndexed for runtime recoloring) is cached under a separate
		// key - "*" can't appear in a real path - so a baked load of the same path can't shadow the indexed variant
		// the player needs (and vice versa). Without this, whichever variant loads first wins for everyone.
		String cacheKey = (forceIndexed ? String(pathNormalized + "*"_s) : String(pathNormalized));
		auto it = _cachedMetadata.find(cacheKey);
		if (it != _cachedMetadata.end()) {
			// Already loaded - Mark as referenced
			it->second->Flags |= MetadataFlags::Referenced;

			for (const auto& resource : it->second->Animations) {
				resource.Base->Flags |= GenericGraphicResourceFlags::Referenced;
			}

#if defined(WITH_AUDIO)
			for (const auto& [key, resource] : it->second->Sounds) {
				for (const auto& base : resource.Buffers) {
					base->Flags |= GenericSoundResourceFlags::Referenced;
				}
			}
#endif

			return it->second.get();
		}

		// Try to load it
		auto s = fs::Open(fs::CombinePath({ GetContentPath(), "Metadata"_s, String(pathNormalized + ".res"_s) }), FileAccess::Read);
		auto fileSize = s->GetSize();
		if (fileSize < 4 || fileSize > 64 * 1024 * 1024) {
			// 64 MB file size limit
			return nullptr;
		}

		auto buffer = std::make_unique<char[]>(fileSize);
		s->Read(buffer.get(), fileSize);

		bool multipleAnimsNoStatesWarning = false;

		std::unique_ptr<Metadata> metadata = std::make_unique<Metadata>();
		metadata->Path = std::move(pathNormalized);
		// The cache key references this string (the map key is a non-owning Reference), so it lives inside the value
		metadata->CacheKey = std::move(cacheKey);
		metadata->Flags |= MetadataFlags::Referenced;

		Json::CharReaderBuilder builder;
		auto reader = std::unique_ptr<Json::CharReader>(builder.newCharReader());
		Json::Value doc; std::string errors;
		if (reader->parse(buffer.get(), buffer.get() + fileSize, &doc, &errors)) {
			metadata->BoundingBox = GetVector2iFromJson(doc["BoundingBox"], Vector2i(InvalidValue, InvalidValue));

			const auto& animations = doc["Animations"];
			if (animations.isObject()) {
				std::size_t count = animations.getMemberCount();
				metadata->Animations.reserve(count);

				for (auto it = animations.begin(); it != animations.end(); ++it) {
					// TODO: Keys are not used
					std::string_view assetPath;
					if ((*it)["Path"].get(assetPath) != Json::SUCCESS || assetPath.empty()) {
						continue;
					}

					GraphicResource graphics;
					graphics.LoopMode = AnimationLoopMode::Loop;

					// Index all palette-based sprites (including UI) so they're recolored at draw time through the shared
					// palette texture - no baking, palette changes stay cheap, and nothing goes stale. Pre-RGBA (true-
					// color) graphics are kept as-is by the `palette != nullptr` guard inside RequestGraphics.
					bool keepIndexed = true;

					std::int64_t flags;
					if ((*it)["Flags"].get(flags) == Json::SUCCESS) {
						if ((flags & 0x01) == 0x01) {
							graphics.LoopMode = AnimationLoopMode::Once;
						}
						/*if ((flags & 0x02) == 0x02) {
							keepIndexed = true;
						}*/
					}

					std::int64_t paletteOffset;
					if ((*it)["PaletteOffset"].get(paletteOffset) != Json::SUCCESS || paletteOffset < 0) {
						paletteOffset = 0;
					}

					graphics.Base = RequestGraphics(assetPath, (std::uint16_t)paletteOffset, keepIndexed);
						// Remember the palette offset so the renderer/manual draws sample the right palette region for
						// an indexed sprite (0 = default sprite palette; gems use the gem-gradient rows)
						graphics.PaletteOffset = (std::uint16_t)paletteOffset;
					if (graphics.Base == nullptr) {
						continue;
					}

					std::int64_t frameOffset;
					if ((*it)["FrameOffset"].get(frameOffset) != Json::SUCCESS) {
						frameOffset = 0;
					}
					graphics.FrameOffset = (std::int32_t)frameOffset;

					graphics.AnimDuration = graphics.Base->AnimDuration;
					graphics.FrameCount = graphics.Base->FrameCount;

					std::int64_t frameCount;
					if ((*it)["FrameCount"].get(frameCount) == Json::SUCCESS) {
						graphics.FrameCount = (std::int32_t)frameCount;
					} else {
						graphics.FrameCount -= graphics.FrameOffset;
					}

					// TODO: Use AnimDuration instead
					double frameRate;
					if ((*it)["FrameRate"].get(frameRate) == Json::SUCCESS) {
						graphics.AnimDuration = (frameRate <= 0 ? -1.0f : (1.0f / (float)frameRate) * 5.0f);
					}

					// If no bounding box is provided, use the first sprite
					if (metadata->BoundingBox == Vector2i(InvalidValue, InvalidValue)) {
						// TODO: Remove this bounding box reduction
						metadata->BoundingBox = graphics.Base->FrameDimensions - Vector2i(2, 2);
					}

					const auto& states = (*it)["States"];
					if (states.isArray()) {
						for (const auto& stateItem : states) {
							std::int64_t state;
							if (stateItem.get(state) == Json::SUCCESS) {
#if defined(DEATH_DEBUG)
								// Additional checks only for Debug configuration
								for (const auto& anim : metadata->Animations) {
									if (anim.State == (AnimState)state) {
										LOGW("Animation state {} defined twice in file \"{}\"", state, path);
										break;
									}
								}
#endif
								graphics.State = (AnimState)state;
								metadata->Animations.push_back(graphics);
							}
						}
					} else if (count > 1) {
						if (!multipleAnimsNoStatesWarning) {
							multipleAnimsNoStatesWarning = true;
							LOGW("Multiple animations defined but no states specified in file \"{}\"", path);
						}
					} else {
						graphics.State = AnimState::Default;
						metadata->Animations.push_back(graphics);
					}
				}

				// Animation states must be sorted, so binary search can be used
				nCine::sort(metadata->Animations.begin(), metadata->Animations.end());
			}

			const auto& sounds = doc["Sounds"];
			if (sounds.isObject()) {
				std::size_t count = sounds.getMemberCount();
				metadata->Sounds.reserve(count);

				for (auto it = sounds.begin(); it != sounds.end(); ++it) {
					std::string_view key = it.memberName();
					const auto& assetPaths = (*it)["Paths"];
					if (key.empty() || !assetPaths.isArray() || assetPaths.empty()) {
						continue;
					}

					SoundResource sound;
#if defined(WITH_AUDIO)
					// Don't load sounds in headless mode
					if (!_isHeadless) {
						for (auto assetPathItem : assetPaths) {
							std::string_view assetPath;
							if (assetPathItem.get(assetPath) == Json::SUCCESS && !assetPath.empty()) {
								auto assetPathNormalized = fs::ToNativeSeparators(assetPath);
								auto it = _cachedSounds.find(assetPathNormalized);
								if (it != _cachedSounds.end()) {
									it->second->Flags |= GenericSoundResourceFlags::Referenced;
									sound.Buffers.push_back(it->second.get());
								} else {
									auto s = OpenContentFile(fs::CombinePath("Animations"_s, assetPathNormalized));
									auto res = _cachedSounds.emplace(assetPathNormalized, std::make_unique<GenericSoundResource>(std::move(s), assetPathNormalized));
									res.first->second->Flags |= GenericSoundResourceFlags::Referenced;
									sound.Buffers.push_back(res.first->second.get());
								}
							}
						}
					}
#endif
					metadata->Sounds.emplace(key, std::move(sound));
				}
			}
		}

		return _cachedMetadata.emplace(metadata->CacheKey, std::move(metadata)).first->second.get();
	}

	GenericGraphicResource* ContentResolver::RequestGraphics(StringView path, std::uint16_t paletteOffset, bool keepIndexed)
	{
		// First resources are requested, reset _isLoading flag, because palette should be already applied
		_isLoading = false;

		// Indexed sprites don't bake a palette, so they're cached under a dedicated key (independent of paletteOffset)
		std::uint16_t cacheKeyOffset = (keepIndexed ? IndexedGraphicsCacheKey : paletteOffset);

		auto pathNormalized = fs::ToNativeSeparators(path);
		auto it = _cachedGraphics.find(Pair(String::nullTerminatedView(pathNormalized), cacheKeyOffset));
		if (it != _cachedGraphics.end()) {
			// Already loaded - Mark as referenced
			it->second->Flags |= GenericGraphicResourceFlags::Referenced;
			return it->second.get();
		}

		if (fs::GetExtension(pathNormalized) == "aura"_s) {
			return RequestGraphicsAura(pathNormalized, paletteOffset, keepIndexed);
		}

		auto s = fs::Open(fs::CombinePath({ GetContentPath(), "Animations"_s, String(pathNormalized + ".res"_s) }), FileAccess::Read);
		auto fileSize = s->GetSize();
		if (fileSize < 4 || fileSize > 64 * 1024 * 1024) {
			// 64 MB file size limit, also if not found try to use cache
			return nullptr;
		}

		auto buffer = std::make_unique<char[]>(fileSize);
		s->Read(buffer.get(), fileSize);
		s->Dispose();

		Json::CharReaderBuilder builder;
		auto reader = std::unique_ptr<Json::CharReader>(builder.newCharReader());
		Json::Value doc; std::string errors;
		if (reader->parse(buffer.get(), buffer.get() + fileSize, &doc, &errors)) {
			// Try to load it
			std::unique_ptr<GenericGraphicResource> graphics = std::make_unique<GenericGraphicResource>();
			graphics->Flags |= GenericGraphicResourceFlags::Referenced;

			String fullPath = fs::CombinePath({ GetContentPath(), "Animations"_s, pathNormalized });
			std::unique_ptr<ITextureLoader> texLoader = ITextureLoader::createFromFile(fullPath);
			if (texLoader->hasLoaded()) {
				auto texFormat = texLoader->texFormat().internalFormat();
				if (texFormat != GL_RGBA8 && texFormat != GL_RGB8) {
					return nullptr;
				}

				std::int32_t w = texLoader->width();
				std::int32_t h = texLoader->height();
				std::uint8_t* pixels = (std::uint8_t*)texLoader->pixels();
				const std::uint32_t* palette = _palettes + paletteOffset;
				bool linearSampling = false;
				bool needsMask = true;

				std::int64_t flags;
				if (doc["Flags"].get(flags) == Json::SUCCESS) {
					// Palette already applied, keep as is
					if ((flags & 0x01) != 0x01) {
						palette = nullptr;
						// TODO: Apply linear sampling only to these images
						if ((flags & 0x02) == 0x02) {
							linearSampling = true;
						}
					}
					if ((flags & 0x08) == 0x08) {
						needsMask = false;
					}
				}

				// Keep the raw palette indices in the texture (red channel) instead of baking colors, so it can be
				// recolored at draw time by the PaletteRemap shader. Only meaningful for actually-indexed sprites.
				if (keepIndexed && palette != nullptr) {
					palette = nullptr;
					graphics->Flags |= GenericGraphicResourceFlags::Indexed;
				}

				if (needsMask) {
					graphics->Mask = std::make_unique<std::uint8_t[]>(w * h);
					for (std::int32_t i = 0; i < w * h; i++) {
						// Save original alpha value for collision checking
						graphics->Mask[i] = pixels[(i * PixelSize) + 3];
					}
				}
				if (palette != nullptr) {
					for (std::uint32_t i = 0; i < w * h; i++) {
						std::uint32_t srcIdx = i * PixelSize;
						std::uint32_t color = palette[pixels[srcIdx]];
						std::uint8_t alpha = pixels[srcIdx + 3];

						std::uint8_t r = (color >> 0) & 0xFF;
						std::uint8_t g = (color >> 8) & 0xFF;
						std::uint8_t b = (color >> 16) & 0xFF;
						std::uint8_t a = ((color >> 24) & 0xFF) * alpha / 255;

						pixels[srcIdx + 0] = r;
						pixels[srcIdx + 1] = g;
						pixels[srcIdx + 2] = b;
						pixels[srcIdx + 3] = a;
					}
				}

				if (!_isHeadless) {
					// Don't load textures in headless mode, only collision masks
					if ((graphics->Flags & GenericGraphicResourceFlags::Indexed) == GenericGraphicResourceFlags::Indexed) {
						bool paletteBaseTransparent = (((_palettes[paletteOffset] >> 24) & 0xFF) == 0);
						graphics->TextureDiffuse = CreateIndexedTexture(fullPath.data(), pixels, w, h, PixelSize, paletteBaseTransparent);
					} else {
						graphics->TextureDiffuse = std::make_unique<Texture>(fullPath.data(), Texture::Format::RGBA8, w, h);
						graphics->TextureDiffuse->LoadFromTexels(pixels, 0, 0, w, h);
					}
					graphics->TextureDiffuse->SetMinFiltering(linearSampling ? SamplerFilter::Linear : SamplerFilter::Nearest);
					graphics->TextureDiffuse->SetMagFiltering(linearSampling ? SamplerFilter::Linear : SamplerFilter::Nearest);
				}

				double animDuration;
				if (doc["Duration"].get(animDuration) != Json::SUCCESS) {
					animDuration = 0.0;
				}
				graphics->AnimDuration = (float)animDuration;

				std::int64_t frameCount;
				if (doc["FrameCount"].get(frameCount) != Json::SUCCESS) {
					frameCount = 0;
				}
				graphics->FrameCount = (std::int32_t)frameCount;

				graphics->FrameDimensions = GetVector2iFromJson(doc["FrameSize"]);
				graphics->FrameConfiguration = GetVector2iFromJson(doc["FrameConfiguration"]);
				
				graphics->Hotspot = GetVector2iFromJson(doc["Hotspot"]);
				graphics->Coldspot = GetVector2iFromJson(doc["Coldspot"], Vector2i(InvalidValue, InvalidValue));
				graphics->Gunspot = GetVector2iFromJson(doc["Gunspot"], Vector2i(InvalidValue, InvalidValue));

#if defined(DEATH_DEBUG)
				MigrateGraphics(pathNormalized);
#endif
				return _cachedGraphics.emplace(Pair(String(pathNormalized), cacheKeyOffset), std::move(graphics)).first->second.get();
			}
		}

		return nullptr;
	}

	GenericGraphicResource* ContentResolver::RequestGraphicsAura(StringView path, std::uint16_t paletteOffset, bool keepIndexed)
	{
		auto s = OpenContentFile(fs::CombinePath("Animations"_s, path));

		auto fileSize = s->GetSize();
		if (fileSize < 16 || fileSize > 64 * 1024 * 1024) {
			// 64 MB file size limit, also if not found try to use cache
			return nullptr;
		}

		std::uint64_t signature1 = s->ReadValueAsLE<std::uint64_t>();
		std::uint32_t signature2 = s->ReadValueAsLE<std::uint16_t>();
		std::uint8_t version = s->ReadValue<std::uint8_t>();
		std::uint8_t flags = s->ReadValue<std::uint8_t>();

		if (signature1 != 0xB8EF8498E2BFBBEF || signature2 != 0x208F || version != 2 || (flags & 0x80) != 0x80) {
			return nullptr;
		}

		std::uint8_t channelCount = s->ReadValue<std::uint8_t>();
		std::uint32_t frameDimensionsX = s->ReadValueAsLE<std::uint32_t>();
		std::uint32_t frameDimensionsY = s->ReadValueAsLE<std::uint32_t>();

		std::uint8_t frameConfigurationX = s->ReadValue<std::uint8_t>();
		std::uint8_t frameConfigurationY = s->ReadValue<std::uint8_t>();
		std::uint16_t frameCount = s->ReadValueAsLE<std::uint16_t>();
		std::uint16_t animDuration = s->ReadValueAsLE<std::uint16_t>();

		std::uint16_t hotspotX = s->ReadValueAsLE<std::uint16_t>();
		std::uint16_t hotspotY = s->ReadValueAsLE<std::uint16_t>();

		std::uint16_t coldspotX = s->ReadValueAsLE<std::uint16_t>();
		std::uint16_t coldspotY = s->ReadValueAsLE<std::uint16_t>();

		std::uint16_t gunspotX = s->ReadValueAsLE<std::uint16_t>();
		std::uint16_t gunspotY = s->ReadValueAsLE<std::uint16_t>();

		std::uint32_t width = frameDimensionsX * frameConfigurationX;
		std::uint32_t height = frameDimensionsY * frameConfigurationY;

		std::unique_ptr<std::uint8_t[]> pixels = std::make_unique<std::uint8_t[]>(width * height * PixelSize);

		ReadImageFromFile(s, pixels.get(), width, height, channelCount);

		std::unique_ptr<GenericGraphicResource> graphics = std::make_unique<GenericGraphicResource>();
		graphics->Flags |= GenericGraphicResourceFlags::Referenced;

		const std::uint32_t* palette = _palettes + paletteOffset;
		bool linearSampling = false;
		bool needsMask = true;
		if ((flags & 0x01) == 0x01) {
			palette = nullptr;
			linearSampling = true;
		}
		if ((flags & 0x02) == 0x02) {
			needsMask = false;
		}

		// Keep the raw palette indices in the texture (red channel) instead of baking colors, so the sprite can be
		// recolored at draw time by the PaletteRemap shader. Only meaningful for actually palette-based sprites.
		if (keepIndexed && palette != nullptr) {
			palette = nullptr;
			graphics->Flags |= GenericGraphicResourceFlags::Indexed;
		}

		if (needsMask) {
			graphics->Mask = std::make_unique<std::uint8_t[]>(width * height);
			for (std::uint32_t i = 0; i < width * height; i++) {
				// Save the original alpha for collision checking. The decoded buffer is tightly packed to
				// `channelCount` bytes/pixel: a 1-channel (index-only) sprite is opaque except index 0 (transparent),
				// a 2-channel sprite has explicit alpha in green, anything wider keeps alpha in the 4th byte.
				if (channelCount == 1) {
					graphics->Mask[i] = (pixels[i] != 0 ? 255 : 0);
				} else if (channelCount == 2) {
					graphics->Mask[i] = pixels[(i * 2) + 1];
				} else {
					graphics->Mask[i] = pixels[(i * channelCount) + 3];
				}
			}
		}
		if (palette != nullptr) {
			for (std::uint32_t i = 0; i < width * height; i++) {
				std::uint32_t srcIdx = i * PixelSize;
				std::uint32_t color = palette[pixels[srcIdx]];
				std::uint8_t alpha = pixels[srcIdx + 3];

				std::uint8_t r = (color >> 0) & 0xFF;
				std::uint8_t g = (color >> 8) & 0xFF;
				std::uint8_t b = (color >> 16) & 0xFF;
				std::uint8_t a = ((color >> 24) & 0xFF) * alpha / 255;

				pixels[srcIdx + 0] = r;
				pixels[srcIdx + 1] = g;
				pixels[srcIdx + 2] = b;
				pixels[srcIdx + 3] = a;
			}
		}

		if (!_isHeadless) {
			// Don't load textures in headless mode, only collision masks
			if ((graphics->Flags & GenericGraphicResourceFlags::Indexed) == GenericGraphicResourceFlags::Indexed) {
				bool paletteBaseTransparent = (((_palettes[paletteOffset] >> 24) & 0xFF) == 0);
				graphics->TextureDiffuse = CreateIndexedTexture(path.data(), pixels.get(), width, height, channelCount, paletteBaseTransparent);
			} else {
				graphics->TextureDiffuse = std::make_unique<Texture>(path.data(), Texture::Format::RGBA8, width, height);
				graphics->TextureDiffuse->LoadFromTexels(pixels.get(), 0, 0, width, height);
			}
			graphics->TextureDiffuse->SetMinFiltering(linearSampling ? SamplerFilter::Linear : SamplerFilter::Nearest);
			graphics->TextureDiffuse->SetMagFiltering(linearSampling ? SamplerFilter::Linear : SamplerFilter::Nearest);
		}

		// AnimDuration is multiplied by 256 before saving, so divide it here back
		graphics->AnimDuration = animDuration / 256.0f;
		graphics->FrameDimensions = Vector2i(frameDimensionsX, frameDimensionsY);
		graphics->FrameConfiguration = Vector2i(frameConfigurationX, frameConfigurationY);
		graphics->FrameCount = frameCount;

		if (hotspotX != UINT16_MAX || hotspotY != UINT16_MAX) {
			graphics->Hotspot = Vector2i(hotspotX, hotspotY);
		} else {
			graphics->Hotspot = Vector2i();
		}

		if (coldspotX != UINT16_MAX || coldspotY != UINT16_MAX) {
			graphics->Coldspot = Vector2i(coldspotX, coldspotY);
		} else {
			graphics->Coldspot = Vector2i(InvalidValue, InvalidValue);
		}

		if (gunspotX != UINT16_MAX || gunspotY != UINT16_MAX) {
			graphics->Gunspot = Vector2i(gunspotX, gunspotY);
		} else {
			graphics->Gunspot = Vector2i(InvalidValue, InvalidValue);
		}

		// Indexed sprites are cached under a dedicated key (matching the lookup in RequestGraphics) so they don't
		// collide with the baked variant of the same sprite
		std::uint16_t cacheKeyOffset = (keepIndexed ? IndexedGraphicsCacheKey : paletteOffset);
		return _cachedGraphics.emplace(Pair(String(path), cacheKeyOffset), std::move(graphics)).first->second.get();
	}

	void ContentResolver::ReadImageFromFile(std::unique_ptr<Stream>& s, std::uint8_t* data, std::int32_t width, std::int32_t height, std::int32_t channelCount)
	{
		typedef union {
			struct {
				unsigned char r, g, b, a;
			} rgba;
			unsigned int v;
		} rgba_t;

		#define QOI_OP_INDEX  0x00 /* 00xxxxxx */
		#define QOI_OP_DIFF   0x40 /* 01xxxxxx */
		#define QOI_OP_LUMA   0x80 /* 10xxxxxx */
		#define QOI_OP_RUN    0xc0 /* 11xxxxxx */
		#define QOI_OP_RGB    0xfe /* 11111110 */
		#define QOI_OP_RGBA   0xff /* 11111111 */

		#define QOI_MASK_2    0xc0 /* 11000000 */

		#define QOI_COLOR_HASH(C) (C.rgba.r*3 + C.rgba.g*5 + C.rgba.b*7 + C.rgba.a*11)

		rgba_t index[64] { };
		rgba_t px;
		std::int32_t run = 0;
		std::int32_t px_len = width * height * channelCount;

		px.rgba.r = 0;
		px.rgba.g = 0;
		px.rgba.b = 0;
		px.rgba.a = 255;

		for (std::int32_t px_pos = 0; px_pos < px_len; px_pos += channelCount) {
			if (run > 0) {
				run--;
			} else {
				std::int32_t b1 = s->ReadValue<std::uint8_t>();

				if (b1 == QOI_OP_RGB) {
					px.rgba.r = s->ReadValue<std::uint8_t>();
					px.rgba.g = (channelCount >= 2 ? s->ReadValue<std::uint8_t>() : 0);
					px.rgba.b = (channelCount >= 3 ? s->ReadValue<std::uint8_t>() : 0);
				} else if (b1 == QOI_OP_RGBA) {
					px.rgba.r = s->ReadValue<std::uint8_t>();
					px.rgba.g = s->ReadValue<std::uint8_t>();
					px.rgba.b = s->ReadValue<std::uint8_t>();
					px.rgba.a = s->ReadValue<std::uint8_t>();
				} else if ((b1 & QOI_MASK_2) == QOI_OP_INDEX) {
					px = index[b1];
				} else if ((b1 & QOI_MASK_2) == QOI_OP_DIFF) {
					px.rgba.r += ((b1 >> 4) & 0x03) - 2;
					px.rgba.g += ((b1 >> 2) & 0x03) - 2;
					px.rgba.b += (b1 & 0x03) - 2;
				} else if ((b1 & QOI_MASK_2) == QOI_OP_LUMA) {
					std::int32_t b2 = s->ReadValue<std::uint8_t>();
					std::int32_t vg = (b1 & 0x3f) - 32;
					px.rgba.r += vg - 8 + ((b2 >> 4) & 0x0f);
					px.rgba.g += vg;
					px.rgba.b += vg - 8 + (b2 & 0x0f);
				} else if ((b1 & QOI_MASK_2) == QOI_OP_RUN) {
					run = (b1 & 0x3f);
				}

				index[QOI_COLOR_HASH(px) & (64 - 1)] = px;
			}

			*(rgba_t*)(data + px_pos) = px;
		}
	}

	void ContentResolver::ExpandTileDiffuse(std::uint8_t* pixelsOffset, std::uint32_t widthWithPadding, std::uint32_t bytesPerPixel)
	{
		// Top
		for (std::uint32_t x = 0; x < TileSet::DefaultTileSize; x++) {
			std::uint32_t from = (1 * widthWithPadding + (x + 1)) * bytesPerPixel;
			std::uint32_t to = (0 * widthWithPadding + (x + 1)) * bytesPerPixel;
			std::memcpy(&pixelsOffset[to], &pixelsOffset[from], bytesPerPixel);
		}

		// Bottom
		for (std::uint32_t x = 0; x < TileSet::DefaultTileSize; x++) {
			std::uint32_t from = (TileSet::DefaultTileSize * widthWithPadding + (x + 1)) * bytesPerPixel;
			std::uint32_t to = ((TileSet::DefaultTileSize + 1) * widthWithPadding + (x + 1)) * bytesPerPixel;
			std::memcpy(&pixelsOffset[to], &pixelsOffset[from], bytesPerPixel);
		}

		// Left
		for (std::uint32_t y = 0; y < TileSet::DefaultTileSize; y++) {
			std::uint32_t from = ((y + 1) * widthWithPadding + 1) * bytesPerPixel;
			std::uint32_t to = ((y + 1) * widthWithPadding + 0) * bytesPerPixel;
			std::memcpy(&pixelsOffset[to], &pixelsOffset[from], bytesPerPixel);
		}

		// Right
		for (std::uint32_t y = 0; y < TileSet::DefaultTileSize; y++) {
			std::uint32_t from = ((y + 1) * widthWithPadding + TileSet::DefaultTileSize) * bytesPerPixel;
			std::uint32_t to = ((y + 1) * widthWithPadding + (TileSet::DefaultTileSize + 1)) * bytesPerPixel;
			std::memcpy(&pixelsOffset[to], &pixelsOffset[from], bytesPerPixel);
		}

		// Corners (TL, TR, BL, BR)
		{
			std::uint32_t from = (0 * widthWithPadding + 1) * bytesPerPixel;
			std::uint32_t to = (0 * widthWithPadding + 0) * bytesPerPixel;
			std::memcpy(&pixelsOffset[to], &pixelsOffset[from], bytesPerPixel);
		}
		{
			std::uint32_t from = (0 * widthWithPadding + TileSet::DefaultTileSize) * bytesPerPixel;
			std::uint32_t to = (0 * widthWithPadding + (TileSet::DefaultTileSize + 1)) * bytesPerPixel;
			std::memcpy(&pixelsOffset[to], &pixelsOffset[from], bytesPerPixel);
		}
		{
			std::uint32_t from = ((TileSet::DefaultTileSize + 1) * widthWithPadding + 1) * bytesPerPixel;
			std::uint32_t to = ((TileSet::DefaultTileSize + 1) * widthWithPadding + 0) * bytesPerPixel;
			std::memcpy(&pixelsOffset[to], &pixelsOffset[from], bytesPerPixel);
		}
		{
			std::uint32_t from = ((TileSet::DefaultTileSize + 1) * widthWithPadding + TileSet::DefaultTileSize) * bytesPerPixel;
			std::uint32_t to = ((TileSet::DefaultTileSize + 1) * widthWithPadding + (TileSet::DefaultTileSize + 1)) * bytesPerPixel;
			std::memcpy(&pixelsOffset[to], &pixelsOffset[from], bytesPerPixel);
		}
	}

	std::unique_ptr<Texture> ContentResolver::CreateIndexedTexture(const char* name, const std::uint8_t* pixels, std::int32_t width, std::int32_t height, std::int32_t srcChannels, bool paletteBaseTransparent)
	{
		// `pixels` holds the palette index in the red (first) channel. The smallest lossless texture format is R8
		// (index only) when transparency is on/off and reproducible by the palette base (entry 0 transparent), or
		// RG8 (index + alpha sampled into .a via swizzle) when alpha is partial or the palette base is opaque (gems).
		// 1/2-channel input is already pre-packed (by the asset converter) and uploads directly with no scan/repack.
		std::int32_t count = width * height;
		std::unique_ptr<Texture> texture;

		if (srcChannels == 2) {
			// Pre-packed (index, alpha) - upload directly as RG8
			texture = std::make_unique<Texture>(name, Texture::Format::RG8, width, height);
			texture->LoadFromTexels(pixels, 0, 0, width, height);
			texture->SetSwizzle(SwizzleChannel::Red, SwizzleChannel::Green, SwizzleChannel::Blue, SwizzleChannel::Green);
		} else if (srcChannels == 1) {
			if (paletteBaseTransparent) {
				// Pre-packed index only, transparency via palette entry 0 - upload directly as R8
				texture = std::make_unique<Texture>(name, Texture::Format::R8, width, height);
				texture->LoadFromTexels(pixels, 0, 0, width, height);
			} else {
				// Opaque palette base (e.g., gems): synthesize on/off alpha from the index so transparency survives
				std::unique_ptr<std::uint8_t[]> packed = std::make_unique<std::uint8_t[]>(count * 2);
				for (std::int32_t i = 0; i < count; i++) {
					packed[(i * 2) + 0] = pixels[i];
					packed[(i * 2) + 1] = (pixels[i] == 0 ? 0 : 255);
				}
				texture = std::make_unique<Texture>(name, Texture::Format::RG8, width, height);
				texture->LoadFromTexels(packed.get(), 0, 0, width, height);
				texture->SetSwizzle(SwizzleChannel::Red, SwizzleChannel::Green, SwizzleChannel::Blue, SwizzleChannel::Green);
			}
		} else {
			// RGBA source that isn't pre-packed (a user-supplied PNG using the indexed pipeline). Pack (index, alpha)
			// into RG8 in a single pass: always correct (fully-opaque sprites just waste the alpha byte) and never
			// crashes on user content. Indexed tilesets don't reach this branch - RequestTileSet hands them over as a
			// single index channel, so they take the R8 path above.
			std::unique_ptr<std::uint8_t[]> packed = std::make_unique<std::uint8_t[]>(count * 2);
			for (std::int32_t i = 0; i < count; i++) {
				packed[(i * 2) + 0] = pixels[(i * srcChannels) + 0];
				packed[(i * 2) + 1] = pixels[(i * srcChannels) + 3];
			}
			texture = std::make_unique<Texture>(name, Texture::Format::RG8, width, height);
			texture->LoadFromTexels(packed.get(), 0, 0, width, height);
			texture->SetSwizzle(SwizzleChannel::Red, SwizzleChannel::Green, SwizzleChannel::Blue, SwizzleChannel::Green);
		}
		return texture;
	}

	std::unique_ptr<Tiles::TileSet> ContentResolver::RequestTileSet(StringView path, std::uint16_t captionTileId, bool applyPalette, const std::uint8_t* paletteRemapping)
	{
		// Try "Content" directory first, then "Cache" directory
		String fullPath;
		if (_pathHandler) {
			fullPath = _pathHandler(String(path + ".j2t"_s));
		}
		if (fullPath.empty()) {
			fullPath = fs::CombinePath({ GetContentPath(), "Tilesets"_s, String(path + ".j2t"_s) });
			if (!fs::IsReadableFile(fullPath)) {
				fullPath = fs::CombinePath({ GetCachePath(), "Tilesets"_s, String(path + ".j2t"_s) });
			}
		}

		auto s = fs::Open(fullPath, FileAccess::Read, 16 * 1024);
		if (!s->IsValid()) {
			return nullptr;
		}

		std::uint64_t signature1 = s->ReadValueAsLE<std::uint64_t>();
		std::uint16_t signature2 = s->ReadValueAsLE<std::uint16_t>();
		std::uint8_t version = s->ReadValue<std::uint8_t>();
		/*std::uint8_t flags =*/ s->ReadValue<std::uint8_t>();
		DEATH_ASSERT(signature1 == 0xB8EF8498E2BFBBEF && signature2 == 0x208F && version == 2,
			("Tile set \"{}\" has invalid signature", fullPath), nullptr);

		std::uint8_t channelCount = s->ReadValue<std::uint8_t>();
		std::uint32_t width = s->ReadValueAsLE<std::uint32_t>();
		std::uint32_t height = s->ReadValueAsLE<std::uint32_t>();
		std::uint16_t tileCount = s->ReadValueAsLE<std::uint16_t>();

		// Read compressed palette and mask
		std::int32_t compressedSize = s->ReadValueAsLE<std::int32_t>();

		DeflateStream uc(*s, compressedSize);

		ReadTilesetPalette(uc, applyPalette);

		// Mark individual tiles as 32-bit or 8-bit
		std::unique_ptr<uint8_t[]> is32bitTile;
		if (!_isHeadless) {
			is32bitTile = std::make_unique<std::uint8_t[]>((tileCount + 7) / 8);
			uc.Read(is32bitTile.get(), (tileCount + 7) / 8);
		} else {
			uc.Seek((tileCount + 7) / 8, SeekOrigin::Current);
		}

		// Mask
		std::uint32_t maskSizeBits;
		std::unique_ptr<std::uint8_t[]> mask = ReadTilesetMask(uc, maskSizeBits);

		std::unique_ptr<Texture> textureDiffuse;
		std::unique_ptr<Color[]> captionTile;
		// Per-tile flag (1 = fully opaque diffuse); used to cull hidden debris. Set by BuildTilesetDiffuse().
		std::unique_ptr<std::uint8_t[]> tileDiffuseOpaque;
		// Whether tiles keep raw palette indices (recolored at draw time) vs baked colors; set by BuildTilesetDiffuse()
		bool indexTiles = false;

		// The image content follows the compressed block, so it's read from the raw stream (headless builds masks only)
		if (!_isHeadless) {
			textureDiffuse = BuildTilesetDiffuse(s, fullPath.data(), channelCount, width, height, tileCount,
				is32bitTile.get(), paletteRemapping, captionTileId, indexTiles, tileDiffuseOpaque, captionTile);
		}

		if (!uc.IsValid()) {
			return nullptr;
		}

		auto tileSet = std::make_unique<Tiles::TileSet>(path,
			tileCount, Death::move(textureDiffuse), Death::move(mask),
			maskSizeBits, Death::move(captionTile), tileDiffuseOpaque.get());
		tileSet->IsIndexed = indexTiles;
		return tileSet;
	}

	void ContentResolver::ReadTilesetPalette(Stream& uc, bool applyPalette)
	{
		if (!applyPalette || _isHeadless) {
			uc.Seek(ColorsPerPalette * sizeof(std::uint32_t), SeekOrigin::Current);
			return;
		}

		std::uint32_t newPalette[ColorsPerPalette];
		for (std::size_t i = 0; i < arraySize(newPalette); i++) {
			newPalette[i] = uc.ReadValueAsLE<std::uint32_t>();
		}

		if (std::memcmp(_palettes, newPalette, ColorsPerPalette * sizeof(std::uint32_t)) != 0) {
			// The sprite palette changed. Indexed sprites/tiles recolor from the live palette texture, so they don't
			// need reloading; only the baked fonts are dropped so they rebake with the new palette.
			if (_isLoading) {
				for (std::int32_t i = 0; i < (std::int32_t)FontType::Count; i++) {
					_fonts[i] = nullptr;
				}
			}

			std::memcpy(_palettes, newPalette, ColorsPerPalette * sizeof(std::uint32_t));
			RecreateGemPalettes();
		}
	}

	std::unique_ptr<std::uint8_t[]> ContentResolver::ReadTilesetMask(Stream& uc, std::uint32_t& maskSizeBits)
	{
		std::uint32_t maskSize = uc.ReadValueAsLE<std::uint32_t>();
		maskSizeBits = maskSize * 8;

		// Unpack the bitmask to one byte per pixel
		std::unique_ptr<std::uint8_t[]> mask = std::make_unique<std::uint8_t[]>(maskSizeBits);
		for (std::uint32_t j = 0; j < maskSize; j++) {
			std::uint8_t idx = uc.ReadValue<std::uint8_t>();
			for (std::uint32_t k = 0; k < 8; k++) {
				mask[8 * j + k] = (((idx >> k) & 0x01) != 0);
			}
		}
		return mask;
	}

	std::unique_ptr<Texture> ContentResolver::BuildTilesetDiffuse(std::unique_ptr<Stream>& s, const char* name, std::uint8_t channelCount,
		std::uint32_t width, std::uint32_t height, std::uint16_t tileCount, const std::uint8_t* is32bitTile,
		const std::uint8_t* paletteRemapping, std::uint16_t captionTileId, bool& indexTiles,
		std::unique_ptr<std::uint8_t[]>& tileDiffuseOpaque, std::unique_ptr<Color[]>& captionTile)
	{
		// 32-bit (true-color) tiles have no palette index, so a tileset containing any must stay baked as RGBA;
		// an all-8-bit tileset keeps raw palette indices and recolors at draw time (uploaded as R8).
		indexTiles = true;
		for (std::int32_t t = 0; t < (std::int32_t)tileCount; t++) {
			if ((is32bitTile[t / 8] & (1 << (t & 7))) != 0) {
				indexTiles = false;
				break;
			}
		}

		// Load raw pixels. The data is laid out `channelCount` bytes per pixel (1 = index only, 4 = RGBA / index-in-red;
		// an 8-bit tile keeps its palette index in the first byte either way), but ReadImageFromFile always writes a full
		// 4-byte RGBA per pixel (overlapping for narrower strides), so the buffer must be sized for 4 bytes/pixel even
		// when channelCount is 1 - otherwise the final pixel's write overruns the heap.
		std::unique_ptr<std::uint8_t[]> pixels = std::make_unique<std::uint8_t[]>(width * height * 4);
		ReadImageFromFile(s, pixels.get(), width, height, channelCount);

		// Build the atlas with 1px padding around each tile (so sampling never bleeds across tiles). Indexed output
		// is a single index channel (-> R8), baked output is RGBA.
		const std::uint32_t tilesPerRow = width / TileSet::DefaultTileSize;
		const std::uint32_t tilesPerColumn = height / TileSet::DefaultTileSize;
		const std::uint32_t paddedWidth = width + (2 * tilesPerRow);
		const std::uint32_t paddedHeight = height + (2 * tilesPerColumn);
		const std::uint32_t dstChannels = (indexTiles ? 1u : 4u);

		std::unique_ptr<std::uint8_t[]> atlas = std::make_unique<std::uint8_t[]>(paddedWidth * paddedHeight * dstChannels);
		tileDiffuseOpaque = std::make_unique<std::uint8_t[]>(tileCount);

		for (std::uint32_t ty = 0; ty < tilesPerColumn; ty++) {
			for (std::uint32_t tx = 0; tx < tilesPerRow; tx++) {
				const std::uint32_t srcX = tx * TileSet::DefaultTileSize;
				const std::uint32_t srcY = ty * TileSet::DefaultTileSize;
				const std::uint32_t dstX = tx * (TileSet::DefaultTileSize + 2);
				const std::uint32_t dstY = ty * (TileSet::DefaultTileSize + 2);
				const std::int32_t tileIdx = ty * tilesPerRow + tx;
				const bool is32bit = ((is32bitTile[tileIdx / 8] & (1 << (tileIdx & 7))) != 0);

				std::uint8_t* dstTile = &atlas[(dstY * paddedWidth + dstX) * dstChannels];
				bool opaque = true;

				for (std::uint32_t y = 0; y < TileSet::DefaultTileSize; y++) {
					for (std::uint32_t x = 0; x < TileSet::DefaultTileSize; x++) {
						const std::uint32_t src = ((srcY + y) * width + (srcX + x)) * channelCount;
						const std::uint32_t dst = ((y + 1) * paddedWidth + (x + 1)) * dstChannels;

						if (is32bit) {
							// True-color: copy RGBA straight through (only happens in baked tilesets, so dstChannels == 4)
							dstTile[dst + 0] = pixels[src + 0];
							dstTile[dst + 1] = pixels[src + 1];
							dstTile[dst + 2] = pixels[src + 2];
							dstTile[dst + 3] = pixels[src + 3];
							if (pixels[src + 3] != 255) {
								opaque = false;
							}
						} else {
							// 8-bit: index is the first byte; transparency is index 0 (carried as on/off alpha when the
							// source is 4-channel). Remapping preserves transparency - a transparent source stays index 0
							// even if the remap table moves index 0 elsewhere.
							const std::uint8_t origIndex = pixels[src];
							const bool transparent = (channelCount >= 4 ? (pixels[src + 3] == 0) : (origIndex == 0));
							const std::uint8_t index = (transparent ? 0 : (paletteRemapping != nullptr ? paletteRemapping[origIndex] : origIndex));

							if (indexTiles) {
								dstTile[dst] = index;
								if (transparent) {
									opaque = false;
								}
							} else {
								const std::uint32_t color = _palettes[index];
								const std::uint8_t alpha = (transparent ? 0 : (std::uint8_t)((color >> 24) & 0xFF));
								dstTile[dst + 0] = (color >> 0) & 0xFF;
								dstTile[dst + 1] = (color >> 8) & 0xFF;
								dstTile[dst + 2] = (color >> 16) & 0xFF;
								dstTile[dst + 3] = alpha;
								if (alpha != 255) {
									opaque = false;
								}
							}
						}
					}
				}

				if (tileIdx < (std::int32_t)tileCount) {
					tileDiffuseOpaque[tileIdx] = (opaque ? 1 : 0);
				}

				ExpandTileDiffuse(dstTile, paddedWidth, dstChannels);
			}
		}

		std::unique_ptr<Texture> textureDiffuse;
		if (indexTiles) {
			// Index 0 is the transparent palette entry (row 0), so this uploads directly as R8 (no per-pixel alpha)
			const bool paletteBaseTransparent = (((_palettes[0] >> 24) & 0xFF) == 0);
			textureDiffuse = CreateIndexedTexture(name, atlas.get(), paddedWidth, paddedHeight, 1, paletteBaseTransparent);
		} else {
			textureDiffuse = std::make_unique<Texture>(name, Texture::Format::RGBA8, paddedWidth, paddedHeight);
			textureDiffuse->LoadFromTexels(atlas.get(), 0, 0, paddedWidth, paddedHeight);
		}
		textureDiffuse->SetMinFiltering(SamplerFilter::Nearest);
		textureDiffuse->SetMagFiltering(SamplerFilter::Nearest);

		// Caption tile (level-select thumbnail): downscale one tile 1:3 vertically, averaging 3 source rows. Resolve
		// 8-bit indices through the palette here (32-bit tiles already hold RGB).
		if (captionTileId > 0) {
			const std::uint32_t tileX = (captionTileId % tilesPerRow) * TileSet::DefaultTileSize;
			const std::uint32_t tileY = (captionTileId / tilesPerRow) * TileSet::DefaultTileSize;
			if (tileX + TileSet::DefaultTileSize <= width && tileY + TileSet::DefaultTileSize <= height) {
				const bool captionIs32bit = ((is32bitTile[captionTileId / 8] & (1 << (captionTileId & 7))) != 0);
				captionTile = std::make_unique<Color[]>(TileSet::DefaultTileSize * TileSet::DefaultTileSize / 3);

				for (std::uint32_t y = 0; y < TileSet::DefaultTileSize / 3; y++) {
					for (std::uint32_t x = 0; x < TileSet::DefaultTileSize; x++) {
						std::uint32_t r = 0, g = 0, b = 0;
						for (std::uint32_t row = 0; row < 3; row++) {
							const std::uint32_t src = ((tileY + y * 3 + row) * width + (tileX + x)) * channelCount;
							if (captionIs32bit) {
								r += pixels[src + 0]; g += pixels[src + 1]; b += pixels[src + 2];
							} else {
								const std::uint32_t color = _palettes[pixels[src]];
								r += (color >> 0) & 0xFF; g += (color >> 8) & 0xFF; b += (color >> 16) & 0xFF;
							}
						}
						captionTile[y * TileSet::DefaultTileSize + x] = Color((std::uint8_t)(r / 3), (std::uint8_t)(g / 3), (std::uint8_t)(b / 3));
					}
				}
			}
		}

		return textureDiffuse;
	}

	bool ContentResolver::LevelExists(StringView levelName)
	{
		// Try "Content" directory first, then "Cache" directory
		auto pathNormalized = fs::ToNativeSeparators(levelName);
		return (fs::IsReadableFile(fs::CombinePath({ GetContentPath(), "Episodes"_s, String(pathNormalized + ".j2l"_s) })) ||
				fs::IsReadableFile(fs::CombinePath({ GetCachePath(), "Episodes"_s, String(pathNormalized + ".j2l"_s) })));
	}

	bool ContentResolver::TryLoadLevel(StringView path, GameDifficulty difficulty, LevelDescriptor& descriptor)
	{
		// Try "Content" directory first, then "Cache" directory
		auto pathNormalized = fs::ToNativeSeparators(path);
		if (_pathHandler) {
			descriptor.FullPath = _pathHandler(String(pathNormalized + ".j2l"_s));
		}
		if (descriptor.FullPath.empty()) {
			descriptor.FullPath = fs::CombinePath({ GetContentPath(), "Episodes"_s, String(pathNormalized + ".j2l"_s) });
			if (!fs::IsReadableFile(descriptor.FullPath)) {
				descriptor.FullPath = fs::CombinePath({ GetCachePath(), "Episodes"_s, String(pathNormalized + ".j2l"_s) });
			}
		}

		auto s = fs::Open(descriptor.FullPath, FileAccess::Read, 16 * 1024);
		if (!s->IsValid()) return false;

		std::uint64_t signature = s->ReadValueAsLE<std::uint64_t>();
		std::uint8_t fileType = s->ReadValue<std::uint8_t>();
		DEATH_ASSERT(signature == 0x2095A59FF0BFBBEF && fileType == LevelFile,
			("Level \"{}\" has invalid signature", descriptor.FullPath), false);

		LevelFlags flags = (LevelFlags)s->ReadValueAsLE<std::uint16_t>();

		// Read compressed data
		std::int32_t compressedSize = s->ReadValueAsLE<std::int32_t>();

		DeflateStream uc(*s, compressedSize);

		// Read metadata
		std::uint8_t stringSize = uc.ReadValue<std::uint8_t>();
		descriptor.DisplayName = String(NoInit, stringSize);
		uc.Read(descriptor.DisplayName.data(), stringSize);

		stringSize = uc.ReadValue<std::uint8_t>();
		descriptor.NextLevel = String(NoInit, stringSize);
		uc.Read(descriptor.NextLevel.data(), stringSize);

		stringSize = uc.ReadValue<std::uint8_t>();
		descriptor.SecretLevel = String(NoInit, stringSize);
		uc.Read(descriptor.SecretLevel.data(), stringSize);

		stringSize = uc.ReadValue<std::uint8_t>();
		descriptor.BonusLevel = String(NoInit, stringSize);
		uc.Read(descriptor.BonusLevel.data(), stringSize);

		// Default Tileset
		stringSize = uc.ReadValue<std::uint8_t>();
		String defaultTileset(NoInit, stringSize);
		uc.Read(defaultTileset.data(), stringSize);

		// Default Music
		stringSize = uc.ReadValue<std::uint8_t>();
		descriptor.MusicPath = String(NoInit, stringSize);
		uc.Read(descriptor.MusicPath.data(), stringSize);

		uint32_t rawAmbientColor = uc.ReadValueAsLE<std::uint32_t>();
		descriptor.AmbientColor = Vector4f((rawAmbientColor & 0xff) / 255.0f, ((rawAmbientColor >> 8) & 0xff) / 255.0f,
			((rawAmbientColor >> 16) & 0xff) / 255.0f, ((rawAmbientColor >> 24) & 0xff) / 255.0f);

		descriptor.Weather = (WeatherType)uc.ReadValue<std::uint8_t>();
		descriptor.WeatherIntensity = uc.ReadValue<std::uint8_t>();
		descriptor.WaterLevel = uc.ReadValueAsLE<std::uint16_t>();

		std::uint16_t captionTileId = uc.ReadValueAsLE<std::uint16_t>();

		PitType pitType;
		if ((flags & LevelFlags::HasPit) == LevelFlags::HasPit) {
			pitType = ((flags & LevelFlags::HasPitInstantDeath) == LevelFlags::HasPitInstantDeath ? PitType::InstantDeathPit : PitType::FallForever);
		} else {
			pitType = PitType::StandOnPlatform;
		}

		bool hasCustomPalette = ((flags & LevelFlags::UseLevelPalette) == LevelFlags::UseLevelPalette);
		if (hasCustomPalette) {
			std::uint32_t newPalette[ColorsPerPalette];
			for (std::size_t i = 0; i < arraySize(newPalette); i++) {
				newPalette[i] = uc.ReadValueAsLE<std::uint32_t>();
			}

			if (!_isHeadless && std::memcmp(_palettes, newPalette, ColorsPerPalette * sizeof(std::uint32_t)) != 0) {
				// Palettes differs, drop all cached resources, so it will be reloaded with new palette
				if (_isLoading) {
					// Indexed sprites/tiles recolor from the live palette texture and need no reload; only the baked
					// fonts are dropped to rebake with the new palette
					for (std::int32_t i = 0; i < (std::int32_t)FontType::Count; i++) {
						_fonts[i] = nullptr;
					}
				}

				std::memcpy(_palettes, newPalette, ColorsPerPalette * sizeof(std::uint32_t));
				RecreateGemPalettes();
			}
		}

		std::uint8_t additionalPaletteCount = uc.ReadValue<std::uint8_t>();
		for (std::int32_t i = 0; i < additionalPaletteCount; i++) {
			std::uint8_t nameLength = uc.ReadValue<std::uint8_t>();
			String name(NoInit, nameLength);
			uc.Read(name.data(), nameLength);
			std::uint32_t palette[ColorsPerPalette];
			for (std::size_t i = 0; i < arraySize(palette); i++) {
				palette[i] = uc.ReadValueAsLE<std::uint32_t>();
			}

			// TODO: Store and use the palette (if not headless)
		}

		descriptor.TileMap = std::make_unique<Tiles::TileMap>(defaultTileset, captionTileId, !hasCustomPalette);
		descriptor.TileMap->SetPitType(pitType);

		// Extra Tilesets
		std::uint8_t extraTilesetCount = uc.ReadValue<std::uint8_t>();
		for (std::uint32_t i = 0; i < extraTilesetCount; i++) {
			std::uint8_t tilesetFlags = uc.ReadValue<std::uint8_t>();

			stringSize = uc.ReadValue<std::uint8_t>();
			String extraTileset{NoInit, stringSize};
			uc.Read(extraTileset.data(), stringSize);

			std::uint16_t offset = uc.ReadValueAsLE<std::uint16_t>();
			std::uint16_t count = uc.ReadValueAsLE<std::uint16_t>();

			std::uint8_t paletteRemapping[ColorsPerPalette];
			bool isRemapped = ((tilesetFlags & 0x01) == 0x01);
			bool is24bit = ((tilesetFlags & 0x02) == 0x02);
			if (isRemapped) {
				if (is24bit) {
					// Alternate palette index
					paletteRemapping[0] = uc.ReadValue<std::uint8_t>();
				} else {
					uc.Read(paletteRemapping, sizeof(paletteRemapping));
				}
			}

			descriptor.TileMap->AddTileSet(extraTileset, offset, count, isRemapped ? paletteRemapping : nullptr);
		}

		if (!descriptor.TileMap->IsValid()) {
			// Cannot load one of required tilesets (errors already logged by TileMap)
			return false;
		}

		// Overriden tiles (diffuse and mask)
		std::uint32_t overridenTilesCount = uc.ReadVariableUint32();
		if (!_isHeadless) {
			for (std::uint32_t i = 0; i < overridenTilesCount; i++) {
				std::uint16_t tileId = uc.ReadValueAsLE<std::uint16_t>();
				std::uint8_t tileDiffuseRaw[TileSet::DefaultTileSize * TileSet::DefaultTileSize];
				uc.Read(tileDiffuseRaw, sizeof(tileDiffuseRaw));

				bool overrideIndexed = descriptor.TileMap->IsTileSetIndexed(tileId);
					std::uint32_t tileDiffuse[(TileSet::DefaultTileSize + 2) * (TileSet::DefaultTileSize + 2)];
				for (std::uint32_t y = 0; y < TileSet::DefaultTileSize; y++) {
					for (std::uint32_t x = 0; x < TileSet::DefaultTileSize; x++) {
						std::uint32_t from = y * TileSet::DefaultTileSize + x;
						std::uint32_t to = (y + 1) * (TileSet::DefaultTileSize + 2) + (x + 1);

						// Store the palette index (with full alpha) for an indexed tileset; otherwise bake the color
						std::uint32_t color = (overrideIndexed ? ((std::uint32_t)tileDiffuseRaw[from] | 0xFF000000u) : _palettes[tileDiffuseRaw[from]]);
						tileDiffuse[to] = color;
					}
				}

				ExpandTileDiffuse((std::uint8_t*)tileDiffuse, TileSet::DefaultTileSize + 2, PixelSize);
				descriptor.TileMap->OverrideTileDiffuse(tileId, tileDiffuse);
			}
		} else {
			uc.Seek(overridenTilesCount * (2 + TileSet::DefaultTileSize * TileSet::DefaultTileSize), SeekOrigin::Current);
		}

		overridenTilesCount = uc.ReadVariableUint32();
		for (std::uint32_t i = 0; i < overridenTilesCount; i++) {
			std::uint16_t tileId = uc.ReadValueAsLE<std::uint16_t>();
			std::uint8_t tileMask[32 * 32];
			uc.Read(tileMask, sizeof(tileMask));

			descriptor.TileMap->OverrideTileMask(tileId, tileMask);
		}

		// Text Event Strings
		std::uint8_t textEventStringsCount = uc.ReadValue<std::uint8_t>();
		descriptor.LevelTexts.reserve(textEventStringsCount);
		for (std::uint32_t i = 0; i < textEventStringsCount; i++) {
			std::uint16_t textLength = uc.ReadValueAsLE<std::uint16_t>();
			String& text = descriptor.LevelTexts.emplace_back(NoInit, textLength);
			uc.Read(text.data(), textLength);
		}

		// Animated Tiles
		descriptor.TileMap->ReadAnimatedTiles(uc);

		// Layers
		std::uint8_t layerCount = uc.ReadValue<std::uint8_t>();
		for (std::uint32_t i = 0; i < layerCount; i++) {
			descriptor.TileMap->ReadLayerConfiguration(uc);
		}

		// Events
		descriptor.EventMap = std::make_unique<Events::EventMap>(descriptor.TileMap->GetSize());
		descriptor.EventMap->SetPitType(pitType);
		descriptor.EventMap->ReadEvents(uc, descriptor.TileMap, difficulty);

		DEATH_ASSERT(uc.IsValid(), "File cannot be decompressed", false);
		return true;
	}

	void ContentResolver::ApplyDefaultPalette()
	{
		static_assert(sizeof(SpritePalette) == ColorsPerPalette * sizeof(std::uint32_t));

		if (!_isHeadless && std::memcmp(_palettes, SpritePalette, ColorsPerPalette * sizeof(std::uint32_t)) != 0) {
			// Palettes differs, drop all cached resources, so it will be reloaded with new palette
			if (_isLoading) {
				// Indexed sprites/tiles recolor from the live palette texture and need no reload; only the baked
				// fonts are dropped to rebake with the new palette
				for (std::int32_t i = 0; i < (std::int32_t)FontType::Count; i++) {
					_fonts[i] = nullptr;
				}
			}

			std::memcpy(_palettes, SpritePalette, ColorsPerPalette * sizeof(std::uint32_t));
			RecreateGemPalettes();
		}
	}

	void ContentResolver::SetSpritePalette(ArrayView<const std::uint32_t> palette)
	{
		std::int32_t paletteSize = (std::int32_t)palette.size();
		std::int32_t count = (paletteSize < ColorsPerPalette ? paletteSize : ColorsPerPalette);
		if (count <= 0) {
			return;
		}

		std::memcpy(_palettes, palette.data(), count * sizeof(std::uint32_t));
		// Rebuilds the gem gradient rows from the new base palette, then marks rows 0..gems dirty and refreshes the
		// per-player recolor rows (which are also derived from the base palette)
		RecreateGemPalettes();
	}

	std::optional<Episode> ContentResolver::GetEpisode(StringView name, bool withImages)
	{
		String fullPath = fs::CombinePath({ GetContentPath(), "Episodes"_s, String(name + ".j2e"_s) });
		if (!fs::IsReadableFile(fullPath)) {
			fullPath = fs::CombinePath({ GetCachePath(), "Episodes"_s, String(name + ".j2e"_s) });
		}
		return GetEpisodeByPath(fullPath, withImages);
	}

	std::optional<Episode> ContentResolver::GetEpisodeByPath(StringView path, bool withImages)
	{
		auto s = fs::Open(path, FileAccess::Read);
		if (s->GetSize() < 16) {
			return std::nullopt;
		}

		std::uint64_t signature = s->ReadValueAsLE<std::uint64_t>();
		std::uint8_t fileType = s->ReadValue<std::uint8_t>();
		if (signature != 0x2095A59FF0BFBBEF || fileType != ContentResolver::EpisodeFile) {
			return std::nullopt;
		}

		Episode episode;
		episode.Name = fs::GetFileNameWithoutExtension(path);

		/*std::uint16_t flags =*/ s->ReadValueAsLE<std::uint16_t>();

		std::uint8_t nameLength = s->ReadValue<std::uint8_t>();
		episode.DisplayName = String(NoInit, nameLength);
		s->Read(episode.DisplayName.data(), nameLength);

		episode.Position = s->ReadValueAsLE<std::uint16_t>();

		nameLength = s->ReadValue<std::uint8_t>();
		episode.FirstLevel = String(NoInit, nameLength);
		s->Read(episode.FirstLevel.data(), nameLength);

		nameLength = s->ReadValue<std::uint8_t>();
		episode.PreviousEpisode = String(NoInit, nameLength);
		s->Read(episode.PreviousEpisode.data(), nameLength);

		nameLength = s->ReadValue<std::uint8_t>();
		episode.NextEpisode = String(NoInit, nameLength);
		s->Read(episode.NextEpisode.data(), nameLength);

		if (withImages && !_isHeadless) {
			std::uint16_t titleWidth = s->ReadValueAsLE<std::uint16_t>();
			std::uint16_t titleHeight = s->ReadValueAsLE<std::uint16_t>();
			if (titleWidth > 0 && titleHeight > 0) {
				std::unique_ptr<std::uint32_t[]> pixels = std::make_unique<std::uint32_t[]>(titleWidth * titleHeight);
				ReadImageFromFile(s, (std::uint8_t*)pixels.get(), titleWidth, titleHeight, 4);

				episode.TitleImage = std::make_unique<Texture>(path.data(), Texture::Format::RGBA8, titleWidth, titleHeight);
				episode.TitleImage->LoadFromTexels((unsigned char*)pixels.get(), 0, 0, titleWidth, titleHeight);
				episode.TitleImage->SetMinFiltering(SamplerFilter::Nearest);
				episode.TitleImage->SetMagFiltering(SamplerFilter::Nearest);
			}

			std::uint16_t backgroundWidth = s->ReadValueAsLE<std::uint16_t>();
			std::uint16_t backgroundHeight = s->ReadValueAsLE<std::uint16_t>();
			if (backgroundWidth > 0 && backgroundHeight > 0) {
				std::unique_ptr<std::uint32_t[]> pixels = std::make_unique<std::uint32_t[]>(backgroundWidth * backgroundHeight);
				ReadImageFromFile(s, (std::uint8_t*)pixels.get(), backgroundWidth, backgroundHeight, 4);

				episode.BackgroundImage = std::make_unique<Texture>(path.data(), Texture::Format::RGBA8, backgroundWidth, backgroundHeight);
				episode.BackgroundImage->LoadFromTexels((unsigned char*)pixels.get(), 0, 0, backgroundWidth, backgroundHeight);
				episode.BackgroundImage->SetMinFiltering(SamplerFilter::Linear);
				episode.BackgroundImage->SetMagFiltering(SamplerFilter::Linear);
			}
		}

		return episode;
	}

	std::unique_ptr<AudioStreamPlayer> ContentResolver::GetMusic(StringView path)
	{
#if defined(WITH_AUDIO)
		// Don't load sounds in headless mode
		if (_isHeadless) {
			return nullptr;
		}

		String fullPath;
		if (_pathHandler) {
			fullPath = _pathHandler(path);
		}
		if (fullPath.empty()) {
			fullPath = fs::CombinePath({ GetContentPath(), "Music"_s, path });
			if (!fs::IsReadableFile(fullPath)) {
				// "Source" directory must be case in-sensitive
				fullPath = fs::FindPathCaseInsensitive(fs::CombinePath(GetSourcePath(), path));
			}
		}
		if (!fs::IsReadableFile(fullPath)) {
			return nullptr;
		}
		return std::make_unique<AudioStreamPlayer>(fullPath);
#else
		return nullptr;
#endif
	}

	UI::Font* ContentResolver::GetFont(FontType fontType)
	{
		if (fontType >= FontType::Count) {
			return nullptr;
		}

		// Don't load fonts in headless mode
		if (_isHeadless) {
			return nullptr;
		}

		auto& font = _fonts[(std::int32_t)fontType];
		if (font == nullptr) {
			switch (fontType) {
				case FontType::Small: font = std::make_unique<UI::Font>(fs::CombinePath(
					{ GetContentPath(), "Animations"_s, "UI"_s, "font_small.png"_s }), _palettes);
					break;
				case FontType::Medium: font = std::make_unique<UI::Font>(fs::CombinePath(
					{ GetContentPath(), "Animations"_s, "UI"_s, "font_medium.png"_s }), _palettes);
					break;
				default:
					return nullptr;
			}
		}

		return font.get();
	}

	Shader* ContentResolver::GetShader(PrecompiledShader shader)
	{
		if (shader >= PrecompiledShader::Count) {
			return nullptr;
		}

		return _precompiledShaders[(std::int32_t)shader].get();
	}

	void ContentResolver::CompileShaders()
	{
		// Don't load shaders in headless mode
		if (_isHeadless) {
			return;
		}

		ZoneScoped;

		_precompiledShaders[(std::int32_t)PrecompiledShader::Lighting] = CompileShader("Lighting", Shaders::LightingVs, Shaders::LightingFs);
		_precompiledShaders[(std::int32_t)PrecompiledShader::BatchedLighting] = CompileShader("BatchedLighting", Shaders::BatchedLightingVs, Shaders::LightingFs, Shader::Introspection::NoUniformsInBlocks);
		_precompiledShaders[(std::int32_t)PrecompiledShader::Lighting]->RegisterBatchedShader(*_precompiledShaders[(int32_t)PrecompiledShader::BatchedLighting]);

		_precompiledShaders[(std::int32_t)PrecompiledShader::Blur] = CompileShader("Blur", Shader::DefaultVertex::SPRITE, Shaders::BlurFs);
		_precompiledShaders[(std::int32_t)PrecompiledShader::Downsample] = CompileShader("Downsample", Shader::DefaultVertex::SPRITE, Shaders::DownsampleFs);
		_precompiledShaders[(std::int32_t)PrecompiledShader::Combine] = CompileShader("Combine", Shaders::CombineVs, Shaders::CombineFs);
		_precompiledShaders[(std::int32_t)PrecompiledShader::CombineWithWater] = CompileShader("CombineWithWater", Shaders::CombineVs, Shaders::CombineWithWaterFs);
		_precompiledShaders[(std::int32_t)PrecompiledShader::CombineWithWaterLow] = CompileShader("CombineWithWaterLow", Shaders::CombineVs, Shaders::CombineWithWaterLowFs);

		_precompiledShaders[(std::int32_t)PrecompiledShader::TexturedBackground] = CompileShader("TexturedBackground", Shader::DefaultVertex::SPRITE, Shaders::TexturedBackgroundFs);
		_precompiledShaders[(std::int32_t)PrecompiledShader::TexturedBackgroundDither] = CompileShader("TexturedBackgroundDither", Shader::DefaultVertex::SPRITE, Shaders::TexturedBackgroundFs, Shader::Introspection::Enabled, { "DITHER"_s });
		_precompiledShaders[(std::int32_t)PrecompiledShader::TexturedBackgroundCircle] = CompileShader("TexturedBackgroundCircle", Shader::DefaultVertex::SPRITE, Shaders::TexturedBackgroundCircleFs);
		_precompiledShaders[(std::int32_t)PrecompiledShader::TexturedBackgroundCircleDither] = CompileShader("TexturedBackgroundCircleDither", Shader::DefaultVertex::SPRITE, Shaders::TexturedBackgroundCircleFs, Shader::Introspection::Enabled, { "DITHER"_s });

		_precompiledShaders[(std::int32_t)PrecompiledShader::Colorized] = CompileShader("Colorized", Shader::DefaultVertex::SPRITE, Shaders::ColorizedFs);
		_precompiledShaders[(std::int32_t)PrecompiledShader::BatchedColorized] = CompileShader("BatchedColorized", Shader::DefaultVertex::BATCHED_SPRITES, Shaders::ColorizedFs, Shader::Introspection::NoUniformsInBlocks);
		_precompiledShaders[(std::int32_t)PrecompiledShader::Colorized]->RegisterBatchedShader(*_precompiledShaders[(int32_t)PrecompiledShader::BatchedColorized]);

		_precompiledShaders[(std::int32_t)PrecompiledShader::Tinted] = CompileShader("Tinted", Shader::DefaultVertex::SPRITE, Shaders::TintedFs);
		_precompiledShaders[(std::int32_t)PrecompiledShader::BatchedTinted] = CompileShader("BatchedTinted", Shader::DefaultVertex::BATCHED_SPRITES, Shaders::TintedFs, Shader::Introspection::NoUniformsInBlocks);
		_precompiledShaders[(std::int32_t)PrecompiledShader::Tinted]->RegisterBatchedShader(*_precompiledShaders[(int32_t)PrecompiledShader::BatchedTinted]);

		// Palette-aware Tinted, for tinted tile layers drawn from an indexed tileset
		_precompiledShaders[(std::int32_t)PrecompiledShader::TintedPalette] = CompileShader("TintedPalette", Shader::DefaultVertex::SPRITE, Shaders::TintedFs, Shader::Introspection::Enabled, { "USE_PALETTE"_s });
		_precompiledShaders[(std::int32_t)PrecompiledShader::BatchedTintedPalette] = CompileShader("BatchedTintedPalette", Shader::DefaultVertex::BATCHED_SPRITES, Shaders::TintedFs, Shader::Introspection::NoUniformsInBlocks, { "USE_PALETTE"_s });
		_precompiledShaders[(std::int32_t)PrecompiledShader::TintedPalette]->RegisterBatchedShader(*_precompiledShaders[(int32_t)PrecompiledShader::BatchedTintedPalette]);

		_precompiledShaders[(std::int32_t)PrecompiledShader::Outline] = CompileShader("Outline", Shader::DefaultVertex::SPRITE, Shaders::OutlineFs);
		_precompiledShaders[(std::int32_t)PrecompiledShader::BatchedOutline] = CompileShader("BatchedOutline", Shader::DefaultVertex::BATCHED_SPRITES, Shaders::OutlineFs, Shader::Introspection::NoUniformsInBlocks);
		_precompiledShaders[(std::int32_t)PrecompiledShader::Outline]->RegisterBatchedShader(*_precompiledShaders[(int32_t)PrecompiledShader::BatchedOutline]);

		_precompiledShaders[(std::int32_t)PrecompiledShader::WhiteMask] = CompileShader("WhiteMask", Shader::DefaultVertex::SPRITE, Shaders::WhiteMaskFs);
		_precompiledShaders[(std::int32_t)PrecompiledShader::BatchedWhiteMask] = CompileShader("BatchedWhiteMask", Shader::DefaultVertex::BATCHED_SPRITES, Shaders::WhiteMaskFs, Shader::Introspection::NoUniformsInBlocks);
		_precompiledShaders[(std::int32_t)PrecompiledShader::WhiteMask]->RegisterBatchedShader(*_precompiledShaders[(int32_t)PrecompiledShader::BatchedWhiteMask]);

		_precompiledShaders[(std::int32_t)PrecompiledShader::PartialWhiteMask] = CompileShader("PartialWhiteMask", Shader::DefaultVertex::SPRITE, Shaders::PartialWhiteMaskFs);
		_precompiledShaders[(std::int32_t)PrecompiledShader::BatchedPartialWhiteMask] = CompileShader("BatchedPartialWhiteMask", Shader::DefaultVertex::BATCHED_SPRITES, Shaders::PartialWhiteMaskFs, Shader::Introspection::NoUniformsInBlocks);
		_precompiledShaders[(std::int32_t)PrecompiledShader::PartialWhiteMask]->RegisterBatchedShader(*_precompiledShaders[(int32_t)PrecompiledShader::BatchedPartialWhiteMask]);

		_precompiledShaders[(std::int32_t)PrecompiledShader::FrozenMask] = CompileShader("FrozenMask", Shader::DefaultVertex::SPRITE, Shaders::FrozenMaskFs);
		_precompiledShaders[(std::int32_t)PrecompiledShader::BatchedFrozenMask] = CompileShader("BatchedFrozenMask", Shader::DefaultVertex::BATCHED_SPRITES, Shaders::FrozenMaskFs, Shader::Introspection::NoUniformsInBlocks);
		_precompiledShaders[(std::int32_t)PrecompiledShader::FrozenMask]->RegisterBatchedShader(*_precompiledShaders[(int32_t)PrecompiledShader::BatchedFrozenMask]);

		// Palette-based rendering: indexed sprites/tiles are recolored at draw time through a palette texture bound
		// at unit 1. Batched variants let many indexed sprites that share the same palette texture (e.g., all actors
		// using the default sprite palette) collapse into a single draw call, exactly like the non-palette shaders.
		_precompiledShaders[(std::int32_t)PrecompiledShader::PaletteRemap] = CompileShader("PaletteRemap", Shader::DefaultVertex::SPRITE, Shaders::PaletteRemapFs);
		_precompiledShaders[(std::int32_t)PrecompiledShader::BatchedPaletteRemap] = CompileShader("BatchedPaletteRemap", Shader::DefaultVertex::BATCHED_SPRITES, Shaders::PaletteRemapFs, Shader::Introspection::NoUniformsInBlocks);
		_precompiledShaders[(std::int32_t)PrecompiledShader::PaletteRemap]->RegisterBatchedShader(*_precompiledShaders[(int32_t)PrecompiledShader::BatchedPaletteRemap]);

		_precompiledShaders[(std::int32_t)PrecompiledShader::OutlinePalette] = CompileShader("OutlinePalette", Shader::DefaultVertex::SPRITE, Shaders::OutlinePaletteFs);
		_precompiledShaders[(std::int32_t)PrecompiledShader::BatchedOutlinePalette] = CompileShader("BatchedOutlinePalette", Shader::DefaultVertex::BATCHED_SPRITES, Shaders::OutlinePaletteFs, Shader::Introspection::NoUniformsInBlocks);
		_precompiledShaders[(std::int32_t)PrecompiledShader::OutlinePalette]->RegisterBatchedShader(*_precompiledShaders[(int32_t)PrecompiledShader::BatchedOutlinePalette]);

		// Palette-aware variants of the mask shaders, so a recolored (indexed) actor keeps correct colors while
		// hit-flashing, frozen, or in sugar rush.
		_precompiledShaders[(std::int32_t)PrecompiledShader::WhiteMaskPalette] = CompileShader("WhiteMaskPalette", Shader::DefaultVertex::SPRITE, Shaders::WhiteMaskFs, Shader::Introspection::Enabled, { "USE_PALETTE"_s });
		_precompiledShaders[(std::int32_t)PrecompiledShader::BatchedWhiteMaskPalette] = CompileShader("BatchedWhiteMaskPalette", Shader::DefaultVertex::BATCHED_SPRITES, Shaders::WhiteMaskFs, Shader::Introspection::NoUniformsInBlocks, { "USE_PALETTE"_s });
		_precompiledShaders[(std::int32_t)PrecompiledShader::WhiteMaskPalette]->RegisterBatchedShader(*_precompiledShaders[(int32_t)PrecompiledShader::BatchedWhiteMaskPalette]);

		_precompiledShaders[(std::int32_t)PrecompiledShader::PartialWhiteMaskPalette] = CompileShader("PartialWhiteMaskPalette", Shader::DefaultVertex::SPRITE, Shaders::PartialWhiteMaskFs, Shader::Introspection::Enabled, { "USE_PALETTE"_s });
		_precompiledShaders[(std::int32_t)PrecompiledShader::BatchedPartialWhiteMaskPalette] = CompileShader("BatchedPartialWhiteMaskPalette", Shader::DefaultVertex::BATCHED_SPRITES, Shaders::PartialWhiteMaskFs, Shader::Introspection::NoUniformsInBlocks, { "USE_PALETTE"_s });
		_precompiledShaders[(std::int32_t)PrecompiledShader::PartialWhiteMaskPalette]->RegisterBatchedShader(*_precompiledShaders[(int32_t)PrecompiledShader::BatchedPartialWhiteMaskPalette]);

		_precompiledShaders[(std::int32_t)PrecompiledShader::FrozenMaskPalette] = CompileShader("FrozenMaskPalette", Shader::DefaultVertex::SPRITE, Shaders::FrozenMaskFs, Shader::Introspection::Enabled, { "USE_PALETTE"_s });
		_precompiledShaders[(std::int32_t)PrecompiledShader::BatchedFrozenMaskPalette] = CompileShader("BatchedFrozenMaskPalette", Shader::DefaultVertex::BATCHED_SPRITES, Shaders::FrozenMaskFs, Shader::Introspection::NoUniformsInBlocks, { "USE_PALETTE"_s });
		_precompiledShaders[(std::int32_t)PrecompiledShader::FrozenMaskPalette]->RegisterBatchedShader(*_precompiledShaders[(int32_t)PrecompiledShader::BatchedFrozenMaskPalette]);

		_precompiledShaders[(std::int32_t)PrecompiledShader::ShieldFire] = CompileShader("ShieldFire", Shaders::ShieldVs, Shaders::ShieldFireFs);
		_precompiledShaders[(std::int32_t)PrecompiledShader::BatchedShieldFire] = CompileShader("BatchedShieldFire", Shaders::BatchedShieldVs, Shaders::ShieldFireFs, Shader::Introspection::NoUniformsInBlocks);
		_precompiledShaders[(std::int32_t)PrecompiledShader::ShieldFire]->RegisterBatchedShader(*_precompiledShaders[(int32_t)PrecompiledShader::BatchedShieldFire]);

		_precompiledShaders[(std::int32_t)PrecompiledShader::ShieldLightning] = CompileShader("ShieldLightning", Shaders::ShieldVs, Shaders::ShieldLightningFs);
		_precompiledShaders[(std::int32_t)PrecompiledShader::BatchedShieldLightning] = CompileShader("BatchedShieldLightning", Shaders::BatchedShieldVs, Shaders::ShieldLightningFs, Shader::Introspection::NoUniformsInBlocks);
		_precompiledShaders[(std::int32_t)PrecompiledShader::ShieldLightning]->RegisterBatchedShader(*_precompiledShaders[(int32_t)PrecompiledShader::BatchedShieldLightning]);

#if defined(TILEMAP_USE_SINGLE_DRAW)
		// Tile-layer mesh shaders (used by TileMap when TILEMAP_USE_SINGLE_DRAW is enabled; cheap to keep compiled
		// either way). Define the interleaved per-vertex format (position.xy, texcoords.xy, color.rgba = 8 floats /
		// 32 bytes) shared by both, filled in TileMap::DrawLayer.
		_precompiledShaders[(std::int32_t)PrecompiledShader::TileMapMesh] = CompileShader("TileMapMesh", Shaders::TileMapVs, Shader::DefaultFragment::SPRITE);
		_precompiledShaders[(std::int32_t)PrecompiledShader::TileMapMeshPalette] = CompileShader("TileMapMeshPalette", Shaders::TileMapVs, Shaders::PaletteRemapFs);
		for (PrecompiledShader meshShader : { PrecompiledShader::TileMapMesh, PrecompiledShader::TileMapMeshPalette }) {
			Shader* shader = _precompiledShaders[(std::int32_t)meshShader].get();
			constexpr std::int32_t Stride = 8 * sizeof(float);
			shader->SetAttribute(Material::PositionAttributeName, Stride, reinterpret_cast<void*>(0 * sizeof(float)));
			shader->SetAttribute(Material::TexCoordsAttributeName, Stride, reinterpret_cast<void*>(2 * sizeof(float)));
			shader->SetAttribute(Material::ColorAttributeName, Stride, reinterpret_cast<void*>(4 * sizeof(float)));
		}
#endif

#if !defined(DISABLE_RESCALE_SHADERS)
		_precompiledShaders[(std::int32_t)PrecompiledShader::ResizeHQ2x] = CompileShader("ResizeHQ2x", Shaders::ResizeHQ2xVs, Shaders::ResizeHQ2xFs);
		_precompiledShaders[(std::int32_t)PrecompiledShader::Resize3xBrz] = CompileShader("Resize3xBrz", Shaders::Resize3xBrzVs, Shaders::Resize3xBrzFs);
		_precompiledShaders[(std::int32_t)PrecompiledShader::ResizeCrtScanlines] = CompileShader("ResizeCrtScanlines", Shaders::ResizeCrtScanlinesVs, Shaders::ResizeCrtScanlinesFs);
		_precompiledShaders[(std::int32_t)PrecompiledShader::ResizeCrtShadowMask] = CompileShader("ResizeCrtShadowMask", Shaders::ResizeCrtVs, Shaders::ResizeCrtShadowMaskFs);
		_precompiledShaders[(std::int32_t)PrecompiledShader::ResizeCrtApertureGrille] = CompileShader("ResizeCrtApertureGrille", Shaders::ResizeCrtVs, Shaders::ResizeCrtApertureGrilleFs);
		_precompiledShaders[(std::int32_t)PrecompiledShader::ResizeMonochrome] = CompileShader("ResizeMonochrome", Shaders::ResizeMonochromeVs, Shaders::ResizeMonochromeFs);
		_precompiledShaders[(std::int32_t)PrecompiledShader::ResizeSabr] = CompileShader("ResizeSabr", Shaders::ResizeSabrVs, Shaders::ResizeSabrFs);
		_precompiledShaders[(std::int32_t)PrecompiledShader::ResizeCleanEdge] = CompileShader("ResizeCleanEdge", Shaders::ResizeCleanEdgeVs, Shaders::ResizeCleanEdgeFs);
#endif
		_precompiledShaders[(std::int32_t)PrecompiledShader::Antialiasing] = CompileShader("Antialiasing", Shaders::AntialiasingVs, Shaders::AntialiasingFs);

		_precompiledShaders[(std::int32_t)PrecompiledShader::Transition] = CompileShader("Transition", Shaders::TransitionVs, Shaders::TransitionFs);
		_precompiledShaders[(std::int32_t)PrecompiledShader::TouchCircle] = CompileShader("TouchCircle", Shaders::TouchCircleVs, Shaders::TouchCircleFs);
	}

	std::unique_ptr<Shader> ContentResolver::CompileShader(const char* shaderName, Shader::DefaultVertex vertex, const char* fragment, Shader::Introspection introspection, std::initializer_list<StringView> defines)
	{
		std::unique_ptr shader = std::make_unique<Shader>();
		if (shader->LoadFromCache(shaderName, Shaders::Version, introspection)) {
			return shader;
		}

		const AppConfiguration& appCfg = theApplication().GetAppConfiguration();
		const IGfxCapabilities& gfxCaps = theServiceLocator().GetGfxCapabilities();
		// Clamping the value as some drivers report a maximum size similar to SSBO one
		std::int32_t maxUniformBlockSize = std::clamp(gfxCaps.GetValue(IGfxCapabilities::GLIntValues::MAX_UNIFORM_BLOCK_SIZE), 0, 64 * 1024);

		// If the UBO is smaller than 64kb and fixed batch size is disabled, batched shaders need to be compiled twice to determine safe `BATCH_SIZE` define value
		bool compileTwice = (maxUniformBlockSize < 64 * 1024 && appCfg.fixedBatchSize <= 0 && introspection == Shader::Introspection::NoUniformsInBlocks);

		std::int32_t batchSize;
		if (appCfg.fixedBatchSize > 0 && introspection == Shader::Introspection::NoUniformsInBlocks) {
			batchSize = appCfg.fixedBatchSize;
		} else if (compileTwice) {
			// The first compilation of a batched shader needs a `BATCH_SIZE` defined as 1
			batchSize = 1;
		} else {
			batchSize = GLShaderProgram::DefaultBatchSize;
		}

		shader->LoadFromMemory(shaderName, compileTwice ? Shader::Introspection::Enabled : introspection, vertex, fragment, batchSize, arrayView(defines));

		if (compileTwice) {
			GLShaderUniformBlocks blocks(shader->GetHandle(), Material::InstancesBlockName, nullptr);
			GLUniformBlockCache* block = blocks.GetUniformBlock(Material::InstancesBlockName);
			DEATH_DEBUG_ASSERT(block != nullptr);
			if (block != nullptr) {
				batchSize = maxUniformBlockSize / block->GetSize();
				LOGI("Shader \"{}\" - block size: {} + {} align bytes, max batch size: {}", shaderName,
					block->GetSize() - block->GetAlignAmount(), block->GetAlignAmount(), batchSize);
				
				bool hasLinked = false;
				while (batchSize > 0) {
					hasLinked = shader->LoadFromMemory(shaderName, introspection, vertex, fragment, batchSize, arrayView(defines));
					if (hasLinked) {
						break;
					}

					batchSize--;
					LOGW("Failed to compile the shader, recompiling with batch size: {}", batchSize);
				}

				if (!hasLinked) {
					// Don't save to cache if it cannot be linked
					return shader;
				}
			}
		}

		shader->SaveToCache(shaderName, Shaders::Version);
		return shader;
	}
	
	std::unique_ptr<Shader> ContentResolver::CompileShader(const char* shaderName, const char* vertex, Shader::DefaultFragment fragment, Shader::Introspection introspection, std::initializer_list<StringView> defines)
	{
		std::unique_ptr shader = std::make_unique<Shader>();
		if (shader->LoadFromCache(shaderName, Shaders::Version, introspection)) {
			return shader;
		}

		const AppConfiguration& appCfg = theApplication().GetAppConfiguration();
		const IGfxCapabilities& gfxCaps = theServiceLocator().GetGfxCapabilities();
		// Clamping the value as some drivers report a maximum size similar to SSBO one
		std::int32_t maxUniformBlockSize = std::clamp(gfxCaps.GetValue(IGfxCapabilities::GLIntValues::MAX_UNIFORM_BLOCK_SIZE), 0, 64 * 1024);

		// If the UBO is smaller than 64kb and fixed batch size is disabled, batched shaders need to be compiled twice to determine safe `BATCH_SIZE` define value
		bool compileTwice = (maxUniformBlockSize < 64 * 1024 && appCfg.fixedBatchSize <= 0 && introspection == Shader::Introspection::NoUniformsInBlocks);

		std::int32_t batchSize;
		if (appCfg.fixedBatchSize > 0 && introspection == Shader::Introspection::NoUniformsInBlocks) {
			batchSize = appCfg.fixedBatchSize;
		} else if (compileTwice) {
			// The first compilation of a batched shader needs a `BATCH_SIZE` defined as 1
			batchSize = 1;
		} else {
			batchSize = GLShaderProgram::DefaultBatchSize;
		}

		shader->LoadFromMemory(shaderName, compileTwice ? Shader::Introspection::Enabled : introspection, vertex, fragment, batchSize, arrayView(defines));

		if (compileTwice) {
			GLShaderUniformBlocks blocks(shader->GetHandle(), Material::InstancesBlockName, nullptr);
			GLUniformBlockCache* block = blocks.GetUniformBlock(Material::InstancesBlockName);
			DEATH_DEBUG_ASSERT(block != nullptr);
			if (block != nullptr) {
				batchSize = maxUniformBlockSize / block->GetSize();
				LOGI("Shader \"{}\" - block size: {} + {} align bytes, max batch size: {}", shaderName,
					block->GetSize() - block->GetAlignAmount(), block->GetAlignAmount(), batchSize);

				bool hasLinked = false;
				while (batchSize > 0) {
					hasLinked = shader->LoadFromMemory(shaderName, introspection, vertex, fragment, batchSize, arrayView(defines));
					if (hasLinked) {
						break;
					}

					batchSize--;
					LOGW("Failed to compile the shader, recompiling with batch size: {}", batchSize);
				}

				if (!hasLinked) {
					// Don't save to cache if it cannot be linked
					return shader;
				}
			}
		}

		shader->SaveToCache(shaderName, Shaders::Version);
		return shader;
	}

	std::unique_ptr<Shader> ContentResolver::CompileShader(const char* shaderName, const char* vertex, const char* fragment, Shader::Introspection introspection, std::initializer_list<StringView> defines)
	{
		std::unique_ptr shader = std::make_unique<Shader>();
		if (shader->LoadFromCache(shaderName, Shaders::Version, introspection)) {
			return shader;
		}

		const AppConfiguration& appCfg = theApplication().GetAppConfiguration();
		const IGfxCapabilities& gfxCaps = theServiceLocator().GetGfxCapabilities();
		// Clamping the value as some drivers report a maximum size similar to SSBO one
		std::int32_t maxUniformBlockSize = std::clamp(gfxCaps.GetValue(IGfxCapabilities::GLIntValues::MAX_UNIFORM_BLOCK_SIZE), 0, 64 * 1024);

		// If the UBO is smaller than 64kb and fixed batch size is disabled, batched shaders need to be compiled twice to determine safe `BATCH_SIZE` define value
		bool compileTwice = (maxUniformBlockSize < 64 * 1024 && appCfg.fixedBatchSize <= 0 && introspection == Shader::Introspection::NoUniformsInBlocks);

		std::int32_t batchSize;
		if (appCfg.fixedBatchSize > 0 && introspection == Shader::Introspection::NoUniformsInBlocks) {
			batchSize = appCfg.fixedBatchSize;
		} else if (compileTwice) {
			// The first compilation of a batched shader needs a `BATCH_SIZE` defined as 1
			batchSize = 1;
		} else {
			batchSize = GLShaderProgram::DefaultBatchSize;
		}

		shader->LoadFromMemory(shaderName, compileTwice ? Shader::Introspection::Enabled : introspection, vertex, fragment, batchSize, arrayView(defines));

		if (compileTwice) {
			GLShaderUniformBlocks blocks(shader->GetHandle(), Material::InstancesBlockName, nullptr);
			GLUniformBlockCache* block = blocks.GetUniformBlock(Material::InstancesBlockName);
			DEATH_DEBUG_ASSERT(block != nullptr);
			if (block != nullptr) {
				batchSize = maxUniformBlockSize / block->GetSize();
				LOGI("Shader \"{}\" - block size: {} + {} align bytes, max batch size: {}", shaderName,
					block->GetSize() - block->GetAlignAmount(), block->GetAlignAmount(), batchSize);

				bool hasLinked = false;
				while (batchSize > 0) {
					hasLinked = shader->LoadFromMemory(shaderName, introspection, vertex, fragment, batchSize, arrayView(defines));
					if (hasLinked) {
						break;
					}

					batchSize--;
					LOGW("Failed to compile the shader, recompiling with batch size: {}", batchSize);
				}

				if (!hasLinked) {
					// Don't save to cache if it cannot be linked
					return shader;
				}
			}
		}

		shader->SaveToCache(shaderName, Shaders::Version);
		return shader;
	}

	std::unique_ptr<Texture> ContentResolver::GetNoiseTexture()
	{
		// Don't load textures in headless mode
		if (_isHeadless) {
			return nullptr;
		}

		std::uint32_t texels[64 * 64];

		for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(arraySize(texels)); i++) {
			texels[i] = Random().Fast(0, INT32_MAX) | 0xff000000;
		}

		std::unique_ptr<Texture> tex = std::make_unique<Texture>("Noise", Texture::Format::RGBA8, 64, 64);
		tex->LoadFromTexels((std::uint8_t*)texels, 0, 0, 64, 64);
		tex->SetMinFiltering(SamplerFilter::Linear);
		tex->SetMagFiltering(SamplerFilter::Linear);
		tex->SetWrap(SamplerWrapping::Repeat);
		return tex;
	}

	void ContentResolver::RecreateGemPalettes()
	{
		constexpr std::int32_t GemColorCount = 4;
		constexpr std::int32_t Expansion = 32;

		static const std::int32_t PaletteStops[] = {
			55, 52, 48, 15, 15,
			87, 84, 80, 15, 15,
			39, 36, 32, 15, 15,
			95, 92, 88, 15, 15
		};

		constexpr std::int32_t StopsPerGem = (std::int32_t(arraySize(PaletteStops)) / GemColorCount) - 1;

		// Start to fill palette texture from the second row (right after base palette)
		std::int32_t src = 0, dst = ColorsPerPalette;
		for (std::int32_t color = 0; color < GemColorCount; color++, src++) {
			// Compress 2 gem color gradients to single palette row
			for (std::int32_t i = 0; i < StopsPerGem; i++) {
				// Base Palette is in first row of "palettes" array
				std::uint32_t from = _palettes[PaletteStops[src++]];
				std::uint32_t to = _palettes[PaletteStops[src]];

				std::int32_t r = (from & 0xff) * 8, dr = ((to & 0xff) * 8) - r;
				std::int32_t g = ((from >> 8) & 0xff) * 8, dg = (((to >> 8) & 0xff) * 8) - g;
				std::int32_t b = ((from >> 16) & 0xff) * 8, db = (((to >> 16) & 0xff) * 8) - b;
				std::int32_t a = (from & 0xff000000);
				r *= Expansion; g *= Expansion; b *= Expansion;

				for (std::int32_t j = 0; j < Expansion; j++) {
					_palettes[dst] = ((r / (8 * Expansion)) & 0xff) | (((g / (8 * Expansion)) & 0xff) << 8) |
									 (((b / (8 * Expansion)) & 0xff) << 16) | a;
					r += dr; g += dg; b += db;
					dst++;
				}
			}
		}

		// The base palette (row 0) and the gem palette rows just changed - mark them for re-upload. `dst` is the
		// first index past the last gem color written, so (dst - 1) is the last touched palette index.
		MarkPaletteDirty(0, (dst - 1) / ColorsPerPalette);

		// Per-player recolor rows are derived from the (now changed) base palette, so rebuild them too
		RefreshDynamicPaletteRows();
	}

	std::uint32_t ContentResolver::ShiftHue(std::uint32_t color, float degrees)
	{
		// Packed color is 0xAABBGGRR
		float r = (color & 0xFF) / 255.0f;
		float g = ((color >> 8) & 0xFF) / 255.0f;
		float b = ((color >> 16) & 0xFF) / 255.0f;

		// Rotate the hue in YIQ space: this keeps the perceived luma (Y) and the chroma magnitude constant, so the
		// recolored result keeps the original's brightness and saturation and only its hue changes. (Rotating in
		// HSL/HSV instead preserves the *nominal* S/L but often looks much stronger, as those aren't perceptual.)
		float y = 0.299f * r + 0.587f * g + 0.114f * b;
		float i = 0.596f * r - 0.274f * g - 0.322f * b;
		float q = 0.211f * r - 0.523f * g + 0.312f * b;

		// YIQ's chroma angle runs opposite to the conventional HSV hue direction, so negate `degrees` here: this makes
		// a positive argument increase the HSV hue (e.g., red 0deg + 60 -> yellow), matching what the per-gradient
		// angles in HueShiftDegreesForGradient assume.
		float rad = -degrees * 0.01745329252f; // degrees -> radians
		float cs = std::cos(rad), sn = std::sin(rad);
		float i2 = i * cs - q * sn;
		float q2 = i * sn + q * cs;

		// YIQ -> RGB (inverse of the matrix above)
		float r2 = y + 0.956f * i2 + 0.621f * q2;
		float g2 = y - 0.272f * i2 - 0.647f * q2;
		float b2 = y - 1.106f * i2 + 1.703f * q2;

		std::int32_t ri = (std::int32_t)(r2 * 255.0f + 0.5f);
		std::int32_t gi = (std::int32_t)(g2 * 255.0f + 0.5f);
		std::int32_t bi = (std::int32_t)(b2 * 255.0f + 0.5f);
		ri = (ri < 0 ? 0 : (ri > 255 ? 255 : ri));
		gi = (gi < 0 ? 0 : (gi > 255 ? 255 : gi));
		bi = (bi < 0 ? 0 : (bi > 255 ? 255 : bi));

		return (color & 0xFF000000u) | ((std::uint32_t)bi << 16) | ((std::uint32_t)gi << 8) | (std::uint32_t)ri;
	}

	float ContentResolver::HueShiftDegreesForGradient(std::int32_t gradientStart)
	{
		// Per-gradient rotations. The combined base+twin hues land roughly evenly around the wheel:
		// base {0, 35, 73, 158, 207, 287, 335} + twins below fill the green/cyan/blue gaps.
		switch (gradientStart) {
			case 0x10: return 42.0f;	// chartreuse ~73°	-> green ~115°
			case 0x18: return 195.0f;	// red ~0°			-> blue ~195°
			case 0x20: return 104.0f;	// azure ~207°		-> magenta ~311°
			case 0x28: return 145.0f;	// orange ~35°		-> cyan ~180°
			case 0x30: return 65.0f;	// rose ~335°		-> gold ~40°
			case 0x50: return 218.0f;	// spring-green ~158° -> crimson ~16°
			case 0x58: return 168.0f;	// purple ~287°		-> lime ~95°
			default: return 190.0f;		// neutrals (0x40, 0x48) and anything unrecognized -> ~190°
		}
	}

	namespace
	{
		// A destination block recolored by a fur section: copies `Size` colors from the section's chosen 8-color source
		// gradient (starting `SrcOffset` colors into it) into the palette starting at `Start`. A section can recolor
		// more than one block, e.g., when a character draws one logical color across two separate palette ranges.
		// `Size == 0` marks an unused block slot.
		struct FurBlock {
			std::int32_t Start;      // First destination palette index (0 = transparent, never a real destination)
			std::int32_t Size;       // Number of consecutive indices to recolor (SrcOffset + Size must be <= FurSectionSize)
			std::int32_t SrcOffset;  // Offset into the source gradient for Start (so Start+i takes gradient color SrcOffset+i)
		};
		constexpr FurBlock NoBlock = { 0, 0, 0 };

		// A character's recolor scheme: the destination blocks for each of the 4 sections, in the canonical order
		// Primary Fur, Secondary Fur, Weapon, Misc - so the same picked color recolors the analogous region on every
		// character. The destination ranges differ per character because each one's sprites are drawn with a different
		// set of palette ranges; derived from a palette-usage analysis of the dumped sprites (see player-color-customization).
		struct FurScheme {
			FurBlock Sections[ContentResolver::FurSectionCount][ContentResolver::FurSectionMaxBlocks];
		};

		// [0] Default - Jazz (also the Frog morph and anything unspecified): the standard rabbit fur ranges.
		// [1] Spaz: green rabbit gun (0x10), but his fur uses different ranges than Jazz - primary red 0x18,
		//     secondary orange 0x28, misc green 0x20.
		// [2] Lori: entirely different ranges. Her blonde hair is a bright highlight (0x28) plus the 0x3B-0x3F gold ramp,
		//     recolored as one section; the ramp is mapped one gradient step below the highlight (SrcOffset 1) so the
		//     highlight->body transition stays smooth instead of jumping. Skin 0x40; her dark-blue weapon spans the 0x20
		//     blue body and the 0x48 steel barrel; misc (lips) 0x30. Her eyes (0x58) are left at their original color.
		constexpr FurScheme FurSchemes[] = {
			//   Primary Fur                        Secondary Fur                Weapon                                      Misc
			{ { { { 0x10, 8, 0 }, NoBlock },        { { 0x18, 8, 0 }, NoBlock }, { { 0x20, 8, 0 }, NoBlock },                { { 0x28, 8, 0 }, NoBlock } } },
			{ { { { 0x18, 8, 0 }, NoBlock },        { { 0x28, 8, 0 }, NoBlock }, { { 0x10, 8, 0 }, NoBlock },                { { 0x20, 8, 0 }, NoBlock } } },
			{ { { { 0x28, 7, 1 }, { 0x3B, 5, 2 } }, { { 0x40, 8, 0 }, NoBlock }, { { 0x20, 8, 0 }, { 0x48, 8, 0 } },         { { 0x30, 8, 0 }, NoBlock } } }
		};
		constexpr std::int32_t FurSchemeCount = (std::int32_t)(sizeof(FurSchemes) / sizeof(FurSchemes[0]));
	}

	std::int32_t ContentResolver::GetFurSchemeIndex(PlayerType playerType)
	{
		// Each playable character's sprites use different palette ranges, so each needs its own recolor scheme; the Frog
		// morph and anything unspecified fall back to the Jazz (default) scheme.
		switch (playerType) {
			case PlayerType::Spaz: return 1;
			case PlayerType::Lori: return 2;
			default: return 0;
		}
	}

	void ContentResolver::BuildPlayerColorPalette(std::uint32_t furColor, PlayerType playerType, std::uint32_t* outPalette) const
	{
		BuildPlayerColorPaletteForScheme(furColor, GetFurSchemeIndex(playerType), outPalette);
	}

	void ContentResolver::BuildPlayerColorPaletteForScheme(std::uint32_t furColor, std::int32_t schemeIndex, std::uint32_t* outPalette) const
	{
		// Start from the default sprite palette (first row of _palettes), then recolor the fur sections.
		std::memcpy(outPalette, _palettes, ColorsPerPalette * sizeof(std::uint32_t));

		if (schemeIndex < 0 || schemeIndex >= FurSchemeCount) {
			schemeIndex = 0;
		}
		const FurScheme& scheme = FurSchemes[schemeIndex];

		// The fur color packs one byte per section (as in the original game). The low 7 bits of each non-zero byte are
		// the starting palette index of an 8-color gradient in the sprite palette; copy those colors into each of the
		// section's destination blocks. If FurHueShiftFlag is set, rotate their hue by the gradient's angle first.
		for (std::int32_t section = 0; section < FurSectionCount; section++) {
			std::uint32_t packed = (furColor >> (section * 8)) & 0xFF;
			std::int32_t base = (std::int32_t)(packed & ~(std::uint32_t)FurHueShiftFlag);
			if (base == 0) {
				// Keep the original colors for this section (the hue-shift flag on a 0 index is a no-op)
				continue;
			}

			bool hueShift = (packed & FurHueShiftFlag) != 0;
			float hueDegrees = HueShiftDegreesForGradient(base);
			for (std::int32_t block = 0; block < FurSectionMaxBlocks; block++) {
				const FurBlock& blk = scheme.Sections[section][block];
				if (blk.Size <= 0 || blk.Start <= 0) {
					// Unused block slot (index 0 is transparent, never a valid destination)
					continue;
				}
				for (std::int32_t i = 0; i < blk.Size; i++) {
					std::int32_t srcIdx = base + blk.SrcOffset + i;
					std::int32_t dstIdx = blk.Start + i;
					if (srcIdx > 0 && srcIdx < ColorsPerPalette && dstIdx > 0 && dstIdx < ColorsPerPalette) {
						std::uint32_t color = _palettes[srcIdx];
						if (hueShift && hueDegrees != 0.0f) {
							color = ShiftHue(color, hueDegrees);
						}
						outPalette[dstIdx] = color;
					}
				}
			}
		}
	}

	Texture* ContentResolver::ApplyPlayerColorPalette(std::unique_ptr<Texture>& texture, std::uint32_t furColor, PlayerType playerType)
	{
		if (_isHeadless) {
			return nullptr;
		}

		std::uint32_t palette[ColorsPerPalette];
		BuildPlayerColorPalette(furColor, playerType, palette);

		if (texture == nullptr) {
			texture = std::make_unique<Texture>("PlayerColorPalette", Texture::Format::RGBA8, ColorsPerPalette, 1);
			texture->SetMinFiltering(SamplerFilter::Nearest);
			texture->SetMagFiltering(SamplerFilter::Nearest);
		}
		texture->LoadFromTexels((std::uint8_t*)palette, 0, 0, ColorsPerPalette, 1);
		return texture.get();
	}

	Texture* ContentResolver::GetPaletteTexture()
	{
		if (_isHeadless) {
			return nullptr;
		}

		if (_paletteTexture == nullptr) {
			_paletteTexture = std::make_unique<Texture>("Palettes", Texture::Format::RGBA8, ColorsPerPalette, PaletteCount);
			_paletteTexture->SetMinFiltering(SamplerFilter::Nearest);
			_paletteTexture->SetMagFiltering(SamplerFilter::Nearest);
			_paletteTexture->SetWrap(SamplerWrapping::ClampToEdge);
			// Force a full upload on first use
			_paletteDirtyFirstRow = 0;
			_paletteDirtyLastRow = PaletteCount - 1;
		}

		// Upload only the rows changed since the last call - cheap enough to support per-frame palette cycling
		if (_paletteDirtyFirstRow <= _paletteDirtyLastRow) {
			std::int32_t rowCount = _paletteDirtyLastRow - _paletteDirtyFirstRow + 1;
			_paletteTexture->LoadFromTexels((std::uint8_t*)(_palettes + _paletteDirtyFirstRow * ColorsPerPalette),
				0, _paletteDirtyFirstRow, ColorsPerPalette, rowCount);
			_paletteDirtyFirstRow = PaletteCount;
			_paletteDirtyLastRow = -1;
		}

		return _paletteTexture.get();
	}

	void ContentResolver::MarkPaletteDirty(std::int32_t firstRow, std::int32_t lastRow)
	{
		if (firstRow < 0) {
			firstRow = 0;
		}
		if (lastRow >= PaletteCount) {
			lastRow = PaletteCount - 1;
		}
		if (firstRow > lastRow) {
			return;
		}
		if (firstRow < _paletteDirtyFirstRow) {
			_paletteDirtyFirstRow = firstRow;
		}
		if (lastRow > _paletteDirtyLastRow) {
			_paletteDirtyLastRow = lastRow;
		}
	}

	std::int32_t ContentResolver::AcquirePaletteOffset(std::uint32_t furColor, PlayerType playerType)
	{
		// Rows are keyed by both the fur color and the recolor scheme: the same packed color produces different
		// palettes for characters with different schemes (e.g., Jazz vs. Lori), so they can't share a row.
		std::uint8_t scheme = (std::uint8_t)GetFurSchemeIndex(playerType);

		// Reuse the row already holding this color+scheme (shared, reference-counted) while noting the first free row
		std::int32_t freeRow = -1;
		for (std::int32_t row = FirstDynamicPaletteRow; row < PaletteCount; row++) {
			if (_paletteRowRefCount[row] == 0) {
				if (freeRow < 0) {
					freeRow = row;
				}
			} else if (_paletteRowColor[row] == furColor && _paletteRowScheme[row] == scheme) {
				_paletteRowRefCount[row]++;
				return row * ColorsPerPalette;
			}
		}

		if (freeRow < 0) {
			// All dynamic rows are in use (would need more than (PaletteCount - FirstDynamicPaletteRow) distinct recolors)
			return -1;
		}

		_paletteRowRefCount[freeRow] = 1;
		_paletteRowColor[freeRow] = furColor;
		_paletteRowScheme[freeRow] = scheme;
		BuildPlayerColorPaletteForScheme(furColor, scheme, _palettes + freeRow * ColorsPerPalette);
		MarkPaletteDirty(freeRow, freeRow);
		return freeRow * ColorsPerPalette;
	}

	void ContentResolver::ReleasePaletteOffset(std::int32_t paletteOffset)
	{
		// The public API deals in flat palette offsets; map back to the texture row that backs it (a negative offset,
		// i.e. "no palette", maps to row 0 and is rejected by the bounds check below)
		std::int32_t row = paletteOffset / ColorsPerPalette;
		if (row >= FirstDynamicPaletteRow && row < PaletteCount && _paletteRowRefCount[row] > 0) {
			_paletteRowRefCount[row]--;
			// When the count hits 0 the row is free for reuse; its contents are overwritten on the next acquire
		}
	}

	void ContentResolver::RefreshDynamicPaletteRows()
	{
		for (std::int32_t row = FirstDynamicPaletteRow; row < PaletteCount; row++) {
			if (_paletteRowRefCount[row] > 0) {
				BuildPlayerColorPaletteForScheme(_paletteRowColor[row], _paletteRowScheme[row], _palettes + row * ColorsPerPalette);
				MarkPaletteDirty(row, row);
			}
		}
	}

	bool ContentResolver::ConfigureSpriteShader(RenderCommand& command, bool indexed)
	{
		bool shaderChanged = (indexed
			? command.GetMaterial().SetShader(GetShader(PrecompiledShader::PaletteRemap))
			: command.GetMaterial().SetShaderProgramType(Material::ShaderProgramType::Sprite));
		if (shaderChanged) {
			command.GetMaterial().ReserveUniformsDataMemory();
			command.GetGeometry().SetDrawParameters(GL_TRIANGLE_STRIP, 0, 4);

			GLUniformCache* textureUniform = command.GetMaterial().Uniform(Material::TextureUniformName);
			if (textureUniform != nullptr && textureUniform->GetIntValue(0) != 0) {
				textureUniform->SetIntValue(0); // GL_TEXTURE0
			}
			GLUniformCache* paletteUniform = command.GetMaterial().Uniform("uTexturePalette");
			if (paletteUniform != nullptr) {
				paletteUniform->SetIntValue(1); // GL_TEXTURE1
			}
		}
		return shaderChanged;
	}

	void ContentResolver::BindSpritePalette(RenderCommand& command, GLUniformBlockCache& instanceBlock, const Texture& diffuse, bool indexed, std::uint16_t paletteOffset)
	{
		command.GetMaterial().SetTexture(0, diffuse);
		if (indexed) {
			Texture* palette = GetPaletteTexture();
			if (palette != nullptr) {
				command.GetMaterial().SetTexture(1, *palette);
			}
			GLUniformCache* palOffsetUniform = instanceBlock.GetUniform(Material::PaletteOffsetUniformName);
			if (palOffsetUniform != nullptr) {
				palOffsetUniform->SetFloatValue((float)paletteOffset);
			}
		}
	}

#if defined(DEATH_DEBUG)
	void ContentResolver::MigrateGraphics(StringView path)
	{
		String auraPath = fs::CombinePath({ GetContentPath(), "Animations"_s, String(path.exceptSuffix(4) + ".aura"_s) });
		if (fs::FileExists(auraPath)) {
			return;
		}

		auto s = fs::Open(fs::CombinePath({ GetContentPath(), "Animations"_s, String(path + ".res"_s) }), FileAccess::Read);
		auto fileSize = s->GetSize();
		if (fileSize < 4 || fileSize > 64 * 1024 * 1024) {
			// 64 MB file size limit, also if not found try to use cache
			return;
		}

		auto buffer = std::make_unique<char[]>(fileSize);
		s->Read(buffer.get(), fileSize);
		s->Dispose();

		Json::CharReaderBuilder builder;
		auto reader = std::unique_ptr<Json::CharReader>(builder.newCharReader());
		Json::Value doc; std::string errors;
		if (reader->parse(buffer.get(), buffer.get() + fileSize, &doc, &errors)) {
			String fullPath = fs::CombinePath({ GetContentPath(), "Animations"_s, path });
			std::unique_ptr<ITextureLoader> texLoader = ITextureLoader::createFromFile(fullPath);
			if (texLoader->hasLoaded()) {
				auto texFormat = texLoader->texFormat().internalFormat();
				if (texFormat != GL_RGBA8 && texFormat != GL_RGB8) {
					return;
				}

				std::int32_t w = texLoader->width();
				std::int32_t h = texLoader->height();
				const std::uint32_t* palette = _palettes;
				bool needsMask = true;

				std::int64_t originalFlags;
				if (doc["Flags"].get(originalFlags) == Json::SUCCESS) {
					// Palette already applied, keep as is
					if ((originalFlags & 0x01) != 0x01) {
						palette = nullptr;
					}
					if ((originalFlags & 0x08) == 0x08) {
						needsMask = false;
					}
				}

				// TODO: Use FrameDuration instead
				double animDuration;
				if (doc["Duration"].get(animDuration) != Json::SUCCESS) {
					animDuration = 0.0;
				}

				std::int64_t frameCount;
				if (doc["FrameCount"].get(frameCount) != Json::SUCCESS || frameCount < 0) {
					frameCount = 0;
				}

				Vector2i frameDimensions = GetVector2iFromJson(doc["FrameSize"]);
				Vector2i frameConfiguration = GetVector2iFromJson(doc["FrameConfiguration"]);

				Vector2i hotspot = GetVector2iFromJson(doc["Hotspot"]);
				Vector2i coldspot = GetVector2iFromJson(doc["Coldspot"], Vector2i(InvalidValue, InvalidValue));
				Vector2i gunspot = GetVector2iFromJson(doc["Gunspot"], Vector2i(InvalidValue, InvalidValue));

				// Write to .aura file
				auto so = fs::Open(auraPath, FileAccess::Write);
				if (!so->IsValid()) return;

				std::uint8_t flags = 0x80;
				if (palette == nullptr) {
					flags |= 0x01;
				}
				if (!needsMask) {
					flags |= 0x02;
				}

				so->WriteValue<std::uint64_t>(0xB8EF8498E2BFBBEF);
				so->WriteValue<std::uint32_t>(0x0002208F | (flags << 24)); // Version 2 is reserved for sprites (or bigger images)

				so->WriteValue<std::uint8_t>(4);
				so->WriteValue<std::uint32_t>((std::uint32_t)frameDimensions.X);
				so->WriteValue<std::uint32_t>((std::uint32_t)frameDimensions.Y);

				// Include Sprite extension
				so->WriteValue<std::uint8_t>((std::uint8_t)frameConfiguration.X);
				so->WriteValue<std::uint8_t>((std::uint8_t)frameConfiguration.Y);
				so->WriteValue<std::uint16_t>((std::uint16_t)frameCount);
				so->WriteValue<std::uint16_t>((std::uint16_t)(animDuration <= 0.0 ? 0 : 256 * animDuration));

				if (hotspot.X != InvalidValue || hotspot.Y != InvalidValue) {
					so->WriteValue<std::uint16_t>((std::uint16_t)hotspot.X);
					so->WriteValue<std::uint16_t>((std::uint16_t)hotspot.Y);
				} else {
					so->WriteValue<std::uint16_t>(UINT16_MAX);
					so->WriteValue<std::uint16_t>(UINT16_MAX);
				}
				if (coldspot.X != InvalidValue || coldspot.Y != InvalidValue) {
					so->WriteValue<std::uint16_t>((std::uint16_t)coldspot.X);
					so->WriteValue<std::uint16_t>((std::uint16_t)coldspot.Y);
				} else {
					so->WriteValue<std::uint16_t>(UINT16_MAX);
					so->WriteValue<std::uint16_t>(UINT16_MAX);
				}
				if (gunspot.X != InvalidValue || gunspot.Y != InvalidValue) {
					so->WriteValue<std::uint16_t>((std::uint16_t)gunspot.X);
					so->WriteValue<std::uint16_t>((std::uint16_t)gunspot.Y);
				} else {
					so->WriteValue<std::uint16_t>(UINT16_MAX);
					so->WriteValue<std::uint16_t>(UINT16_MAX);
				}

				Compatibility::JJ2Anims::WriteImageContent(*so, texLoader->pixels(), w, h, 4);
			}
		}
	}
#endif
}