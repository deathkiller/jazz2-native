#include "GuTexture.h"
#include "GuDevice.h"
#include "../../../../Main.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <malloc.h>

#include <pspge.h>
#include <pspgu.h>
#include <psputils.h>

using namespace Death::Containers::Literals;

namespace nCine::RHI::GU
{
	namespace
	{
		// Rounds a texture dimension up to the next power of two the GE can address. The lower bound is 8
		// rather than 1 so that every store is wide enough for one 16-byte swizzle block and tall enough for
		// one block row (see SwizzlePageInPlace), which keeps the padding rules uniform.
		std::int32_t NextPow2(std::int32_t value)
		{
			std::int32_t result = 8;
			while (result < value && result < GuTexture::MaxPageDimension) {
				result <<= 1;
			}
			return result;
		}

		// GU_PSM_4444 keeps red in the LOW nibble (the GE's 16-bit formats are little-endian with red first),
		// which is the opposite end from the PowerVR's ARGB4444
		inline std::uint16_t Abgr4444FromRgba(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a)
		{
			return std::uint16_t(((a >> 4) << 12) | ((b >> 4) << 8) | ((g >> 4) << 4) | (r >> 4));
		}

		// The engine packs RGB565 with red in the HIGH bits (see Cinematics' palette conversion), while
		// GU_PSM_5650 puts red in the low five - so the two outer channels trade places
		inline std::uint16_t Bgr565FromEngine565(std::uint16_t value)
		{
			return std::uint16_t(((value & 0xF800u) >> 11) | (value & 0x07E0u) | ((value & 0x001Fu) << 11));
		}

		/**
			@brief Rearranges one page into the GE's texture block interleave, in place

			The GE reads a swizzled texture as consecutive blocks of 16 bytes by 8 rows, block rows running
			top to bottom - which turns the 2D neighbourhood a sprite blit samples into a contiguous run and
			is measurably cheaper than the linear layout. The transform maps each block row onto exactly the
			bytes that block row occupied, so one block row of scratch is enough to do it without a second
			full-size buffer.
		*/
		void SwizzlePageInPlace(std::uint8_t* data, std::int32_t widthBytes, std::int32_t height)
		{
			// Widest supported page is 512 texels of 4 bytes; a block row is 8 of those
			alignas(16) static std::uint8_t blockRow[8 * GuTexture::MaxPageDimension * 4];
			const std::int32_t blocksX = widthBytes / 16;
			const std::int32_t blockRows = height / 8;
			for (std::int32_t by = 0; by < blockRows; by++) {
				std::uint8_t* rows = data + std::size_t(by) * 8 * widthBytes;
				std::memcpy(blockRow, rows, std::size_t(8) * std::size_t(widthBytes));
				std::uint8_t* dst = rows;
				for (std::int32_t bx = 0; bx < blocksX; bx++) {
					for (std::int32_t r = 0; r < 8; r++) {
						std::memcpy(dst, blockRow + std::size_t(r) * std::size_t(widthBytes) + std::size_t(bx) * 16, 16);
						dst += 16;
					}
				}
			}
		}
	}

	std::uint32_t GuTexture::nextHandle_ = 0;
	std::uint32_t GuTexture::nextContentVersion_ = 0;

	GuTexture::GuTexture(TextureTarget target)
		: handle_(++nextHandle_), contentVersion_(0), target_(target), format_(PixelFormat::Unknown),
			uploadFormat_(PixelFormat::Unknown), width_(0), height_(0), strideBytes_(0), bytesPerPixel_(0),
			minFilter_(nCine::SamplerFilter::Nearest), magFilter_(nCine::SamplerFilter::Nearest),
			wrap_(SamplerWrapping::ClampToEdge), textureUnit_(0), isRenderTarget_(false), isPaletteTexture_(false),
			geStore_(nullptr), geStoreSize_(0), guFormat_(-1), geBytesPerTexel_(0), pagesX_(0), pagesY_(0),
			geStoreValid_(false), bakedStores_{}, activeBakedStore_(-1), nextBakedStore_(0), uploadCount_(0),
			streamingSwapPending_(false), renderTargetSurface_(nullptr), renderTargetStride_(0),
			renderTargetSurfaceInVram_(false)
	{
		swizzle_[0] = SwizzleChannel::Red;
		swizzle_[1] = SwizzleChannel::Green;
		swizzle_[2] = SwizzleChannel::Blue;
		swizzle_[3] = SwizzleChannel::Alpha;
	}

