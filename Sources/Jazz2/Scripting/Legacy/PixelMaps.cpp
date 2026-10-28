#if defined(WITH_ANGELSCRIPT)

#include "PixelMaps.h"
#include "AnimationTables.h"
#include "Internal.h"

#include "../../../nCine/Graphics/ITextureLoader.h"
#include "../../../nCine/Graphics/PngCodec.h"
#include "../../../nCine/Base/Algorithms.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include <Containers/StringConcatenable.h>

using namespace Jazz2::Tiles;

namespace Jazz2::Scripting
{
	namespace Legacy
	{
		// The largest pixel map, big enough for any area of a level a script takes in practice and small enough to allocate
		static constexpr std::uint64_t MaxPixelMapPixels = 8192ull * 8192ull;

		// Reads a tile as a layer holds it and `jjPIXELMAP(uint16)` takes it - its index, its flip flags and whether it's
		// animated, in which case it's the frame shown now - into 32 × 32 palette indices, transparent if it has none
		static void ReadTileImage(LevelScriptLoader* owner, std::uint16_t tile, const jjPAL* palette, std::uint8_t* pixels)
		{
			constexpr std::int32_t Size = TileSet::DefaultTileSize;
			std::memset(pixels, 0, Size * Size);

			auto* tileMap = owner->GetLevelHandler()->TileMap();
			if (tileMap == nullptr) {
				return;
			}
			std::int32_t tileId = (tile & TileMap::TileIndexMask);
			if ((tile & TileMap::TileFlagAnimated) != 0) {
				tileId = tileMap->ResolveStaticTileId(tileMap->GetStaticTileCount() + tileId);
				if (tileId < 0) {
					return;
				}
			}

			std::uint8_t source[Size * Size];
			if (!owner->GetTilePixels(tileId, source, palette)) {
				return;
			}

			// JJ2 has the horizontally flipped tiles apart and gives an empty map for one it never made, here every
			// tile can be drawn flipped, so the flipped image is always there
			bool flipX = ((tile & TileMap::TileFlagFlipX) != 0);
			bool flipY = ((tile & TileMap::TileFlagFlipY) != 0);
			for (std::int32_t y = 0; y < Size; y++) {
				const std::uint8_t* src = &source[(flipY ? Size - 1 - y : y) * Size];
				for (std::int32_t x = 0; x < Size; x++) {
					pixels[y * Size + x] = src[flipX ? Size - 1 - x : x];
				}
			}
		}

		jjPIXELMAP::jjPIXELMAP() : _refCount(1) {
		}
		jjPIXELMAP::~jjPIXELMAP() {
		}

		jjPIXELMAP* jjPIXELMAP::Create(std::uint32_t width, std::uint32_t height) {
			if (std::uint64_t(width) * height > MaxPixelMapPixels) {
				asIScriptContext* ctx = asGetActiveContext();
				if (ctx != nullptr) {
					ctx->SetException("Pixel map is too large");
				}
				return nullptr;
			}

			void* mem = asAllocMem(sizeof(jjPIXELMAP));
			jjPIXELMAP* map = new(mem) jjPIXELMAP();
			map->width = width;
			map->height = height;
			// Value-initialized, so the map is transparent
			map->_pixels = std::make_unique<std::uint8_t[]>(std::size_t(width) * height);
			return map;
		}

		jjPIXELMAP* jjPIXELMAP::CreateFromTile(std::uint16_t tileID, const jjPAL* palette) {
			jjPIXELMAP* map = Create(TileSet::DefaultTileSize, TileSet::DefaultTileSize);
			ReadTileImage(ScriptLoader::FromActiveContext<LevelScriptLoader>(), tileID, palette, map->_pixels.get());
			return map;
		}
		jjPIXELMAP* jjPIXELMAP::CreateFromSize(std::uint32_t width, std::uint32_t height) {
			return Create(width, height);
		}
		jjPIXELMAP* jjPIXELMAP::CreateFromOther(const jjPIXELMAP& other) {
			jjPIXELMAP* map = Create(other.width, other.height);
			std::memcpy(map->_pixels.get(), other._pixels.get(), std::size_t(other.width) * other.height);
			return map;
		}
		jjPIXELMAP* jjPIXELMAP::CreateFromFrame(const jjANIMFRAME* animFrame, const jjPAL* palette) {
			if (animFrame == nullptr) {
				return Create(0, 0);
			}

			// The frame came from jjAnimFrames, which read its image already
			jjPIXELMAP* map = Create(animFrame->width, animFrame->height);
			if (map != nullptr && animFrame->_pixels != nullptr) {
				std::memcpy(map->_pixels.get(), animFrame->_pixels.get(), std::size_t(animFrame->width) * animFrame->height);
			}
			// A handle passed to a native function is the function's to release
			const_cast<jjANIMFRAME*>(animFrame)->Release();
			return map;
		}
		jjPIXELMAP* jjPIXELMAP::CreateFromLayer(std::uint32_t left, std::uint32_t top, std::uint32_t width, std::uint32_t height, std::uint32_t layer, const jjPAL* palette) {
			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			return CreateFromEngineLayer(left, top, width, height, owner->GetLayerIndex(std::int32_t(layer)), palette);
		}
		jjPIXELMAP* jjPIXELMAP::CreateFromLayerObject(std::uint32_t left, std::uint32_t top, std::uint32_t width, std::uint32_t height, const jjLAYER* layer, const jjPAL* palette) {
			// A layer a script made (see jjLAYER::CreateFromSize()) has no tiles, its map stays transparent
			return CreateFromEngineLayer(left, top, width, height, (layer != nullptr ? layer->_layerIndex : -1), palette);
		}
		jjPIXELMAP* jjPIXELMAP::CreateFromEngineLayer(std::uint32_t left, std::uint32_t top, std::uint32_t width, std::uint32_t height, std::int32_t layerIndex, const jjPAL* palette) {
			constexpr std::uint32_t Size = TileSet::DefaultTileSize;

			jjPIXELMAP* map = Create(width, height);
			if (map == nullptr || layerIndex < 0) {
				return map;
			}
			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			auto* tileMap = owner->GetLevelHandler()->TileMap();
			if (tileMap == nullptr) {
				return map;
			}

			// The tiles out of the layer are empty, as GetTile() returns tile 0 for them
			std::uint8_t tilePixels[Size * Size];
			std::int32_t lastTile = -1;
			for (std::uint32_t y = 0; y < height; y++) {
				std::uint64_t py = std::uint64_t(top) + y;
				std::uint8_t* dst = &map->_pixels[std::size_t(y) * width];
				for (std::uint32_t x = 0; x < width; x++) {
					std::uint64_t px = std::uint64_t(left) + x;
					std::int32_t tile = tileMap->GetTile(layerIndex, std::int32_t(px / Size), std::int32_t(py / Size));
					if (tile != lastTile) {
						ReadTileImage(owner, std::uint16_t(tile), palette, tilePixels);
						lastTile = tile;
					}
					dst[x] = tilePixels[(py % Size) * Size + (px % Size)];
				}
			}
			return map;
		}
		jjPIXELMAP* jjPIXELMAP::CreateFromTexture(std::uint32_t texture, const jjPAL* palette) {
			constexpr std::uint32_t Size = Formats::LayerPropertiesChunk::TextureSize;
			if (texture == 0) {
				// TEXTURE::LAYER8, the tiles of the textured background
				return CreateFromLayer(0, 0, Size, Size, 8, palette);
			}
			jjPIXELMAP* map = Create(Size, Size);
			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			if (map != nullptr && !owner->GetPlusTexture(std::int32_t(texture), map->_pixels.get())) {
				LOGW("Texture {} of JJ2+ isn't available, \"Plus.j2d\" of JJ2+ is needed in the \"Source\" directory", texture);
			}
			return map;
		}
		jjPIXELMAP* jjPIXELMAP::CreateFromFilename(const String& filename, const jjPAL* palette, std::uint8_t threshold) {
			// An image of a palette gives its indices as they are, 0 is transparent (a burger booth was a black
			// square otherwise)
			if (IsScriptFileNameSafe(filename)) {
				auto stream = OpenScriptFile(filename);
				PngImage image;
				PngDecodeOptions options;
				options.VerifyCrc = true;
				if (stream != nullptr && stream->IsValid() && PngCodec::Decode(*stream, image, options) == PngError::None && image.Indexed) {
					jjPIXELMAP* map = Create(std::uint32_t(image.Width), std::uint32_t(image.Height));
					if (map != nullptr) {
						std::memcpy(map->_pixels.get(), image.Pixels.data(), std::size_t(image.Width) * image.Height);
					}
					return map;
				}
			}

			// The colors of the image become the nearest ones of the palette, a pixel of less alpha than `threshold` is
			// transparent
			jjCOLORMAP* colors = jjCOLORMAP::CreateFromFilename(filename);
			if (colors == nullptr) {
				return nullptr;
			}
			jjPIXELMAP* map = Create(colors->width, colors->height);
			if (map != nullptr) {
				auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
				const jjPAL& target = (palette != nullptr ? *palette : owner->jjPalette);
				std::size_t count = std::size_t(colors->width) * colors->height;
				for (std::size_t i = 0; i < count; i++) {
					const jjPALCOLOR& color = colors->_pixels[i];
					if (color.alpha != 0 && color.alpha >= threshold) {
						map->_pixels[i] = target.FindNearestOpaqueColor(color);
					}
				}
			}
			colors->Release();
			return map;
		}

