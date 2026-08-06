#include "GsTexture.h"
#include "GsDevice.h"

#include "../../../../Main.h"

#include <cstring>

extern "C" {
#include <draw.h>
#include <dma.h>
#include <gs_psm.h>
}

using namespace Death::Containers::Literals;

namespace nCine::RHI::GS
{
	namespace
	{
		/** @brief Words per page - `libgraph`/`libdraw` address local memory in 32-bit words, `GsVram` in pages */
		constexpr std::uint32_t WordsPerPage = GsVram::PageBytes / 4;

		/** @brief Rounds a texture dimension up to the next power of two within the GS's [8, 1024] window */
		std::int32_t NextPow2(std::int32_t value)
		{
			std::int32_t result = 8;
			while (result < value && result < 1024) {
				result <<= 1;
			}
			return result;
		}

		/** @brief Rounds up to a multiple of @p alignment */
		std::int32_t AlignUpInt(std::int32_t value, std::int32_t alignment)
		{
			return (value + alignment - 1) / alignment * alignment;
		}

		/** @brief The hardware `PSM` value of a storage mode */
		std::int32_t PsmToHardware(GsPsm psm)
		{
			switch (psm) {
				case GsPsm::Ct32: return GS_PSM_32;
				case GsPsm::Ct24: return GS_PSM_24;
				case GsPsm::Ct16: return GS_PSM_16;
				case GsPsm::Ct16S: return GS_PSM_16S;
				case GsPsm::T8: return GS_PSM_8;
				case GsPsm::T4: return GS_PSM_4;
				default: return GS_PSM_32;
			}
		}

		/** @brief Bits per texel of a storage mode, for sizing the staging buffer */
		std::int32_t PsmBitsPerTexel(GsPsm psm)
		{
			switch (psm) {
				case GsPsm::Ct32:
				case GsPsm::Ct24: return 32;
				case GsPsm::Ct16:
				case GsPsm::Ct16S: return 16;
				case GsPsm::T8: return 8;
				case GsPsm::T4: return 4;
				default: return 32;
			}
		}

		/**
			@brief Staging buffer the transfers are issued from

			A transfer reads main memory directly, so the source has to be a contiguous, qword-aligned image
			in the destination's pitch - which is what this holds. It persists across uploads (the renderer is
			single-threaded) and only ever grows.
		*/
		alignas(64) SmallVector<std::uint8_t, 0> _staging;

		/**
			@brief GIF packet scratch for the transfers

			Separate from anything the device builds, because a transfer is a DMA **chain** and cannot be
			appended to the device's ordinary register packet. That makes the ORDER between the two the
			caller's problem - see @ref TransferToLocalMemory().
		*/
		alignas(64) qword_t _uploadPacket[512];

		/**
			@brief Transfers @p source into local memory at @p page

			`draw_texture_transfer()` emits DMA **tags** around the image data, so the packet it builds is a
			chain and has to go out with `dma_channel_send_chain()`. Sending it as a normal transfer hands
			those tags to the GIF as if they were pixels and wedges the channel with no error of any kind -
			this is the single most expensive mistake available on this path, so it is spelled out here rather
			than left to the reader of the call site.
		*/
		void TransferToLocalMemory(const void* source, std::int32_t pitchTexels, std::int32_t height,
			GsPsm psm, std::uint32_t page)
		{
			if (source == nullptr || pitchTexels <= 0 || height <= 0 || page == GsVram::InvalidPage) {
				return;
			}

			// The GS consumes GIF data in order, and this transfer goes out on its own channel packet, so
			// anything the device still has queued has to reach the GS FIRST. An upload is triggered from the
			// middle of the draw path whenever a store was evicted and has to be rebuilt - and without this
			// flush the transfer overtakes the draws already queued *behind* it, which then sample the new
			// store instead of the one they were submitted with. At 100% cache occupancy that happens several
			// times a frame, and it is what made the picture flash and sprites take other sprites' pixels.
			GsDevice::FlushPendingPackets();

			qword_t* q = _uploadPacket;
			q = draw_texture_transfer(q, const_cast<void*>(source), pitchTexels, height, PsmToHardware(psm),
				std::int32_t(page * WordsPerPage), pitchTexels);
			q = draw_texture_flush(q);

			dma_channel_send_chain(DMA_CHANNEL_GIF, _uploadPacket, std::int32_t(q - _uploadPacket), 0, 0);
			dma_wait_fast();
		}
	}