	GuTexture::~GuTexture()
	{
		// Clear from every unit so a destroyed texture can't dangle in the device's bind tracking, and make
		// sure the GE is not still reading a store that is about to be freed
		GuDevice::UnbindTexture(this);
		FreeGeStores();
		FreeRenderTargetSurface();
	}

	std::int32_t GuTexture::BytesPerPixel(PixelFormat format)
	{
		switch (format) {
			case PixelFormat::R8: return 1;
			case PixelFormat::RG8: return 2;
			case PixelFormat::RGB565: return 2;
			case PixelFormat::RGB8: return 3;
			case PixelFormat::RGBA8: return 4;
			default: return 0;
		}
	}

	void GuTexture::Allocate(PixelFormat format, std::int32_t width, std::int32_t height)
	{
		uploadFormat_ = format;
		format_ = format;
		width_ = width;
		height_ = height;
		bytesPerPixel_ = BytesPerPixel(format_);
		strideBytes_ = width * bytesPerPixel_;
		pixels_.assign(std::size_t(strideBytes_) * std::size_t(height > 0 ? height : 0), std::uint8_t(0));
		uploadCount_ = 0;
		streamingSwapPending_ = false;
		FreeGeStores();
		FreeRenderTargetSurface();
		PlanGeStore();
		contentVersion_ = ++nextContentVersion_;
	}

	void GuTexture::PlanGeStore()
	{
		pages_.clear();
		pagesX_ = 0;
		pagesY_ = 0;
		geBytesPerTexel_ = 0;
		guFormat_ = -1;
		if (width_ <= 0 || height_ <= 0) {
			return;
		}

		switch (uploadFormat_) {
			case PixelFormat::R8:
				// Palette indices are exactly what GU_PSM_T8 samples; the CLUT is loaded per draw
				guFormat_ = GU_PSM_T8;
				geBytesPerTexel_ = 1;
				break;
			case PixelFormat::RG8:
			case PixelFormat::RGB8:
			case PixelFormat::RGBA8:
				// A paletted texel's alpha comes from the CLUT entry, so index+alpha has no hardware form and
				// is baked per palette row; true colour converts to the same 16-bit format (halving both the
				// store and the sampling bandwidth over 8888, which is what the PowerVR settles on too)
				guFormat_ = GU_PSM_4444;
				geBytesPerTexel_ = 2;
				break;
			case PixelFormat::RGB565:
				guFormat_ = GU_PSM_5650;
				geBytesPerTexel_ = 2;
				break;
			default:
				return;
		}
		if (isRenderTarget_) {
			// The GE renders into a 16-bit surface matching the display format
			guFormat_ = GU_PSM_5650;
			geBytesPerTexel_ = 2;
		}

		pagesX_ = (width_ + MaxPageDimension - 1) / MaxPageDimension;
		pagesY_ = (height_ + MaxPageDimension - 1) / MaxPageDimension;
		pages_.reserve(std::size_t(pagesX_) * std::size_t(pagesY_));
		for (std::int32_t py = 0; py < pagesY_; py++) {
			for (std::int32_t px = 0; px < pagesX_; px++) {
				Page page;
				page.OriginX = px * MaxPageDimension;
				page.OriginY = py * MaxPageDimension;
				page.Width = std::min(MaxPageDimension, width_ - page.OriginX);
				page.Height = std::min(MaxPageDimension, height_ - page.OriginY);
				page.PaddedWidth = NextPow2(page.Width);
				// The GE addresses texture rows in 16-byte blocks, so a row has to be at least one block
				// wide - which for an 8bpp store means 16 texels rather than the 8 NextPow2() floors at
				while (page.PaddedWidth * geBytesPerTexel_ < 16) {
					page.PaddedWidth <<= 1;
				}
				page.PaddedHeight = NextPow2(page.Height);
				pages_.push_back(page);
			}
		}
		if (pagesX_ > 1 || pagesY_ > 1) {
			LOGI("Texture {}x{} split into {}x{} GE pages", width_, height_, pagesX_, pagesY_);
		}
	}