		void jjPIXELMAP::AddRef()
		{
			_refCount++;
		}

		void jjPIXELMAP::Release()
		{
			if (--_refCount == 0) {
				this->~jjPIXELMAP();
				asFreeMem(this);
			}
		}

		// Assignment operator
		jjPIXELMAP& jjPIXELMAP::operator=(const jjPIXELMAP& o)
		{
			// Copy only the content, not the script proxy class
			if (this != &o) {
				std::size_t size = std::size_t(o.width) * o.height;
				auto pixels = std::make_unique<std::uint8_t[]>(size);
				std::memcpy(pixels.get(), o._pixels.get(), size);
				width = o.width;
				height = o.height;
				_pixels = Death::move(pixels);
			}
			return *this;
		}

		std::uint8_t* jjPIXELMAP::GetPixel(std::uint32_t x, std::uint32_t y) {
			if (x >= width || y >= height) {
				// Scripts of JJ2+ reach past the edge (`x <= 32` of a tile) and work there, a pixel out of the map is a
				// scratch one, what's written to it is lost
				_outOfRange = 0;
				return &_outOfRange;
			}
			return &_pixels[std::size_t(y) * width + x];
		}

		bool jjPIXELMAP::saveToTile(std::uint16_t tileID, bool hFlip) const {
			constexpr std::int32_t Size = TileSet::DefaultTileSize;

			// Saving into an animated tile is undefined in JJ2+
			if (width != Size || height != Size || (tileID & TileMap::TileFlagAnimated) != 0) {
				return false;
			}

			// The image saved into a flipped tile is what the tile shows when it's drawn so. JJ2 has the horizontally flipped
			// tiles apart and changes only the one saved into unless `hFlip`, here a tile is flipped as it's drawn, so its
			// flipped version always changes along with it - as with `hFlip` in JJ2+
			bool flipX = ((tileID & TileMap::TileFlagFlipX) != 0);
			bool flipY = ((tileID & TileMap::TileFlagFlipY) != 0);
			std::uint8_t image[Size * Size];
			for (std::int32_t y = 0; y < Size; y++) {
				const std::uint8_t* src = &_pixels[(flipY ? Size - 1 - y : y) * Size];
				for (std::int32_t x = 0; x < Size; x++) {
					image[y * Size + x] = src[flipX ? Size - 1 - x : x];
				}
			}

			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			return owner->SetTilePixels(tileID & TileMap::TileIndexMask, image);
		}
		bool jjPIXELMAP::saveToFrame(jjANIMFRAME* frame) const {
			if (frame == nullptr) {
				return false;
			}

			// The image and its size, the spots stay as they are; the sprites show it once the script returns
			bool result = (width <= UINT16_MAX && height <= UINT16_MAX);
			if (result) {
				auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
				owner->GetAnimationTables().SetFramePixels(*frame, _pixels.get(), width, height);
			}
			// A handle passed to a native function is the function's to release
			frame->Release();
			return result;
		}
		bool jjPIXELMAP::saveToFile(const String& filename, const jjPAL& palette) const {
			noop();
			return false;
		}

		bool jjPIXELMAP::makeTexture(jjLAYER* layer) const {
			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			std::int32_t layerIndex = (layer != nullptr ? layer->_layerIndex : owner->GetLayerIndex(8));
			// TEXTURE::CUSTOM, as JJ2+ reports a texture of a pixel map
			jjLAYER* proxy = (layer != nullptr ? layer : owner->GetLayerProxy(layerIndex));
			if (proxy == nullptr) {
				return false;
			}
			proxy->_texture = -1;

			constexpr std::uint32_t Size = Formats::LayerPropertiesChunk::TextureSize;
			bool result = (width == Size && height == Size && proxy->SetCustomTexture(_pixels.get()));
			// A handle passed to a native function is the function's to release, and GetLayerProxy() gave one too
			proxy->Release();
			return result;
		}

		void jjPIXELMAP::Reframe(std::int64_t left, std::int64_t top, std::uint32_t newWidth, std::uint32_t newHeight, std::uint8_t fill) {
			auto pixels = std::make_unique<std::uint8_t[]>(std::size_t(newWidth) * newHeight);
			for (std::uint32_t y = 0; y < newHeight; y++) {
				std::int64_t sy = top + y;
				std::uint8_t* dst = &pixels[std::size_t(y) * newWidth];
				for (std::uint32_t x = 0; x < newWidth; x++) {
					std::int64_t sx = left + x;
					dst[x] = (sx >= 0 && sy >= 0 && sx < width && sy < height ? _pixels[std::size_t(sy) * width + std::size_t(sx)] : fill);
				}
			}
			width = newWidth;
			height = newHeight;
			_pixels = Death::move(pixels);
		}