	std::uint32_t GsTexture::_nextHandle = 1;
	std::uint32_t GsTexture::_nextContentVersion = 0;
	GsTexture* GsTexture::_liveHead = nullptr;
	GsTexture* GsTexture::_liveTail = nullptr;

	GsTexture::GsTexture(TextureTarget target)
		: _handle(_nextHandle++), _contentVersion(0), _target(target), _format(PixelFormat::Unknown),
			_uploadFormat(PixelFormat::Unknown), _width(0), _height(0), _strideBytes(0), _bytesPerPixel(0),
			_minFilter(nCine::SamplerFilter::Nearest), _magFilter(nCine::SamplerFilter::Nearest),
			_wrap(SamplerWrapping::ClampToEdge), _textureUnit(0), _isRenderTarget(false), _isPaletteTexture(false),
			_page(GsVram::InvalidPage), _pageCount(0), _psm(GsPsm::Ct32), _bufferPitch(0),
			_paddedWidth(0), _paddedHeight(0), _uScale(1.0f), _vScale(1.0f),
			_bakedSlots{}, _nextBakedSlot(0), _livePrev(nullptr), _liveNext(nullptr), _lastUsedFrame(NeverUsed)
	{
		_swizzle[0] = SwizzleChannel::Red;
		_swizzle[1] = SwizzleChannel::Green;
		_swizzle[2] = SwizzleChannel::Blue;
		_swizzle[3] = SwizzleChannel::Alpha;
	}

	GsTexture::~GsTexture()
	{
		GsDevice::UnbindTexture(this);
		FreeStores();
		Unlink();
	}

	// ---- Most-recently-used list ------------------------------------------------------------------

	void GsTexture::Unlink()
	{
		if (_livePrev != nullptr) {
			_livePrev->_liveNext = _liveNext;
		} else if (_liveHead == this) {
			_liveHead = _liveNext;
		}
		if (_liveNext != nullptr) {
			_liveNext->_livePrev = _livePrev;
		} else if (_liveTail == this) {
			_liveTail = _livePrev;
		}
		_livePrev = nullptr;
		_liveNext = nullptr;
	}

	void GsTexture::Touch()
	{
		_lastUsedFrame = GsDevice::GetFrameCounter();
		if (_liveHead == this) {
			return;
		}
		Unlink();
		_liveNext = _liveHead;
		if (_liveHead != nullptr) {
			_liveHead->_livePrev = this;
		}
		_liveHead = this;
		if (_liveTail == nullptr) {
			_liveTail = this;
		}
	}

	void GsTexture::LinkAsLeastRecent()
	{
		if (_livePrev != nullptr || _liveNext != nullptr || _liveHead == this) {
			return;		// Already in the list, keep its position (and its frame stamp)
		}
		_livePrev = _liveTail;
		if (_liveTail != nullptr) {
			_liveTail->_liveNext = this;
		}
		_liveTail = this;
		if (_liveHead == nullptr) {
			_liveHead = this;
		}
	}