	void GuTexture::InvalidateGeStore()
	{
		geStoreValid_ = false;
		activeBakedStore_ = -1;
		for (std::int32_t i = 0; i < BakedStoreCount; i++) {
			bakedStores_[i].Valid = false;
		}
	}

	void GuTexture::FreeGeStores()
	{
		if (geStore_ != nullptr) {
			std::free(geStore_);
			geStore_ = nullptr;
			geStoreSize_ = 0;
		}
		for (std::int32_t i = 0; i < BakedStoreCount; i++) {
			if (bakedStores_[i].Data != nullptr) {
				std::free(bakedStores_[i].Data);
				bakedStores_[i].Data = nullptr;
			}
			bakedStores_[i].Valid = false;
		}
		activeBakedStore_ = -1;
		nextBakedStore_ = 0;
		geStoreValid_ = false;
	}

	void GuTexture::FreeRenderTargetSurface()
	{
		if (renderTargetSurface_ != nullptr) {
			if (renderTargetSurfaceInVram_) {
				GuDevice::FreeVram(renderTargetSurface_);
			} else {
				std::free(renderTargetSurface_);
			}
			renderTargetSurface_ = nullptr;
			renderTargetStride_ = 0;
			renderTargetSurfaceInVram_ = false;
		}
	}

	void GuTexture::BuildPage(const Page& page, std::uint8_t* dst, bool swizzle)
	{
		const std::int32_t rowBytes = page.PaddedWidth * geBytesPerTexel_;
		const std::uint8_t* const hostBase = pixels_.data() + std::size_t(page.OriginY) * strideBytes_
			+ std::size_t(page.OriginX) * bytesPerPixel_;

		for (std::int32_t y = 0; y < page.Height; y++) {
			const std::uint8_t* DEATH_RESTRICT src = hostBase + std::size_t(y) * strideBytes_;
			std::uint8_t* DEATH_RESTRICT row = dst + std::size_t(y) * rowBytes;
			switch (uploadFormat_) {
				case PixelFormat::R8:
					std::memcpy(row, src, std::size_t(page.Width));
					break;
				case PixelFormat::RGB565: {
					std::uint16_t* DEATH_RESTRICT out = reinterpret_cast<std::uint16_t*>(row);
					const std::uint16_t* DEATH_RESTRICT in = reinterpret_cast<const std::uint16_t*>(src);
					for (std::int32_t x = 0; x < page.Width; x++) {
						out[x] = Bgr565FromEngine565(in[x]);
					}
					break;
				}
				case PixelFormat::RGB8:
				case PixelFormat::RGBA8: {
					std::uint16_t* DEATH_RESTRICT out = reinterpret_cast<std::uint16_t*>(row);
					if (bytesPerPixel_ >= 4) {
						for (std::int32_t x = 0; x < page.Width; x++) {
							out[x] = Abgr4444FromRgba(src[0], src[1], src[2], src[3]);
							src += 4;
						}
					} else {
						for (std::int32_t x = 0; x < page.Width; x++) {
							out[x] = Abgr4444FromRgba(src[0], src[1], src[2], 255);
							src += bytesPerPixel_;
						}
					}
					break;
				}
				default:
					break;
			}
			// The padding columns are only ever reached by a bilinear tap at the very last texel, but leaving
			// them uninitialised would show up as fringing there
			std::memset(row + std::size_t(page.Width) * geBytesPerTexel_, 0,
				std::size_t(page.PaddedWidth - page.Width) * geBytesPerTexel_);
		}
		if (page.Height < page.PaddedHeight) {
			std::memset(dst + std::size_t(page.Height) * rowBytes, 0,
				std::size_t(page.PaddedHeight - page.Height) * rowBytes);
		}
		if (swizzle) {
			SwizzlePageInPlace(dst, rowBytes, page.PaddedHeight);
		}
	}