		jjPIXELMAP& jjPIXELMAP::addBorders(std::int32_t left, std::int32_t top, std::int32_t right, std::int32_t bottom, std::uint8_t borderColor) {
			// Nothing changes if the image would be empty, or if more is cropped of it than it has
			std::int64_t newWidth = std::int64_t(width) + left + right;
			std::int64_t newHeight = std::int64_t(height) + top + bottom;
			std::int64_t cropX = std::int64_t(std::max<std::int32_t>(0, -left)) + std::max<std::int32_t>(0, -right);
			std::int64_t cropY = std::int64_t(std::max<std::int32_t>(0, -top)) + std::max<std::int32_t>(0, -bottom);
			if (newWidth <= 0 || newHeight <= 0 || cropX > width || cropY > height ||
				std::uint64_t(newWidth) * std::uint64_t(newHeight) > MaxPixelMapPixels) {
				return *this;
			}
			Reframe(-std::int64_t(left), -std::int64_t(top), std::uint32_t(newWidth), std::uint32_t(newHeight), borderColor);
			return *this;
		}
		jjPIXELMAP& jjPIXELMAP::crop(std::uint32_t left, std::uint32_t top, std::uint32_t newWidth, std::uint32_t newHeight) {
			if (newWidth == 0 || newHeight == 0 || std::uint64_t(left) + newWidth > width || std::uint64_t(top) + newHeight > height) {
				return *this;
			}
			Reframe(left, top, newWidth, newHeight, 0);
			return *this;
		}
		jjPIXELMAP& jjPIXELMAP::flip(std::int32_t directions) {
			// SPRITE::Direction is the direction byte of JJ2, its sign flips horizontally, and the bit below differing
			// from it vertically (FLIPH = 0xFF, FLIPV = 0x40, FLIPHV = 0xBF)
			std::uint8_t value = std::uint8_t(directions);
			bool flipX = ((value & 0x80) != 0);
			bool flipY = (((value & 0x40) != 0) != flipX);
			if (flipX) {
				for (std::uint32_t y = 0; y < height; y++) {
					std::uint8_t* row = &_pixels[std::size_t(y) * width];
					std::reverse(row, row + width);
				}
			}
			if (flipY) {
				for (std::uint32_t y = 0; y < height / 2; y++) {
					std::swap_ranges(&_pixels[std::size_t(y) * width], &_pixels[std::size_t(y) * width] + width,
						&_pixels[std::size_t(height - 1 - y) * width]);
				}
			}
			return *this;
		}
		jjPIXELMAP& jjPIXELMAP::recolor(const CScriptArray& colorMapping) {
			if (colorMapping.GetSize() < 256) {
				return *this;
			}
			const std::uint8_t* mapping = static_cast<const std::uint8_t*>(colorMapping.At(0));
			std::size_t count = std::size_t(width) * height;
			for (std::size_t i = 0; i < count; i++) {
				_pixels[i] = mapping[_pixels[i]];
			}
			return *this;
		}
		jjPIXELMAP& jjPIXELMAP::resize(std::uint32_t newWidth, std::uint32_t newHeight) {
			if (width == 0 || height == 0 || newWidth == 0 || newHeight == 0 || std::uint64_t(newWidth) * newHeight > MaxPixelMapPixels) {
				return *this;
			}
			// Nearest neighbor
			auto pixels = std::make_unique<std::uint8_t[]>(std::size_t(newWidth) * newHeight);
			for (std::uint32_t y = 0; y < newHeight; y++) {
				std::size_t sy = std::size_t(std::uint64_t(y) * height / newHeight);
				std::uint8_t* dst = &pixels[std::size_t(y) * newWidth];
				for (std::uint32_t x = 0; x < newWidth; x++) {
					std::size_t sx = std::size_t(std::uint64_t(x) * width / newWidth);
					dst[x] = _pixels[sy * width + sx];
				}
			}
			width = newWidth;
			height = newHeight;
			_pixels = Death::move(pixels);
			return *this;
		}
		jjPIXELMAP& jjPIXELMAP::rotate() {
			// 90 degrees clockwise, the bottom-left pixel ends up at the top-left
			auto pixels = std::make_unique<std::uint8_t[]>(std::size_t(width) * height);
			for (std::uint32_t y = 0; y < width; y++) {
				for (std::uint32_t x = 0; x < height; x++) {
					pixels[std::size_t(y) * height + x] = _pixels[std::size_t(height - 1 - x) * width + y];
				}
			}
			std::swap(width, height);
			_pixels = Death::move(pixels);
			return *this;
		}
		jjPIXELMAP& jjPIXELMAP::trim(std::uint32_t& left, std::uint32_t& top, std::uint32_t& right, std::uint32_t& bottom, std::uint8_t trimColor) {
			std::uint32_t minX = width, minY = height, maxX = 0, maxY = 0;
			for (std::uint32_t y = 0; y < height; y++) {
				const std::uint8_t* row = &_pixels[std::size_t(y) * width];
				for (std::uint32_t x = 0; x < width; x++) {
					if (row[x] != trimColor) {
						minX = std::min(minX, x);
						maxX = std::max(maxX, x);
						minY = std::min(minY, y);
						maxY = std::max(maxY, y);
					}
				}
			}

			if (minX > maxX || minY > maxY) {
				// Nothing but the trimmed color, as a special case the map is cropped to 1 × 1 instead of 0 × 0
				left = right = width;
				top = bottom = height;
				if (width > 0 && height > 0) {
					Reframe(0, 0, 1, 1, trimColor);
				}
				return *this;
			}

			left = minX;
			top = minY;
			right = width - 1 - maxX;
			bottom = height - 1 - maxY;
			if (left != 0 || top != 0 || right != 0 || bottom != 0) {
				Reframe(minX, minY, maxX - minX + 1, maxY - minY + 1, 0);
			}
			return *this;
		}
		jjPIXELMAP& jjPIXELMAP::trimBasic(std::uint8_t trimColor) {
			std::uint32_t left, top, right, bottom;
			return trim(left, top, right, bottom, trimColor);
		}
		jjPIXELMAP* jjPIXELMAP::CreateFromColorMap(const jjCOLORMAP& other, const jjPAL* palette) {
			jjPIXELMAP* map = Create(other.width, other.height);
			if (map != nullptr) {
				auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
				auto indices = other.ToIndices(palette != nullptr ? *palette : owner->jjPalette);
				std::memcpy(map->_pixels.get(), indices.get(), std::size_t(other.width) * other.height);
			}
			return map;
		}

		jjCOLORMAP::jjCOLORMAP() : _refCount(1) {
		}
		jjCOLORMAP::~jjCOLORMAP() {
		}