	std::uint32_t GsTexture::AllocatePages(std::uint32_t pageCount, const GsTexture* keepAlive)
	{
		if (std::uint32_t result = GsVram::AllocatePages(pageCount); result != GsVram::InvalidPage) {
			return result;
		}

		// Out of local memory: drop the stores of the textures that have gone unused the longest and try
		// again. The first pass spares anything already drawn into the frame being assembled, because the
		// GS may still be reading it - a GIF packet already handed to the DMA controller is not done yet.
		const std::uint32_t currentFrame = GsDevice::GetFrameCounter();
		for (std::int32_t pass = 0; pass < 2; pass++) {
			// Nothing outside the current frame was left to free. Rather than fail the allocation - which
			// happens when a level's working set suddenly has to make room for the pause menu's - the second
			// pass evicts from the current frame too. Primitives already submitted this frame can then sample
			// a store that has been reused, so they can come out wrong; that is a single frame of artifacts
			// against a texture that never appears, and the store is re-transferred on the next draw.
			const bool sparingCurrentFrame = (pass == 0);
			GsTexture* victim = _liveTail;
			while (victim != nullptr) {
				GsTexture* next = victim->_livePrev;
				// Render targets have no copy in main memory to rebuild from, so they are never evicted -
				// and they live in the reserve rather than the cache anyway
				const bool evictable = (victim != keepAlive && !victim->_isRenderTarget &&
					(!sparingCurrentFrame || victim->_lastUsedFrame != currentFrame));
				if (evictable) {
					victim->FreeStores();
					victim->Unlink();

					if (std::uint32_t result = GsVram::AllocatePages(pageCount); result != GsVram::InvalidPage) {
						return result;
					}
				}
				victim = next;
			}
		}

		return GsVram::InvalidPage;
	}

	// ---- Storage layout --------------------------------------------------------------------------

	void GsTexture::ResolveStorage(GsPsm& psm, std::int32_t& bufferPitch, std::uint32_t& pageCount) const
	{
		switch (_uploadFormat) {
			case PixelFormat::R8:
				// Indices sample straight through a CLUT the device selects per draw
				psm = GsPsm::T8;
				break;
			case PixelFormat::RGB565:
				// NOT PSMCT16: the GS's 16-bit colour mode is RGBA**5551**, not RGB565 - five bits of green
				// instead of six, and an alpha bit where green's low bit used to be. Storing 565 texels there
				// verbatim shifts every channel and comes out as a wrong palette, which is exactly how the
				// cinematics looked. The texels are expanded to PSMCT32 on upload instead; the cinematic
				// player is the only user and one full-screen frame is 1120 KB against a 2816 KB cache, so
				// the cost is affordable where the corruption was not.
				psm = GsPsm::Ct32;
				break;
			default:
				// RGB8 / RGBA8, and the RG8 bakes. The GS samples 32-bit natively, so unlike the PowerVR
				// there is no precision loss here; PSMCT16 would halve the cost but its single alpha bit is
				// useless for blended sprite art
				psm = GsPsm::Ct32;
				break;
		}

		// The store only has to COVER the real image, not the sampled extent. `TEX0.TBW` is the pitch and
		// `TW`/`TH` are the sampled power-of-two extent, and they are INDEPENDENT fields - so padding the
		// store to the sampled size as well wasted up to 37% of every texture (a 640x480 sheet was stored as
		// 1024x512, 64 pages where 40 will do). That waste is what put a level's working set over the cache:
		// at 100% occupancy the allocator starts refusing pages, a refused page skips the draw, and whole
		// groups of sprites - the HUD first, since it is drawn last - dropped out on alternate frames.
		//
		// The pitch rounds up to the page width (which is also a multiple of the 64-texel TBW unit) and the
		// height to the page height, because a page is the allocation unit.
		bufferPitch = GsVram::GetPaddedWidth(psm, _width);
		pageCount = GsVram::GetPageCount(psm, bufferPitch, ResolveStoreHeight(psm));
	}

	std::int32_t GsTexture::ResolveStoreHeight(GsPsm psm) const
	{
		std::int32_t pageWidth, pageHeight;
		GsVram::GetPageGeometry(psm, pageWidth, pageHeight);
		return AlignUpInt(_height, pageHeight);
	}

