#include "ContentResolver.h"

#include "../nCine/IO/IFileStream.h"
#include "../nCine/Graphics/ITextureLoader.h"

#include "Tiles/TileSet.h"

#if defined(_WIN32)
#   if defined(__SSE4_2__) || defined(__AVX__)
#       define RAPIDJSON_SSE42
#   elif defined(__SSE2__)
#       define RAPIDJSON_SSE2
#   elif defined(__ARM_NEON)
#       define RAPIDJSON_NEON
#   endif
#endif
#include "../RapidJson/document.h"

using namespace rapidjson;

namespace Jazz2
{
	ContentResolver ContentResolver::_current;

	ContentResolver& ContentResolver::Current()
	{
		return _current;
	}

	ContentResolver::ContentResolver()
		:
		_isLoading(false)
	{
		memset(_palettes, 0, sizeof(_palettes));
	}

	ContentResolver::~ContentResolver()
	{
		// TODO
	}

	void ContentResolver::Initialize()
	{
		// TODO
	}

	void ContentResolver::Release()
	{
		_cachedMetadata.clear();
		_cachedGraphics.clear();
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

	void ContentResolver::PreloadMetadataAsync(const std::string& path)
	{
		// TODO: reimplement async preloading
		RequestMetadata(path);
	}

	Metadata* ContentResolver::RequestMetadata(const std::string& path)
	{
		// TODO: add locks?
		auto it = _cachedMetadata.find(path);
		if (it != _cachedMetadata.end()) {
			// Already loaded - Mark as referenced
			it->second->Flags |= MetadataFlags::Referenced;

			for (auto& resource : it->second->Graphics) {
				resource.second.Base->Flags |= GenericGraphicResourceFlags::Referenced;
			}

			return it->second.get();
		} else {
			// Try to load it
			// TODO
			auto fileHandle = IFileStream::createFileHandle(FileSystem::joinPath("Content/Metadata", path + ".res").c_str());
			fileHandle->Open(FileAccessMode::Read);
			auto fileSize = fileHandle->GetSize();
			if (fileSize < 4 || fileSize > 64 * 1024 * 1024) {
				// 64 MB file size limit
				return nullptr;
			}

			auto buffer = std::make_unique<char[]>(fileSize + 1);
			fileHandle->Read(buffer.get(), fileSize);
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
					for (rapidjson::Value::ConstMemberIterator it2 = animations.MemberBegin(); it2 != animations.MemberEnd(); ++it2) {
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

						GenericGraphicResource* graphicsBase = RequestGraphics(path);
						if (graphicsBase == nullptr) {
							continue;
						}

						GraphicResource graphics;
						graphics.Base = graphicsBase;
						graphics.FrameDuration = graphicsBase->FrameDuration;
						graphics.FrameCount = graphicsBase->FrameCount;
						graphics.LoopMode = AnimationLoopMode::Loop;

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

						const auto& frameOffsetItem = item.FindMember("FrameOffset");
						if (frameOffsetItem != item.MemberEnd() && frameOffsetItem->value.IsInt()) {
							graphics.FrameOffset = frameOffsetItem->value.GetInt();
						} else {
							graphics.FrameOffset = 0;
						}

						const auto& frameCountItem = item.FindMember("FrameCount");
						if (frameCountItem != item.MemberEnd() && frameCountItem->value.IsInt()) {
							graphics.FrameCount = frameCountItem->value.GetInt();
						} else {
							graphics.FrameCount -= graphics.FrameOffset;
						}

						const auto& frameRateItem = item.FindMember("FrameRate");
						if (frameRateItem != item.MemberEnd() && frameRateItem->value.IsInt()) {
							int frameRate = frameRateItem->value.GetInt();
							graphics.FrameDuration = (frameRate <= 0 ? -1.0f : (1.0f / frameRate) * 5.0f);
						}

						const auto& flagsItem = item.FindMember("Flags");
						if (flagsItem != item.MemberEnd() && flagsItem->value.IsInt()) {
							int flags = flagsItem->value.GetInt();
							if ((flags & 0x01) == 0x01) {
								graphics.LoopMode = AnimationLoopMode::Once;
							}
						}

						// If no bounding box is provided, use the first sprite
						if (metadata->BoundingBox == Vector2i(InvalidValue, InvalidValue)) {
							// TODO: Remove this bounding box reduction
							metadata->BoundingBox = graphicsBase->FrameDimensions - Vector2i(2, 2);
						}

						metadata->Graphics[key] = graphics;
					}
				}
			}

			Metadata* ptr = metadata.get();
			_cachedMetadata.emplace(path, std::move(metadata));
			return ptr;
		}
	}

	GenericGraphicResource* ContentResolver::RequestGraphics(const std::string& path)
	{
		// First resources are requested, reset _isLoading flag, because palette should be already applied
		_isLoading = false;

		auto it = _cachedGraphics.find(path);
		if (it != _cachedGraphics.end()) {
			// Already loaded - Mark as referenced
			it->second->Flags |= GenericGraphicResourceFlags::Referenced;
			return it->second.get();
		} else {
			// TODO
			auto fileHandle = IFileStream::createFileHandle(FileSystem::joinPath("Content/Animations", path + ".res").c_str());
			fileHandle->Open(FileAccessMode::Read);
			auto fileSize = fileHandle->GetSize();
			if (fileSize < 4 || fileSize > 64 * 1024 * 1024) {
				// 64 MB file size limit
				return nullptr;
			}

			auto buffer = std::make_unique<char[]>(fileSize + 1);
			fileHandle->Read(buffer.get(), fileSize);
			buffer[fileSize] = '\0';

			Document document;
			if (document.ParseInsitu(buffer.get()).HasParseError() || !document.IsObject()) {
				return nullptr;
			}

			// Try to load it
			std::unique_ptr<GenericGraphicResource> graphics = std::make_unique<GenericGraphicResource>();
			graphics->Flags |= GenericGraphicResourceFlags::Referenced;

			std::string fullPath = fs::joinPath("Content/Animations", path);
			std::unique_ptr<ITextureLoader> texLoader = ITextureLoader::createFromFile(fullPath.c_str());
			if (texLoader->hasLoaded()) {
				auto texFormat = texLoader->texFormat().internalFormat();
				if (texFormat != GL_RGBA8 && texFormat != GL_RGB8) {
					return nullptr;
				}

				int w = texLoader->width();
				int h = texLoader->height();
				auto pixels = (uint32_t*)texLoader->pixels();

				graphics->Mask = std::make_unique<uint8_t[]>(w * h);

				for (int i = 0; i < w * h; i++) {
					uint32_t color = _palettes[pixels[i] & 0xff];
					// Save original alpha value for collision checking
					graphics->Mask[i] = ((pixels[i] >> 24) & 0xff);
					pixels[i] = (color & 0xffffff) | ((((color >> 24) & 0xff) * ((pixels[i] >> 24) & 0xff) / 255) << 24);
				}

				graphics->TextureDiffuse = std::make_unique<Texture>(fullPath.c_str(), Texture::Format::RGBA8, w, h);
				graphics->TextureDiffuse->loadFromTexels((unsigned char*)pixels, 0, 0, w, h);
				graphics->TextureDiffuse->setMinFiltering(SamplerFilter::Nearest);
				graphics->TextureDiffuse->setMagFiltering(SamplerFilter::Nearest);

				const auto& frameDimensions = document["FrameSize"].GetArray();
				const auto& frameConfiguration = document["FrameConfiguration"].GetArray();
				const auto& frameRate = document["FrameRate"].GetInt();
				const auto& frameCount = document["FrameCount"].GetInt();

				graphics->FrameDimensions = Vector2i(frameDimensions[0].GetInt(), frameDimensions[1].GetInt());
				graphics->FrameConfiguration = Vector2i(frameConfiguration[0].GetInt(), frameConfiguration[1].GetInt());
				graphics->FrameDuration = (frameRate <= 0 ? -1.0f : (1.0f / frameRate) * 5.0f);
				graphics->FrameCount = frameCount;

				const auto& hotspotItem = document.FindMember("Hotspot");
				if (hotspotItem != document.MemberEnd() && hotspotItem->value.IsArray() && hotspotItem->value.Size() >= 2) {
					graphics->Hotspot = Vector2i(hotspotItem->value[0].GetInt(), hotspotItem->value[1].GetInt());
				} else {
					graphics->Hotspot = Vector2i();
				}

				const auto& coldspotItem = document.FindMember("Coldspot");
				if (coldspotItem != document.MemberEnd() && coldspotItem->value.IsArray() && hotspotItem->value.Size() >= 2) {
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

				GenericGraphicResource* ptr = graphics.get();
				_cachedGraphics.emplace(path, std::move(graphics));
				return ptr;
			}
		}

		return nullptr;
	}

	std::unique_ptr<Tiles::TileSet> ContentResolver::RequestTileSet(const std::string& path, bool applyPalette, Color* customPalette)
	{
		if (applyPalette) {
			// TODO
			/*if (customPalette != nullptr) {
				//ApplyPalette(customPalette);
			} else if (tilesetPackage.FileExists("Main.palette"))*/ {
				ApplyPalette(fs::joinPath("Content/Tilesets", path + "/Main.palette"));
			}
		}

		// Load diffuse texture
		std::unique_ptr<Texture> textureDiffuse = nullptr;
		{
			std::string diffusePath = fs::joinPath("Content/Tilesets", path + "/Diffuse.png");
			std::unique_ptr<ITextureLoader> texLoader = ITextureLoader::createFromFile(diffusePath.c_str());
			if (texLoader->hasLoaded()) {
				auto texFormat = texLoader->texFormat().internalFormat();
				if (texFormat == GL_RGBA8 || texFormat == GL_RGB8) {
					int w = texLoader->width();
					int h = texLoader->height();
					auto pixels = (uint32_t*)texLoader->pixels();

					for (int i = 0; i < w * h; i++) {
						uint32_t color = _palettes[pixels[i] & 0xff];
						pixels[i] = (color & 0xffffff) | ((((color >> 24) & 0xff) * ((pixels[i] >> 24) & 0xff) / 255) << 24);
					}

					textureDiffuse = std::make_unique<Texture>(diffusePath.c_str(), Texture::Format::RGBA8, w, h);
					textureDiffuse->loadFromTexels((unsigned char*)pixels, 0, 0, w, h);
					textureDiffuse->setMinFiltering(SamplerFilter::Nearest);
					textureDiffuse->setMagFiltering(SamplerFilter::Nearest);
				}
			}
		}

		if (textureDiffuse == nullptr) {
			return nullptr;
		}

		// TODO: Load normal texture

		// Load collision mask
		std::unique_ptr<uint8_t[]> mask = nullptr;
		{
			std::string maskPath = fs::joinPath("Content/Tilesets", path + "/Mask.png");
			std::unique_ptr<ITextureLoader> texLoader = ITextureLoader::createFromFile(maskPath.c_str());
			if (texLoader->hasLoaded()) {
				auto texFormat = texLoader->texFormat().internalFormat();
				if (texFormat != GL_RGBA8 && texFormat != GL_RGB8) {
					return nullptr;
				}

				int w = texLoader->width();
				int h = texLoader->height();
				auto pixels = (uint32_t*)texLoader->pixels();

				int tw = (w / Tiles::TileSet::DefaultTileSize);
				int th = (h / Tiles::TileSet::DefaultTileSize);

				mask = std::make_unique<uint8_t[]>(tw * th * (Tiles::TileSet::DefaultTileSize * Tiles::TileSet::DefaultTileSize));

				int k = 0;
				for (int i = 0; i < th; i++) {
					for (int j = 0; j < tw; j++) {
						//auto pixelOffset = &pixels[(i * Tiles::TileSet::DefaultTileSize * w) + (j * Tiles::TileSet::DefaultTileSize)];
						int pixelsBase = (i * Tiles::TileSet::DefaultTileSize * w) + (j * Tiles::TileSet::DefaultTileSize);
						auto maskOffset = &mask[k * Tiles::TileSet::DefaultTileSize * Tiles::TileSet::DefaultTileSize];
						for (int y = 0; y < Tiles::TileSet::DefaultTileSize; y++) {
							for (int x = 0; x < Tiles::TileSet::DefaultTileSize; x++) {
								bool isFilled = ((pixels[pixelsBase + (y * w) + x] >> 24) & 0xff) > 0;
								maskOffset[y * Tiles::TileSet::DefaultTileSize + x] = isFilled;
							}
						}
						k++;
					}
				}
			}
		}

		if (mask == nullptr) {
			return nullptr;
		}

		return std::make_unique<Tiles::TileSet>(std::move(textureDiffuse), std::move(mask));
	}

	void ContentResolver::ApplyPalette(const std::string& path)
	{
		std::unique_ptr<IFileStream> file = nullptr;
		if (!path.empty()) {
			file = IFileStream::createFileHandle(path.c_str());
			file->Open(FileAccessMode::Read);
		}
		if (file == nullptr || file->GetSize() == 0) {
			// Try to load default palette
			file = IFileStream::createFileHandle("Content/Animations/Main.palette");
			file->Open(FileAccessMode::Read);
		}

		auto fileSize = file->GetSize();
		if (fileSize < 5) {
			return;
		}

		uint16_t colorCount;
		file->Read(&colorCount, sizeof(colorCount));
		if (colorCount <= 0 || colorCount > ColorsPerPalette) {
			return;
		}

		uint32_t newPalette[ColorsPerPalette];
		file->Read(newPalette, colorCount * sizeof(uint32_t));

		if (memcmp(_palettes, newPalette, ColorsPerPalette * sizeof(uint32_t)) != 0) {
			// Palettes differs, drop all cached resources, so it will be reloaded with new palette
			if (_isLoading) {
				_cachedMetadata.clear();
				_cachedGraphics.clear();
			}

			memcpy(_palettes, newPalette, ColorsPerPalette * sizeof(uint32_t));
		}
	}
}