	void GuTexture::BuildBakedPage(const Page& page, std::uint8_t* dst, const std::uint16_t* rgb444, bool swizzle)
	{
		const std::int32_t rowBytes = page.PaddedWidth * geBytesPerTexel_;
		const std::uint8_t* const hostBase = pixels_.data() + std::size_t(page.OriginY) * strideBytes_
			+ std::size_t(page.OriginX) * bytesPerPixel_;

		for (std::int32_t y = 0; y < page.Height; y++) {
			const std::uint8_t* DEATH_RESTRICT src = hostBase + std::size_t(y) * strideBytes_;
			std::uint16_t* DEATH_RESTRICT out = reinterpret_cast<std::uint16_t*>(dst + std::size_t(y) * rowBytes);
			for (std::int32_t x = 0; x < page.Width; x++) {
				// Colour from the palette row the index points at, coverage from the texel's own alpha byte
				out[x] = std::uint16_t(rgb444[src[x * 2]] | std::uint16_t((src[x * 2 + 1] >> 4) << 12));
			}
			std::memset(out + page.Width, 0, std::size_t(page.PaddedWidth - page.Width) * 2);
		}
		if (page.Height < page.PaddedHeight) {
			std::memset(dst + std::size_t(page.Height) * rowBytes, 0,
				std::size_t(page.PaddedHeight - page.Height) * rowBytes);
		}
		if (swizzle) {
			SwizzlePageInPlace(dst, rowBytes, page.PaddedHeight);
		}
	}

	bool GuTexture::RefreshGeStore()
	{
		if (pages_.empty() || geBytesPerTexel_ <= 0 || pixels_.empty() || isPaletteTexture_) {
			return false;
		}
		// RG8 has no store of its own - the bake owns it (see EnsureBakedStore)
		if (uploadFormat_ == PixelFormat::RG8) {
			return false;
		}

		std::size_t size = 0;
		for (const Page& page : pages_) {
			size += std::size_t(page.PaddedWidth) * std::size_t(page.PaddedHeight) * std::size_t(geBytesPerTexel_);
		}
		if (geStore_ == nullptr || geStoreSize_ != size) {
			if (geStore_ != nullptr) {
				std::free(geStore_);
			}
			// The GE fetches texels through physical addresses and expects the base to be quad-word aligned
			geStore_ = static_cast<std::uint8_t*>(memalign(16, size));
			geStoreSize_ = (geStore_ != nullptr ? size : 0);
			if (geStore_ == nullptr) {
				LOGE("Out of memory allocating a {} B GE store for a {}x{} texture", size, width_, height_);
				return false;
			}
		}

		// Content that is replaced again after its first upload is streamed (the cinematic frames), and
		// interleaving it every frame would cost more than the sampling saves
		std::size_t offset = 0;
		for (Page& page : pages_) {
			const std::int32_t rowBytes = page.PaddedWidth * geBytesPerTexel_;
			const bool swizzle = (uploadCount_ <= 1 && uploadFormat_ != PixelFormat::RGB565 &&
				(rowBytes % 16) == 0 && (page.PaddedHeight % 8) == 0);
			page.Data = geStore_ + offset;
			page.Swizzled = swizzle;
			BuildPage(page, geStore_ + offset, swizzle);
			offset += std::size_t(rowBytes) * std::size_t(page.PaddedHeight);
		}

		// The GE reads main memory without looking at the data cache
		sceKernelDcacheWritebackRange(geStore_, geStoreSize_);
		geStoreValid_ = true;
		return true;
	}