	bool GsTexture::EnsureStore()
	{
		GsPsm psm;
		std::int32_t bufferPitch;
		std::uint32_t pageCount;
		ResolveStorage(psm, bufferPitch, pageCount);

		// A texture does not always end up in the storage mode it was first uploaded in - a true-colour one
		// that turns out to fit a colour table becomes indexed and shrinks fourfold - and writing the new
		// mode into the old allocation would either waste most of it or run past its end
		if (_page != GsVram::InvalidPage && (_psm != psm || _pageCount != pageCount)) {
			GsVram::FreePages(_page, _pageCount);
			_page = GsVram::InvalidPage;
			_pageCount = 0;
		}

		if (_page == GsVram::InvalidPage) {
			_page = AllocatePages(pageCount, this);
			if (_page == GsVram::InvalidPage) {
				return false;
			}
			_pageCount = pageCount;
		}

		_psm = psm;
		_bufferPitch = bufferPitch;
		// The store has to be reachable by the eviction walk immediately, not only from the first draw
		LinkAsLeastRecent();
		return true;
	}

	void GsTexture::RefreshStore()
	{
		if (_pixels.empty() || _width <= 0 || _height <= 0 || _isPaletteTexture || _isRenderTarget) {
			return;
		}

		if (!EnsureStore()) {
			LOGE("Out of GS local memory for a {}x{} texture ({} pages)", _width, _height, _pageCount);
			return;
		}

		const std::int32_t storeHeight = ResolveStoreHeight(_psm);
		const std::int32_t bytesPerTexel = PsmBitsPerTexel(_psm) / 8;

		// Pad the linear image into a staging buffer in the destination's pitch. The GS does the block
		// swizzling on the way in, so the source stays plain raster order - the Dreamcast's twiddling pass
		// has no counterpart here. Only the padding is cleared; the image area is overwritten right after.
		const std::size_t stagingSize = std::size_t(_bufferPitch) * std::size_t(storeHeight) * std::size_t(bytesPerTexel);
		_staging.resize_for_overwrite(stagingSize);
		const std::int32_t dstRowBytes = _bufferPitch * bytesPerTexel;

		if (_uploadFormat == PixelFormat::R8) {
			for (std::int32_t y = 0; y < _height; y++) {
				std::uint8_t* dstRow = _staging.data() + std::size_t(y) * dstRowBytes;
				std::memcpy(dstRow, _pixels.data() + std::size_t(y) * _strideBytes, std::size_t(_width));
				std::memset(dstRow + _width, 0, std::size_t(dstRowBytes - _width));
			}
		} else if (_uploadFormat == PixelFormat::RGB565) {
			// Expand 565 to the GS's PSMCT32; the low bits are replicated so that full-scale input stays
			// full-scale output rather than topping out at 248/252
			for (std::int32_t y = 0; y < _height; y++) {
				std::uint32_t* dstRow = reinterpret_cast<std::uint32_t*>(_staging.data() + std::size_t(y) * dstRowBytes);
				const std::uint16_t* srcRow = reinterpret_cast<const std::uint16_t*>(_pixels.data() + std::size_t(y) * _strideBytes);
				for (std::int32_t x = 0; x < _width; x++) {
					const std::uint32_t texel = srcRow[x];
					const std::uint32_t r5 = (texel >> 11) & 0x1F;
					const std::uint32_t g6 = (texel >> 5) & 0x3F;
					const std::uint32_t b5 = texel & 0x1F;
					const std::uint32_t r = (r5 << 3) | (r5 >> 2);
					const std::uint32_t g = (g6 << 2) | (g6 >> 4);
					const std::uint32_t b = (b5 << 3) | (b5 >> 2);
					// 0x80 is fully opaque in the GS's alpha convention, not 0xFF
					dstRow[x] = r | (g << 8) | (b << 16) | (0x80u << 24);
				}
				std::memset(dstRow + _width, 0, std::size_t(dstRowBytes) - std::size_t(_width) * 4);
			}
		} else {
			// RGB8 / RGBA8 expand to PSMCT32, which is what the GS samples
			const std::int32_t srcBytes = _bytesPerPixel;
			for (std::int32_t y = 0; y < _height; y++) {
				std::uint32_t* dstRow = reinterpret_cast<std::uint32_t*>(_staging.data() + std::size_t(y) * dstRowBytes);
				const std::uint8_t* srcRow = _pixels.data() + std::size_t(y) * _strideBytes;
				for (std::int32_t x = 0; x < _width; x++) {
					const std::uint8_t* t = srcRow + std::size_t(x) * srcBytes;
					// The GS treats 0x80 as fully opaque, so an 8-bit alpha is halved rather than copied -
					// passing 0xFF through would read as "twice opaque" and clamp oddly in blending
					const std::uint32_t a = (srcBytes >= 4 ? (std::uint32_t(t[3]) + 1) >> 1 : 0x80);
					dstRow[x] = std::uint32_t(t[0]) | (std::uint32_t(t[1]) << 8) | (std::uint32_t(t[2]) << 16) | (a << 24);
				}
				std::memset(dstRow + _width, 0, std::size_t(dstRowBytes) - std::size_t(_width) * 4);
			}
		}

		if (_height < storeHeight) {
			std::memset(_staging.data() + std::size_t(_height) * dstRowBytes, 0,
				std::size_t(storeHeight - _height) * std::size_t(dstRowBytes));
		}

		TransferToLocalMemory(_staging.data(), _bufferPitch, storeHeight, _psm, _page);
	}