		jjCOLORMAP* jjCOLORMAP::Create(std::uint32_t width, std::uint32_t height) {
			if (std::uint64_t(width) * height > MaxPixelMapPixels) {
				asIScriptContext* ctx = asGetActiveContext();
				if (ctx != nullptr) {
					ctx->SetException("Color map is too large");
				}
				return nullptr;
			}

			void* mem = asAllocMem(sizeof(jjCOLORMAP));
			jjCOLORMAP* map = new(mem) jjCOLORMAP();
			map->width = width;
			map->height = height;
			// Value-initialized, so the map is transparent
			map->_pixels = std::make_unique<jjPALCOLOR[]>(std::size_t(width) * height);
			return map;
		}

		jjCOLORMAP* jjCOLORMAP::CreateFromIndices(const std::uint8_t* indices, std::uint32_t width, std::uint32_t height, const jjPAL& palette) {
			jjCOLORMAP* map = Create(width, height);
			if (map == nullptr || indices == nullptr) {
				return map;
			}
			std::size_t count = std::size_t(width) * height;
			for (std::size_t i = 0; i < count; i++) {
				if (indices[i] != 0) {
					map->_pixels[i] = palette.getConstColor(indices[i]);
					map->_pixels[i].alpha = 255;
				}
			}
			return map;
		}

		jjCOLORMAP* jjCOLORMAP::CreateFromSize(std::uint32_t width, std::uint32_t height) {
			return Create(width, height);
		}
		jjCOLORMAP* jjCOLORMAP::CreateFromPixelMap(const jjPIXELMAP& other, const jjPAL& palette) {
			jjPIXELMAP& source = const_cast<jjPIXELMAP&>(other);
			return CreateFromIndices(other.width > 0 && other.height > 0 ? source.GetPixel(0, 0) : nullptr, other.width, other.height, palette);
		}
		jjCOLORMAP* jjCOLORMAP::CreateFromOther(const jjCOLORMAP& other) {
			jjCOLORMAP* map = Create(other.width, other.height);
			if (map != nullptr) {
				std::memcpy(map->_pixels.get(), other._pixels.get(), std::size_t(other.width) * other.height * sizeof(jjPALCOLOR));
			}
			return map;
		}
		jjCOLORMAP* jjCOLORMAP::CreateFromFrame(const jjANIMFRAME* animFrame, const jjPAL& palette) {
			if (animFrame == nullptr) {
				return Create(0, 0);
			}
			// The frame came from jjAnimFrames, which read its image already; a 32-bit one has its own colors
			jjCOLORMAP* map;
			if (animFrame->_colors != nullptr) {
				map = Create(animFrame->width, animFrame->height);
				if (map != nullptr) {
					std::memcpy(map->_pixels.get(), animFrame->_colors.get(), std::size_t(animFrame->width) * animFrame->height * sizeof(jjPALCOLOR));
				}
			} else {
				map = CreateFromIndices(animFrame->_pixels.get(), animFrame->width, animFrame->height, palette);
			}
			// A handle passed to a native function is the function's to release
			const_cast<jjANIMFRAME*>(animFrame)->Release();
			return map;
		}
		jjCOLORMAP* jjCOLORMAP::CreateFromTexture(std::uint32_t texture, const jjPAL& palette) {
			jjPIXELMAP* indices = jjPIXELMAP::CreateFromTexture(texture, &palette);
			if (indices == nullptr) {
				return nullptr;
			}
			jjCOLORMAP* map = CreateFromPixelMap(*indices, palette);
			indices->Release();
			return map;
		}
		jjCOLORMAP* jjCOLORMAP::CreateFromTile(std::uint16_t tileID, const jjPAL& palette) {
			constexpr std::uint32_t Size = TileSet::DefaultTileSize;
			jjCOLORMAP* map = Create(Size, Size);
			if (map == nullptr) {
				return map;
			}
			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			auto* tileMap = owner->GetLevelHandler()->TileMap();
			if (tileMap == nullptr) {
				return map;
			}
			// An animated tile is the frame it shows now, as jjPIXELMAP has it
			std::int32_t tileId = (tileID & TileMap::TileIndexMask);
			if ((tileID & TileMap::TileFlagAnimated) != 0) {
				tileId = tileMap->ResolveStaticTileId(tileMap->GetStaticTileCount() + tileId);
				if (tileId < 0) {
					return map;
				}
			}
			jjPALCOLOR source[Size * Size];
			if (!owner->GetTileColors(tileId, source, palette)) {
				return map;
			}
			// As jjPIXELMAP has it, every tile can be drawn flipped
			bool flipX = ((tileID & TileMap::TileFlagFlipX) != 0);
			bool flipY = ((tileID & TileMap::TileFlagFlipY) != 0);
			for (std::uint32_t y = 0; y < Size; y++) {
				const jjPALCOLOR* src = &source[(flipY ? Size - 1 - y : y) * Size];
				for (std::uint32_t x = 0; x < Size; x++) {
					map->_pixels[y * Size + x] = src[flipX ? Size - 1 - x : x];
				}
			}
			return map;
		}
		jjCOLORMAP* jjCOLORMAP::CreateFromLayer(std::uint32_t left, std::uint32_t top, std::uint32_t width, std::uint32_t height, std::uint32_t layer, const jjPAL& palette) {
			// TODO: A true color tile of the layer is in the nearest colors of the palette
			jjPIXELMAP* indices = jjPIXELMAP::CreateFromLayer(left, top, width, height, layer, &palette);
			if (indices == nullptr) {
				return nullptr;
			}
			jjCOLORMAP* map = CreateFromPixelMap(*indices, palette);
			indices->Release();
			return map;
		}
		jjCOLORMAP* jjCOLORMAP::CreateFromLayerObject(std::uint32_t left, std::uint32_t top, std::uint32_t width, std::uint32_t height, const jjLAYER& layer, const jjPAL& palette) {
			jjPIXELMAP* indices = jjPIXELMAP::CreateFromLayerObject(left, top, width, height, &layer, &palette);
			if (indices == nullptr) {
				return nullptr;
			}
			jjCOLORMAP* map = CreateFromPixelMap(*indices, palette);
			indices->Release();
			return map;
		}
		jjCOLORMAP* jjCOLORMAP::CreateFromFilename(const String& filename) {
			// The real colors and alpha of the file, whatever kind of image it is; JJ2+ gives a 1 × 1 map for a file it can't read
			std::unique_ptr<ITextureLoader> loader;
			if (IsScriptFileNameSafe(filename)) {
				auto stream = OpenScriptFile(filename);
				if (stream != nullptr && stream->IsValid()) {
					loader = ITextureLoader::createFromStream(Death::move(stream), filename);
				}
			}
			if (loader == nullptr || !loader->hasLoaded() || loader->pixels() == nullptr) {
				return Create(1, 1);
			}

			const std::uint32_t channels = loader->texFormat().numChannels();
			const bool bgr = loader->texFormat().isBgr();
			jjCOLORMAP* map = Create(std::uint32_t(loader->width()), std::uint32_t(loader->height()));
			if (map == nullptr || channels < 1 || channels > 4) {
				return map;
			}
			const std::uint8_t* src = loader->pixels();
			std::size_t count = std::size_t(map->width) * map->height;
			for (std::size_t i = 0; i < count; i++, src += channels) {
				jjPALCOLOR& color = map->_pixels[i];
				if (channels <= 2) {
					color.red = color.green = color.blue = src[0];
					color.alpha = (channels == 2 ? src[1] : 255);
				} else {
					color.red = src[bgr ? 2 : 0];
					color.green = src[1];
					color.blue = src[bgr ? 0 : 2];
					color.alpha = (channels == 4 ? src[3] : 255);
				}
			}
			return map;
		}