	bool GuTexture::EnsureBakedStore(const std::uint32_t* paletteRow, std::uint32_t paletteRowIndex,
		std::uint32_t paletteGeneration, const void* palette)
	{
		if (uploadFormat_ != PixelFormat::RG8 || paletteRow == nullptr || pixels_.empty() || pages_.empty()) {
			return false;
		}

		// A bake that is already the active store needs nothing at all, which is the case for every draw of
		// the frame once the sprite's palette row has been seen once
		for (std::int32_t i = 0; i < BakedStoreCount; i++) {
			const BakedStore& store = bakedStores_[i];
			if (store.Valid && store.PaletteRow == paletteRowIndex && store.Palette == palette &&
				store.PaletteGeneration == paletteGeneration && store.ContentVersion == contentVersion_) {
				if (activeBakedStore_ != i) {
					activeBakedStore_ = i;
					std::size_t offset = 0;
					for (Page& page : pages_) {
						page.Data = store.Data + offset;
						offset += std::size_t(page.PaddedWidth) * std::size_t(page.PaddedHeight) * 2;
					}
				}
				geStoreValid_ = true;
				return true;
			}
		}

		std::size_t size = 0;
		for (const Page& page : pages_) {
			size += std::size_t(page.PaddedWidth) * std::size_t(page.PaddedHeight) * 2;
		}

		// Reuse a stale entry of the same row, then any free one, then the round-robin victim
		std::int32_t slot = -1;
		for (std::int32_t i = 0; i < BakedStoreCount && slot < 0; i++) {
			if (bakedStores_[i].Valid && bakedStores_[i].PaletteRow == paletteRowIndex &&
				bakedStores_[i].Palette == palette) {
				slot = i;
			}
		}
		for (std::int32_t i = 0; i < BakedStoreCount && slot < 0; i++) {
			if (!bakedStores_[i].Valid) {
				slot = i;
			}
		}
		if (slot < 0) {
			slot = nextBakedStore_;
			nextBakedStore_ = (nextBakedStore_ + 1) % BakedStoreCount;
			static bool warnedSlots = false;
			if (!warnedSlots) {
				warnedSlots = true;
				LOGW("More than {} palette rows used with one indexed+alpha texture, expect rebuild churn", BakedStoreCount);
			}
		}

		BakedStore& store = bakedStores_[slot];
		if (store.Data == nullptr) {
			store.Data = static_cast<std::uint8_t*>(memalign(16, size));
			if (store.Data == nullptr) {
				LOGE("Out of memory allocating a {} B baked store for a {}x{} texture", size, width_, height_);
				return false;
			}
		}

		// The 256 palette entries collapse into packed 12-bit BGR once per bake, so the texel loop is one
		// lookup and an alpha merge instead of unpacking and requantizing every pixel
		std::uint16_t rgb444[256];
		for (std::int32_t i = 0; i < 256; i++) {
			const std::uint32_t color = paletteRow[i];
			rgb444[i] = std::uint16_t(((((color >> 16) & 0xFF) >> 4) << 8)
				| ((((color >> 8) & 0xFF) >> 4) << 4)
				|   (((color) & 0xFF) >> 4));
		}

		std::size_t offset = 0;
		for (Page& page : pages_) {
			const std::int32_t rowBytes = page.PaddedWidth * 2;
			const bool swizzle = (uploadCount_ <= 1 && (rowBytes % 16) == 0 && (page.PaddedHeight % 8) == 0);
			page.Data = store.Data + offset;
			page.Swizzled = swizzle;
			BuildBakedPage(page, store.Data + offset, rgb444, swizzle);
			offset += std::size_t(rowBytes) * std::size_t(page.PaddedHeight);
		}
		sceKernelDcacheWritebackRange(store.Data, size);

		store.Valid = true;
		store.PaletteRow = paletteRowIndex;
		store.PaletteGeneration = paletteGeneration;
		store.ContentVersion = contentVersion_;
		store.Palette = palette;
		activeBakedStore_ = slot;
		geStoreValid_ = true;
		return true;
	}