	std::uint32_t GsTexture::AcquireTexturePage()
	{
		if (_page == GsVram::InvalidPage) {
			RefreshStore();
		}
		if (_page != GsVram::InvalidPage) {
			Touch();
		}
		return _page;
	}

	void GsTexture::FreeStores()
	{
		if (_page != GsVram::InvalidPage) {
			if (_isRenderTarget) {
				GsVram::FreeReservedPages(_page, _pageCount);
			} else {
				GsVram::FreePages(_page, _pageCount);
			}
			_page = GsVram::InvalidPage;
			_pageCount = 0;
		}
		for (std::int32_t i = 0; i < BakedSlotCount; i++) {
			if (_bakedSlots[i].Page != GsVram::InvalidPage) {
				GsVram::FreePages(_bakedSlots[i].Page, _bakedSlots[i].PageCount);
				_bakedSlots[i].Page = GsVram::InvalidPage;
				_bakedSlots[i].PageCount = 0;
			}
			_bakedSlots[i].Valid = false;
		}
	}

	// ---- Allocation and uploads -----------------------------------------------------------------

	std::int32_t GsTexture::BytesPerPixel(PixelFormat format)
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

	void GsTexture::Allocate(PixelFormat format, std::int32_t width, std::int32_t height)
	{
		_uploadFormat = format;
		_format = format;
		_width = width;
		_height = height;
		_bytesPerPixel = BytesPerPixel(_format);
		_strideBytes = width * _bytesPerPixel;
		_pixels.assign(std::size_t(_strideBytes) * std::size_t(height > 0 ? height : 0), std::uint8_t(0));
		FreeStores();

		// `TEX0.TW`/`TH` are log2 fields, so the sampled extent is always a power of two and the texture
		// coordinates are compensated for it - the same as on the PowerVR. Note this is NOT the storage
		// pitch: `TBW` is a separate field, so padding the pitch to the page geometry costs memory but no
		// coordinate scaling at all (see GsVram::GetPaddedWidth).
		_paddedWidth = NextPow2(width);
		_paddedHeight = NextPow2(height);
		_uScale = (_paddedWidth > 0 ? float(width) / float(_paddedWidth) : 1.0f);
		_vScale = (_paddedHeight > 0 ? float(height) / float(_paddedHeight) : 1.0f);
		if (width > 1024 || height > 1024) {
			LOGE("Texture {}x{} exceeds the GS 1024 limit", width, height);
		}
		_contentVersion = ++_nextContentVersion;
	}

