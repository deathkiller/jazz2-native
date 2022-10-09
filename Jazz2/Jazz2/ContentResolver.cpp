#include "ContentResolver.h"
#include "ContentResolver.Shaders.h"
#include "Compatibility/JJ2Anims.Palettes.h"
#include "LevelHandler.h"
#include "Tiles/TileSet.h"
#if _DEBUG
#	include "Compatibility/JJ2Anims.h"
#endif

#include "../nCine/IO/CompressionUtils.h"
#include "../nCine/IO/IFileStream.h"
#include "../nCine/IO/MemoryFile.h"
#include "../nCine/Graphics/ITextureLoader.h"
#include "../nCine/Base/Random.h"

#if defined(DEATH_TARGET_SSE42) || defined(DEATH_TARGET_AVX)
#	define RAPIDJSON_SSE42
#elif defined(DEATH_TARGET_SSE2)
#	define RAPIDJSON_SSE2
#elif defined(DEATH_TARGET_NEON)
#	define RAPIDJSON_NEON
#endif
#include "../RapidJson/document.h"

using namespace rapidjson;

namespace Jazz2
{
	ContentResolver& ContentResolver::Current()
	{
		static ContentResolver current;
		return current;
	}

	ContentResolver::ContentResolver()
		:
		_isLoading(false),
		_cachedMetadata(64),
		_cachedGraphics(128)
	{
		std::memset(_palettes, 0, sizeof(_palettes));

#if defined(DEATH_TARGET_UNIX)
#	if !defined(NCINE_LINUX_PACKAGE)
#		define NCINE_LINUX_PACKAGE "Jazz² Resurrection"
#	endif

		_contentPath = "/usr/share/" NCINE_LINUX_PACKAGE "/Content/";
		if (fs::IsDirectory(_contentPath)) {
			// Shared Content exists, try to use standard XDG paths
			auto localStorage = fs::GetLocalStorage();
			if (!localStorage.empty()) {
				_cachePath = fs::JoinPath(localStorage, "Jazz² Resurrection/Cache/"_s);
				_sourcePath = fs::JoinPath(localStorage, "Jazz² Resurrection/Source/"_s);
			} else {
				_cachePath = "Cache/"_s;
				_sourcePath = "Source/"_s;
			}
		} else {
			// Fallback to relative paths
			_contentPath = "Content/"_s;
			_cachePath = "Cache/"_s;
			_sourcePath = "Source/"_s;
		}
#endif

		CompileShaders();
	}

	ContentResolver::~ContentResolver()
	{
	}