	const GuTexture::Page* GuTexture::AcquirePage(std::int32_t texelX, std::int32_t texelY)
	{
		if (pages_.empty()) {
			return nullptr;
		}
		if (isRenderTarget_) {
			// The GE wrote the surface itself; there is nothing to convert
			return (renderTargetSurface_ != nullptr ? &pages_[0] : nullptr);
		}
		if (streamingSwapPending_) {
			// The cinematic path wrote engine-order RGB565 straight into the store, so the two outer
			// channels are traded in place once - a single pass over the frame, where a separate host copy
			// would have cost a conversion AND a copy
			streamingSwapPending_ = false;
			Page& page = pages_[0];
			std::uint16_t* const base = reinterpret_cast<std::uint16_t*>(geStore_);
			for (std::int32_t y = 0; y < page.Height; y++) {
				std::uint16_t* DEATH_RESTRICT row = base + std::size_t(y) * page.PaddedWidth;
				for (std::int32_t x = 0; x < page.Width; x++) {
					row[x] = Bgr565FromEngine565(row[x]);
				}
			}
			sceKernelDcacheWritebackRange(geStore_, geStoreSize_);
		}
		if (!geStoreValid_ && !RefreshGeStore()) {
			return nullptr;
		}

		const std::int32_t px = (texelX < 0 ? 0 : (texelX >= width_ ? width_ - 1 : texelX)) / MaxPageDimension;
		const std::int32_t py = (texelY < 0 ? 0 : (texelY >= height_ ? height_ - 1 : texelY)) / MaxPageDimension;
		return &pages_[std::size_t(py) * std::size_t(pagesX_) + std::size_t(px)];
	}

	void* GuTexture::MapStreamingTexels(std::int32_t& strideBytes)
	{
		// Only the format the hardware samples verbatim, and only while the whole image fits one page (a
		// paged store has no single linear pitch the caller could write through)
		if (uploadFormat_ != PixelFormat::RGB565 || isRenderTarget_ || isPaletteTexture_ ||
			pages_.size() != 1 || width_ <= 0 || height_ <= 0) {
			return nullptr;
		}
		if (geStore_ == nullptr) {
			// The very first frame allocates the store through the ordinary rebuild
			uploadCount_ = 2;	// Streaming content is never swizzled - it is rewritten every frame
			if (!RefreshGeStore()) {
				return nullptr;
			}
		}

		strideBytes = pages_[0].PaddedWidth * 2;
		streamingSwapPending_ = true;
		geStoreValid_ = true;
		contentVersion_ = ++nextContentVersion_;
		return geStore_;
	}

	void GuTexture::SetRenderTarget(bool isRenderTarget)
	{
		if (isRenderTarget_ == isRenderTarget) {
			return;
		}
		isRenderTarget_ = isRenderTarget;
		FreeGeStores();
		FreeRenderTargetSurface();
		PlanGeStore();
		if (!isRenderTarget || width_ <= 0 || height_ <= 0) {
			return;
		}
		if (pages_.size() != 1) {
			LOGE("A {}x{} render target cannot be sampled as one GE texture (limit is {})", width_, height_,
				MaxPageDimension);
			return;
		}

		// The GE writes into a 16-bit surface whose pitch is the padded width. Video memory is worth using
		// here - unlike for sprite atlases, the target is written by the rasterizer every frame - but it is
		// only 2 MB with the display buffers already in it, so main memory is an accepted fallback.
		Page& page = pages_[0];
		const std::size_t size = std::size_t(page.PaddedWidth) * std::size_t(page.PaddedHeight) * 2;
		renderTargetSurface_ = GuDevice::AllocateVram(size);
		renderTargetSurfaceInVram_ = (renderTargetSurface_ != nullptr);
		if (renderTargetSurface_ == nullptr) {
			renderTargetSurface_ = memalign(16, size);
			if (renderTargetSurface_ == nullptr) {
				LOGE("Out of memory allocating a {} B render-target surface ({}x{})", size, width_, height_);
				return;
			}
			LOGW("Render target {}x{} did not fit video memory and renders into main memory", width_, height_);
		}
		// A render target has no host content worth keeping - the rasterizer owns every texel - and its
		// linear store would be another quarter of a megabyte of main memory for nothing
		pixels_ = {};

		renderTargetStride_ = page.PaddedWidth;
		// The two GE registers this surface reaches do NOT take the same form of address. The draw-buffer
		// family (sceGuDrawBufferList) takes an offset RELATIVE to the start of video memory - which is
		// exactly what AllocateVram() hands out, and why `sceGuDrawBuffer(psm, 0, stride)` means "the front
		// of VRAM" - while sceGuTexImage takes an ABSOLUTE address, because a texture may just as well live
		// in main memory. Sampling the relative offset would point the texture unit at low memory instead of
		// the surface, so the page keeps the absolute form (the main-memory fallback below is already
		// absolute and needs no base).
		void* absoluteSurface = (renderTargetSurfaceInVram_
			? static_cast<std::uint8_t*>(sceGeEdramGetAddr()) + std::uintptr_t(renderTargetSurface_)
			: static_cast<std::uint8_t*>(renderTargetSurface_));
		page.Data = absoluteSurface;
		page.Swizzled = false;
		geStoreValid_ = true;

		std::memset(absoluteSurface, 0, size);
		if (!renderTargetSurfaceInVram_) {
			sceKernelDcacheWritebackRange(absoluteSurface, size);
		}
	}