	void GsTexture::TexImage2D(std::int32_t level, PixelFormat format, bool bgr, std::int32_t width, std::int32_t height, const void* data)
	{
		static_cast<void>(bgr);
		if (level != 0) {
			return;		// Mip levels above 0 are accepted but not stored
		}

		Allocate(format, width, height);
		if (data != nullptr && RawPixelsSize() > 0) {
			std::memcpy(_pixels.data(), data, std::size_t(RawPixelsSize()));
			_contentVersion = ++_nextContentVersion;
		}
		// The shared palette texture keeps no store of its own - its rows become CLUTs the device loads on
		// demand - so an upload has to invalidate whatever it already loaded from them instead. Without this
		// a level's palette never reached the hardware: the resident CLUTs and RG8 bakes still held the
		// previous screen's colours, which is what made a level inherit the main menu's palette.
		if (_isPaletteTexture) {
			GsDevice::NotifyPaletteTextureChanged(this, 0, _height);
		} else if (data != nullptr && RawPixelsSize() > 0) {
			RefreshStore();
		}
	}

	void GsTexture::TexSubImage2D(std::int32_t level, std::int32_t xoffset, std::int32_t yoffset, std::int32_t width, std::int32_t height, PixelFormat format, bool bgr, const void* data)
	{
		static_cast<void>(format);
		static_cast<void>(bgr);
		if (level != 0 || data == nullptr || _pixels.empty() || width <= 0 || height <= 0) {
			return;
		}
		if (xoffset < 0 || yoffset < 0 || xoffset + width > _width || yoffset + height > _height) {
			return;
		}

		const std::int32_t rowBytes = width * _bytesPerPixel;
		for (std::int32_t y = 0; y < height; y++) {
			std::memcpy(_pixels.data() + std::size_t(yoffset + y) * _strideBytes + std::size_t(xoffset) * _bytesPerPixel,
				static_cast<const std::uint8_t*>(data) + std::size_t(y) * rowBytes, std::size_t(rowBytes));
		}

		_contentVersion = ++_nextContentVersion;
		if (_isPaletteTexture) {
			// Only the rows that changed, so a palette animation does not evict every resident CLUT
			GsDevice::NotifyPaletteTextureChanged(this, yoffset, height);
			return;
		}
		// The whole store is re-transferred rather than the patched rectangle: a partial transfer would have
		// to reproduce the destination's page swizzle to place the sub-rectangle, and the sub-uploads that
		// exist (tileset overrides) happen at load time, not per frame
		RefreshStore();
	}

	void GsTexture::TexStorage2D(std::int32_t levels, PixelFormat format, std::int32_t width, std::int32_t height)
	{
		static_cast<void>(levels);
		Allocate(format, width, height);
	}

	void GsTexture::CompressedTexImage2D(std::int32_t level, PixelFormat format, std::int32_t width, std::int32_t height, std::int32_t imageSize, const void* data)
	{
		static_cast<void>(level);
		static_cast<void>(format);
		static_cast<void>(width);
		static_cast<void>(height);
		static_cast<void>(imageSize);
		static_cast<void>(data);
	}

	void GsTexture::CompressedTexSubImage2D(std::int32_t level, std::int32_t xoffset, std::int32_t yoffset, std::int32_t width, std::int32_t height, PixelFormat format, std::int32_t imageSize, const void* data)
	{
		static_cast<void>(level);
		static_cast<void>(xoffset);
		static_cast<void>(yoffset);
		static_cast<void>(width);
		static_cast<void>(height);
		static_cast<void>(format);
		static_cast<void>(imageSize);
		static_cast<void>(data);
	}

	void GsTexture::GetTexImage(std::int32_t level, PixelFormat format, bool bgr, void* pixels)
	{
		static_cast<void>(format);
		static_cast<void>(bgr);
		if (level != 0 || pixels == nullptr || _pixels.empty()) {
			return;
		}
		std::memcpy(pixels, _pixels.data(), std::size_t(RawPixelsSize()));
	}

	// ---- Render target ---------------------------------------------------------------------------

