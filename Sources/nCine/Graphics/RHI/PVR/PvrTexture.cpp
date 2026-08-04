#include "PvrTexture.h"
#include "PvrDevice.h"

#include "../../../../Main.h"

#include <cstring>

#include <dc/sq.h>

using namespace Death::Containers::Literals;

namespace nCine::RHI::PVR
{
	namespace
	{
		// Rounds a texture dimension up to the next power of two within the PVR's [8, 1024] window
		std::int32_t NextPow2(std::int32_t value)
		{
			std::int32_t result = 8;
			while (result < value && result < 1024) {
				result <<= 1;
			}
			return result;
		}

		inline std::uint16_t Argb4444FromRgba(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a)
		{
			return std::uint16_t(((a >> 4) << 12) | ((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4));
		}

		// PackBits run-length coding for the host pixel stores. Indexed pixel art is dominated by runs
		// (transparent texels, flat fills), and measured on the stock levels the R8/RG8 stores shrink to
		// 33-42% - about 3 MB less main memory per level, which is what makes the biggest levels (e.g.
		// "secretf/04_haunted1", "xmas99/02_xmas2") fit into the console's 16 MB at all. Header byte n:
		// 0..127 = copy the next n+1 bytes verbatim, 129..255 = repeat the next byte 257-n times; worst
		// case grows by 1 byte per 128 (on data that would not stay compressed anyway - see StorePixels).
		void EncodePackBits(const std::uint8_t* src, std::size_t size, SmallVector<std::uint8_t, 0>& out)
		{
			out.clear();
			// Half is a safe upper estimate for the content that qualifies; growing past it is harmless
			out.reserve(size / 2);

			std::size_t i = 0;
			while (i < size) {
				// A repeat packet only pays off from 3 bytes (2 bytes of output either way at 2,
				// but a run of 2 inside a literal costs nothing extra and avoids a packet break)
				std::size_t run = 1;
				while (i + run < size && run < 128 && src[i + run] == src[i]) {
					run++;
				}
				if (run >= 3) {
					out.push_back(std::uint8_t(257 - run));
					out.push_back(src[i]);
					i += run;
					continue;
				}

				// Literal packet: extend until a worthwhile run starts or the packet is full
				std::size_t literal = run;
				while (i + literal < size && literal < 128) {
					std::size_t nextRun = 1;
					while (i + literal + nextRun < size && nextRun < 3 && src[i + literal + nextRun] == src[i + literal]) {
						nextRun++;
					}
					if (nextRun >= 3) {
						break;
					}
					literal += nextRun;
				}
				if (literal > 128) {
					literal = 128;
				}
				out.push_back(std::uint8_t(literal - 1));
				const std::uint8_t* from = &src[i];
				for (std::size_t b = 0; b < literal; b++) {
					out.push_back(from[b]);
				}
				i += literal;
			}
		}

		// Streaming PackBits decoder: the consumers (VRAM rebuild, palette bake) walk the image strictly
		// front to back one row at a time, so the store never has to be inflated whole - each Read() call
		// hands out the next `count` bytes and keeps the packet state across calls
		struct RlePixelReader
		{
			const std::uint8_t* Src;
			// Remaining bytes of the packet in progress; positive = literal (copied from Src),
			// negative = repeat (of RunValue)
			std::int32_t Pending;
			std::uint8_t RunValue;

			explicit RlePixelReader(const std::uint8_t* src)
				: Src(src), Pending(0), RunValue(0) {}

			void Read(std::uint8_t* DEATH_RESTRICT dst, std::size_t count)
			{
				while (count > 0) {
					if (Pending == 0) {
						std::uint8_t header = *Src++;
						if (header < 128) {
							Pending = std::int32_t(header) + 1;
						} else {
							Pending = -(257 - std::int32_t(header));
							RunValue = *Src++;
						}
					}
					if (Pending > 0) {
						std::size_t n = (std::size_t(Pending) < count ? std::size_t(Pending) : count);
						std::memcpy(dst, Src, n);
						Src += n;
						dst += n;
						Pending -= std::int32_t(n);
						count -= n;
					} else {
						std::size_t n = (std::size_t(-Pending) < count ? std::size_t(-Pending) : count);
						std::memset(dst, RunValue, n);
						dst += n;
						Pending += std::int32_t(n);
						count -= n;
					}
				}
			}
		};

		// Texture memory only supports 16 and 32-bit accesses. libc memcpy is free to fall back to byte
		// writes for unaligned heads and odd tails, which silently corrupts texels on real hardware while
		// emulators tolerate it - so copies into video memory spell their access width out
		void CopyTexelsToVram(std::uint16_t* DEATH_RESTRICT dst, const std::uint16_t* DEATH_RESTRICT src, std::size_t count)
		{
			if (((reinterpret_cast<std::uintptr_t>(dst) | reinterpret_cast<std::uintptr_t>(src)) & 3) == 0) {
				std::uint32_t* DEATH_RESTRICT dst32 = reinterpret_cast<std::uint32_t*>(dst);
				const std::uint32_t* DEATH_RESTRICT src32 = reinterpret_cast<const std::uint32_t*>(src);
				const std::size_t words = count >> 1;
				for (std::size_t i = 0; i < words; i++) {
					dst32[i] = src32[i];
				}
				if ((count & 1) != 0) {
					dst[count - 1] = src[count - 1];
				}
			} else {
				for (std::size_t i = 0; i < count; i++) {
					dst[i] = src[i];
				}
			}
		}
	}