		void jjCOLORMAP::AddRef()
		{
			_refCount++;
		}

		void jjCOLORMAP::Release()
		{
			if (--_refCount == 0) {
				this->~jjCOLORMAP();
				asFreeMem(this);
			}
		}

		jjCOLORMAP& jjCOLORMAP::operator=(const jjCOLORMAP& o)
		{
			// Copy only the content, not the script proxy class
			if (this != &o) {
				std::size_t size = std::size_t(o.width) * o.height;
				auto pixels = std::make_unique<jjPALCOLOR[]>(size);
				std::memcpy(pixels.get(), o._pixels.get(), size * sizeof(jjPALCOLOR));
				width = o.width;
				height = o.height;
				_pixels = Death::move(pixels);
			}
			return *this;
		}

		jjPALCOLOR* jjCOLORMAP::GetPixel(std::uint32_t x, std::uint32_t y) {
			if (x >= width || y >= height) {
				// As of jjPIXELMAP, what's written to a pixel out of the map is lost
				_outOfRange = {};
				return &_outOfRange;
			}
			return &_pixels[std::size_t(y) * width + x];
		}

		std::unique_ptr<std::uint8_t[]> jjCOLORMAP::ToIndices(const jjPAL& palette) const {
			// The same color is looked up only once, an image has few of them
			std::size_t count = std::size_t(width) * height;
			std::unique_ptr<std::uint8_t[]> indices = std::make_unique<std::uint8_t[]>(count);
			HashMap<std::uint32_t, std::uint8_t> nearest;
			for (std::size_t i = 0; i < count; i++) {
				const jjPALCOLOR& color = _pixels[i];
				if (color.alpha == 0) {
					continue;
				}
				std::uint32_t key = std::uint32_t(color.red) | (std::uint32_t(color.green) << 8) | (std::uint32_t(color.blue) << 16);
				auto it = nearest.find(key);
				if (it == nearest.end()) {
					it = nearest.emplace(key, palette.FindNearestOpaqueColor(color)).first;
				}
				indices[i] = it->second;
			}
			return indices;
		}

		bool jjCOLORMAP::saveToTile(std::uint16_t tileID, bool hFlip) const {
			constexpr std::uint32_t Size = TileSet::DefaultTileSize;

			// Saving into an animated tile is undefined in JJ2+
			if (width != Size || height != Size || (tileID & TileMap::TileFlagAnimated) != 0) {
				return false;
			}

			// The image saved into a flipped tile is what the tile shows when it's drawn so, see jjPIXELMAP::saveToTile()
			bool flipX = ((tileID & TileMap::TileFlagFlipX) != 0);
			bool flipY = ((tileID & TileMap::TileFlagFlipY) != 0);
			jjPALCOLOR image[Size * Size];
			for (std::uint32_t y = 0; y < Size; y++) {
				const jjPALCOLOR* src = &_pixels[(flipY ? Size - 1 - y : y) * Size];
				for (std::uint32_t x = 0; x < Size; x++) {
					image[y * Size + x] = src[flipX ? Size - 1 - x : x];
				}
			}

			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			return owner->SetTileColors(tileID & TileMap::TileIndexMask, image);
		}
		bool jjCOLORMAP::saveToFrame(jjANIMFRAME* frame) const {
			if (frame == nullptr) {
				return false;
			}

			// As jjPIXELMAP::saveToFrame(), the frame keeps its spots and is a 32-bit one from now on
			bool result = (width <= UINT16_MAX && height <= UINT16_MAX);
			if (result) {
				auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
				auto indices = ToIndices(owner->jjPalette);
				owner->GetAnimationTables().SetFrameColors(*frame, _pixels.get(), indices.get(), width, height);
			}
			// A handle passed to a native function is the function's to release
			frame->Release();
			return result;
		}
		bool jjCOLORMAP::saveToFile(const String& filename, bool allowCompression) const {
			noop();
			return false;
		}

		void jjCOLORMAP::Reframe(std::int64_t left, std::int64_t top, std::uint32_t newWidth, std::uint32_t newHeight, jjPALCOLOR fill) {
			auto pixels = std::make_unique<jjPALCOLOR[]>(std::size_t(newWidth) * newHeight);
			for (std::uint32_t y = 0; y < newHeight; y++) {
				std::int64_t sy = top + y;
				jjPALCOLOR* dst = &pixels[std::size_t(y) * newWidth];
				for (std::uint32_t x = 0; x < newWidth; x++) {
					std::int64_t sx = left + x;
					dst[x] = (sx >= 0 && sy >= 0 && sx < width && sy < height ? _pixels[std::size_t(sy) * width + std::size_t(sx)] : fill);
				}
			}
			width = newWidth;
			height = newHeight;
			_pixels = Death::move(pixels);
		}

