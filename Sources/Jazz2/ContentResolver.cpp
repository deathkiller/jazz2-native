#include "ContentResolver.h"
#include "ContentResolver.Shaders.h"
#include "Compatibility/JJ2Anims.Palettes.h"
#include "LevelHandler.h"
#include "Tiles/TileSet.h"
#if defined(DEATH_DEBUG)
#	include "Compatibility/JJ2Anims.h"
#endif

#include "../nCine/Application.h"
#include "../nCine/AppConfiguration.h"
#include "../nCine/ServiceLocator.h"
#include "../nCine/IO/CompressionUtils.h"
#include "../nCine/Graphics/ITextureLoader.h"
#include "../nCine/Graphics/RenderResources.h"
#include "../nCine/Base/Random.h"

#if defined(DEATH_TARGET_ANDROID)
#	include "../nCine/Backends/Android/AndroidApplication.h"
#	include "../nCine/Backends/Android/AndroidJniHelper.h"
#elif defined(DEATH_TARGET_WINDOWS_RT)
#	include <Environment.h>
#endif

#include <Containers/StringStlView.h>
#include <IO/MemoryStream.h>

#define SIMDJSON_EXCEPTIONS 0
#include "../simdjson/simdjson.h"

using namespace simdjson;

template<class T>
static Vector2i GetVector2iFromJson(simdjson_result<T> value, Vector2i defaultValue = Vector2i::Zero)
{
	ondemand::array itemArray;
	ondemand::array_iterator itemIterator;
	if (value.get(itemArray) == SUCCESS && itemArray.begin().get(itemIterator) == SUCCESS) {
		int64_t x = 0, y = 0;
		bool xf = (*itemIterator).get(x) == SUCCESS;
		++itemIterator;
		bool yf = (*itemIterator).get(y) == SUCCESS;
		if (xf && yf) {
			return Vector2i((int32_t)x, (int32_t)y);
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
		: _isHeadless(false), _isLoading(false), _cachedMetadata(64), _cachedGraphics(128), _palettes{}
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

		for (int32_t i = 0; i < (int32_t)FontType::Count; i++) {
			_fonts[i] = nullptr;
		}

		for (int32_t i = 0; i < (int32_t)PrecompiledShader::Count; i++) {
			_precompiledShaders[i] = nullptr;
		}
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
		if (AndroidJniWrap_Activity::hasExternalStoragePermission()) {
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
				String deviceBrand = AndroidJniClass_Version::deviceBrand();
				String deviceModel = AndroidJniClass_Version::deviceModel();
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
			auto& app = static_cast<AndroidApplication&>(theApplication());
			StringView dataPath = app.externalDataPath();
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
		_contentPath = "/usr/share/" NCINE_LINUX_PACKAGE "/Content/";
#	endif
#	if defined(NCINE_PACKAGED_CONTENT_PATH)
		// If Content is packaged with binaries, always use standard XDG paths for everything else
		auto localStorage = fs::GetLocalStorage();
		if (!localStorage.empty()) {
			auto appData = fs::CombinePath(localStorage, NCINE_LINUX_PACKAGE);
			_sourcePath = fs::CombinePath(appData, "Source/"_s);
			_cachePath = fs::CombinePath(appData, "Cache/"_s);
		} else {
			_sourcePath = "Source/"_s;
			_cachePath = "Cache/"_s;
		}
#	else
		if (fs::DirectoryExists(_contentPath)) {
			// Shared Content exists, try to use standard XDG paths
			auto localStorage = fs::GetLocalStorage();
			if (!localStorage.empty()) {
				// TODO: Change "Jazz² Resurrection" to NCINE_LINUX_PACKAGE
				_sourcePath = fs::CombinePath(localStorage, "Jazz² Resurrection/Source/"_s);
				_cachePath = fs::CombinePath(localStorage, "Jazz² Resurrection/Cache/"_s);
			} else {
				_sourcePath = "Source/"_s;
				_cachePath = "Cache/"_s;
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

			for (char letter = 'D'; letter <= 'F'; letter++) {
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
	}

	void ContentResolver::EndLoading()
	{
		// Release unreferenced metadata
		{
			auto it = _cachedMetadata.begin();
			while (it != _cachedMetadata.end()) {
				if ((it->second->Flags & MetadataFlags::Referenced) != MetadataFlags::Referenced) {
					it = _cachedMetadata.erase(it);
				} else {
					++it;
				}
			}
		}

		// Released unreferenced graphics
		{
			auto it = _cachedGraphics.begin();
			while (it != _cachedGraphics.end()) {
				if ((it->second->Flags & GenericGraphicResourceFlags::Referenced) != GenericGraphicResourceFlags::Referenced) {
					it = _cachedGraphics.erase(it);
				} else {
					++it;
				}
			}
		}

		_isLoading = false;
	}

	void ContentResolver::PreloadMetadataAsync(const StringView& path)
	{
		// TODO: reimplement async preloading
		RequestMetadata(path);
	}

	Metadata* ContentResolver::RequestMetadata(const StringView& path)
	{
		auto pathNormalized = fs::ToNativeSeparators(path);
		auto it = _cachedMetadata.find(String::nullTerminatedView(pathNormalized));
		if (it != _cachedMetadata.end()) {
			// Already loaded - Mark as referenced
			it->second->Flags |= MetadataFlags::Referenced;

			for (auto& resource : it->second->Graphics) {
				resource.second.Base->Flags |= GenericGraphicResourceFlags::Referenced;
			}

			return it->second.get();
		}

		// Try to load it
		auto s = fs::Open(fs::CombinePath({ GetContentPath(), "Metadata"_s, pathNormalized + ".res"_s }), FileAccessMode::Read);
		auto fileSize = s->GetSize();
		if (fileSize < 4 || fileSize > 64 * 1024 * 1024) {
			// 64 MB file size limit
			return nullptr;
		}

		auto buffer = std::make_unique<char[]>(fileSize + simdjson::SIMDJSON_PADDING);
		s->Read(buffer.get(), fileSize);
		buffer[fileSize] = '\0';

		std::unique_ptr<Metadata> metadata = std::make_unique<Metadata>();
		metadata->Flags |= MetadataFlags::Referenced;

		ondemand::parser parser;
		ondemand::document doc;
		if (parser.iterate(buffer.get(), fileSize, fileSize + simdjson::SIMDJSON_PADDING).get(doc) == SUCCESS) {
			metadata->BoundingBox = GetVector2iFromJson(doc["BoundingBox"], Vector2i(InvalidValue, InvalidValue));

			ondemand::object animations;
			if (doc["Animations"].get(animations) == SUCCESS) {
				size_t count;
				if (animations.count_fields().get(count) == SUCCESS) {
					metadata->Graphics.reserve(count);
				}

				for (auto it : animations) {
					std::string_view key, assetPath;
					ondemand::object value;
					if (it.unescaped_key().get(key) != SUCCESS || it.value().get(value) != SUCCESS || key.empty() ||
						value["Path"].get(assetPath) != SUCCESS || assetPath.empty()) {
						continue;
					}

					GraphicResource graphics;
					graphics.LoopMode = AnimationLoopMode::Loop;

					//bool keepIndexed = false;

					uint64_t flags;
					if (value["Flags"].get(flags) == SUCCESS) {
						if ((flags & 0x01) == 0x01) {
							graphics.LoopMode = AnimationLoopMode::Once;
						}
						//if ((flags & 0x02) == 0x02) {
						//	keepIndexed = true;
						//}
					}

					// TODO: Implement true indexed sprites
					uint64_t paletteOffset;
					if (value["PaletteOffset"].get(paletteOffset) != SUCCESS) {
						paletteOffset = 0;
					}

					graphics.Base = RequestGraphics(assetPath, (uint16_t)paletteOffset);
					if (graphics.Base == nullptr) {
						continue;
					}

					int64_t frameOffset;
					if (value["FrameOffset"].get(frameOffset) != SUCCESS) {
						frameOffset = 0;
					}
					graphics.FrameOffset = (int32_t)frameOffset;

					graphics.AnimDuration = graphics.Base->AnimDuration;
					graphics.FrameCount = graphics.Base->FrameCount;

					int64_t frameCount;
					if (value["FrameCount"].get(frameCount) == SUCCESS) {
						graphics.FrameCount = (int32_t)frameCount;
					} else {
						graphics.FrameCount -= graphics.FrameOffset;
					}

					// TODO: Use AnimDuration instead
					double frameRate;
					if (value["FrameRate"].get(frameRate) == SUCCESS) {
						graphics.AnimDuration = (frameRate <= 0 ? -1.0f : (1.0f / (float)frameRate) * 5.0f);
					}

					ondemand::array states;
					if (value["States"].get(states) == SUCCESS) {
						for (auto stateItem : states) {
							int64_t state;
							if (stateItem.get(state) == SUCCESS) {
								graphics.State.push_back((AnimState)state);
							}
						}
					}

					// If no bounding box is provided, use the first sprite
					if (metadata->BoundingBox == Vector2i(InvalidValue, InvalidValue)) {
						// TODO: Remove this bounding box reduction
						metadata->BoundingBox = graphics.Base->FrameDimensions - Vector2i(2, 2);
					}

					metadata->Graphics.emplace(key, std::move(graphics));
				}
			}

			if (!_isHeadless) {
				// Don't load sounds in headless mode
				ondemand::object sounds;
				if (doc["Sounds"].get(sounds) == SUCCESS) {
					size_t count;
					if (sounds.count_fields().get(count) == SUCCESS) {
						metadata->Sounds.reserve(count);
					}

					for (auto it : sounds) {
						std::string_view key;
						ondemand::object value;
						ondemand::array assetPaths;
						bool isEmpty;
						if (it.unescaped_key().get(key) != SUCCESS || it.value().get(value) != SUCCESS || key.empty() ||
							value["Paths"].get(assetPaths) != SUCCESS || assetPaths.is_empty().get(isEmpty) != SUCCESS || isEmpty) {
							continue;
						}

						SoundResource sound;

						for (auto assetPathItem : assetPaths) {
							std::string_view assetPath;
							if (assetPathItem.get(assetPath) == SUCCESS && !assetPath.empty()) {
								auto assetPathNormalized = fs::ToNativeSeparators(assetPath);
								String fullPath = fs::CombinePath({ GetContentPath(), "Animations"_s, assetPathNormalized });
								if (!fs::IsReadableFile(fullPath)) {
									fullPath = fs::CombinePath({ GetCachePath(), "Animations"_s, assetPathNormalized });
									if (!fs::IsReadableFile(fullPath)) {
										continue;
									}
								}
								sound.Buffers.emplace_back(std::make_unique<AudioBuffer>(fullPath));
							}
						}

						if (!sound.Buffers.empty()) {
							metadata->Sounds.emplace(key, std::move(sound));
						}
					}
				}
			}
		}

		return _cachedMetadata.emplace(pathNormalized, std::move(metadata)).first->second.get();
	}

	GenericGraphicResource* ContentResolver::RequestGraphics(const StringView& path, uint16_t paletteOffset)
	{
		// First resources are requested, reset _isLoading flag, because palette should be already applied
		_isLoading = false;

		auto pathNormalized = fs::ToNativeSeparators(path);
		auto it = _cachedGraphics.find(Pair(String::nullTerminatedView(pathNormalized), paletteOffset));
		if (it != _cachedGraphics.end()) {
			// Already loaded - Mark as referenced
			it->second->Flags |= GenericGraphicResourceFlags::Referenced;
			return it->second.get();
		}

		if (fs::GetExtension(pathNormalized) == "aura"_s) {
			return RequestGraphicsAura(pathNormalized, paletteOffset);
		}

		auto s = fs::Open(fs::CombinePath({ GetContentPath(), "Animations"_s, pathNormalized + ".res"_s }), FileAccessMode::Read);
		auto fileSize = s->GetSize();
		if (fileSize < 4 || fileSize > 64 * 1024 * 1024) {
			// 64 MB file size limit, also if not found try to use cache
			return nullptr;
		}

		auto buffer = std::make_unique<char[]>(fileSize + simdjson::SIMDJSON_PADDING);
		s->Read(buffer.get(), fileSize);
		s->Close();
		buffer[fileSize] = '\0';

		ondemand::parser parser;
		ondemand::document doc;
		if (parser.iterate(buffer.get(), fileSize, fileSize + simdjson::SIMDJSON_PADDING).get(doc) == SUCCESS) {
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

				int32_t w = texLoader->width();
				int32_t h = texLoader->height();
				uint32_t* pixels = (uint32_t*)texLoader->pixels();
				const uint32_t* palette = _palettes + paletteOffset;
				bool linearSampling = false;
				bool needsMask = true;

				uint64_t flags;
				if (doc["Flags"].get(flags) == SUCCESS) {
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

				if (needsMask) {
					graphics->Mask = std::make_unique<uint8_t[]>(w * h);

					for (int32_t i = 0; i < w * h; i++) {
						// Save original alpha value for collision checking
						graphics->Mask[i] = ((pixels[i] >> 24) & 0xff);
						if (palette != nullptr) {
							uint32_t color = palette[pixels[i] & 0xff];
							pixels[i] = (color & 0xffffff) | ((((color >> 24) & 0xff) * ((pixels[i] >> 24) & 0xff) / 255) << 24);
						}
					}
				} else if (palette != nullptr) {
					for (int32_t i = 0; i < w * h; i++) {
						uint32_t color = palette[pixels[i] & 0xff];
						pixels[i] = (color & 0xffffff) | ((((color >> 24) & 0xff) * ((pixels[i] >> 24) & 0xff) / 255) << 24);
					}
				}

				if (!_isHeadless) {
					// Don't load textures in headless mode, only collision masks
					graphics->TextureDiffuse = std::make_unique<Texture>(fullPath.data(), Texture::Format::RGBA8, w, h);
					graphics->TextureDiffuse->loadFromTexels((unsigned char*)pixels, 0, 0, w, h);
					graphics->TextureDiffuse->setMinFiltering(linearSampling ? SamplerFilter::Linear : SamplerFilter::Nearest);
					graphics->TextureDiffuse->setMagFiltering(linearSampling ? SamplerFilter::Linear : SamplerFilter::Nearest);
				}

				// TODO: Use FrameDuration instead
				double animDuration;
				if (doc["Duration"].get(animDuration) != SUCCESS) {
					animDuration = 0.0;
				}
				graphics->AnimDuration = (float)animDuration;

				int64_t frameCount;
				if (doc["FrameCount"].get(frameCount) != SUCCESS) {
					frameCount = 0;
				}
				graphics->FrameCount = (int32_t)frameCount;

				graphics->FrameDimensions = GetVector2iFromJson(doc["FrameSize"]);
				graphics->FrameConfiguration = GetVector2iFromJson(doc["FrameConfiguration"]);
				
				graphics->Hotspot = GetVector2iFromJson(doc["Hotspot"]);
				graphics->Coldspot = GetVector2iFromJson(doc["Coldspot"], Vector2i(InvalidValue, InvalidValue));
				graphics->Gunspot = GetVector2iFromJson(doc["Gunspot"], Vector2i(InvalidValue, InvalidValue));

#if defined(DEATH_DEBUG)
				MigrateGraphics(pathNormalized);
#endif
				return _cachedGraphics.emplace(Pair(String(pathNormalized), paletteOffset), std::move(graphics)).first->second.get();
			}
		}

		return nullptr;
	}

	GenericGraphicResource* ContentResolver::RequestGraphicsAura(const StringView& path, uint16_t paletteOffset)
	{
		// Try "Content" directory first, then "Cache" directory
		String fullPath = fs::CombinePath({ GetContentPath(), "Animations"_s, path });
		if (!fs::IsReadableFile(fullPath)) {
			fullPath = fs::CombinePath({ GetCachePath(), "Animations"_s, path });
		}

		auto s = fs::Open(fullPath, FileAccessMode::Read);
		auto fileSize = s->GetSize();
		if (fileSize < 16 || fileSize > 64 * 1024 * 1024) {
			// 64 MB file size limit, also if not found try to use cache
			return nullptr;
		}

		uint64_t signature1 = s->ReadValue<uint64_t>();
		uint32_t signature2 = s->ReadValue<uint16_t>();
		uint8_t version = s->ReadValue<uint8_t>();
		uint8_t flags = s->ReadValue<uint8_t>();

		if (signature1 != 0xB8EF8498E2BFBBEF || signature2 != 0x208F || version != 2 || (flags & 0x80) != 0x80) {
			return nullptr;
		}

		uint8_t channelCount = s->ReadValue<uint8_t>();
		uint32_t frameDimensionsX = s->ReadValue<uint32_t>();
		uint32_t frameDimensionsY = s->ReadValue<uint32_t>();

		uint8_t frameConfigurationX = s->ReadValue<uint8_t>();
		uint8_t frameConfigurationY = s->ReadValue<uint8_t>();
		uint16_t frameCount = s->ReadValue<uint16_t>();
		uint16_t animDuration = s->ReadValue<uint16_t>();

		uint16_t hotspotX = s->ReadValue<uint16_t>();
		uint16_t hotspotY = s->ReadValue<uint16_t>();

		uint16_t coldspotX = s->ReadValue<uint16_t>();
		uint16_t coldspotY = s->ReadValue<uint16_t>();

		uint16_t gunspotX = s->ReadValue<uint16_t>();
		uint16_t gunspotY = s->ReadValue<uint16_t>();

		uint32_t width = frameDimensionsX * frameConfigurationX;
		uint32_t height = frameDimensionsY * frameConfigurationY;

		std::unique_ptr<uint32_t[]> pixels = std::make_unique<uint32_t[]>(width * height);

		ReadImageFromFile(s, (uint8_t*)pixels.get(), width, height, channelCount);

		std::unique_ptr<GenericGraphicResource> graphics = std::make_unique<GenericGraphicResource>();
		graphics->Flags |= GenericGraphicResourceFlags::Referenced;

		const uint32_t* palette = _palettes + paletteOffset;
		bool linearSampling = false;
		bool needsMask = true;
		if ((flags & 0x01) == 0x01) {
			palette = nullptr;
			linearSampling = true;
		}
		if ((flags & 0x02) == 0x02) {
			needsMask = false;
		}

		if (needsMask) {
			graphics->Mask = std::make_unique<uint8_t[]>(width * height);

			for (uint32_t i = 0; i < width * height; i++) {
				// Save original alpha value for collision checking
				graphics->Mask[i] = ((pixels[i] >> 24) & 0xff);
				if (palette != nullptr) {
					uint32_t color = palette[pixels[i] & 0xff];
					pixels[i] = (color & 0xffffff) | ((((color >> 24) & 0xff) * ((pixels[i] >> 24) & 0xff) / 255) << 24);
				}
			}
		} else if (palette != nullptr) {
			for (uint32_t i = 0; i < width * height; i++) {
				uint32_t color = palette[pixels[i] & 0xff];
				pixels[i] = (color & 0xffffff) | ((((color >> 24) & 0xff) * ((pixels[i] >> 24) & 0xff) / 255) << 24);
			}
		}

		if (!_isHeadless) {
			// Don't load textures in headless mode, only collision masks
			graphics->TextureDiffuse = std::make_unique<Texture>(fullPath.data(), Texture::Format::RGBA8, width, height);
			graphics->TextureDiffuse->loadFromTexels((unsigned char*)pixels.get(), 0, 0, width, height);
			graphics->TextureDiffuse->setMinFiltering(linearSampling ? SamplerFilter::Linear : SamplerFilter::Nearest);
			graphics->TextureDiffuse->setMagFiltering(linearSampling ? SamplerFilter::Linear : SamplerFilter::Nearest);
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

		return _cachedGraphics.emplace(Pair(String(path), paletteOffset), std::move(graphics)).first->second.get();
	}

	void ContentResolver::ReadImageFromFile(std::unique_ptr<Stream>& s, uint8_t* data, int32_t width, int32_t height, int32_t channelCount)
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
		int32_t run = 0;
		int32_t px_len = width * height * channelCount;

		px.rgba.r = 0;
		px.rgba.g = 0;
		px.rgba.b = 0;
		px.rgba.a = 255;

		for (int32_t px_pos = 0; px_pos < px_len; px_pos += channelCount) {
			if (run > 0) {
				run--;
			} else {
				int32_t b1 = s->ReadValue<uint8_t>();

				if (b1 == QOI_OP_RGB) {
					px.rgba.r = s->ReadValue<uint8_t>();
					px.rgba.g = s->ReadValue<uint8_t>();
					px.rgba.b = s->ReadValue<uint8_t>();
				} else if (b1 == QOI_OP_RGBA) {
					px.rgba.r = s->ReadValue<uint8_t>();
					px.rgba.g = s->ReadValue<uint8_t>();
					px.rgba.b = s->ReadValue<uint8_t>();
					px.rgba.a = s->ReadValue<uint8_t>();
				} else if ((b1 & QOI_MASK_2) == QOI_OP_INDEX) {
					px = index[b1];
				} else if ((b1 & QOI_MASK_2) == QOI_OP_DIFF) {
					px.rgba.r += ((b1 >> 4) & 0x03) - 2;
					px.rgba.g += ((b1 >> 2) & 0x03) - 2;
					px.rgba.b += (b1 & 0x03) - 2;
				} else if ((b1 & QOI_MASK_2) == QOI_OP_LUMA) {
					int32_t b2 = s->ReadValue<uint8_t>();
					int32_t vg = (b1 & 0x3f) - 32;
					px.rgba.r += vg - 8 + ((b2 >> 4) & 0x0f);
					px.rgba.g += vg;
					px.rgba.b += vg - 8 + (b2 & 0x0f);
				} else if ((b1 & QOI_MASK_2) == QOI_OP_RUN) {
					run = (b1 & 0x3f);
				}

				index[QOI_COLOR_HASH(px) & 63] = px;
			}

			*(rgba_t*)(data + px_pos) = px;
		}
	}

	std::unique_ptr<Tiles::TileSet> ContentResolver::RequestTileSet(const StringView& path, uint16_t captionTileId, bool applyPalette, const uint8_t* paletteRemapping)
	{
		// Try "Content" directory first, then "Cache" directory
		String fullPath = fs::CombinePath({ GetContentPath(), "Tilesets"_s, path + ".j2t"_s });
		if (!fs::IsReadableFile(fullPath)) {
			fullPath = fs::CombinePath({ GetCachePath(), "Tilesets"_s, path + ".j2t"_s });
		}

		auto s = fs::Open(fullPath, FileAccessMode::Read);
		if (!s->IsValid()) {
			return nullptr;
		}

		uint64_t signature1 = s->ReadValue<uint64_t>();
		uint16_t signature2 = s->ReadValue<uint16_t>();
		uint8_t version = s->ReadValue<uint8_t>();
		/*uint8_t flags =*/ s->ReadValue<uint8_t>();
		ASSERT_MSG(signature1 == 0xB8EF8498E2BFBBEF && signature2 == 0x208F && version == 2, "Invalid file");

		// TODO: Use single channel instead
		uint8_t channelCount = s->ReadValue<uint8_t>();
		uint32_t width = s->ReadValue<uint32_t>();
		uint32_t height = s->ReadValue<uint32_t>();
		uint16_t tileCount = s->ReadValue<uint16_t>();

		// Read compressed palette and mask
		int32_t compressedSize = s->ReadValue<int32_t>();
		int32_t uncompressedSize = s->ReadValue<int32_t>();
		std::unique_ptr<uint8_t[]> compressedBuffer = std::make_unique<uint8_t[]>(compressedSize);
		std::unique_ptr<uint8_t[]> uncompressedBuffer = std::make_unique<uint8_t[]>(uncompressedSize);
		s->Read(compressedBuffer.get(), compressedSize);

		auto result = CompressionUtils::Inflate(compressedBuffer.get(), compressedSize, uncompressedBuffer.get(), uncompressedSize);
		if (result != DecompressionResult::Success) {
			return nullptr;
		}
		MemoryStream uc(uncompressedBuffer.get(), uncompressedSize);

		// Palette
		if (applyPalette) {
			uint32_t newPalette[ColorsPerPalette];
			uc.Read(newPalette, ColorsPerPalette * sizeof(uint32_t));

			if (std::memcmp(_palettes, newPalette, ColorsPerPalette * sizeof(uint32_t)) != 0) {
				// Palettes differs, drop all cached resources, so it will be reloaded with new palette
				if (_isLoading) {
					_cachedMetadata.clear();
					_cachedGraphics.clear();

					for (int32_t i = 0; i < (int32_t)FontType::Count; i++) {
						_fonts[i] = nullptr;
					}
				}

				std::memcpy(_palettes, newPalette, ColorsPerPalette * sizeof(uint32_t));
				RecreateGemPalettes();
			}
		} else {
			uc.Seek(ColorsPerPalette * sizeof(uint32_t), SeekOrigin::Current);
		}

		// Mask
		uint32_t maskSize = uc.ReadValue<uint32_t>();
		std::unique_ptr<uint8_t[]> mask = std::make_unique<uint8_t[]>(maskSize * 8);
		for (uint32_t j = 0; j < maskSize; j++) {
			uint8_t idx = uc.ReadValue<uint8_t>();
			for (uint32_t k = 0; k < 8; k++) {
				uint32_t pixelIdx = 8 * j + k;
				mask[pixelIdx] = (((idx >> k) & 0x01) != 0);
			}
		}

		std::unique_ptr<Texture> textureDiffuse;
		std::unique_ptr<Color[]> captionTile;

		if (!_isHeadless) {
			// Don't load textures in headless mode, only collision masks
			// Texture
			std::unique_ptr<uint32_t[]> pixels = std::make_unique<uint32_t[]>(width * height);
			ReadImageFromFile(s, (uint8_t*)pixels.get(), width, height, channelCount);

			if (paletteRemapping != nullptr) {
				for (uint32_t i = 0; i < width * height; i++) {
					uint32_t color = _palettes[paletteRemapping[pixels[i] & 0xff]];
					pixels[i] = (color & 0xffffff) | ((((color >> 24) & 0xff) * ((pixels[i] >> 24) & 0xff) / 255) << 24);
				}
			} else {
				for (uint32_t i = 0; i < width * height; i++) {
					uint32_t color = _palettes[pixels[i] & 0xff];
					pixels[i] = (color & 0xffffff) | ((((color >> 24) & 0xff) * ((pixels[i] >> 24) & 0xff) / 255) << 24);
				}
			}

			textureDiffuse = std::make_unique<Texture>(fullPath.data(), Texture::Format::RGBA8, width, height);
			textureDiffuse->loadFromTexels((unsigned char*)pixels.get(), 0, 0, width, height);
			textureDiffuse->setMinFiltering(SamplerFilter::Nearest);
			textureDiffuse->setMagFiltering(SamplerFilter::Nearest);

			// Caption Tile
			if (captionTileId > 0) {
				int32_t tw = (width / TileSet::DefaultTileSize);
				int32_t tx = (captionTileId % tw) * TileSet::DefaultTileSize;
				int32_t ty = (captionTileId / tw) * TileSet::DefaultTileSize;
				if (tx + TileSet::DefaultTileSize <= width && ty + TileSet::DefaultTileSize <= height) {
					captionTile = std::make_unique<Color[]>(TileSet::DefaultTileSize * TileSet::DefaultTileSize / 3);
					for (int32_t y = 0; y < TileSet::DefaultTileSize / 3; y++) {
						for (int32_t x = 0; x < TileSet::DefaultTileSize; x++) {
							Color c1 = Color(pixels[((ty + y * 3) * width) + tx + x]);
							Color c2 = Color(pixels[((ty + y * 3 + 1) * width) + tx + x]);
							Color c3 = Color(pixels[((ty + y * 3 + 2) * width) + tx + x]);
							captionTile[y * TileSet::DefaultTileSize + x] = Color((c1.B() + c2.B() + c3.B()) / 3, (c1.G() + c2.G() + c3.G()) / 3, (c1.R() + c2.R() + c3.R()) / 3);
						}
					}
				}
			}
		}

		return std::make_unique<Tiles::TileSet>(tileCount, std::move(textureDiffuse), std::move(mask), maskSize * 8, std::move(captionTile));
	}

	bool ContentResolver::LevelExists(const StringView& episodeName, const StringView& levelName)
	{
		// Try "Content" directory first, then "Cache" directory
		return (fs::IsReadableFile(fs::CombinePath({ GetContentPath(), "Episodes"_s, episodeName, levelName + ".j2l"_s })) || 
				fs::IsReadableFile(fs::CombinePath({ GetCachePath(), "Episodes"_s, episodeName, levelName + ".j2l"_s })));
	}

	bool ContentResolver::LoadLevel(LevelHandler* levelHandler, const StringView& path, GameDifficulty difficulty)
	{
		// Try "Content" directory first, then "Cache" directory
		auto pathNormalized = fs::ToNativeSeparators(path);
		String fullPath = fs::CombinePath({ GetContentPath(), "Episodes"_s, pathNormalized + ".j2l"_s });
		if (!fs::IsReadableFile(fullPath)) {
			fullPath = fs::CombinePath({ GetCachePath(), "Episodes"_s, pathNormalized + ".j2l"_s });
		}

		auto s = fs::Open(fullPath, FileAccessMode::Read);
		RETURNF_ASSERT_MSG(s->IsValid(), "Cannot open file for reading");

		uint64_t signature = s->ReadValue<uint64_t>();
		uint8_t fileType = s->ReadValue<uint8_t>();
		RETURNF_ASSERT_MSG(signature == 0x2095A59FF0BFBBEF && fileType == LevelFile, "File has invalid signature");

		uint16_t flags = s->ReadValue<uint16_t>();

		// Read compressed data
		int32_t compressedSize = s->ReadValue<int32_t>();
		int32_t uncompressedSize = s->ReadValue<int32_t>();
		std::unique_ptr<uint8_t[]> compressedBuffer = std::make_unique<uint8_t[]>(compressedSize);
		std::unique_ptr<uint8_t[]> uncompressedBuffer = std::make_unique<uint8_t[]>(uncompressedSize);
		s->Read(compressedBuffer.get(), compressedSize);

		s->Close();

		auto result = CompressionUtils::Inflate(compressedBuffer.get(), compressedSize, uncompressedBuffer.get(), uncompressedSize);
		RETURNF_ASSERT_MSG(result == DecompressionResult::Success, "File cannot be uncompressed");
		MemoryStream uc(uncompressedBuffer.get(), uncompressedSize);

		// Read metadata
		uint8_t nameSize = uc.ReadValue<uint8_t>();
		String name(NoInit, nameSize);
		uc.Read(name.data(), nameSize);

		nameSize = uc.ReadValue<uint8_t>();
		String nextLevel(NoInit, nameSize);
		uc.Read(nextLevel.data(), nameSize);

		nameSize = uc.ReadValue<uint8_t>();
		String secretLevel(NoInit, nameSize);
		uc.Read(secretLevel.data(), nameSize);

		nameSize = uc.ReadValue<uint8_t>();
		String bonusLevel(NoInit, nameSize);
		uc.Read(bonusLevel.data(), nameSize);

		// Default Tileset
		nameSize = uc.ReadValue<uint8_t>();
		String defaultTileset(NoInit, nameSize);
		uc.Read(defaultTileset.data(), nameSize);

		// Default Music
		nameSize = uc.ReadValue<uint8_t>();
		String defaultMusic(NoInit, nameSize);
		uc.Read(defaultMusic.data(), nameSize);

		uint32_t rawAmbientColor = uc.ReadValue<uint32_t>();
		Vector4f ambientColor = Vector4f((rawAmbientColor & 0xff) / 255.0f, ((rawAmbientColor >> 8) & 0xff) / 255.0f,
			((rawAmbientColor >> 16) & 0xff) / 255.0f, ((rawAmbientColor >> 24) & 0xff) / 255.0f);

		WeatherType defaultWeatherType = (WeatherType)uc.ReadValue<uint8_t>();
		uint8_t defaultWeatherIntensity = uc.ReadValue<uint8_t>();
		uint16_t waterLevel = uc.ReadValue<uint16_t>();

		uint16_t captionTileId = uc.ReadValue<uint16_t>();

		PitType pitType;
		if ((flags & 0x01) == 0x01) {
			pitType = ((flags & 0x02) == 0x02 ? PitType::InstantDeathPit : PitType::FallForever);
		} else {
			pitType = PitType::StandOnPlatform;
		}

		bool hasCustomPalette = ((flags & 0x04) == 0x04);
		if (hasCustomPalette) {
			uint32_t newPalette[ColorsPerPalette];
			uc.Read(newPalette, ColorsPerPalette * sizeof(uint32_t));

			if (std::memcmp(_palettes, newPalette, ColorsPerPalette * sizeof(uint32_t)) != 0) {
				// Palettes differs, drop all cached resources, so it will be reloaded with new palette
				if (_isLoading) {
					_cachedMetadata.clear();
					_cachedGraphics.clear();

					for (int32_t i = 0; i < (int32_t)FontType::Count; i++) {
						_fonts[i] = nullptr;
					}
				}

				std::memcpy(_palettes, newPalette, ColorsPerPalette * sizeof(uint32_t));
				RecreateGemPalettes();
			}
		}

		std::unique_ptr<Tiles::TileMap> tileMap = std::make_unique<Tiles::TileMap>(levelHandler, defaultTileset, captionTileId, pitType, !hasCustomPalette);

		// Extra Tilesets
		uint8_t extraTilesetCount = uc.ReadValue<uint8_t>();
		for (uint32_t i = 0; i < extraTilesetCount; i++) {
			uint8_t tilesetFlags = uc.ReadValue<uint8_t>();

			nameSize = uc.ReadValue<uint8_t>();
			String extraTileset = String(NoInit, nameSize);
			uc.Read(extraTileset.data(), nameSize);

			uint16_t offset = uc.ReadValue<uint16_t>();
			uint16_t count = uc.ReadValue<uint16_t>();

			uint8_t paletteRemapping[ColorsPerPalette];
			bool hasPaletteRemapping = ((tilesetFlags & 0x01) == 0x01);
			if (hasPaletteRemapping) {
				uc.Read(paletteRemapping, sizeof(paletteRemapping));
			}

			tileMap->AddTileSet(extraTileset, offset, count, hasPaletteRemapping ? paletteRemapping : nullptr);
		}

		// Text Event Strings
		uint8_t textEventStringsCount = uc.ReadValue<uint8_t>();
		SmallVector<String, 0> levelTexts;
		levelTexts.reserve(textEventStringsCount);
		for (uint32_t i = 0; i < textEventStringsCount; i++) {
			uint16_t textLength = uc.ReadValue<uint16_t>();
			String& text = levelTexts.emplace_back(NoInit, textLength);
			uc.Read(text.data(), textLength);
		}

		// Animated Tiles
		tileMap->ReadAnimatedTiles(uc);

		// Layers
		uint8_t layerCount = uc.ReadValue<uint8_t>();
		for (uint32_t i = 0; i < layerCount; i++) {
			tileMap->ReadLayerConfiguration(uc);
		}

		// Events
		std::unique_ptr<Events::EventMap> eventMap = std::make_unique<Events::EventMap>(levelHandler, tileMap->Size(), pitType);
		eventMap->ReadEvents(uc, tileMap, difficulty);

		// TODO: Bonus level
		levelHandler->OnLevelLoaded(fullPath, name, nextLevel, secretLevel, tileMap, eventMap, defaultMusic, ambientColor,
			defaultWeatherType, defaultWeatherIntensity, waterLevel, levelTexts);

		return true;
	}

	void ContentResolver::ApplyDefaultPalette()
	{
		static_assert(sizeof(SpritePalette) == ColorsPerPalette * sizeof(uint32_t));

		if (std::memcmp(_palettes, SpritePalette, ColorsPerPalette * sizeof(uint32_t)) != 0) {
			// Palettes differs, drop all cached resources, so it will be reloaded with new palette
			if (_isLoading) {
				_cachedMetadata.clear();
				_cachedGraphics.clear();

				for (int32_t i = 0; i < (int32_t)FontType::Count; i++) {
					_fonts[i] = nullptr;
				}
			}

			std::memcpy(_palettes, SpritePalette, ColorsPerPalette * sizeof(uint32_t));
			RecreateGemPalettes();
		}
	}

	std::optional<Episode> ContentResolver::GetEpisode(const StringView& name)
	{
		String fullPath = fs::CombinePath({ GetContentPath(), "Episodes"_s, name + ".j2e"_s });
		if (!fs::IsReadableFile(fullPath)) {
			fullPath = fs::CombinePath({ GetCachePath(), "Episodes"_s, name + ".j2e"_s });
		}
		return GetEpisodeByPath(fullPath);
	}

	std::optional<Episode> ContentResolver::GetEpisodeByPath(const StringView& path)
	{
		auto s = fs::Open(path, FileAccessMode::Read);
		if (s->GetSize() < 16) {
			return std::nullopt;
		}

		uint64_t signature = s->ReadValue<uint64_t>();
		uint8_t fileType = s->ReadValue<uint8_t>();
		if (signature != 0x2095A59FF0BFBBEF || fileType != ContentResolver::EpisodeFile) {
			return std::nullopt;
		}

		Episode episode;
		episode.Name = fs::GetFileNameWithoutExtension(path);

		/*uint16_t flags =*/ s->ReadValue<uint16_t>();

		uint8_t nameLength = s->ReadValue<uint8_t>();
		episode.DisplayName = String(NoInit, nameLength);
		s->Read(episode.DisplayName.data(), nameLength);

		episode.Position = s->ReadValue<uint16_t>();

		nameLength = s->ReadValue<uint8_t>();
		episode.FirstLevel = String(NoInit, nameLength);
		s->Read(episode.FirstLevel.data(), nameLength);

		nameLength = s->ReadValue<uint8_t>();
		episode.PreviousEpisode = String(NoInit, nameLength);
		s->Read(episode.PreviousEpisode.data(), nameLength);

		nameLength = s->ReadValue<uint8_t>();
		episode.NextEpisode = String(NoInit, nameLength);
		s->Read(episode.NextEpisode.data(), nameLength);

		return episode;
	}

	std::unique_ptr<AudioStreamPlayer> ContentResolver::GetMusic(const StringView& path)
	{
		// Don't load sounds in headless mode
		if (_isHeadless) {
			return nullptr;
		}

		String fullPath = fs::CombinePath({ GetContentPath(), "Music"_s, path });
		if (!fs::IsReadableFile(fullPath)) {
			// "Source" directory must be case in-sensitive
			fullPath = fs::FindPathCaseInsensitive(fs::CombinePath(GetSourcePath(), path));
		}
		if (!fs::IsReadableFile(fullPath)) {
			return nullptr;
		}
		return std::make_unique<AudioStreamPlayer>(fullPath);
	}

	UI::Font* ContentResolver::GetFont(FontType fontType)
	{
		if (fontType >= FontType::Count) {
			return nullptr;
		}

		auto& font = _fonts[(int32_t)fontType];
		if (font == nullptr) {
			switch (fontType) {
				case FontType::Small: font = std::make_unique<UI::Font>(fs::CombinePath({ GetContentPath(), "Animations"_s, "UI"_s, "font_small.png"_s }), _palettes); break;
				case FontType::Medium: font = std::make_unique<UI::Font>(fs::CombinePath({ GetContentPath(), "Animations"_s, "UI"_s, "font_medium.png"_s }), _palettes); break;
				default: return nullptr;
			}
		}

		return font.get();
	}

	Shader* ContentResolver::GetShader(PrecompiledShader shader)
	{
		if (shader >= PrecompiledShader::Count) {
			return nullptr;
		}

		return _precompiledShaders[(int32_t)shader].get();
	}

	void ContentResolver::CompileShaders()
	{
		_precompiledShaders[(int32_t)PrecompiledShader::Lighting] = CompileShader("Lighting", Shaders::LightingVs, Shaders::LightingFs);
		_precompiledShaders[(int32_t)PrecompiledShader::BatchedLighting] = CompileShader("BatchedLighting", Shaders::BatchedLightingVs, Shaders::LightingFs, Shader::Introspection::NoUniformsInBlocks);
		_precompiledShaders[(int32_t)PrecompiledShader::Lighting]->registerBatchedShader(*_precompiledShaders[(int32_t)PrecompiledShader::BatchedLighting]);

		_precompiledShaders[(int32_t)PrecompiledShader::Blur] = CompileShader("Blur", Shader::DefaultVertex::SPRITE, Shaders::BlurFs);
		_precompiledShaders[(int32_t)PrecompiledShader::Downsample] = CompileShader("Downsample", Shader::DefaultVertex::SPRITE, Shaders::DownsampleFs);
		_precompiledShaders[(int32_t)PrecompiledShader::Combine] = CompileShader("Combine", Shaders::CombineVs, Shaders::CombineFs);
		_precompiledShaders[(int32_t)PrecompiledShader::CombineWithWater] = CompileShader("CombineWithWater", Shaders::CombineVs, Shaders::CombineWithWaterFs);
		_precompiledShaders[(int32_t)PrecompiledShader::CombineWithWaterLow] = CompileShader("CombineWithWaterLow", Shaders::CombineVs, Shaders::CombineWithWaterLowFs);

		_precompiledShaders[(int32_t)PrecompiledShader::TexturedBackground] = CompileShader("TexturedBackground", Shader::DefaultVertex::SPRITE, Shaders::TexturedBackgroundFs);
		_precompiledShaders[(int32_t)PrecompiledShader::TexturedBackgroundCircle] = CompileShader("TexturedBackgroundCircle", Shader::DefaultVertex::SPRITE, Shaders::TexturedBackgroundCircleFs);

		_precompiledShaders[(int32_t)PrecompiledShader::Colorized] = CompileShader("Colorized", Shader::DefaultVertex::SPRITE, Shaders::ColorizedFs);
		_precompiledShaders[(int32_t)PrecompiledShader::BatchedColorized] = CompileShader("BatchedColorized", Shader::DefaultVertex::BATCHED_SPRITES, Shaders::ColorizedFs, Shader::Introspection::NoUniformsInBlocks);
		_precompiledShaders[(int32_t)PrecompiledShader::Colorized]->registerBatchedShader(*_precompiledShaders[(int32_t)PrecompiledShader::BatchedColorized]);

		_precompiledShaders[(int32_t)PrecompiledShader::Tinted] = CompileShader("Tinted", Shader::DefaultVertex::SPRITE, Shaders::TintedFs);
		_precompiledShaders[(int32_t)PrecompiledShader::BatchedTinted] = CompileShader("BatchedTinted", Shader::DefaultVertex::BATCHED_SPRITES, Shaders::TintedFs, Shader::Introspection::NoUniformsInBlocks);
		_precompiledShaders[(int32_t)PrecompiledShader::Tinted]->registerBatchedShader(*_precompiledShaders[(int32_t)PrecompiledShader::BatchedTinted]);

		_precompiledShaders[(int32_t)PrecompiledShader::Outline] = CompileShader("Outline", Shader::DefaultVertex::SPRITE, Shaders::OutlineFs);
		_precompiledShaders[(int32_t)PrecompiledShader::BatchedOutline] = CompileShader("BatchedOutline", Shader::DefaultVertex::BATCHED_SPRITES, Shaders::OutlineFs, Shader::Introspection::NoUniformsInBlocks);
		_precompiledShaders[(int32_t)PrecompiledShader::Outline]->registerBatchedShader(*_precompiledShaders[(int32_t)PrecompiledShader::BatchedOutline]);

		_precompiledShaders[(int32_t)PrecompiledShader::WhiteMask] = CompileShader("WhiteMask", Shader::DefaultVertex::SPRITE, Shaders::WhiteMaskFs);
		_precompiledShaders[(int32_t)PrecompiledShader::BatchedWhiteMask] = CompileShader("BatchedWhiteMask", Shader::DefaultVertex::BATCHED_SPRITES, Shaders::WhiteMaskFs, Shader::Introspection::NoUniformsInBlocks);
		_precompiledShaders[(int32_t)PrecompiledShader::WhiteMask]->registerBatchedShader(*_precompiledShaders[(int32_t)PrecompiledShader::BatchedWhiteMask]);

		_precompiledShaders[(int32_t)PrecompiledShader::PartialWhiteMask] = CompileShader("PartialWhiteMask", Shader::DefaultVertex::SPRITE, Shaders::PartialWhiteMaskFs);
		_precompiledShaders[(int32_t)PrecompiledShader::BatchedPartialWhiteMask] = CompileShader("BatchedPartialWhiteMask", Shader::DefaultVertex::BATCHED_SPRITES, Shaders::PartialWhiteMaskFs, Shader::Introspection::NoUniformsInBlocks);
		_precompiledShaders[(int32_t)PrecompiledShader::PartialWhiteMask]->registerBatchedShader(*_precompiledShaders[(int32_t)PrecompiledShader::BatchedPartialWhiteMask]);

		_precompiledShaders[(int32_t)PrecompiledShader::FrozenMask] = CompileShader("FrozenMask", Shader::DefaultVertex::SPRITE, Shaders::FrozenMaskFs);
		_precompiledShaders[(int32_t)PrecompiledShader::BatchedFrozenMask] = CompileShader("BatchedFrozenMask", Shader::DefaultVertex::BATCHED_SPRITES, Shaders::FrozenMaskFs, Shader::Introspection::NoUniformsInBlocks);
		_precompiledShaders[(int32_t)PrecompiledShader::FrozenMask]->registerBatchedShader(*_precompiledShaders[(int32_t)PrecompiledShader::BatchedFrozenMask]);

		_precompiledShaders[(int32_t)PrecompiledShader::ShieldFire] = CompileShader("ShieldFire", Shaders::ShieldVs, Shaders::ShieldFireFs);
		_precompiledShaders[(int32_t)PrecompiledShader::BatchedShieldFire] = CompileShader("BatchedShieldFire", Shaders::BatchedShieldVs, Shaders::ShieldFireFs, Shader::Introspection::NoUniformsInBlocks);
		_precompiledShaders[(int32_t)PrecompiledShader::ShieldFire]->registerBatchedShader(*_precompiledShaders[(int32_t)PrecompiledShader::BatchedShieldFire]);

		_precompiledShaders[(int32_t)PrecompiledShader::ShieldLightning] = CompileShader("ShieldLightning", Shaders::ShieldVs, Shaders::ShieldLightningFs);
		_precompiledShaders[(int32_t)PrecompiledShader::BatchedShieldLightning] = CompileShader("BatchedShieldFire", Shaders::BatchedShieldVs, Shaders::ShieldLightningFs, Shader::Introspection::NoUniformsInBlocks);
		_precompiledShaders[(int32_t)PrecompiledShader::ShieldLightning]->registerBatchedShader(*_precompiledShaders[(int32_t)PrecompiledShader::BatchedShieldLightning]);

#if defined(ALLOW_RESCALE_SHADERS)
		_precompiledShaders[(int32_t)PrecompiledShader::ResizeHQ2x] = CompileShader("ResizeHQ2x", Shaders::ResizeHQ2xVs, Shaders::ResizeHQ2xFs);
		_precompiledShaders[(int32_t)PrecompiledShader::Resize3xBrz] = CompileShader("Resize3xBrz", Shaders::Resize3xBrzVs, Shaders::Resize3xBrzFs);
		_precompiledShaders[(int32_t)PrecompiledShader::ResizeCrtScanlines] = CompileShader("ResizeCrtScanlines", Shaders::ResizeCrtScanlinesVs, Shaders::ResizeCrtScanlinesFs);
		_precompiledShaders[(int32_t)PrecompiledShader::ResizeCrtShadowMask] = CompileShader("ResizeCrtShadowMask", Shaders::ResizeCrtVs, Shaders::ResizeCrtShadowMaskFs);
		_precompiledShaders[(int32_t)PrecompiledShader::ResizeCrtApertureGrille] = CompileShader("ResizeCrtApertureGrille", Shaders::ResizeCrtVs, Shaders::ResizeCrtApertureGrilleFs);
		_precompiledShaders[(int32_t)PrecompiledShader::ResizeMonochrome] = CompileShader("ResizeMonochrome", Shaders::ResizeMonochromeVs, Shaders::ResizeMonochromeFs);
#endif
		_precompiledShaders[(int32_t)PrecompiledShader::Antialiasing] = CompileShader("Antialiasing", Shaders::AntialiasingVs, Shaders::AntialiasingFs);

		_precompiledShaders[(int32_t)PrecompiledShader::Transition] = CompileShader("Transition", Shaders::TransitionVs, Shaders::TransitionFs);
	}

	std::unique_ptr<Shader> ContentResolver::CompileShader(const char* shaderName, Shader::DefaultVertex vertex, const char* fragment, Shader::Introspection introspection)
	{
		std::unique_ptr shader = std::make_unique<Shader>();
		if (shader->loadFromCache(shaderName, Shaders::Version, introspection)) {
			return shader;
		}

		const AppConfiguration& appCfg = theApplication().appConfiguration();
		const IGfxCapabilities& gfxCaps = theServiceLocator().gfxCapabilities();
		// Clamping the value as some drivers report a maximum size similar to SSBO one
		const int32_t maxUniformBlockSize = std::clamp(gfxCaps.value(IGfxCapabilities::GLIntValues::MAX_UNIFORM_BLOCK_SIZE), 0, 64 * 1024);

		// If the UBO is smaller than 64kb and fixed batch size is disabled, batched shaders need to be compiled twice to determine safe `BATCH_SIZE` define value
		const bool compileTwice = (maxUniformBlockSize < 64 * 1024 && appCfg.fixedBatchSize <= 0 && introspection == Shader::Introspection::NoUniformsInBlocks);

		int32_t batchSize;
		if (appCfg.fixedBatchSize > 0 && introspection == Shader::Introspection::NoUniformsInBlocks) {
			batchSize = appCfg.fixedBatchSize;
		} else if (compileTwice) {
			// The first compilation of a batched shader needs a `BATCH_SIZE` defined as 1
			batchSize = 1;
		} else {
			batchSize = GLShaderProgram::DefaultBatchSize;
		}

		shader->loadFromMemory(shaderName, compileTwice ? Shader::Introspection::Enabled : introspection, vertex, fragment, batchSize);

		if (compileTwice) {
			GLShaderUniformBlocks blocks(shader->getHandle(), Material::InstancesBlockName, nullptr);
			GLUniformBlockCache* block = blocks.uniformBlock(Material::InstancesBlockName);
			ASSERT(block != nullptr);
			if (block != nullptr) {
				batchSize = maxUniformBlockSize / block->size();
				LOGI("Shader \"%s\" - block size: %d + %d align bytes, max batch size: %d", shaderName,
					block->size() - block->alignAmount(), block->alignAmount(), batchSize);
				
				bool hasLinked = false;
				while (batchSize > 0) {
					hasLinked = shader->loadFromMemory(shaderName, introspection, vertex, fragment, batchSize);
					if (hasLinked) {
						break;
					}

					batchSize--;
					LOGW("Failed to compile the shader, recompiling with batch size: %i", batchSize);
				}

				if (!hasLinked) {
					// Don't save to cache if it cannot be linked
					return shader;
				}
			}
		}

		shader->saveToCache(shaderName, Shaders::Version);
		return shader;
	}
	
	std::unique_ptr<Shader> ContentResolver::CompileShader(const char* shaderName, const char* vertex, const char* fragment, Shader::Introspection introspection)
	{
		std::unique_ptr shader = std::make_unique<Shader>();
		if (shader->loadFromCache(shaderName, Shaders::Version, introspection)) {
			return shader;
		}

		const AppConfiguration& appCfg = theApplication().appConfiguration();
		const IGfxCapabilities& gfxCaps = theServiceLocator().gfxCapabilities();
		// Clamping the value as some drivers report a maximum size similar to SSBO one
		const int32_t maxUniformBlockSize = std::clamp(gfxCaps.value(IGfxCapabilities::GLIntValues::MAX_UNIFORM_BLOCK_SIZE), 0, 64 * 1024);

		// If the UBO is smaller than 64kb and fixed batch size is disabled, batched shaders need to be compiled twice to determine safe `BATCH_SIZE` define value
		const bool compileTwice = (maxUniformBlockSize < 64 * 1024 && appCfg.fixedBatchSize <= 0 && introspection == Shader::Introspection::NoUniformsInBlocks);

		int32_t batchSize;
		if (appCfg.fixedBatchSize > 0 && introspection == Shader::Introspection::NoUniformsInBlocks) {
			batchSize = appCfg.fixedBatchSize;
		} else if (compileTwice) {
			// The first compilation of a batched shader needs a `BATCH_SIZE` defined as 1
			batchSize = 1;
		} else {
			batchSize = GLShaderProgram::DefaultBatchSize;
		}

		shader->loadFromMemory(shaderName, compileTwice ? Shader::Introspection::Enabled : introspection, vertex, fragment, batchSize);

		if (compileTwice) {
			GLShaderUniformBlocks blocks(shader->getHandle(), Material::InstancesBlockName, nullptr);
			GLUniformBlockCache* block = blocks.uniformBlock(Material::InstancesBlockName);
			ASSERT(block != nullptr);
			if (block != nullptr) {
				batchSize = maxUniformBlockSize / block->size();
				LOGI("Shader \"%s\" - block size: %d + %d align bytes, max batch size: %d", shaderName,
					block->size() - block->alignAmount(), block->alignAmount(), batchSize);

				bool hasLinked = false;
				while (batchSize > 0) {
					hasLinked = shader->loadFromMemory(shaderName, introspection, vertex, fragment, batchSize);
					if (hasLinked) {
						break;
					}

					batchSize--;
					LOGW("Failed to compile the shader, recompiling with batch size: %i", batchSize);
				}

				if (!hasLinked) {
					// Don't save to cache if it cannot be linked
					return shader;
				}
			}
		}

		shader->saveToCache(shaderName, Shaders::Version);
		return shader;
	}

	std::unique_ptr<Texture> ContentResolver::GetNoiseTexture()
	{
		uint32_t texels[64 * 64];

		for (uint32_t i = 0; i < countof(texels); i++) {
			texels[i] = Random().Fast(0, INT32_MAX) | 0xff000000;
		}

		std::unique_ptr<Texture> tex = std::make_unique<Texture>("Noise", Texture::Format::RGBA8, 64, 64);
		tex->loadFromTexels((unsigned char*)texels, 0, 0, 64, 64);
		tex->setMinFiltering(SamplerFilter::Linear);
		tex->setMagFiltering(SamplerFilter::Linear);
		tex->setWrap(SamplerWrapping::Repeat);
		return tex;
	}

	void ContentResolver::RecreateGemPalettes()
	{
		constexpr int32_t GemColorCount = 4;
		constexpr int32_t Expansion = 32;

		constexpr int32_t PaletteStops[] = {
			55, 52, 48, 15, 15,
			87, 84, 80, 15, 15,
			39, 36, 32, 15, 15,
			95, 92, 88, 15, 15
		};

		constexpr int32_t StopsPerGem = (countof(PaletteStops) / GemColorCount) - 1;

		// Start to fill palette texture from the second row (right after base palette)
		int32_t src = 0, dst = ColorsPerPalette;
		for (int32_t color = 0; color < GemColorCount; color++, src++) {
			// Compress 2 gem color gradients to single palette row
			for (int32_t i = 0; i < StopsPerGem; i++) {
				// Base Palette is in first row of "palettes" array
				uint32_t from = _palettes[PaletteStops[src++]];
				uint32_t to = _palettes[PaletteStops[src]];

				int32_t r = (from & 0xff) * 8, dr = ((to & 0xff) * 8) - r;
				int32_t g = ((from >> 8) & 0xff) * 8, dg = (((to >> 8) & 0xff) * 8) - g;
				int32_t b = ((from >> 16) & 0xff) * 8, db = (((to >> 16) & 0xff) * 8) - b;
				int32_t a = (from & 0xff000000);
				r *= Expansion; g *= Expansion; b *= Expansion;

				for (int32_t j = 0; j < Expansion; j++) {
					_palettes[dst] = ((r / (8 * Expansion)) & 0xff) | (((g / (8 * Expansion)) & 0xff) << 8) |
									 (((b / (8 * Expansion)) & 0xff) << 16) | a;
					r += dr; g += dg; b += db;
					dst++;
				}
			}
		}
	}

#if defined(DEATH_DEBUG)
	void ContentResolver::MigrateGraphics(const StringView& path)
	{
		String auraPath = fs::CombinePath({ GetContentPath(), "Animations"_s, path.exceptSuffix(4) + ".aura"_s });
		if (fs::FileExists(auraPath)) {
			return;
		}

		auto s = fs::Open(fs::CombinePath({ GetContentPath(), "Animations"_s, path + ".res"_s }), FileAccessMode::Read);
		auto fileSize = s->GetSize();
		if (fileSize < 4 || fileSize > 64 * 1024 * 1024) {
			// 64 MB file size limit, also if not found try to use cache
			return;
		}


		auto buffer = std::make_unique<char[]>(fileSize + simdjson::SIMDJSON_PADDING);
		s->Read(buffer.get(), fileSize);
		s->Close();
		buffer[fileSize] = '\0';

		ondemand::parser parser;
		ondemand::document doc;
		if (parser.iterate(buffer.get(), fileSize, fileSize + simdjson::SIMDJSON_PADDING).get(doc) == SUCCESS) {
			String fullPath = fs::CombinePath({ GetContentPath(), "Animations"_s, path });
			std::unique_ptr<ITextureLoader> texLoader = ITextureLoader::createFromFile(fullPath);
			if (texLoader->hasLoaded()) {
				auto texFormat = texLoader->texFormat().internalFormat();
				if (texFormat != GL_RGBA8 && texFormat != GL_RGB8) {
					return;
				}

				int32_t w = texLoader->width();
				int32_t h = texLoader->height();
				auto pixels = (uint32_t*)texLoader->pixels();
				const uint32_t* palette = _palettes;
				bool needsMask = true;

				uint64_t originalFlags;
				if (doc["Flags"].get(originalFlags) == SUCCESS) {
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
				if (doc["Duration"].get(animDuration) != SUCCESS) {
					animDuration = 0.0;
				}

				uint64_t frameCount;
				if (doc["FrameCount"].get(frameCount) != SUCCESS) {
					frameCount = 0;
				}

				Vector2i frameDimensions = GetVector2iFromJson(doc["FrameSize"]);
				Vector2i frameConfiguration = GetVector2iFromJson(doc["FrameConfiguration"]);

				Vector2i hotspot = GetVector2iFromJson(doc["Hotspot"]);
				Vector2i coldspot = GetVector2iFromJson(doc["Coldspot"], Vector2i(InvalidValue, InvalidValue));
				Vector2i gunspot = GetVector2iFromJson(doc["Gunspot"], Vector2i(InvalidValue, InvalidValue));

				// Write to .aura file
				auto so = fs::Open(auraPath, FileAccessMode::Write);
				ASSERT_MSG(so->IsValid(), "Cannot open file for writing");

				uint8_t flags = 0x80;
				if (palette == nullptr) {
					flags |= 0x01;
				}
				if (!needsMask) {
					flags |= 0x02;
				}

				so->WriteValue<uint64_t>(0xB8EF8498E2BFBBEF);
				so->WriteValue<uint32_t>(0x0002208F | (flags << 24)); // Version 2 is reserved for sprites (or bigger images)

				so->WriteValue<uint8_t>(4);
				so->WriteValue<uint32_t>((uint32_t)frameDimensions.X);
				so->WriteValue<uint32_t>((uint32_t)frameDimensions.Y);

				// Include Sprite extension
				so->WriteValue<uint8_t>((uint8_t)frameConfiguration.X);
				so->WriteValue<uint8_t>((uint8_t)frameConfiguration.Y);
				so->WriteValue<uint16_t>((uint16_t)frameCount);
				so->WriteValue<uint16_t>((uint16_t)(animDuration <= 0.0 ? 0 : 256 * animDuration));

				if (hotspot.X != InvalidValue || hotspot.Y != InvalidValue) {
					so->WriteValue<uint16_t>((uint16_t)hotspot.X);
					so->WriteValue<uint16_t>((uint16_t)hotspot.Y);
				} else {
					so->WriteValue<uint16_t>(UINT16_MAX);
					so->WriteValue<uint16_t>(UINT16_MAX);
				}
				if (coldspot.X != InvalidValue || coldspot.Y != InvalidValue) {
					so->WriteValue<uint16_t>((uint16_t)coldspot.X);
					so->WriteValue<uint16_t>((uint16_t)coldspot.Y);
				} else {
					so->WriteValue<uint16_t>(UINT16_MAX);
					so->WriteValue<uint16_t>(UINT16_MAX);
				}
				if (gunspot.X != InvalidValue || gunspot.Y != InvalidValue) {
					so->WriteValue<uint16_t>((uint16_t)gunspot.X);
					so->WriteValue<uint16_t>((uint16_t)gunspot.Y);
				} else {
					so->WriteValue<uint16_t>(UINT16_MAX);
					so->WriteValue<uint16_t>(UINT16_MAX);
				}

				Compatibility::JJ2Anims::WriteImageToFileInternal(so, texLoader->pixels(), w, h, 4);
			}
		}
	}
#endif
}