	std::uint32_t PvrTexture::_nextHandle = 1;
	std::uint32_t PvrTexture::_nextContentVersion = 0;

	PvrTexture::PvrTexture(TextureTarget target)
		: _handle(_nextHandle++), _contentVersion(0), _target(target), _format(PixelFormat::Unknown), _uploadFormat(PixelFormat::Unknown),
			_width(0), _height(0), _strideBytes(0), _bytesPerPixel(0),
			_minFilter(nCine::SamplerFilter::Nearest), _magFilter(nCine::SamplerFilter::Nearest), _wrap(SamplerWrapping::ClampToEdge),
			_textureUnit(0), _pixelsCompressed(false), _isRenderTarget(false), _isPaletteTexture(false),
			_vram(nullptr), _vramFormat(0), _vramBytesPerTexel(0), _paddedWidth(0), _paddedHeight(0), _uScale(1.0f), _vScale(1.0f),
			_bakedSlots{}, _nextBakedSlot(0), _livePrev(nullptr), _liveNext(nullptr), _lastUsedScene(NeverUsed)
	{
		_swizzle[0] = SwizzleChannel::Red;
		_swizzle[1] = SwizzleChannel::Green;
		_swizzle[2] = SwizzleChannel::Blue;
		_swizzle[3] = SwizzleChannel::Alpha;
	}

	PvrTexture::~PvrTexture()
	{
		PvrDevice::UnbindTexture(this);
		FreeVramStores();
		Unlink();
	}

	PvrTexture* PvrTexture::_liveHead = nullptr;
	PvrTexture* PvrTexture::_liveTail = nullptr;

	void PvrTexture::Unlink()
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