		jjCOLORMAP& jjCOLORMAP::addBorders(std::int32_t left, std::int32_t top, std::int32_t right, std::int32_t bottom, jjPALCOLOR borderColor) {
			// The same as jjPIXELMAP::addBorders()
			std::int64_t newWidth = std::int64_t(width) + left + right;
			std::int64_t newHeight = std::int64_t(height) + top + bottom;
			std::int64_t cropX = std::int64_t(std::max<std::int32_t>(0, -left)) + std::max<std::int32_t>(0, -right);
			std::int64_t cropY = std::int64_t(std::max<std::int32_t>(0, -top)) + std::max<std::int32_t>(0, -bottom);
			if (newWidth <= 0 || newHeight <= 0 || cropX > width || cropY > height ||
				std::uint64_t(newWidth) * std::uint64_t(newHeight) > MaxPixelMapPixels) {
				return *this;
			}
			Reframe(-std::int64_t(left), -std::int64_t(top), std::uint32_t(newWidth), std::uint32_t(newHeight), borderColor);
			return *this;
		}
		jjCOLORMAP& jjCOLORMAP::crop(std::uint32_t left, std::uint32_t top, std::uint32_t newWidth, std::uint32_t newHeight) {
			if (newWidth == 0 || newHeight == 0 || std::uint64_t(left) + newWidth > width || std::uint64_t(top) + newHeight > height) {
				return *this;
			}
			Reframe(left, top, newWidth, newHeight, {});
			return *this;
		}
		jjCOLORMAP& jjCOLORMAP::flip(std::int32_t directions) {
			// SPRITE::Direction as jjPIXELMAP::flip() takes it
			std::uint8_t value = std::uint8_t(directions);
			bool flipX = ((value & 0x80) != 0);
			bool flipY = (((value & 0x40) != 0) != flipX);
			if (flipX) {
				for (std::uint32_t y = 0; y < height; y++) {
					jjPALCOLOR* row = &_pixels[std::size_t(y) * width];
					std::reverse(row, row + width);
				}
			}
			if (flipY) {
				for (std::uint32_t y = 0; y < height / 2; y++) {
					std::swap_ranges(&_pixels[std::size_t(y) * width], &_pixels[std::size_t(y) * width] + width,
						&_pixels[std::size_t(height - 1 - y) * width]);
				}
			}
			return *this;
		}
		jjCOLORMAP& jjCOLORMAP::resize(std::uint32_t newWidth, std::uint32_t newHeight) {
			if (width == 0 || height == 0 || newWidth == 0 || newHeight == 0 || std::uint64_t(newWidth) * newHeight > MaxPixelMapPixels) {
				return *this;
			}
			// Nearest neighbor
			auto pixels = std::make_unique<jjPALCOLOR[]>(std::size_t(newWidth) * newHeight);
			for (std::uint32_t y = 0; y < newHeight; y++) {
				std::size_t sy = std::size_t(std::uint64_t(y) * height / newHeight);
				jjPALCOLOR* dst = &pixels[std::size_t(y) * newWidth];
				for (std::uint32_t x = 0; x < newWidth; x++) {
					std::size_t sx = std::size_t(std::uint64_t(x) * width / newWidth);
					dst[x] = _pixels[sy * width + sx];
				}
			}
			width = newWidth;
			height = newHeight;
			_pixels = Death::move(pixels);
			return *this;
		}
		jjCOLORMAP& jjCOLORMAP::rotate() {
			// 90 degrees clockwise, the bottom-left pixel ends up at the top-left
			auto pixels = std::make_unique<jjPALCOLOR[]>(std::size_t(width) * height);
			for (std::uint32_t y = 0; y < width; y++) {
				for (std::uint32_t x = 0; x < height; x++) {
					pixels[std::size_t(y) * height + x] = _pixels[std::size_t(height - 1 - x) * width + y];
				}
			}
			std::swap(width, height);
			_pixels = Death::move(pixels);
			return *this;
		}
		jjCOLORMAP& jjCOLORMAP::swizzle(std::uint32_t red, std::uint32_t green, std::uint32_t blue, std::uint32_t alpha) {
			// COLOR::Component, RED to ALPHA
			std::size_t count = std::size_t(width) * height;
			for (std::size_t i = 0; i < count; i++) {
				jjPALCOLOR& color = _pixels[i];
				const std::uint8_t components[4] = { color.red, color.green, color.blue, color.alpha };
				color.red = components[red & 3];
				color.green = components[green & 3];
				color.blue = components[blue & 3];
				color.alpha = components[alpha & 3];
			}
			return *this;
		}
		jjCOLORMAP& jjCOLORMAP::trim(std::uint32_t& left, std::uint32_t& top, std::uint32_t& right, std::uint32_t& bottom, std::uint8_t trimAlpha) {
			std::uint32_t minX = width, minY = height, maxX = 0, maxY = 0;
			for (std::uint32_t y = 0; y < height; y++) {
				const jjPALCOLOR* row = &_pixels[std::size_t(y) * width];
				for (std::uint32_t x = 0; x < width; x++) {
					if (row[x].alpha > trimAlpha) {
						minX = std::min(minX, x);
						maxX = std::max(maxX, x);
						minY = std::min(minY, y);
						maxY = std::max(maxY, y);
					}
				}
			}

			if (minX > maxX || minY > maxY) {
				// Nothing to keep, as jjPIXELMAP::trim() the map is cropped to 1 × 1 instead of 0 × 0
				left = right = width;
				top = bottom = height;
				if (width > 0 && height > 0) {
					Reframe(0, 0, 1, 1, {});
				}
				return *this;
			}

			left = minX;
			top = minY;
			right = width - 1 - maxX;
			bottom = height - 1 - maxY;
			if (left != 0 || top != 0 || right != 0 || bottom != 0) {
				Reframe(minX, minY, maxX - minX + 1, maxY - minY + 1, {});
			}
			return *this;
		}
		jjCOLORMAP& jjCOLORMAP::trimBasic(std::uint8_t trimAlpha) {
			std::uint32_t left, top, right, bottom;
			return trim(left, top, right, bottom, trimAlpha);
		}

		jjMASKMAP::jjMASKMAP() : _refCount(1), _mask{} {
		}
		jjMASKMAP::~jjMASKMAP() {
		}

		jjMASKMAP* jjMASKMAP::CreateFromBool(bool filled) {
			void* mem = asAllocMem(sizeof(jjMASKMAP));
			jjMASKMAP* map = new(mem) jjMASKMAP();
			std::fill(map->_mask, map->_mask + Size * Size, filled);
			return map;
		}
		jjMASKMAP* jjMASKMAP::CreateFromTile(std::uint16_t tileID) {
			jjMASKMAP* map = CreateFromBool(false);

			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			auto* tileMap = owner->GetLevelHandler()->TileMap();
			if (tileMap == nullptr) {
				return map;
			}
			std::int32_t tileId = (tileID & TileMap::TileIndexMask);
			if ((tileID & TileMap::TileFlagAnimated) != 0) {
				tileId = tileMap->ResolveStaticTileId(tileMap->GetStaticTileCount() + tileId);
			}
			const std::uint8_t* packed = tileMap->GetStaticTileMask(tileId);
			if (packed == nullptr) {
				return map;
			}

			// As with jjPIXELMAP, the flipped mask of every tile is there
			constexpr std::int32_t S = std::int32_t(Size);
			bool flipX = ((tileID & TileMap::TileFlagFlipX) != 0);
			bool flipY = ((tileID & TileMap::TileFlagFlipY) != 0);
			for (std::int32_t y = 0; y < S; y++) {
				for (std::int32_t x = 0; x < S; x++) {
					map->_mask[y * S + x] = TileSet::IsTileMaskBitSet(packed, flipX ? S - 1 - x : x, flipY ? S - 1 - y : y);
				}
			}
			return map;
		}

		void jjMASKMAP::AddRef()
		{
			_refCount++;
		}

		void jjMASKMAP::Release()
		{
			if (--_refCount == 0) {
				this->~jjMASKMAP();
				asFreeMem(this);
			}
		}

		jjMASKMAP& jjMASKMAP::operator=(const jjMASKMAP& o)
		{
			// Copy only the content, not the script proxy class
			std::memcpy(_mask, o._mask, sizeof(_mask));
			return *this;
		}

		bool* jjMASKMAP::GetPixel(std::uint32_t x, std::uint32_t y) {
			if (x >= Size || y >= Size) {
				// The same as jjPIXELMAP::GetPixel()
				_outOfRange = false;
				return &_outOfRange;
			}
			return &_mask[y * Size + x];
		}