	bool GuTexture::Bind(std::uint32_t textureUnit) const
	{
		textureUnit_ = textureUnit;
		GuDevice::BindTexture(textureUnit, this);
		return true;
	}

	bool GuTexture::Unbind() const
	{
		GuDevice::BindTexture(textureUnit_, nullptr);
		return true;
	}

	bool GuTexture::Unbind(std::uint32_t textureUnit)
	{
		GuDevice::BindTexture(textureUnit, nullptr);
		return true;
	}

	void GuTexture::TexImage2D(std::int32_t level, PixelFormat format, bool bgr, std::int32_t width, std::int32_t height, const void* data)
	{
		static_cast<void>(bgr);
		if (level != 0) {
			return;		// Level 0 only
		}
		Allocate(format, width, height);
		if (data != nullptr && !pixels_.empty()) {
			std::memcpy(pixels_.data(), data, pixels_.size());
			contentVersion_ = ++nextContentVersion_;
			uploadCount_++;
		}
		if (isPaletteTexture_) {
			GuDevice::NotifyPaletteTextureChanged(this, 0, height_);
		} else {
			InvalidateGeStore();
		}
	}

	void GuTexture::TexSubImage2D(std::int32_t level, std::int32_t xoffset, std::int32_t yoffset, std::int32_t width, std::int32_t height, PixelFormat format, bool bgr, const void* data)
	{
		static_cast<void>(bgr);
		if (level != 0 || data == nullptr || pixels_.empty()) {
			return;
		}
		const std::int32_t srcBpp = BytesPerPixel(format);
		const std::int32_t dstBpp = bytesPerPixel_;
		const std::int32_t copyBpp = (srcBpp < dstBpp ? srcBpp : dstBpp);

		for (std::int32_t y = 0; y < height; y++) {
			const std::int32_t dstY = yoffset + y;
			if (dstY < 0 || dstY >= height_) {
				continue;
			}
			std::int32_t dstX = xoffset;
			std::int32_t copyW = width;
			std::int32_t srcX0 = 0;
			if (dstX < 0) {
				srcX0 = -dstX;
				copyW += dstX;
				dstX = 0;
			}
			if (dstX + copyW > width_) {
				copyW = width_ - dstX;
			}
			if (copyW <= 0) {
				continue;
			}
			const std::uint8_t* srcRow = static_cast<const std::uint8_t*>(data) + std::size_t(y) * std::size_t(width) * srcBpp + std::size_t(srcX0) * srcBpp;
			std::uint8_t* dstRow = pixels_.data() + std::size_t(dstY) * strideBytes_ + std::size_t(dstX) * dstBpp;
			if (srcBpp == dstBpp) {
				std::memcpy(dstRow, srcRow, std::size_t(copyW) * dstBpp);
			} else {
				// A narrower source fills the channels it has and opaque-fills the rest, which is what the
				// RGB8-into-RGBA8 uploads the loaders produce need
				for (std::int32_t x = 0; x < copyW; x++) {
					std::int32_t c = 0;
					for (; c < copyBpp; c++) {
						dstRow[x * dstBpp + c] = srcRow[x * srcBpp + c];
					}
					for (; c < dstBpp; c++) {
						dstRow[x * dstBpp + c] = 255;
					}
				}
			}
		}
		contentVersion_ = ++nextContentVersion_;
		if (xoffset == 0 && yoffset == 0 && width == width_ && height == height_) {
			uploadCount_++;
		}
		if (isPaletteTexture_) {
			GuDevice::NotifyPaletteTextureChanged(this, yoffset, height);
		} else {
			// v1: any sub-update rebuilds the whole GE store on the next draw (sub-updates are rare - tileset
			// overrides at load time, and the palette texture, which is intercepted above)
			InvalidateGeStore();
		}
	}