	void GsTexture::SetRenderTarget(bool isRenderTarget)
	{
		if (_isRenderTarget == isRenderTarget) {
			return;
		}

		FreeStores();
		_isRenderTarget = isRenderTarget;
		if (!_isRenderTarget) {
			return;
		}

		// A render target is a colour surface the rasterizer writes into, so it takes PSMCT16 pages out of
		// the reserve. It has no host copy, which is why it must never be evicted - and why placing it in
		// the reserve rather than the cache turns "it did not fit" into a startup-time configuration error
		// instead of a level-dependent surprise (the Dreamcast's 256x256 TexturedBackground target used to
		// fail its allocation only on certain levels).
		_psm = GsPsm::Ct16;
		_bufferPitch = GsVram::GetPaddedWidth(_psm, _paddedWidth);

		std::int32_t pageWidth, pageHeight;
		GsVram::GetPageGeometry(_psm, pageWidth, pageHeight);
		const std::int32_t storeHeight = AlignUpInt(_paddedHeight, pageHeight);
		_pageCount = GsVram::GetPageCount(_psm, _bufferPitch, storeHeight);

		_page = GsVram::AllocateReservedPages(_pageCount);
		if (_page == GsVram::InvalidPage) {
			LOGE("The GS render-target reserve cannot fit a {}x{} target ({} pages); raise GsVramLayout::ReservedRttPages",
				_paddedWidth, _paddedHeight, _pageCount);
			_pageCount = 0;
		}
	}

	// ---- Per-palette-row bake (RG8) --------------------------------------------------------------

	std::uint32_t GsTexture::EnsureBakedColor(const std::uint32_t* paletteRow, std::uint32_t paletteRowIndex,
		std::uint32_t paletteGeneration, const void* palette)
	{
		if (!NeedsPaletteBake() || paletteRow == nullptr || _pixels.empty() || _width <= 0 || _height <= 0) {
			return GsVram::InvalidPage;
		}

		const std::uint32_t currentFrame = GsDevice::GetFrameCounter();

		// A slot already holding this row with the same palette contents and texel version is a hit
		for (std::int32_t i = 0; i < BakedSlotCount; i++) {
			BakedSlot& slot = _bakedSlots[i];
			if (slot.Valid && slot.Page != GsVram::InvalidPage && slot.PaletteRow == paletteRowIndex &&
				slot.PaletteGeneration == paletteGeneration && slot.ContentVersion == _contentVersion &&
				slot.Palette == palette) {
				slot.LastUsedFrame = currentFrame;
				Touch();
				return slot.Page;
			}
		}

		// Otherwise take the slot that has gone unused the longest, but never one used this frame - the GS
		// may still be reading it for a primitive already submitted
		BakedSlot* target = nullptr;
		for (std::int32_t i = 0; i < BakedSlotCount; i++) {
			BakedSlot& slot = _bakedSlots[i];
			if (slot.LastUsedFrame == currentFrame && slot.Valid) {
				continue;
			}
			if (target == nullptr || slot.LastUsedFrame < target->LastUsedFrame) {
				target = &slot;
			}
		}
		if (target == nullptr) {
			// Every slot is in use this frame; round-robin and accept that the oldest of them may flicker
			target = &_bakedSlots[_nextBakedSlot];
			_nextBakedSlot = (_nextBakedSlot + 1) % BakedSlotCount;
		}

		const std::int32_t bakedPitch = GsVram::GetPaddedWidth(GsPsm::Ct32, _width);
		const std::int32_t storeHeight = ResolveStoreHeight(GsPsm::Ct32);
		const std::uint32_t pageCount = GsVram::GetPageCount(GsPsm::Ct32, bakedPitch, storeHeight);

		if (target->Page != GsVram::InvalidPage && target->PageCount != pageCount) {
			GsVram::FreePages(target->Page, target->PageCount);
			target->Page = GsVram::InvalidPage;
			target->PageCount = 0;
		}
		if (target->Page == GsVram::InvalidPage) {
			target->Page = AllocatePages(pageCount, this);
			if (target->Page == GsVram::InvalidPage) {
				target->Valid = false;
				return GsVram::InvalidPage;
			}
			target->PageCount = pageCount;
		}

		// Resolve every index through the row, keeping the texel's own alpha - the reason this bake exists at
		// all is that a PSMT8 texel could only take its alpha from the CLUT entry
		const std::size_t stagingSize = std::size_t(bakedPitch) * std::size_t(storeHeight) * 4;
		_staging.resize_for_overwrite(stagingSize);
		for (std::int32_t y = 0; y < _height; y++) {
			std::uint32_t* dstRow = reinterpret_cast<std::uint32_t*>(_staging.data() + std::size_t(y) * bakedPitch * 4);
			const std::uint8_t* srcRow = _pixels.data() + std::size_t(y) * _strideBytes;
			for (std::int32_t x = 0; x < _width; x++) {
				const std::uint32_t entry = paletteRow[srcRow[std::size_t(x) * 2]];
				// Halved into the GS's alpha convention, where 0x80 - not 0xFF - is fully opaque, exactly as
				// the RGBA8 path and the CLUT upload do. Copying the byte through instead made every texel at
				// or above half alpha come out fully opaque (the blend's `>> 7` saturates), which is what left
				// the per-pixel-alpha sprites - the glows, the shields, the fades - looking like cutouts.
				const std::uint32_t alpha = (std::uint32_t(srcRow[std::size_t(x) * 2 + 1]) + 1) >> 1;
				dstRow[x] = (entry & 0x00FFFFFFu) | (alpha << 24);
			}
			std::memset(dstRow + _width, 0, (std::size_t(bakedPitch) - std::size_t(_width)) * 4);
		}
		if (_height < storeHeight) {
			std::memset(_staging.data() + std::size_t(_height) * bakedPitch * 4, 0,
				std::size_t(storeHeight - _height) * std::size_t(bakedPitch) * 4);
		}

		TransferToLocalMemory(_staging.data(), bakedPitch, storeHeight, GsPsm::Ct32, target->Page);

		target->Valid = true;
		target->PaletteRow = paletteRowIndex;
		target->PaletteGeneration = paletteGeneration;
		target->ContentVersion = _contentVersion;
		target->Palette = palette;
		target->LastUsedFrame = currentFrame;
		LinkAsLeastRecent();
		Touch();
		return target->Page;
	}