		bool jjMASKMAP::save(std::uint16_t tileID, bool hFlip) const {
			// The same as jjPIXELMAP::saveToTile(), the flipped version of a tile always changes along with it
			if ((tileID & TileMap::TileFlagAnimated) != 0) {
				return false;
			}
			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			auto* tileMap = owner->GetLevelHandler()->TileMap();
			if (tileMap == nullptr) {
				return false;
			}

			bool flipX = ((tileID & TileMap::TileFlagFlipX) != 0);
			bool flipY = ((tileID & TileMap::TileFlagFlipY) != 0);
			std::uint8_t mask[Size * Size];
			for (std::uint32_t y = 0; y < Size; y++) {
				for (std::uint32_t x = 0; x < Size; x++) {
					mask[y * Size + x] = (_mask[(flipY ? Size - 1 - y : y) * Size + (flipX ? Size - 1 - x : x)] ? 1 : 0);
				}
			}
			return tileMap->OverrideTileMask(tileID & TileMap::TileIndexMask, mask);
		}
	}

	void LevelScriptLoader::RegisterLegacyPixelMaps(asIScriptEngine* engine)
	{
		engine->RegisterObjectType("jjLAYER", sizeof(jjLAYER), asOBJ_REF);

		engine->RegisterObjectType("jjPIXELMAP", sizeof(jjPIXELMAP), asOBJ_REF);
		engine->RegisterObjectBehaviour("jjPIXELMAP", asBEHAVE_FACTORY, "jjPIXELMAP@ f(uint16 tileID = 0, const jjPAL &in palette = jjPalette)", asFUNCTION(jjPIXELMAP::CreateFromTile), asCALL_CDECL);
		engine->RegisterObjectBehaviour("jjPIXELMAP", asBEHAVE_FACTORY, "jjPIXELMAP@ f(uint width, uint height)", asFUNCTION(jjPIXELMAP::CreateFromSize), asCALL_CDECL);
		engine->RegisterObjectBehaviour("jjPIXELMAP", asBEHAVE_FACTORY, "jjPIXELMAP@ f(const jjPIXELMAP &in other)", asFUNCTION(jjPIXELMAP::CreateFromOther), asCALL_CDECL);
		engine->RegisterObjectBehaviour("jjPIXELMAP", asBEHAVE_FACTORY, "jjPIXELMAP@ f(const jjANIMFRAME@ animFrame, const jjPAL &in palette = jjPalette)", asFUNCTION(jjPIXELMAP::CreateFromFrame), asCALL_CDECL);
		engine->RegisterObjectBehaviour("jjPIXELMAP", asBEHAVE_FACTORY, "jjPIXELMAP@ f(uint left, uint top, uint width, uint height, uint layer = 4, const jjPAL &in palette = jjPalette)", asFUNCTION(jjPIXELMAP::CreateFromLayer), asCALL_CDECL);
		engine->RegisterObjectBehaviour("jjPIXELMAP", asBEHAVE_FACTORY, "jjPIXELMAP@ f(uint left, uint top, uint width, uint height, const jjLAYER &in layer, const jjPAL &in palette = jjPalette)", asFUNCTION(jjPIXELMAP::CreateFromLayerObject), asCALL_CDECL);
		engine->RegisterObjectBehaviour("jjPIXELMAP", asBEHAVE_FACTORY, "jjPIXELMAP@ f(TEXTURE::Texture texture, const jjPAL &in palette = jjPalette)", asFUNCTION(jjPIXELMAP::CreateFromTexture), asCALL_CDECL);
		engine->RegisterObjectBehaviour("jjPIXELMAP", asBEHAVE_FACTORY, "jjPIXELMAP@ f(const ::string &in filename, const jjPAL &in palette = jjPalette, uint8 threshold = 1)", asFUNCTION(jjPIXELMAP::CreateFromFilename), asCALL_CDECL);
		engine->RegisterObjectBehaviour("jjPIXELMAP", asBEHAVE_ADDREF, "void f()", asMETHOD(jjPIXELMAP, AddRef), asCALL_THISCALL);
		engine->RegisterObjectBehaviour("jjPIXELMAP", asBEHAVE_RELEASE, "void f()", asMETHOD(jjPIXELMAP, Release), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPIXELMAP", "uint8& opIndex(uint, uint)", asMETHOD(jjPIXELMAP, GetPixel), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPIXELMAP", "const uint8& opIndex(uint, uint) const", asMETHOD(jjPIXELMAP, GetPixel), asCALL_THISCALL);
		engine->RegisterObjectProperty("jjPIXELMAP", "const uint width", asOFFSET(jjPIXELMAP, width));
		engine->RegisterObjectProperty("jjPIXELMAP", "const uint height", asOFFSET(jjPIXELMAP, height));
		engine->RegisterObjectMethod("jjPIXELMAP", "bool save(uint16 tileID, bool hFlip = false) const", asMETHOD(jjPIXELMAP, saveToTile), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPIXELMAP", "bool save(jjANIMFRAME@ frame) const", asMETHOD(jjPIXELMAP, saveToFrame), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPIXELMAP", "bool save(const ::string &in filename, const jjPAL &in palette = jjPalette) const", asMETHOD(jjPIXELMAP, saveToFile), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPIXELMAP", "jjPIXELMAP& crop(uint, uint, uint, uint)", asMETHOD(jjPIXELMAP, crop), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPIXELMAP", "jjPIXELMAP& addBorders(int, int, int, int, uint8 = 0)", asMETHOD(jjPIXELMAP, addBorders), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPIXELMAP", "jjPIXELMAP& flip(SPRITE::Direction)", asMETHOD(jjPIXELMAP, flip), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPIXELMAP", "jjPIXELMAP& rotate()", asMETHOD(jjPIXELMAP, rotate), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPIXELMAP", "jjPIXELMAP& recolor(const array<uint8> &in)", asMETHOD(jjPIXELMAP, recolor), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPIXELMAP", "jjPIXELMAP& resize(uint, uint)", asMETHOD(jjPIXELMAP, resize), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPIXELMAP", "jjPIXELMAP& trim(uint &out, uint &out, uint &out, uint &out, uint8 = 0)", asMETHOD(jjPIXELMAP, trim), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPIXELMAP", "jjPIXELMAP& trim(uint8 = 0)", asMETHOD(jjPIXELMAP, trimBasic), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPIXELMAP", "bool makeTexture(jjLAYER@ layer = null) const", asMETHOD(jjPIXELMAP, makeTexture), asCALL_THISCALL);

		engine->RegisterObjectType("jjCOLORMAP", sizeof(jjCOLORMAP), asOBJ_REF);
		engine->RegisterObjectBehaviour("jjCOLORMAP", asBEHAVE_FACTORY, "jjCOLORMAP@ f(uint16 tileID = 0, const jjPAL &in palette = jjPalette)", asFUNCTION(jjCOLORMAP::CreateFromTile), asCALL_CDECL);
		engine->RegisterObjectBehaviour("jjCOLORMAP", asBEHAVE_FACTORY, "jjCOLORMAP@ f(uint width, uint height)", asFUNCTION(jjCOLORMAP::CreateFromSize), asCALL_CDECL);
		engine->RegisterObjectBehaviour("jjCOLORMAP", asBEHAVE_FACTORY, "jjCOLORMAP@ f(const jjPIXELMAP &in pixelMap, const jjPAL &in palette = jjPalette)", asFUNCTION(jjCOLORMAP::CreateFromPixelMap), asCALL_CDECL);
		engine->RegisterObjectBehaviour("jjCOLORMAP", asBEHAVE_FACTORY, "jjCOLORMAP@ f(const jjCOLORMAP &in other)", asFUNCTION(jjCOLORMAP::CreateFromOther), asCALL_CDECL);
		engine->RegisterObjectBehaviour("jjCOLORMAP", asBEHAVE_FACTORY, "jjCOLORMAP@ f(const jjANIMFRAME@ animFrame, const jjPAL &in palette = jjPalette)", asFUNCTION(jjCOLORMAP::CreateFromFrame), asCALL_CDECL);
		engine->RegisterObjectBehaviour("jjCOLORMAP", asBEHAVE_FACTORY, "jjCOLORMAP@ f(uint left, uint top, uint width, uint height, uint layer = 4, const jjPAL &in palette = jjPalette)", asFUNCTION(jjCOLORMAP::CreateFromLayer), asCALL_CDECL);
		engine->RegisterObjectBehaviour("jjCOLORMAP", asBEHAVE_FACTORY, "jjCOLORMAP@ f(uint left, uint top, uint width, uint height, const jjLAYER &in layer, const jjPAL &in palette = jjPalette)", asFUNCTION(jjCOLORMAP::CreateFromLayerObject), asCALL_CDECL);
		engine->RegisterObjectBehaviour("jjCOLORMAP", asBEHAVE_FACTORY, "jjCOLORMAP@ f(TEXTURE::Texture texture, const jjPAL &in palette = jjPalette)", asFUNCTION(jjCOLORMAP::CreateFromTexture), asCALL_CDECL);
		engine->RegisterObjectBehaviour("jjCOLORMAP", asBEHAVE_FACTORY, "jjCOLORMAP@ f(const ::string &in filename)", asFUNCTION(jjCOLORMAP::CreateFromFilename), asCALL_CDECL);
		engine->RegisterObjectBehaviour("jjCOLORMAP", asBEHAVE_ADDREF, "void f()", asMETHOD(jjCOLORMAP, AddRef), asCALL_THISCALL);
		engine->RegisterObjectBehaviour("jjCOLORMAP", asBEHAVE_RELEASE, "void f()", asMETHOD(jjCOLORMAP, Release), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjCOLORMAP", "jjPALCOLOR& opIndex(uint, uint)", asMETHOD(jjCOLORMAP, GetPixel), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjCOLORMAP", "const jjPALCOLOR& opIndex(uint, uint) const", asMETHOD(jjCOLORMAP, GetPixel), asCALL_THISCALL);
		engine->RegisterObjectProperty("jjCOLORMAP", "const uint width", asOFFSET(jjCOLORMAP, width));
		engine->RegisterObjectProperty("jjCOLORMAP", "const uint height", asOFFSET(jjCOLORMAP, height));
		engine->RegisterObjectMethod("jjCOLORMAP", "bool save(uint16 tileID, bool hFlip = false) const", asMETHOD(jjCOLORMAP, saveToTile), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjCOLORMAP", "bool save(jjANIMFRAME@ frame) const", asMETHOD(jjCOLORMAP, saveToFrame), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjCOLORMAP", "bool save(const ::string &in filename, bool allowCompression = false) const", asMETHOD(jjCOLORMAP, saveToFile), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjCOLORMAP", "jjCOLORMAP& addBorders(int, int, int, int, jjPALCOLOR = jjPALCOLOR())", asMETHOD(jjCOLORMAP, addBorders), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjCOLORMAP", "jjCOLORMAP& crop(uint, uint, uint, uint)", asMETHOD(jjCOLORMAP, crop), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjCOLORMAP", "jjCOLORMAP& flip(SPRITE::Direction)", asMETHOD(jjCOLORMAP, flip), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjCOLORMAP", "jjCOLORMAP& resize(uint, uint)", asMETHOD(jjCOLORMAP, resize), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjCOLORMAP", "jjCOLORMAP& rotate()", asMETHOD(jjCOLORMAP, rotate), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjCOLORMAP", "jjCOLORMAP& swizzle(COLOR::Component red, COLOR::Component green, COLOR::Component blue, COLOR::Component alpha = COLOR::ALPHA)", asMETHOD(jjCOLORMAP, swizzle), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjCOLORMAP", "jjCOLORMAP& trim(uint &out, uint &out, uint &out, uint &out, uint8 = 0)", asMETHOD(jjCOLORMAP, trim), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjCOLORMAP", "jjCOLORMAP& trim(uint8 = 0)", asMETHOD(jjCOLORMAP, trimBasic), asCALL_THISCALL);
		engine->RegisterObjectBehaviour("jjPIXELMAP", asBEHAVE_FACTORY, "jjPIXELMAP@ f(const jjCOLORMAP &in colorMap, const jjPAL &in palette = jjPalette)", asFUNCTION(jjPIXELMAP::CreateFromColorMap), asCALL_CDECL);
		engine->RegisterObjectMethod("jjANIMSET", "jjANIMSET @load(const jjPIXELMAP &in, uint frameWidth, uint frameHeight, uint frameSpacingX = 0, uint frameSpacingY = 0, uint startX = 0, uint startY = 0, const array<int> &in coldSpotYOffsets = array<int>(), int firstAnimToOverwrite = -1, int firstFrameToOverwrite = -1)", asMETHOD(jjANIMSET, loadSheet), asCALL_THISCALL);

		engine->RegisterObjectType("jjMASKMAP", sizeof(jjMASKMAP), asOBJ_REF);
		engine->RegisterObjectBehaviour("jjMASKMAP", asBEHAVE_FACTORY, "jjMASKMAP@ f(bool filled = false)", asFUNCTION(jjMASKMAP::CreateFromBool), asCALL_CDECL);
		engine->RegisterObjectBehaviour("jjMASKMAP", asBEHAVE_FACTORY, "jjMASKMAP@ f(uint16 tileID)", asFUNCTION(jjMASKMAP::CreateFromTile), asCALL_CDECL);
		engine->RegisterObjectBehaviour("jjMASKMAP", asBEHAVE_ADDREF, "void f()", asMETHOD(jjMASKMAP, AddRef), asCALL_THISCALL);
		engine->RegisterObjectBehaviour("jjMASKMAP", asBEHAVE_RELEASE, "void f()", asMETHOD(jjMASKMAP, Release), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjMASKMAP", "bool& opIndex(uint, uint)", asMETHOD(jjMASKMAP, GetPixel), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjMASKMAP", "const bool& opIndex(uint, uint) const", asMETHOD(jjMASKMAP, GetPixel), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjMASKMAP", "bool save(uint16 tileID, bool hFlip = false) const", asMETHOD(jjMASKMAP, save), asCALL_THISCALL);
	}
}

#endif