	void PvrTexture::Touch()
	{
		_lastUsedScene = PvrDevice::GetSceneCounter();
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

	void PvrTexture::LinkAsLeastRecent()
	{
		if (_livePrev != nullptr || _liveNext != nullptr || _liveHead == this) {
			return;		// Already in the list, keep its position (and its scene stamp)
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

	pvr_ptr_t PvrTexture::AllocateVram(std::size_t size, const PvrTexture* keepAlive)
	{
		if (pvr_ptr_t result = pvr_mem_malloc(size)) {
			return result;
		}

		// Out of video memory: drop the stores of the textures that have gone unused the longest and try
		// again. The first pass spares anything already drawn into the scene being assembled, as the tile
		// accelerator still reads those when the scene is submitted.
		const std::uint32_t currentScene = PvrDevice::GetSceneCounter();
		for (std::int32_t pass = 0; pass < 2; pass++) {
			// Nothing outside the current scene was left to free. Rather than fail the allocation - which
			// used to abort the whole frame and take the process with it, as happens when a level's working
			// set suddenly has to make room for the pause menu's - the second pass evicts from the current
			// scene too. The tile accelerator then samples a store that has been reused, so the primitives
			// already submitted this frame can come out wrong; that is a single frame of artifacts against
			// a hard crash, and the store is rebuilt from main memory on the next draw.
			const bool sparingCurrentScene = (pass == 0);
			PvrTexture* victim = _liveTail;
			while (victim != nullptr) {
				PvrTexture* next = victim->_livePrev;
				// Render targets have no copy in main memory to rebuild from, so they are never evicted
				const bool evictable = (victim != keepAlive && !victim->_isRenderTarget &&
					(!sparingCurrentScene || victim->_lastUsedScene != currentScene));
				if (evictable) {
					victim->FreeVramStores();
					victim->Unlink();

					if (pvr_ptr_t result = pvr_mem_malloc(size)) {
						return result;
					}
				}
				victim = next;
			}
		}

		return nullptr;
	}

	pvr_ptr_t PvrTexture::AcquireVramPointer()
	{
		if (_vram == nullptr) {
			RefreshVramStore();
		}
		if (_vram != nullptr) {
			Touch();
		}
		return _vram;
	}

	void PvrTexture::FreeVramStores()
	{
		if (_vram != nullptr) {
			pvr_mem_free(_vram);
			_vram = nullptr;
			_vramBytesPerTexel = 0;
		}
		for (std::int32_t i = 0; i < BakedSlotCount; i++) {
			if (_bakedSlots[i].Vram != nullptr) {
				pvr_mem_free(_bakedSlots[i].Vram);
				_bakedSlots[i].Vram = nullptr;
			}
			_bakedSlots[i].Valid = false;
		}
	}

	std::int32_t PvrTexture::BytesPerPixel(PixelFormat format)
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

	void PvrTexture::Allocate(PixelFormat format, std::int32_t width, std::int32_t height)
	{
		_uploadFormat = format;
		_format = format;
		_width = width;
		_height = height;
		_bytesPerPixel = BytesPerPixel(_format);
		_strideBytes = width * _bytesPerPixel;
		if (CanCompressPixels()) {
			// The compressible formats defer their host store to the first upload (StorePixels), so the
			// full-size linear buffer never exists for them - not even between allocation and upload
			_pixels.clear();
		} else {
			_pixels.assign(std::size_t(_strideBytes) * std::size_t(height > 0 ? height : 0), std::uint8_t(0));
		}
		_pixelsCompressed = false;
		FreeVramStores();
		_paddedWidth = NextPow2(width);
		_paddedHeight = NextPow2(height);
		_uScale = (_paddedWidth > 0 ? float(width) / float(_paddedWidth) : 1.0f);
		_vScale = (_paddedHeight > 0 ? float(height) / float(_paddedHeight) : 1.0f);
		if (width > 1024 || height > 1024) {
			LOGE("Texture {}x{} exceeds the PowerVR 1024 limit", width, height);
		}
		_contentVersion = ++_nextContentVersion;
	}

	bool PvrTexture::CanCompressPixels() const
	{
		// Only static indexed content: R8 (paletted tiles/sprites) and RG8 (index + alpha). Their host
		// stores are consumed strictly sequentially (RefreshVramStore, EnsureBakedArgb4444), so they can
		// be streamed out of the compressed form. RGB565 belongs to the streaming path (cinematics) that
		// rewrites the store every frame, RGBA8 covers the palette texture (read in place by the device)
		// and the few baked sheets, and render targets have no host content to keep at all. The width
		// limit matches both the bake path's fixed row scratch and the widest texture the PVR can sample.
		return (_uploadFormat == PixelFormat::R8 || _uploadFormat == PixelFormat::RG8) &&
			!_isPaletteTexture && !_isRenderTarget && _width <= 1024;
	}

	void PvrTexture::StorePixels(const std::uint8_t* data)
	{
		const std::size_t rawSize = std::size_t(RawPixelsSize());
		if (rawSize == 0) {
			return;
		}
		if (CanCompressPixels()) {
			EncodePackBits(data, rawSize, _pixels);
			if (_pixels.size() < rawSize) {
				_pixelsCompressed = true;
			} else {
				// Content that does not compress is kept linear, so the read paths skip the decoder
				_pixels.assign(data, data + rawSize);
				_pixelsCompressed = false;
			}
		} else {
			_pixels.assign(data, data + rawSize);
			_pixelsCompressed = false;
		}
	}

	void PvrTexture::MaterializePixelsRaw()
	{
		const std::size_t rawSize = std::size_t(RawPixelsSize());
		if (rawSize == 0) {
			return;
		}
		if (_pixelsCompressed) {
			SmallVector<std::uint8_t, 0> raw;
			raw.resize_for_overwrite(rawSize);
			RlePixelReader reader(_pixels.data());
			reader.Read(raw.data(), rawSize);
			_pixels = std::move(raw);
			_pixelsCompressed = false;
		} else if (_pixels.empty()) {
			// A compressible texture defers its store until the first upload; a partial first upload
			// gets the zero-filled base the uncompressed formats always had
			_pixels.assign(rawSize, std::uint8_t(0));
		}
	}

	void PvrTexture::RecompressPixels()
	{
		const std::size_t rawSize = std::size_t(RawPixelsSize());
		if (!CanCompressPixels() || _pixelsCompressed || _pixels.size() != rawSize || rawSize == 0) {
			return;
		}
		SmallVector<std::uint8_t, 0> compressed;
		EncodePackBits(_pixels.data(), rawSize, compressed);
		if (compressed.size() < rawSize) {
			_pixels = std::move(compressed);
			_pixelsCompressed = true;
		}
	}

	bool PvrTexture::EnsureVramStore(std::int32_t bytesPerTexel)
	{
		if (_vram != nullptr && _vramBytesPerTexel != bytesPerTexel) {
			pvr_mem_free(_vram);
			_vram = nullptr;
		}
		if (_vram == nullptr) {
			_vram = AllocateVram(std::size_t(_paddedWidth) * std::size_t(_paddedHeight) * std::size_t(bytesPerTexel), this);
			if (_vram == nullptr) {
				return false;
			}
		}
		_vramBytesPerTexel = bytesPerTexel;
		// The store has to be reachable by the eviction walk immediately, not only from the first draw
		LinkAsLeastRecent();
		return true;
	}

	void* PvrTexture::MapStreamingTexels(std::int32_t& strideBytes)
	{
		if (_uploadFormat != PixelFormat::RGB565 || _isRenderTarget || _isPaletteTexture) {
			return nullptr;
		}

		// The store is allocated on demand and can be reclaimed when video memory runs short, so it is asked
		// for again every frame rather than remembered
		pvr_ptr_t vram = AcquireVramPointer();
		if (vram == nullptr) {
			return nullptr;
		}

		strideBytes = _paddedWidth * 2;
		return vram;
	}

	void PvrTexture::RefreshVramStore()
	{
		if (_pixels.empty() || _width <= 0 || _height <= 0 || _isPaletteTexture || _isRenderTarget) {
			return;
		}

		if (_uploadFormat == PixelFormat::R8) {
			// 8bpp paletted, twiddled; the palette bank is or-ed into the format word per draw
			const std::size_t size = std::size_t(_paddedWidth) * std::size_t(_paddedHeight);
			if (!EnsureVramStore(1)) {
				LOGE("Out of PVR memory allocating {} B (8bpp {}x{}, source {}x{})", size, _paddedWidth, _paddedHeight, _width, _height);
				return;
			}
			// Pad the linear image into a power-of-two staging buffer, then let the loader twiddle it.
			// The buffer persists across uploads (the renderer is single-threaded), and only the padding
			// is zeroed - the image area is overwritten right after. A compressed store streams its rows
			// through the decoder (it is walked strictly front to back), so it is never inflated whole.
			static SmallVector<std::uint8_t, 0> staging;
			staging.resize_for_overwrite(size);
			RlePixelReader reader(_pixels.data());
			for (std::int32_t y = 0; y < _height; y++) {
				if (_pixelsCompressed) {
					reader.Read(staging.data() + std::size_t(y) * _paddedWidth, std::size_t(_width));
				} else {
					std::memcpy(staging.data() + std::size_t(y) * _paddedWidth, _pixels.data() + std::size_t(y) * _strideBytes, std::size_t(_width));
				}
				std::memset(staging.data() + std::size_t(y) * _paddedWidth + _width, 0, std::size_t(_paddedWidth - _width));
			}
			if (_height < _paddedHeight) {
				std::memset(staging.data() + std::size_t(_height) * _paddedWidth, 0, std::size_t(_paddedHeight - _height) * _paddedWidth);
			}
			pvr_txr_load_ex(staging.data(), _vram, std::uint32_t(_paddedWidth), std::uint32_t(_paddedHeight), PVR_TXRLOAD_8BPP);
			_vramFormat = PVR_TXRFMT_PAL8BPP | PVR_TXRFMT_TWIDDLED;
		} else if (_uploadFormat == PixelFormat::RGB565) {
			// Already in a format the hardware samples directly, so the rows go straight into video memory -
			// no conversion and no twiddling. This is the cheap path for textures that are replaced every
			// frame (the cinematic player), where a twiddle would cost more than the whole upload.
			const std::size_t size = std::size_t(_paddedWidth) * std::size_t(_paddedHeight) * 2;
			if (!EnsureVramStore(2)) {
				LOGE("Out of PVR memory allocating {} B (RGB565 {}x{})", size, _paddedWidth, _paddedHeight);
				return;
			}

			std::uint16_t* dst = static_cast<std::uint16_t*>(_vram);
			const std::uint16_t* src = reinterpret_cast<const std::uint16_t*>(_pixels.data());
			if (_paddedWidth == _width) {
				CopyTexelsToVram(dst, src, std::size_t(_width) * _height);
			} else {
				for (std::int32_t y = 0; y < _height; y++) {
					CopyTexelsToVram(dst + std::size_t(y) * _paddedWidth, src + std::size_t(y) * (_strideBytes / 2),
						std::size_t(_width));
				}
			}
			_vramFormat = PVR_TXRFMT_RGB565 | PVR_TXRFMT_NONTWIDDLED;
		} else if (_uploadFormat == PixelFormat::RGB8 || _uploadFormat == PixelFormat::RGBA8) {
			// True-color converts to twiddled ARGB4444 (the PVR has no 32-bit sampled format)
			const std::size_t size = std::size_t(_paddedWidth) * std::size_t(_paddedHeight) * 2;
			if (!EnsureVramStore(2)) {
				LOGE("Out of PVR memory allocating {} B (ARGB4444 {}x{})", size, _paddedWidth, _paddedHeight);
				return;
			}
			// Converted without the twiddling pass: interleaving the texel order costs more than the
			// conversion itself, and it only pays off for the sampling patterns of 3D geometry - these
			// are axis-aligned sprite blits. Each row is converted into a small cached scratch line and
			// then burst out through the store queues: texture memory is uncached, and the per-texel
			// 16-bit stores made an eviction rebuild several times more expensive than the conversion.
			// Only rows narrower than one 32-byte block (padded width 8) keep the direct stores; the
			// padded width never exceeds 1024 (see NextPow2).
			std::uint16_t* dst = static_cast<std::uint16_t*>(_vram);
			alignas(32) static std::uint16_t scratchRow[1024];
			const std::size_t rowBytes = std::size_t(_paddedWidth) * 2;
			const bool useStoreQueues = ((rowBytes & 31) == 0);
			if (useStoreQueues) {
				sq_lock(dst);
			}
			for (std::int32_t y = 0; y < _height; y++) {
				const std::uint8_t* DEATH_RESTRICT src = _pixels.data() + std::size_t(y) * _strideBytes;
				std::uint16_t* DEATH_RESTRICT row = (useStoreQueues ? scratchRow : dst + std::size_t(y) * _paddedWidth);
				if (_bytesPerPixel >= 4) {
					for (std::int32_t x = 0; x < _width; x++) {
						row[x] = Argb4444FromRgba(src[0], src[1], src[2], src[3]);
						src += 4;
					}
				} else {
					for (std::int32_t x = 0; x < _width; x++) {
						row[x] = Argb4444FromRgba(src[0], src[1], src[2], 255);
						src += _bytesPerPixel;
					}
				}
				// The padding columns are only sampled through the compensated texture coordinates, but
				// leaving them uninitialised would show up as fringing on the last texel
				for (std::int32_t x = _width; x < _paddedWidth; x++) {
					row[x] = 0;
				}
				if (useStoreQueues) {
					sq_fast_cpy(SQ_MASK_DEST(dst + std::size_t(y) * _paddedWidth), scratchRow, rowBytes / 32);
				}
			}
			if (useStoreQueues) {
				// One zeroed line serves every padding row below the image
				for (std::int32_t x = 0; x < _paddedWidth; x++) {
					scratchRow[x] = 0;
				}
				for (std::int32_t y = _height; y < _paddedHeight; y++) {
					sq_fast_cpy(SQ_MASK_DEST(dst + std::size_t(y) * _paddedWidth), scratchRow, rowBytes / 32);
				}
				sq_unlock();
			} else {
				for (std::int32_t y = _height; y < _paddedHeight; y++) {
					// Word stores - video memory only takes 16/32-bit accesses (padded width is even)
					std::uint32_t* DEATH_RESTRICT fill = reinterpret_cast<std::uint32_t*>(dst + std::size_t(y) * _paddedWidth);
					for (std::int32_t x = 0, n = _paddedWidth / 2; x < n; x++) {
						fill[x] = 0;
					}
				}
			}
			_vramFormat = PVR_TXRFMT_ARGB4444 | PVR_TXRFMT_NONTWIDDLED;
		}
		// RG8 keeps only the linear store; the ARGB4444 copy is baked per palette row on demand
	}

	pvr_ptr_t PvrTexture::EnsureBakedArgb4444(const std::uint32_t* paletteRow, std::uint32_t paletteRowIndex, std::uint32_t paletteGeneration, const void* palette)
	{
		if (_pixels.empty() || _uploadFormat != PixelFormat::RG8 || paletteRow == nullptr) {
			return nullptr;
		}
		const std::uint32_t currentScene = PvrDevice::GetSceneCounter();

		// Bakes that have gone unused for a while give their video memory back proactively instead of
		// waiting for an allocation failure to evict them - each one costs as much as the texture itself.
		// Far older than anything the tile accelerator could still read.
		constexpr std::uint32_t ReclaimAfterScenes = 300;
		for (std::int32_t i = 0; i < BakedSlotCount; i++) {
			if (_bakedSlots[i].Vram != nullptr && currentScene - _bakedSlots[i].LastUsedScene > ReclaimAfterScenes) {
				pvr_mem_free(_bakedSlots[i].Vram);
				_bakedSlots[i].Vram = nullptr;
				_bakedSlots[i].Valid = false;
			}
		}

		BakedSlot* slot = nullptr;
		for (std::int32_t i = 0; i < BakedSlotCount; i++) {
			if (_bakedSlots[i].Valid && _bakedSlots[i].PaletteRow == paletteRowIndex && _bakedSlots[i].Palette == palette) {
				if (_bakedSlots[i].PaletteGeneration == paletteGeneration && _bakedSlots[i].ContentVersion == _contentVersion) {
					_bakedSlots[i].LastUsedScene = currentScene;
					Touch();
					return _bakedSlots[i].Vram;
				}
				slot = &_bakedSlots[i];	// Stale bake of the same row, refresh it in place
				break;
			}
		}
		if (slot == nullptr) {
			for (std::int32_t i = 0; i < BakedSlotCount; i++) {
				if (!_bakedSlots[i].Valid) {
					slot = &_bakedSlots[i];
					break;
				}
			}
		}
		if (slot == nullptr) {
			// Never evict a bake the current scene still references - the tile accelerator reads the
			// textures only at scene end, so overwriting one would corrupt the already submitted quads
			for (std::int32_t i = 0; i < BakedSlotCount; i++) {
				const std::int32_t candidate = (_nextBakedSlot + i) % BakedSlotCount;
				if (_bakedSlots[candidate].LastUsedScene != currentScene) {
					slot = &_bakedSlots[candidate];
					_nextBakedSlot = (candidate + 1) % BakedSlotCount;
					break;
				}
			}
		}
		if (slot == nullptr) {
			static bool warnedSlots = false;
			if (!warnedSlots) {
				warnedSlots = true;
				LOGW("More than {} palette rows used with one texture in a single scene, expect glitches", BakedSlotCount);
			}
			slot = &_bakedSlots[_nextBakedSlot];
			_nextBakedSlot = (_nextBakedSlot + 1) % BakedSlotCount;
		}

		const std::size_t size = std::size_t(_paddedWidth) * std::size_t(_paddedHeight) * 2;
		if (slot->Vram == nullptr) {
			slot->Vram = AllocateVram(size, this);
			if (slot->Vram == nullptr) {
				LOGE("Out of PVR memory allocating {} B (baked ARGB4444 {}x{})", size, _paddedWidth, _paddedHeight);
				return nullptr;
			}
		}

		// The 256 palette entries collapse into packed 12-bit RGB once per bake, so the texel loop below
		// is one table lookup and an alpha merge instead of unpacking and requantizing every pixel
		std::uint16_t rgb444[256];
		for (std::int32_t i = 0; i < 256; i++) {
			const std::uint32_t color = paletteRow[i];
			rgb444[i] = std::uint16_t(((( color        & 0xFF) >> 4) << 8)
				| ((((color >> 8)  & 0xFF) >> 4) << 4)
				|  (((color >> 16) & 0xFF) >> 4));
		}

		// The staging buffer persists across bakes (the renderer is single-threaded); only the padding
		// is zeroed, the image area is overwritten right after. A compressed store is streamed row by
		// row through a fixed scratch line - the bake walks the image strictly front to back, so the
		// linear form never has to exist in full.
		static SmallVector<std::uint16_t, 0> staging;
		staging.resize_for_overwrite(std::size_t(_paddedWidth) * std::size_t(_paddedHeight));
		// Widest supported source row: 1024 texels of 2 bytes (see NextPow2's 1024 clamp)
		static std::uint8_t rowScratch[1024 * 2];
		RlePixelReader reader(_pixels.data());
		for (std::int32_t y = 0; y < _height; y++) {
			const std::uint8_t* DEATH_RESTRICT src;
			if (_pixelsCompressed) {
				reader.Read(rowScratch, std::size_t(_strideBytes));
				src = rowScratch;
			} else {
				src = _pixels.data() + std::size_t(y) * _strideBytes;
			}
			std::uint16_t* DEATH_RESTRICT dst = staging.data() + std::size_t(y) * _paddedWidth;
			for (std::int32_t x = 0; x < _width; x++) {
				dst[x] = std::uint16_t(rgb444[src[x * 2]] | ((src[x * 2 + 1] >> 4) << 12));
			}
			std::memset(dst + _width, 0, std::size_t(_paddedWidth - _width) * 2);
		}
		if (_height < _paddedHeight) {
			std::memset(staging.data() + std::size_t(_height) * _paddedWidth, 0,
				std::size_t(_paddedHeight - _height) * _paddedWidth * 2);
		}
		pvr_txr_load_ex(staging.data(), slot->Vram, std::uint32_t(_paddedWidth), std::uint32_t(_paddedHeight), PVR_TXRLOAD_16BPP);

		slot->Valid = true;
		slot->PaletteRow = paletteRowIndex;
		slot->PaletteGeneration = paletteGeneration;
		slot->ContentVersion = _contentVersion;
		slot->LastUsedScene = currentScene;
		slot->Palette = palette;
		Touch();
		return slot->Vram;
	}

	void PvrTexture::SetRenderTarget(bool isRenderTarget)
	{
		_isRenderTarget = isRenderTarget;
		if (isRenderTarget && _width > 0 && _height > 0) {
			// The tile accelerator renders into a non-twiddled RGB565 surface of power-of-two width
			const std::size_t size = std::size_t(_paddedWidth) * std::size_t(_paddedHeight) * 2;
			if (!EnsureVramStore(2)) {
				LOGE("Out of PVR memory allocating {} B (RTT {}x{})", size, _paddedWidth, _paddedHeight);
				return;
			}
			_vramFormat = PVR_TXRFMT_RGB565 | PVR_TXRFMT_NONTWIDDLED;
		}
	}

	bool PvrTexture::Bind(std::uint32_t textureUnit) const
	{
		_textureUnit = textureUnit;
		PvrDevice::BindTexture(textureUnit, this);
		return true;
	}

	bool PvrTexture::Unbind() const
	{
		PvrDevice::BindTexture(_textureUnit, nullptr);
		return true;
	}

	bool PvrTexture::Unbind(std::uint32_t textureUnit)
	{
		PvrDevice::BindTexture(textureUnit, nullptr);
		return true;
	}

	void PvrTexture::TexImage2D(std::int32_t level, PixelFormat format, bool bgr, std::int32_t width, std::int32_t height, const void* data)
	{
		static_cast<void>(bgr);
		if (level != 0) {
			return;		// Level 0 only
		}
		Allocate(format, width, height);
		if (data != nullptr && RawPixelsSize() > 0) {
			// Full upload - the store is (re)built straight from the caller's buffer, compressed when
			// the format qualifies, so the linear copy never exists on this side
			StorePixels(static_cast<const std::uint8_t*>(data));
			_contentVersion = ++_nextContentVersion;
		}
		if (_isPaletteTexture) {
			PvrDevice::NotifyPaletteTextureChanged(this, 0, _height);
		} else {
			RefreshVramStore();
		}
	}

	void PvrTexture::TexSubImage2D(std::int32_t level, std::int32_t xoffset, std::int32_t yoffset, std::int32_t width, std::int32_t height, PixelFormat format, bool bgr, const void* data)
	{
		static_cast<void>(bgr);
		if (level != 0 || data == nullptr || RawPixelsSize() <= 0) {
			return;
		}
		const std::int32_t srcBpp = BytesPerPixel(format);
		const std::int32_t dstBpp = _bytesPerPixel;
		const std::int32_t copyBpp = (srcBpp < dstBpp ? srcBpp : dstBpp);

		if (xoffset == 0 && yoffset == 0 && width == _width && height == _height && srcBpp == dstBpp) {
			// Full replacement (the common upload path goes TexStorage2D + full-rect TexSubImage2D):
			// skip the patch loop and rebuild the store from the caller's buffer directly, compressed
			// when the format qualifies - the full-size linear copy never exists on this side
			StorePixels(static_cast<const std::uint8_t*>(data));
		} else {
			// Partial patch (tileset overrides): the only writer that needs the store linear. Rare and
			// load-time only, so the compressed form is inflated for the patch and compressed again after
			MaterializePixelsRaw();
			for (std::int32_t y = 0; y < height; y++) {
				const std::int32_t dstY = yoffset + y;
				if (dstY < 0 || dstY >= _height) {
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
				if (dstX + copyW > _width) {
					copyW = _width - dstX;
				}
				if (copyW <= 0) {
					continue;
				}
				const std::uint8_t* srcRow = static_cast<const std::uint8_t*>(data) + std::size_t(y) * std::size_t(width) * srcBpp + std::size_t(srcX0) * srcBpp;
				std::uint8_t* dstRow = _pixels.data() + std::size_t(dstY) * _strideBytes + std::size_t(dstX) * dstBpp;
				if (srcBpp == dstBpp) {
					std::memcpy(dstRow, srcRow, std::size_t(copyW) * dstBpp);
				} else {
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
			RecompressPixels();
		}
		_contentVersion = ++_nextContentVersion;
		if (_isPaletteTexture) {
			PvrDevice::NotifyPaletteTextureChanged(this, yoffset, height);
		} else {
			// v1: any sub-update re-uploads the whole level (sub-updates are rare - tileset overrides and
			// the palette, which is intercepted above)
			RefreshVramStore();
		}
	}

	void PvrTexture::TexStorage2D(std::int32_t levels, PixelFormat format, std::int32_t width, std::int32_t height)
	{
		static_cast<void>(levels);
		Allocate(format, width, height);
		if (_isRenderTarget) {
			SetRenderTarget(true);
		}
	}

	void PvrTexture::CompressedTexImage2D(std::int32_t level, PixelFormat format, std::int32_t width, std::int32_t height, std::int32_t imageSize, const void* data)
	{
		static_cast<void>(level); static_cast<void>(format); static_cast<void>(width);
		static_cast<void>(height); static_cast<void>(imageSize); static_cast<void>(data);
	}

	void PvrTexture::CompressedTexSubImage2D(std::int32_t level, std::int32_t xoffset, std::int32_t yoffset, std::int32_t width, std::int32_t height, PixelFormat format, std::int32_t imageSize, const void* data)
	{
		static_cast<void>(level); static_cast<void>(xoffset); static_cast<void>(yoffset); static_cast<void>(width);
		static_cast<void>(height); static_cast<void>(format); static_cast<void>(imageSize); static_cast<void>(data);
	}

	void PvrTexture::GetTexImage(std::int32_t level, PixelFormat format, bool bgr, void* pixels)
	{
		static_cast<void>(level);
		static_cast<void>(format);
		static_cast<void>(bgr);
		if (pixels != nullptr && !_pixels.empty()) {
			if (_pixelsCompressed) {
				RlePixelReader reader(_pixels.data());
				reader.Read(static_cast<std::uint8_t*>(pixels), std::size_t(RawPixelsSize()));
			} else {
				std::memcpy(pixels, _pixels.data(), _pixels.size());
			}
		}
	}

	void PvrTexture::SetMinFiltering(nCine::SamplerFilter filter)
	{
		_minFilter = filter;
	}

	void PvrTexture::SetMagFiltering(nCine::SamplerFilter filter)
	{
		_magFilter = filter;
	}

	void PvrTexture::SetWrap(SamplerWrapping wrap)
	{
		_wrap = wrap;
	}

	void PvrTexture::SetSwizzle(SwizzleChannel r, SwizzleChannel g, SwizzleChannel b, SwizzleChannel a)
	{
		_swizzle[0] = r;
		_swizzle[1] = g;
		_swizzle[2] = b;
		_swizzle[3] = a;
	}

	void PvrTexture::SetMaxLevel(std::int32_t maxLevel)
	{
		static_cast<void>(maxLevel);
	}

	void PvrTexture::SetUnpackAlignment(std::int32_t alignment)
	{
		static_cast<void>(alignment);
	}

	void PvrTexture::SetObjectLabel(StringView label)
	{
		// The shared palette texture is uploaded by ContentResolver under this exact name; its rows are
		// loaded into the hardware palette banks by the device instead of a sampled texture
		if (label == "Palettes"_s) {
			_isPaletteTexture = true;
			FreeVramStores();
			PvrDevice::RegisterPaletteTexture(this);
		}
	}
}