	// ---- Sampler state and binding ---------------------------------------------------------------

	void* GsTexture::MapStreamingTexels(std::int32_t& strideBytes)
	{
		// The GS has no host mapping of its local memory - see the declaration
		strideBytes = 0;
		return nullptr;
	}

	bool GsTexture::Bind(std::uint32_t textureUnit) const
	{
		if (textureUnit >= MaxTextureUnits) {
			return false;
		}
		_textureUnit = textureUnit;
		GsDevice::BindTexture(textureUnit, this);
		return true;
	}

	bool GsTexture::Unbind() const
	{
		return Unbind(_textureUnit);
	}

	bool GsTexture::Unbind(std::uint32_t textureUnit)
	{
		if (textureUnit >= MaxTextureUnits) {
			return false;
		}
		GsDevice::BindTexture(textureUnit, nullptr);
		return true;
	}

	void GsTexture::SetMinFiltering(nCine::SamplerFilter filter)
	{
		_minFilter = filter;
	}

	void GsTexture::SetMagFiltering(nCine::SamplerFilter filter)
	{
		_magFilter = filter;
	}

	void GsTexture::SetWrap(SamplerWrapping wrap)
	{
		_wrap = wrap;
	}

	void GsTexture::SetSwizzle(SwizzleChannel r, SwizzleChannel g, SwizzleChannel b, SwizzleChannel a)
	{
		_swizzle[0] = r;
		_swizzle[1] = g;
		_swizzle[2] = b;
		_swizzle[3] = a;
	}

	void GsTexture::SetMaxLevel(std::int32_t maxLevel)
	{
		static_cast<void>(maxLevel);
	}

	void GsTexture::SetUnpackAlignment(std::int32_t alignment)
	{
		static_cast<void>(alignment);
	}

	void GsTexture::SetObjectLabel(StringView label)
	{
		// The shared palette texture is intercepted by label, exactly as on the PVR and GU backends: its rows
		// become CLUTs loaded on demand, so it keeps no local-memory store of its own
		if (label == "Palettes"_s) {
			_isPaletteTexture = true;
			FreeStores();
			GsDevice::RegisterPaletteTexture(this);
		}
	}
}