	void GuTexture::TexStorage2D(std::int32_t levels, PixelFormat format, std::int32_t width, std::int32_t height)
	{
		static_cast<void>(levels);
		const bool wasRenderTarget = isRenderTarget_;
		Allocate(format, width, height);
		if (wasRenderTarget) {
			// Allocate() dropped the surface with the old size; re-attach one for the new one
			isRenderTarget_ = false;
			SetRenderTarget(true);
		}
	}

	void GuTexture::CompressedTexImage2D(std::int32_t level, PixelFormat format, std::int32_t width, std::int32_t height, std::int32_t imageSize, const void* data)
	{
		static_cast<void>(level); static_cast<void>(format); static_cast<void>(width);
		static_cast<void>(height); static_cast<void>(imageSize); static_cast<void>(data);
	}

	void GuTexture::CompressedTexSubImage2D(std::int32_t level, std::int32_t xoffset, std::int32_t yoffset, std::int32_t width, std::int32_t height, PixelFormat format, std::int32_t imageSize, const void* data)
	{
		static_cast<void>(level); static_cast<void>(xoffset); static_cast<void>(yoffset); static_cast<void>(width);
		static_cast<void>(height); static_cast<void>(format); static_cast<void>(imageSize); static_cast<void>(data);
	}

	void GuTexture::GetTexImage(std::int32_t level, PixelFormat format, bool bgr, void* pixels)
	{
		static_cast<void>(level);
		static_cast<void>(format);
		static_cast<void>(bgr);
		if (pixels != nullptr && !pixels_.empty()) {
			std::memcpy(pixels, pixels_.data(), pixels_.size());
		}
	}

	void GuTexture::SetMinFiltering(nCine::SamplerFilter filter)
	{
		minFilter_ = filter;
	}

	void GuTexture::SetMagFiltering(nCine::SamplerFilter filter)
	{
		magFilter_ = filter;
	}

	void GuTexture::SetWrap(SamplerWrapping wrap)
	{
		wrap_ = wrap;
	}

	void GuTexture::SetSwizzle(SwizzleChannel r, SwizzleChannel g, SwizzleChannel b, SwizzleChannel a)
	{
		swizzle_[0] = r;
		swizzle_[1] = g;
		swizzle_[2] = b;
		swizzle_[3] = a;
	}

	void GuTexture::SetMaxLevel(std::int32_t maxLevel)
	{
		static_cast<void>(maxLevel);
	}

	void GuTexture::SetUnpackAlignment(std::int32_t alignment)
	{
		static_cast<void>(alignment);
	}

	void GuTexture::SetObjectLabel(StringView label)
	{
		// The shared palette texture is uploaded by ContentResolver under this exact name; its rows become
		// hardware CLUTs loaded per draw instead of a sampled texture, so it keeps only the host store
		if (label == "Palettes"_s) {
			isPaletteTexture_ = true;
			FreeGeStores();
			GuDevice::RegisterPaletteTexture(this);
		}
	}
}