	void ContentResolver::Release()
	{
		_cachedMetadata.clear();
		_cachedGraphics.clear();

		for (int i = 0; i < (int)FontType::Count; i++) {
			_fonts[i] = nullptr;
		}

		for (int i = 0; i < (int)PrecompiledShader::Count; i++) {
			_precompiledShaders[i] = nullptr;
		}
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
		auto it = _cachedMetadata.find(String::nullTerminatedView(path));
		if (it != _cachedMetadata.end()) {
			// Already loaded - Mark as referenced
			it->second->Flags |= MetadataFlags::Referenced;

			for (auto& resource : it->second->Graphics) {
				resource.second.Base->Flags |= GenericGraphicResourceFlags::Referenced;
			}

			return it->second.get();
		}

		// Try to load it
		auto s = fs::Open(fs::JoinPath({ GetContentPath(), "Metadata"_s, path + ".res"_s }), FileAccessMode::Read);
		auto fileSize = s->GetSize();
		if (fileSize < 4 || fileSize > 64 * 1024 * 1024) {
			// 64 MB file size limit
			return nullptr;
		}

		auto buffer = std::make_unique<char[]>(fileSize + 1);
		s->Read(buffer.get(), fileSize);
		buffer[fileSize] = '\0';

		std::unique_ptr<Metadata> metadata = std::make_unique<Metadata>();
		metadata->Flags |= MetadataFlags::Referenced;

		Document document;
		if (!document.ParseInsitu(buffer.get()).HasParseError() && document.IsObject()) {
			const auto& boundingBoxItem = document.FindMember("BoundingBox");
			if (boundingBoxItem != document.MemberEnd() && boundingBoxItem->value.IsArray() && boundingBoxItem->value.Size() >= 2) {
				metadata->BoundingBox = Vector2i(boundingBoxItem->value[0].GetInt(), boundingBoxItem->value[1].GetInt());
			} else {
				metadata->BoundingBox = Vector2i(InvalidValue, InvalidValue);
			}

			const auto& animations_ = document.FindMember("Animations");
			if (animations_ != document.MemberEnd() && animations_->value.IsObject()) {
				auto& animations = animations_->value;

				metadata->Graphics.reserve(animations.MemberCount());

				for (auto it2 = animations.MemberBegin(); it2 != animations.MemberEnd(); ++it2) {
					if (!it2->name.IsString() || !it2->value.IsObject()) {
						continue;
					}

					const auto& key = it2->name.GetString();
					const auto& item = it2->value;
					const auto& pathItem = item.FindMember("Path");
					if (key[0] == '\0' || pathItem == item.MemberEnd() || !pathItem->value.IsString()) {
						continue;
					}

					const auto& path = pathItem->value.GetString();
					if (path == nullptr || path[0] == '\0') {
						continue;
					}

					GraphicResource graphics;
					graphics.LoopMode = AnimationLoopMode::Loop;

					//bool keepIndexed = false;

					const auto& flagsItem = item.FindMember("Flags");
					if (flagsItem != item.MemberEnd() && flagsItem->value.IsInt()) {
						int flags = flagsItem->value.GetInt();
						if ((flags & 0x01) == 0x01) {
							graphics.LoopMode = AnimationLoopMode::Once;
						}
						//if ((flags & 0x02) == 0x02) {
						//	keepIndexed = true;
						//}
					}

					uint16_t paletteOffset = 0;
					// TODO: Implement true indexed sprites
					const auto& paletteOffsetItem = item.FindMember("PaletteOffset");
					if (paletteOffsetItem != item.MemberEnd() && paletteOffsetItem->value.IsInt()) {
						paletteOffset = (uint16_t)paletteOffsetItem->value.GetInt();
					}

					graphics.Base = RequestGraphics(path, paletteOffset);
					if (graphics.Base == nullptr) {
						continue;
					}

					const auto& frameOffsetItem = item.FindMember("FrameOffset");
					if (frameOffsetItem != item.MemberEnd() && frameOffsetItem->value.IsInt()) {
						graphics.FrameOffset = frameOffsetItem->value.GetInt();
					} else {
						graphics.FrameOffset = 0;
					}

					graphics.AnimDuration = graphics.Base->AnimDuration;
					graphics.FrameCount = graphics.Base->FrameCount;

					const auto& frameCountItem = item.FindMember("FrameCount");
					if (frameCountItem != item.MemberEnd() && frameCountItem->value.IsInt()) {
						graphics.FrameCount = frameCountItem->value.GetInt();
					} else {
						graphics.FrameCount -= graphics.FrameOffset;
					}

					// TODO: Use AnimDuration instead
					const auto& frameRateItem = item.FindMember("FrameRate");
					if (frameRateItem != item.MemberEnd() && frameRateItem->value.IsInt()) {
						int frameRate = frameRateItem->value.GetInt();
						graphics.AnimDuration = (frameRate <= 0 ? -1.0f : (1.0f / frameRate) * 5.0f);
					}

					const auto& statesItem = item.FindMember("States");
					if (statesItem != item.MemberEnd() && statesItem->value.IsArray()) {
						for (SizeType i = 0; i < statesItem->value.Size(); i++) {
							const auto& state = statesItem->value[i];
							if (!state.IsInt()) {
								continue;
							}

							graphics.State.push_back((AnimState)state.GetInt());
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

			const auto& sounds_ = document.FindMember("Sounds");
			if (sounds_ != document.MemberEnd() && sounds_->value.IsObject()) {
				auto& sounds = sounds_->value;

				metadata->Sounds.reserve(sounds.MemberCount());

				for (auto it2 = sounds.MemberBegin(); it2 != sounds.MemberEnd(); ++it2) {
					if (!it2->name.IsString() || !it2->value.IsObject()) {
						continue;
					}

					const auto& key = it2->name.GetString();
					const auto& item = it2->value;
					const auto& pathsItem = item.FindMember("Paths");
					if (key[0] == '\0' || pathsItem == item.MemberEnd() || !pathsItem->value.IsArray() || pathsItem->value.Empty()) {
						continue;
					}

					SoundResource sound;

					for (uint32_t i = 0; i < pathsItem->value.Size(); i++) {
						const auto& pathItem = pathsItem->value[i];
						const auto& path = pathItem.GetString();
						if (path[0] == '\0') {
							continue;
						}

						String fullPath = fs::JoinPath({ GetContentPath(), "Animations"_s, path });
						if (!fs::IsReadableFile(fullPath)) {
							fullPath = fs::JoinPath({ GetCachePath(), "Animations"_s, path });
							if (!fs::IsReadableFile(fullPath)) {
								continue;
							}
						}
						sound.Buffers.emplace_back(std::make_unique<AudioBuffer>(fullPath));
					}

					if (!sound.Buffers.empty()) {
						metadata->Sounds.emplace(key, std::move(sound));
					}
				}
			}
		}

		return _cachedMetadata.emplace(path, std::move(metadata)).first->second.get();
	}

	GenericGraphicResource* ContentResolver::RequestGraphics(const StringView& path, uint16_t paletteOffset)
	{
		// First resources are requested, reset _isLoading flag, because palette should be already applied
		_isLoading = false;

		auto it = _cachedGraphics.find(Pair(String::nullTerminatedView(path), paletteOffset));
		if (it != _cachedGraphics.end()) {
			// Already loaded - Mark as referenced
			it->second->Flags |= GenericGraphicResourceFlags::Referenced;
			return it->second.get();
		}

		if (fs::GetExtension(path) == "aura"_s) {
			return RequestGraphicsAura(path, paletteOffset);
		}

		auto s = fs::Open(fs::JoinPath({ GetContentPath(), "Animations"_s, path + ".res"_s }), FileAccessMode::Read);
		auto fileSize = s->GetSize();
		if (fileSize < 4 || fileSize > 64 * 1024 * 1024) {
			// 64 MB file size limit, also if not found try to use cache
			return nullptr;
		}

		auto buffer = std::make_unique<char[]>(fileSize + 1);
		s->Read(buffer.get(), fileSize);
		s->Close();
		buffer[fileSize] = '\0';

		Document document;
		if (document.ParseInsitu(buffer.get()).HasParseError() || !document.IsObject()) {
			return nullptr;
		}

		// Try to load it
		std::unique_ptr<GenericGraphicResource> graphics = std::make_unique<GenericGraphicResource>();
		graphics->Flags |= GenericGraphicResourceFlags::Referenced;

		String fullPath = fs::JoinPath({ GetContentPath(), "Animations"_s, path });
		std::unique_ptr<ITextureLoader> texLoader = ITextureLoader::createFromFile(fullPath);
		if (texLoader->hasLoaded()) {
			auto texFormat = texLoader->texFormat().internalFormat();
			if (texFormat != GL_RGBA8 && texFormat != GL_RGB8) {
				return nullptr;
			}

			int w = texLoader->width();
			int h = texLoader->height();
			auto pixels = (uint32_t*)texLoader->pixels();
			const uint32_t* palette = _palettes + paletteOffset;
			bool linearSampling = false;
			bool needsMask = true;

			const auto& flagsItem = document.FindMember("Flags");
			if (flagsItem != document.MemberEnd() && flagsItem->value.IsInt()) {
				int flags = flagsItem->value.GetInt();
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

				for (int i = 0; i < w * h; i++) {
					// Save original alpha value for collision checking
					graphics->Mask[i] = ((pixels[i] >> 24) & 0xff);
					if (palette != nullptr) {
						uint32_t color = palette[pixels[i] & 0xff];
						pixels[i] = (color & 0xffffff) | ((((color >> 24) & 0xff) * ((pixels[i] >> 24) & 0xff) / 255) << 24);
					}
				}
			} else if (palette != nullptr) {
				for (int i = 0; i < w * h; i++) {
					uint32_t color = palette[pixels[i] & 0xff];
					pixels[i] = (color & 0xffffff) | ((((color >> 24) & 0xff) * ((pixels[i] >> 24) & 0xff) / 255) << 24);
				}
			}

			graphics->TextureDiffuse = std::make_unique<Texture>(fullPath.data(), Texture::Format::RGBA8, w, h);
			graphics->TextureDiffuse->loadFromTexels((unsigned char*)pixels, 0, 0, w, h);
			graphics->TextureDiffuse->setMinFiltering(linearSampling ? SamplerFilter::Linear : SamplerFilter::Nearest);
			graphics->TextureDiffuse->setMagFiltering(linearSampling ? SamplerFilter::Linear : SamplerFilter::Nearest);

			const auto& frameDimensions = document["FrameSize"].GetArray();
			const auto& frameConfiguration = document["FrameConfiguration"].GetArray();
			const auto& frameCount = document["FrameCount"].GetInt();

			// TODO: Use FrameDuration instead
			const auto& durationItem = document.FindMember("Duration");
			if (durationItem != document.MemberEnd() && durationItem->value.IsNumber()) {
				graphics->AnimDuration = durationItem->value.GetFloat();
			} else {
				graphics->AnimDuration = 0.0f;
			}

			graphics->FrameDimensions = Vector2i(frameDimensions[0].GetInt(), frameDimensions[1].GetInt());
			graphics->FrameConfiguration = Vector2i(frameConfiguration[0].GetInt(), frameConfiguration[1].GetInt());
			graphics->FrameCount = frameCount;

			const auto& hotspotItem = document.FindMember("Hotspot");
			if (hotspotItem != document.MemberEnd() && hotspotItem->value.IsArray() && hotspotItem->value.Size() >= 2) {
				graphics->Hotspot = Vector2i(hotspotItem->value[0].GetInt(), hotspotItem->value[1].GetInt());
			} else {
				graphics->Hotspot = Vector2i();
			}

			const auto& coldspotItem = document.FindMember("Coldspot");
			if (coldspotItem != document.MemberEnd() && coldspotItem->value.IsArray() && coldspotItem->value.Size() >= 2) {
				graphics->Coldspot = Vector2i(coldspotItem->value[0].GetInt(), coldspotItem->value[1].GetInt());
			} else {
				graphics->Coldspot = Vector2i(InvalidValue, InvalidValue);
			}

			const auto& gunspotItem = document.FindMember("Gunspot");
			if (gunspotItem != document.MemberEnd() && gunspotItem->value.IsArray() && gunspotItem->value.Size() >= 2) {
				graphics->Gunspot = Vector2i(gunspotItem->value[0].GetInt(), gunspotItem->value[1].GetInt());
			} else {
				graphics->Gunspot = Vector2i(InvalidValue, InvalidValue);
			}

#if _DEBUG
			MigrateGraphics(path);
#endif

			return _cachedGraphics.emplace(Pair(String(path), paletteOffset), std::move(graphics)).first->second.get();
		}

		return nullptr;
	}

	GenericGraphicResource* ContentResolver::RequestGraphicsAura(const StringView& path, uint16_t paletteOffset)
	{
		// Try "Content" directory first, then "Cache" directory
		String fullPath = fs::JoinPath({ GetContentPath(), "Animations"_s, path });
		if (!fs::IsReadableFile(fullPath)) {
			fullPath = fs::JoinPath({ GetCachePath(), "Animations"_s, path });
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

		graphics->TextureDiffuse = std::make_unique<Texture>(fullPath.data(), Texture::Format::RGBA8, width, height);
		graphics->TextureDiffuse->loadFromTexels((unsigned char*)pixels.get(), 0, 0, width, height);
		graphics->TextureDiffuse->setMinFiltering(linearSampling ? SamplerFilter::Linear : SamplerFilter::Nearest);
		graphics->TextureDiffuse->setMagFiltering(linearSampling ? SamplerFilter::Linear : SamplerFilter::Nearest);

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

	void ContentResolver::ReadImageFromFile(std::unique_ptr<IFileStream>& s, uint8_t* data, int width, int height, int channelCount)
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
		int run = 0;
		int px_len = width * height * channelCount;

		px.rgba.r = 0;
		px.rgba.g = 0;
		px.rgba.b = 0;
		px.rgba.a = 255;

		for (int px_pos = 0; px_pos < px_len; px_pos += channelCount) {
			if (run > 0) {
				run--;
			} else {
				int b1 = s->ReadValue<uint8_t>();

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
					int b2 = s->ReadValue<uint8_t>();
					int vg = (b1 & 0x3f) - 32;
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

	std::unique_ptr<Tiles::TileSet> ContentResolver::RequestTileSet(const StringView& path, uint16_t captionTileId, bool applyPalette)
	{
		// Try "Content" directory first, then "Cache" directory
		String fullPath = fs::JoinPath({ GetContentPath(), "Tilesets"_s, path + ".j2t"_s });
		if (!fs::IsReadableFile(fullPath)) {
			fullPath = fs::JoinPath({ GetCachePath(), "Tilesets"_s, path + ".j2t"_s });
		}

		auto s = fs::Open(fullPath, FileAccessMode::Read);
		if (!s->IsOpened()) {
			return nullptr;
		}

		uint64_t signature1 = s->ReadValue<uint64_t>();
		uint16_t signature2 = s->ReadValue<uint16_t>();
		uint8_t version = s->ReadValue<uint8_t>();
		uint8_t flags = s->ReadValue<uint8_t>();
		ASSERT_MSG(signature1 == 0xB8EF8498E2BFBBEF && signature2 == 0x208F && version == 2, "Invalid file");

		// TODO: Use single channel instead
		uint8_t channelCount = s->ReadValue<uint8_t>();
		uint32_t width = s->ReadValue<uint32_t>();
		uint32_t height = s->ReadValue<uint32_t>();

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
		MemoryFile uc(uncompressedBuffer.get(), uncompressedSize);

		// Palette
		if (applyPalette) {
			uint32_t newPalette[ColorsPerPalette];
			uc.Read(newPalette, ColorsPerPalette * sizeof(uint32_t));

			if (std::memcmp(_palettes, newPalette, ColorsPerPalette * sizeof(uint32_t)) != 0) {
				// Palettes differs, drop all cached resources, so it will be reloaded with new palette
				if (_isLoading) {
					_cachedMetadata.clear();
					_cachedGraphics.clear();

					for (int i = 0; i < (int)FontType::Count; i++) {
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
			for (int k = 0; k < 8; k++) {
				int pixelIdx = 8 * j + k;
				mask[pixelIdx] = (((idx >> k) & 0x01) != 0);
			}
		}

		// Image
		std::unique_ptr<uint32_t[]> pixels = std::make_unique<uint32_t[]>(width * height);
		ReadImageFromFile(s, (uint8_t*)pixels.get(), width, height, channelCount);

		for (uint32_t i = 0; i < width * height; i++) {
			uint32_t color = _palettes[pixels[i] & 0xff];
			pixels[i] = (color & 0xffffff) | ((((color >> 24) & 0xff) * ((pixels[i] >> 24) & 0xff) / 255) << 24);
		}

		std::unique_ptr<Texture> textureDiffuse = std::make_unique<Texture>(fullPath.data(), Texture::Format::RGBA8, width, height);
		textureDiffuse->loadFromTexels((unsigned char*)pixels.get(), 0, 0, width, height);
		textureDiffuse->setMinFiltering(SamplerFilter::Nearest);
		textureDiffuse->setMagFiltering(SamplerFilter::Nearest);

		// Caption Tile
		std::unique_ptr<Color[]> captionTile;
		if (captionTileId > 0) {
			int tw = (width / TileSet::DefaultTileSize);
			int tx = (captionTileId % tw) * TileSet::DefaultTileSize;
			int ty = (captionTileId / tw) * TileSet::DefaultTileSize;
			captionTile = std::make_unique<Color[]>(TileSet::DefaultTileSize * TileSet::DefaultTileSize / 3);
			for (int y = 0; y < TileSet::DefaultTileSize / 3; y++) {
				for (int x = 0; x < TileSet::DefaultTileSize; x++) {
					Color c1 = Color(pixels[((ty + y * 3) * width) + tx + x]);
					Color c2 = Color(pixels[((ty + y * 3 + 1) * width) + tx + x]);
					Color c3 = Color(pixels[((ty + y * 3 + 2) * width) + tx + x]);
					captionTile[y * TileSet::DefaultTileSize + x] = Color((c1.B() + c2.B() + c3.B()) / 3, (c1.G() + c2.G() + c3.G()) / 3, (c1.R() + c2.R() + c3.R()) / 3);
				}
			}
		} else {
			captionTile = nullptr;
		}

		return std::make_unique<Tiles::TileSet>(std::move(textureDiffuse), std::move(mask), std::move(captionTile));
	}

	bool ContentResolver::LoadLevel(LevelHandler* levelHandler, const StringView& path, GameDifficulty difficulty)
	{
		// Try "Content" directory first, then "Cache" directory
		String fullPath = fs::JoinPath({ GetContentPath(), "Episodes"_s, path + ".j2l"_s });
		if (!fs::IsReadableFile(fullPath)) {
			fullPath = fs::JoinPath({ GetCachePath(), "Episodes"_s, path + ".j2l"_s });
		}

		auto s = fs::Open(fullPath, FileAccessMode::Read);
		RETURNF_ASSERT_MSG(s->IsOpened(), "Cannot open file for reading");

		uint64_t signature = s->ReadValue<uint64_t>();
		uint8_t fileType = s->ReadValue<uint8_t>();
		RETURNF_ASSERT_MSG(signature == 0x2095A59FF0BFBBEF && fileType == LevelFile, "File has invalid signature");

		// TODO: Level flags
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
		MemoryFile uc(uncompressedBuffer.get(), uncompressedSize);

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

		uint16_t captionTileId = uc.ReadValue<uint16_t>();

		// Text Event Strings
		uint8_t textEventStringsCount = uc.ReadValue<uint8_t>();
		SmallVector<String, 0> levelTexts;
		levelTexts.reserve(textEventStringsCount);
		for (int i = 0; i < textEventStringsCount; i++) {
			uint16_t textLength = uc.ReadValue<uint16_t>();
			String& text = levelTexts.emplace_back(NoInit, textLength);
			uc.Read(text.data(), textLength);
		}

		std::unique_ptr<Tiles::TileMap> tileMap = std::make_unique<Tiles::TileMap>(levelHandler, defaultTileset, captionTileId);

		// Animated Tiles
		tileMap->ReadAnimatedTiles(uc);

		// Layers
		uint8_t layerCount = uc.ReadValue<uint8_t>();
		for (int i = 0; i < layerCount; i++) {
			tileMap->ReadLayerConfiguration(uc);
		}

		// Events
		std::unique_ptr<Events::EventMap> eventMap = std::make_unique<Events::EventMap>(levelHandler, tileMap->Size());
		eventMap->ReadEvents(uc, tileMap, difficulty);

		// TODO: Bonus level
		levelHandler->OnLevelLoaded(fullPath, name, nextLevel, secretLevel, tileMap, eventMap, defaultMusic, ambientColor,
			defaultWeatherType, defaultWeatherIntensity, levelTexts);

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

				for (int i = 0; i < (int)FontType::Count; i++) {
					_fonts[i] = nullptr;
				}
			}

			std::memcpy(_palettes, SpritePalette, ColorsPerPalette * sizeof(uint32_t));
			RecreateGemPalettes();
		}
	}

	std::optional<Episode> ContentResolver::GetEpisode(const StringView& name)
	{
		String fullPath = fs::JoinPath({ GetContentPath(), "Episodes"_s, name + ".j2e"_s });
		if (!fs::IsReadableFile(fullPath)) {
			fullPath = fs::JoinPath({ GetCachePath(), "Episodes"_s, name + ".j2e"_s });
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

		uint16_t flags = s->ReadValue<uint16_t>();

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
		String fullPath = fs::JoinPath({ GetContentPath(), "Music"_s, path });
		if (!fs::IsReadableFile(fullPath)) {
			// "Source" directory must be case in-sensitive
			fullPath = fs::FindPathCaseInsensitive(fs::JoinPath(GetSourcePath(), path));
		}
		if (fs::IsReadableFile(fullPath)) {
			return std::make_unique<AudioStreamPlayer>(fullPath);
		} else {
			return nullptr;
		}
	}

	UI::Font* ContentResolver::GetFont(FontType fontType)
	{
		if (fontType >= FontType::Count) {
			return nullptr;
		}

		auto& font = _fonts[(int)fontType];
		if (font == nullptr) {
			switch (fontType) {
				case FontType::Small: font = std::make_unique<UI::Font>(fs::JoinPath({ GetContentPath(), "Animations"_s, "UI"_s, "font_small.png"_s }), _palettes); break;
				case FontType::Medium: font = std::make_unique<UI::Font>(fs::JoinPath({ GetContentPath(), "Animations"_s, "UI"_s, "font_medium.png"_s }), _palettes); break;
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

		return _precompiledShaders[(int)shader].get();
	}

	void ContentResolver::CompileShaders()
	{
		_precompiledShaders[(int)PrecompiledShader::Lighting] = std::make_unique<Shader>("Lighting",
			Shader::LoadMode::STRING, Shaders::LightingVs, Shaders::LightingFs);
		_precompiledShaders[(int)PrecompiledShader::BatchedLighting] = std::make_unique<Shader>("BatchedLighting",
			Shader::LoadMode::STRING, Shader::Introspection::NO_UNIFORMS_IN_BLOCKS, Shaders::BatchedLightingVs, Shaders::LightingFs);
		_precompiledShaders[(int)PrecompiledShader::Lighting]->registerBatchedShader(*_precompiledShaders[(int)PrecompiledShader::BatchedLighting]);

		_precompiledShaders[(int)PrecompiledShader::Blur] = std::make_unique<Shader>("Blur",
			Shader::LoadMode::STRING, Shader::DefaultVertex::SPRITE, Shaders::BlurFs);
		_precompiledShaders[(int)PrecompiledShader::Downsample] = std::make_unique<Shader>("Downsample",
			Shader::LoadMode::STRING, Shader::DefaultVertex::SPRITE, Shaders::DownsampleFs);
		_precompiledShaders[(int)PrecompiledShader::Combine] = std::make_unique<Shader>("Combine",
			Shader::LoadMode::STRING, Shaders::CombineVs, Shaders::CombineFs);
		_precompiledShaders[(int)PrecompiledShader::CombineWithWater] = std::make_unique<Shader>("CombineWithWater",
			Shader::LoadMode::STRING, Shaders::CombineVs, Shaders::CombineWithWaterFs);

		_precompiledShaders[(int)PrecompiledShader::TexturedBackground] = std::make_unique<Shader>("TexturedBackground",
			Shader::LoadMode::STRING, Shader::DefaultVertex::SPRITE, Shaders::TexturedBackgroundFs);
		_precompiledShaders[(int)PrecompiledShader::TexturedBackgroundCircle] = std::make_unique<Shader>("TexturedBackground",
			Shader::LoadMode::STRING, Shader::DefaultVertex::SPRITE, Shaders::TexturedBackgroundCircleFs);

		_precompiledShaders[(int)PrecompiledShader::Colorize] = std::make_unique<Shader>("Colorize",
			Shader::LoadMode::STRING, Shader::DefaultVertex::SPRITE, Shaders::ColorizeFs);
		_precompiledShaders[(int)PrecompiledShader::BatchedColorize] = std::make_unique<Shader>("BatchedColorize",
			Shader::LoadMode::STRING, Shader::Introspection::NO_UNIFORMS_IN_BLOCKS, Shader::DefaultVertex::BATCHED_SPRITES, Shaders::ColorizeFs);
		_precompiledShaders[(int)PrecompiledShader::Colorize]->registerBatchedShader(*_precompiledShaders[(int)PrecompiledShader::BatchedColorize]);

		_precompiledShaders[(int)PrecompiledShader::Outline] = std::make_unique<Shader>("Outline",
			Shader::LoadMode::STRING, Shader::DefaultVertex::SPRITE, Shaders::OutlineFs);
		_precompiledShaders[(int)PrecompiledShader::BatchedOutline] = std::make_unique<Shader>("BatchedOutline",
			Shader::LoadMode::STRING, Shader::Introspection::NO_UNIFORMS_IN_BLOCKS, Shader::DefaultVertex::BATCHED_SPRITES, Shaders::OutlineFs);
		_precompiledShaders[(int)PrecompiledShader::Outline]->registerBatchedShader(*_precompiledShaders[(int)PrecompiledShader::BatchedOutline]);

		_precompiledShaders[(int)PrecompiledShader::WhiteMask] = std::make_unique<Shader>("WhiteMask",
			Shader::LoadMode::STRING, Shader::DefaultVertex::SPRITE, Shaders::WhiteMaskFs);
		_precompiledShaders[(int)PrecompiledShader::PartialWhiteMask] = std::make_unique<Shader>("PartialWhiteMask",
			Shader::LoadMode::STRING, Shader::DefaultVertex::SPRITE, Shaders::PartialWhiteMaskFs);
		_precompiledShaders[(int)PrecompiledShader::BatchedWhiteMask] = std::make_unique<Shader>("BatchedWhiteMask",
			Shader::LoadMode::STRING, Shader::Introspection::NO_UNIFORMS_IN_BLOCKS, Shader::DefaultVertex::BATCHED_SPRITES, Shaders::WhiteMaskFs);
		_precompiledShaders[(int)PrecompiledShader::WhiteMask]->registerBatchedShader(*_precompiledShaders[(int)PrecompiledShader::BatchedWhiteMask]);
		_precompiledShaders[(int)PrecompiledShader::PartialWhiteMask]->registerBatchedShader(*_precompiledShaders[(int)PrecompiledShader::BatchedWhiteMask]);

#if defined(ALLOW_RESCALE_SHADERS)
		_precompiledShaders[(int)PrecompiledShader::ResizeHQ2x] = std::make_unique<Shader>("ResizeHQ2x",
			Shader::LoadMode::STRING, Shaders::ResizeHQ2xVs, Shaders::ResizeHQ2xFs);
		_precompiledShaders[(int)PrecompiledShader::Resize3xBrz] = std::make_unique<Shader>("Resize3xBrz",
			Shader::LoadMode::STRING, Shaders::Resize3xBrzVs, Shaders::Resize3xBrzFs);
		_precompiledShaders[(int)PrecompiledShader::ResizeCrt] = std::make_unique<Shader>("ResizeCrt",
			Shader::LoadMode::STRING, Shaders::ResizeCrtVs, Shaders::ResizeCrtFs);
		_precompiledShaders[(int)PrecompiledShader::ResizeMonochrome] = std::make_unique<Shader>("ResizeMonochrome",
			Shader::LoadMode::STRING, Shader::DefaultVertex::SPRITE, Shaders::ResizeMonochromeFs);
#endif

		_precompiledShaders[(int)PrecompiledShader::Transition] = std::make_unique<Shader>("Transition",
			Shader::LoadMode::STRING, Shaders::TransitionVs, Shaders::TransitionFs);
	}

	std::unique_ptr<Texture> ContentResolver::GetNoiseTexture()
	{
		uint32_t texels[64 * 64];

		for (int i = 0; i < _countof(texels); i++) {
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
		constexpr int GemColorCount = 4;
		constexpr int Expansion = 32;

		constexpr int PaletteStops[] = {
			55, 52, 48, 15, 15,
			87, 84, 80, 15, 15,
			39, 36, 32, 15, 15,
			95, 92, 88, 15, 15
		};

		constexpr int StopsPerGem = (_countof(PaletteStops) / GemColorCount) - 1;

		// Start to fill palette texture from the second row (right after base palette)
		int src = 0, dst = ColorsPerPalette;
		for (int color = 0; color < GemColorCount; color++, src++) {
			// Compress 2 gem color gradients to single palette row
			for (int i = 0; i < StopsPerGem; i++) {
				// Base Palette is in first row of "palettes" array
				uint32_t from = _palettes[PaletteStops[src++]];
				uint32_t to = _palettes[PaletteStops[src]];

				int r = (from & 0xff) * 8, dr = ((to & 0xff) * 8) - r;
				int g = ((from >> 8) & 0xff) * 8, dg = (((to >> 8) & 0xff) * 8) - g;
				int b = ((from >> 16) & 0xff) * 8, db = (((to >> 16) & 0xff) * 8) - b;
				int a = (from & 0xff000000);
				r *= Expansion; g *= Expansion; b *= Expansion;

				for (int j = 0; j < Expansion; j++) {
					_palettes[dst] = ((r / (8 * Expansion)) & 0xff) | (((g / (8 * Expansion)) & 0xff) << 8) |
									 (((b / (8 * Expansion)) & 0xff) << 16) | a;
					r += dr; g += dg; b += db;
					dst++;
				}
			}
		}
	}

#if _DEBUG
	void ContentResolver::MigrateGraphics(const StringView& path)
	{
		String auraPath = fs::JoinPath({ GetContentPath(), "Animations"_s, path.exceptSuffix(4) + ".aura"_s });
		if (fs::IsFile(auraPath)) {
			return;
		}

		auto s = fs::Open(fs::JoinPath({ GetContentPath(), "Animations"_s, path + ".res"_s }), FileAccessMode::Read);
		auto fileSize = s->GetSize();
		if (fileSize < 4 || fileSize > 64 * 1024 * 1024) {
			// 64 MB file size limit, also if not found try to use cache
			return;
		}

		auto buffer = std::make_unique<char[]>(fileSize + 1);
		s->Read(buffer.get(), fileSize);
		s->Close();
		buffer[fileSize] = '\0';

		Document document;
		if (document.ParseInsitu(buffer.get()).HasParseError() || !document.IsObject()) {
			return;
		}

		String fullPath = fs::JoinPath({ GetContentPath(), "Animations"_s, path });
		std::unique_ptr<ITextureLoader> texLoader = ITextureLoader::createFromFile(fullPath);
		if (texLoader->hasLoaded()) {
			auto texFormat = texLoader->texFormat().internalFormat();
			if (texFormat != GL_RGBA8 && texFormat != GL_RGB8) {
				return;
			}

			int w = texLoader->width();
			int h = texLoader->height();
			auto pixels = (uint32_t*)texLoader->pixels();
			const uint32_t* palette = _palettes;
			bool needsMask = true;

			const auto& flagsItem = document.FindMember("Flags");
			if (flagsItem != document.MemberEnd() && flagsItem->value.IsInt()) {
				int flags = flagsItem->value.GetInt();
				// Palette already applied, keep as is
				if ((flags & 0x01) != 0x01) {
					palette = nullptr;
				}
				if ((flags & 0x08) == 0x08) {
					needsMask = false;
				}
			}

			const auto& frameDimensions = document["FrameSize"].GetArray();
			const auto& frameConfiguration = document["FrameConfiguration"].GetArray();
			const auto& frameCount = document["FrameCount"].GetInt();

			float animDuration;
			const auto& durationItem = document.FindMember("Duration");
			if (durationItem != document.MemberEnd() && durationItem->value.IsNumber()) {
				animDuration = durationItem->value.GetFloat();
			} else {
				animDuration = 0.0f;
			}

			Vector2i hotspot, coldspot, gunspot;
			const auto& hotspotItem = document.FindMember("Hotspot");
			if (hotspotItem != document.MemberEnd() && hotspotItem->value.IsArray() && hotspotItem->value.Size() >= 2) {
				hotspot = Vector2i(hotspotItem->value[0].GetInt(), hotspotItem->value[1].GetInt());
			} else {
				hotspot = Vector2i();
			}

			const auto& coldspotItem = document.FindMember("Coldspot");
			if (coldspotItem != document.MemberEnd() && coldspotItem->value.IsArray() && coldspotItem->value.Size() >= 2) {
				coldspot = Vector2i(coldspotItem->value[0].GetInt(), coldspotItem->value[1].GetInt());
			} else {
				coldspot = Vector2i(InvalidValue, InvalidValue);
			}

			const auto& gunspotItem = document.FindMember("Gunspot");
			if (gunspotItem != document.MemberEnd() && gunspotItem->value.IsArray() && gunspotItem->value.Size() >= 2) {
				gunspot = Vector2i(gunspotItem->value[0].GetInt(), gunspotItem->value[1].GetInt());
			} else {
				gunspot = Vector2i(InvalidValue, InvalidValue);
			}

			// Write to .aura file
			auto so = fs::Open(auraPath, FileAccessMode::Write);
			ASSERT_MSG(so->IsOpened(), "Cannot open file for writing");

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
			so->WriteValue<uint32_t>((uint32_t)frameDimensions[0].GetInt());
			so->WriteValue<uint32_t>((uint32_t)frameDimensions[1].GetInt());

			// Include Sprite extension
			so->WriteValue<uint8_t>((uint8_t)frameConfiguration[0].GetInt());
			so->WriteValue<uint8_t>((uint8_t)frameConfiguration[1].GetInt());
			so->WriteValue<uint16_t>(frameCount);
			so->WriteValue<uint16_t>((uint16_t)(animDuration <= 0.0f ? 0 : 256 * animDuration));

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
#endif
}