#include "ContentResolver.h"

#include "../nCine/IO/IFileStream.h"
#include "../nCine/Graphics/ITextureLoader.h"

#include "LevelHandler.h"
#include "Tiles/TileSet.h"

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
	ContentResolver ContentResolver::_current;

	ContentResolver& ContentResolver::Current()
	{
		return _current;
	}

	ContentResolver::ContentResolver()
		:
		_isLoading(false),
		_cachedMetadata(64),
		_cachedGraphics(128)
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
		auto fileHandle = IFileStream::createFileHandle(fs::joinPath({ "Content"_s, "Metadata"_s, path + ".res"_s }));
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

					graphics.FrameDuration = graphics.Base->FrameDuration;
					graphics.FrameCount = graphics.Base->FrameCount;

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

					for (int i = 0; i < pathsItem->value.Size(); i++) {
						const auto& pathItem = pathsItem->value[i];
						const auto& path = pathItem.GetString();
						if (path[0] == '\0') {
							continue;
						}

						sound.Buffers.emplace_back(std::make_unique<AudioBuffer>(fs::joinPath({ "Content"_s, "Animations"_s, path })));
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

		auto fileHandle = IFileStream::createFileHandle(fs::joinPath({ "Content"_s, "Animations"_s, path + ".res"_s }));
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

		String fullPath = fs::joinPath({ "Content"_s, "Animations"_s, path });
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

			const auto& frameRateItem = document.FindMember("FrameRate");
			if (frameRateItem != document.MemberEnd() && frameRateItem->value.IsNumber()) {
				const auto& frameRate = frameRateItem->value.GetFloat();
				graphics->FrameDuration = (frameRate <= 0 ? -1.0f : (1.0f / frameRate) * 5.0f);
			} else {
				graphics->FrameDuration = -1.0f;
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

			return _cachedGraphics.emplace(Pair(String(path), paletteOffset), std::move(graphics)).first->second.get();
		}

		return nullptr;
	}

	std::unique_ptr<Tiles::TileSet> ContentResolver::RequestTileSet(const StringView& path, bool applyPalette, Color* customPalette)
	{
		if (applyPalette) {
			// TODO
			/*if (customPalette != nullptr) {
				//ApplyPalette(customPalette);
			} else if (tilesetPackage.FileExists("Main.palette"))*/ {
				ApplyPalette(fs::joinPath({ "Content"_s, "Tilesets"_s, path, "Main.palette"_s }));
			}
		}

		// Load diffuse texture
		std::unique_ptr<Texture> textureDiffuse = nullptr;
		{
			String diffusePath = fs::joinPath({ "Content"_s, "Tilesets"_s, path, "Diffuse.png"_s });
			std::unique_ptr<ITextureLoader> texLoader = ITextureLoader::createFromFile(diffusePath);
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

					textureDiffuse = std::make_unique<Texture>(diffusePath.data(), Texture::Format::RGBA8, w, h);
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
			String maskPath = fs::joinPath({ "Content"_s, "Tilesets"_s, path, "Mask.png"_s });
			std::unique_ptr<ITextureLoader> texLoader = ITextureLoader::createFromFile(maskPath);
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

	bool ContentResolver::LoadLevel(LevelHandler* levelHandler, const StringView& path, GameDifficulty difficulty)
	{
		String levelRoot = fs::joinPath({ "Content"_s, "Episodes"_s, path });

		// Try to load level description
		auto fileHandle = IFileStream::createFileHandle(fs::joinPath({ levelRoot, ".res"_s }));
		fileHandle->Open(FileAccessMode::Read);
		auto fileSize = fileHandle->GetSize();
		if (fileSize < 4 || fileSize > 64 * 1024 * 1024) {
			// 64 MB file size limit
			return false;
		}

		auto buffer = std::make_unique<char[]>(fileSize + 1);
		fileHandle->Read(buffer.get(), fileSize);
		buffer[fileSize] = '\0';

		Document document;
		if (document.ParseInsitu(buffer.get()).HasParseError() || !document.IsObject()) {
			return false;
		}

		// These 3 sections are required
		const auto& versionItem = document.FindMember("Version");
		const auto& descriptionItem = document.FindMember("Description");
		const auto& layersItem = document.FindMember("Layers");
		if (versionItem == document.MemberEnd() || !versionItem->value.IsObject() ||
			descriptionItem == document.MemberEnd() || !descriptionItem->value.IsObject() ||
			layersItem == document.MemberEnd() || !layersItem->value.IsObject()) {
			return false;
		}

		const auto& layerFormatItem = versionItem->value.FindMember("LayerFormat");
		const auto& eventSetItem = versionItem->value.FindMember("EventSet");

		const auto& nameItem = descriptionItem->value.FindMember("Name");
		const auto& nextLevelItem = descriptionItem->value.FindMember("NextLevel");
		const auto& secretLevelItem = descriptionItem->value.FindMember("SecretLevel");
		const auto& defaultTilesetItem = descriptionItem->value.FindMember("DefaultTileset");
		const auto& defaultMusicItem = descriptionItem->value.FindMember("DefaultMusic");
		const auto& defaultLightItem = descriptionItem->value.FindMember("DefaultLight");

		if (layerFormatItem == versionItem->value.MemberEnd() || !layerFormatItem->value.IsNumber() ||
			eventSetItem == versionItem->value.MemberEnd() || !eventSetItem->value.IsNumber() ||
			defaultTilesetItem == descriptionItem->value.MemberEnd() || !defaultTilesetItem->value.IsString()) {
			return false;
		}

		std::unique_ptr<Tiles::TileMap> tileMap = std::make_unique<Tiles::TileMap>(levelHandler, defaultTilesetItem->value.GetString());

		// Sprite layer is mandatory
		{
			auto layerFile = IFileStream::createFileHandle(fs::joinPath({ levelRoot, "Sprite.layer"_s }));
			tileMap->ReadLayerConfiguration(LayerType::Sprite, layerFile, { .SpeedX = 1, .SpeedY = 1, .Depth = -50 });
		}

		// Load all layers
		auto it = layersItem->value.MemberBegin();
		while (it != layersItem->value.MemberEnd()) {
			const auto& layerName = it->name.GetString();
			const auto& speedXItem = it->value.FindMember("XSpeed");
			const auto& speedYItem = it->value.FindMember("YSpeed");
			const auto& autoSpeedXItem = it->value.FindMember("XAutoSpeed");
			const auto& autoSpeedYItem = it->value.FindMember("YAutoSpeed");
			const auto& repeatXItem = it->value.FindMember("XRepeat");
			const auto& repeatYItem = it->value.FindMember("YRepeat");
			const auto& offsetXItem = it->value.FindMember("XOffset");
			const auto& offsetYItem = it->value.FindMember("YOffset");
			const auto& depthItem = it->value.FindMember("Depth");
			const auto& inherentOffsetItem = it->value.FindMember("InherentOffset");
			const auto& backgroundStyleItem = it->value.FindMember("BackgroundStyle");
			const auto& backgroundColorItem = it->value.FindMember("BackgroundColor");
			const auto& parallaxStarsEnabledItem = it->value.FindMember("ParallaxStarsEnabled");

			LayerType type;
			if (strcmp(layerName, "Sprite") == 0) {
				type = LayerType::Sprite;
			} else if (strcmp(layerName, "Sky") == 0) {
				type = LayerType::Sky;
			} else {
				type = LayerType::Other;
			}

			BackgroundStyle backgroundStyle = (backgroundStyleItem != it->value.MemberEnd() && backgroundStyleItem->value.IsInt() ? (BackgroundStyle)backgroundStyleItem->value.GetInt() : BackgroundStyle::Plain);
			Vector3f backgroundColor;
			bool parallaxStarsEnabled = false;
			if (backgroundStyle != BackgroundStyle::Plain) {
				if (backgroundColorItem != it->value.MemberEnd() && backgroundColorItem->value.IsArray() && backgroundColorItem->value.Size() >= 3) {
					const auto& color = backgroundColorItem->value.GetArray();
					backgroundColor = Vector3f(
						color[0].GetInt() / 255.0f,
						color[1].GetInt() / 255.0f,
						color[2].GetInt() / 255.0f
					);
				}
				parallaxStarsEnabled = (parallaxStarsEnabledItem != it->value.MemberEnd() && parallaxStarsEnabledItem->value.IsBool() && parallaxStarsEnabledItem->value.GetBool());
			}

			auto layerFile = IFileStream::createFileHandle(fs::joinPath({ levelRoot, StringView(layerName) + ".layer"_s }));
			tileMap->ReadLayerConfiguration(type, layerFile, {
				.SpeedX = (speedXItem != it->value.MemberEnd() && speedXItem->value.IsNumber() ? speedXItem->value.GetFloat() : 0.0f),
				.SpeedY = (speedYItem != it->value.MemberEnd() && speedYItem->value.IsNumber() ? speedYItem->value.GetFloat() : 0.0f),
				.AutoSpeedX = (autoSpeedXItem != it->value.MemberEnd() && autoSpeedXItem->value.IsNumber() ? autoSpeedXItem->value.GetFloat() : 0.0f),
				.AutoSpeedY = (autoSpeedYItem != it->value.MemberEnd() && autoSpeedYItem->value.IsNumber() ? autoSpeedYItem->value.GetFloat() : 0.0f),
				.RepeatX = (repeatXItem != it->value.MemberEnd() && repeatXItem->value.IsBool() && repeatXItem->value.GetBool()),
				.RepeatY = (repeatYItem != it->value.MemberEnd() && repeatYItem->value.IsBool() && repeatYItem->value.GetBool()),
				.OffsetX = (offsetXItem != it->value.MemberEnd() && offsetXItem->value.IsNumber() ? offsetXItem->value.GetFloat() : 0.0f),
				.OffsetY = (offsetYItem != it->value.MemberEnd() && offsetYItem->value.IsNumber() ? offsetYItem->value.GetFloat() : 0.0f),
				// TODO: Depth is negative
				.Depth = (depthItem != it->value.MemberEnd() && depthItem->value.IsNumber() ? -depthItem->value.GetInt() : 0),
				.UseInherentOffset = (inherentOffsetItem != it->value.MemberEnd() && inherentOffsetItem->value.IsBool() && inherentOffsetItem->value.GetBool()),
				.BackgroundStyle = backgroundStyle,
				.BackgroundColor = backgroundColor,
				.ParallaxStarsEnabled = parallaxStarsEnabled
			});

			++it;
		}

		// Load animated tiles
		auto animTilesFile = IFileStream::createFileHandle(fs::joinPath({ levelRoot, "Animated.tiles"_s }));
		tileMap->ReadAnimatedTiles(animTilesFile);

		// Load events
		std::unique_ptr<Events::EventMap> eventMap = std::make_unique<Events::EventMap>(levelHandler, tileMap->Size());
		{
			auto layerFile = IFileStream::createFileHandle(fs::joinPath({ levelRoot, "Events.layer"_s }));
			eventMap->ReadEvents(layerFile, tileMap, eventSetItem->value.GetInt(), difficulty);
		}

		// Load level texts
		SmallVector<String, 0> levelTexts;
		const auto& textEventsItem = document.FindMember("TextEvents");
		if (textEventsItem != document.MemberEnd() && textEventsItem->value.IsArray()) {
			for (int i = 0; i < textEventsItem->value.Size(); i++) {
				if (textEventsItem->value[i].IsString()) {
					const auto& text = textEventsItem->value[i].GetString();
					levelTexts.emplace_back(text);
				} else {
					levelTexts.emplace_back();
				}
			}
		}

		levelHandler->OnLevelLoaded(
			nameItem != descriptionItem->value.MemberEnd() && nameItem->value.IsString() ? nameItem->value.GetString() : nullptr,
			nextLevelItem != descriptionItem->value.MemberEnd() && nextLevelItem->value.IsString() ? nextLevelItem->value.GetString() : nullptr,
			secretLevelItem != descriptionItem->value.MemberEnd() && defaultMusicItem->value.IsString() ? secretLevelItem->value.GetString() : nullptr,
			tileMap, eventMap,
			defaultMusicItem != descriptionItem->value.MemberEnd() && defaultMusicItem->value.IsString() ? defaultMusicItem->value.GetString() : nullptr,
			defaultLightItem != descriptionItem->value.MemberEnd() && defaultLightItem->value.IsNumber() ? (defaultLightItem->value.GetFloat() * 0.01f) : 1.0f,
			levelTexts
		);

		return true;
	}

	void ContentResolver::ApplyPalette(const StringView& path)
	{
		std::unique_ptr<IFileStream> file = nullptr;
		if (!path.empty()) {
			file = IFileStream::createFileHandle(path);
			file->setExitOnFailToOpen(false);
			file->Open(FileAccessMode::Read);
		}
		if (file == nullptr || file->GetSize() == 0) {
			// Try to load default palette
			file = IFileStream::createFileHandle(fs::joinPath({ "Content"_s, "Animations"_s, "Main.palette"_s }));
			file->setExitOnFailToOpen(false);
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

		if (std::memcmp(_palettes, newPalette, ColorsPerPalette * sizeof(uint32_t)) != 0) {
			// Palettes differs, drop all cached resources, so it will be reloaded with new palette
			if (_isLoading) {
				_cachedMetadata.clear();
				_cachedGraphics.clear();
			}

			std::memcpy(_palettes, newPalette, ColorsPerPalette * sizeof(uint32_t));
			RecreateGemPalettes();
		}
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
				// Base Palette is in first row of "palette" array
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
}