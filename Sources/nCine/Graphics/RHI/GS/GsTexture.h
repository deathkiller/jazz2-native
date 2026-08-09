#pragma once

#include "../RhiTypes.h"
#include "GsVram.h"

#include <cstddef>
#include <cstdint>

#include <Containers/SmallVector.h>
#include <Containers/StringView.h>

using namespace Death::Containers;

namespace nCine::RHI::GS
{
	/**
		@brief Texture object of the GS backend (aliased as `RHI::Texture`)

		Keeps a LINEAR host store of level 0 in the uploaded pixel format (the same native R8/RG8/RGBA8 layout
		the software and PVR backends use) plus a residency in the Graphics Synthesizer's local memory, placed
		by @ref GsVram in whole pages:
		- R8 index textures become `PSMT8`, with the colours coming from a CLUT the device selects per draw
		  out of the CLUT slab (from the instance's `palOffset`) - the GS resolves the lookup in the texture
		  read, so indices upload as they are.
		- RG8 (index + per-pixel alpha) has no paletted GS equivalent, because a `PSMT8` texel's alpha comes
		  from its CLUT entry. Those keep only the host store and the device asks for a per-palette-row CPU
		  bake to `PSMCT32` (@ref EnsureBakedColor), cached until the row or the texel content changes.
		- RGB8 / RGBA8 upload as `PSMCT32`. Unlike the PowerVR - which has no 32-bit sampled format and had to
		  convert to ARGB4444 - the GS sampples 32-bit directly, so these keep full precision. `PSMCT16` would
		  halve the footprint but its alpha is a single bit, which is useless for blended sprite art, and
		  true-colour sheets are few once the content is indexed.
		- The shared 256x256 palette texture (labelled "Palettes") is intercepted: it keeps only the host
		  store and the device loads its rows into CLUT slots on demand.

		Render targets take a `PSMCT16` surface out of the layout's render-target reserve rather than the
		streaming cache, and are never evicted - there is no host copy to rebuild one from.

		@section GsTexture-residency Residency

		The host store is the source of truth and local memory is a cache, exactly as on the Dreamcast: when
		@ref GsVram runs out of pages the least recently used stores are dropped (see @ref AllocatePages()),
		and a texture drawn again afterwards is re-uploaded from its host copy by @ref AcquireTexturePage().
		Unlike the PVR backend the host stores are **not** run-length compressed: a rebuild here is a plain
		DMA out of main memory (the GS does the block swizzling itself), so keeping the copy in the layout the
		transfer wants makes a rebuild cost almost no CPU at all. The Dreamcast traded CPU time for main
		memory because it only had 16 MB and had to twiddle in software; with 32 MB and a DMA engine the trade
		goes the other way.

		Mip levels above 0 and compressed formats are accepted but not stored.
	*/
	class GsTexture
	{
	public:
		/** @brief Number of texture units tracked by the device */
		static constexpr std::uint32_t MaxTextureUnits = 8;
		/** @brief Number of per-palette-row baked copies kept for RG8 textures */
		static constexpr std::int32_t BakedSlotCount = 3;

		explicit GsTexture(TextureTarget target);
		~GsTexture();

		GsTexture(const GsTexture&) = delete;
		GsTexture& operator=(const GsTexture&) = delete;

		/** @brief Returns a backend-neutral identifier uniquely identifying the texture (feeds material sort keys) */
		inline std::uint32_t GetUniqueId() const {
			return _handle;
		}
		/** @brief Returns the texture target */
		inline TextureTarget GetTarget() const {
			return _target;
		}

		/** @brief Returns the width of level 0 in texels */
		inline std::int32_t GetWidth() const {
			return _width;
		}
		/** @brief Returns the height of level 0 in texels */
		inline std::int32_t GetHeight() const {
			return _height;
		}
		/** @brief Returns the pixel format of the linear host store */
		inline PixelFormat GetFormat() const {
			return _format;
		}
		/** @brief Returns the original upload format (R8/RG8 kept so the palette path can tell them apart) */
		inline PixelFormat GetUploadFormat() const {
			return _uploadFormat;
		}
		/** @brief Returns the byte distance between two consecutive rows of the linear host store */
		inline std::int32_t GetStrideBytes() const {
			return _strideBytes;
		}
		/** @brief Returns the base pointer of the linear host store (may be `nullptr` before an upload) */
		inline const std::uint8_t* GetPixels(std::int32_t level = 0) const {
			static_cast<void>(level);
			return (_pixels.empty() ? nullptr : _pixels.data());
		}
		/** @brief Returns a writable base pointer of the linear host store */
		inline std::uint8_t* MutablePixels() {
			return (_pixels.empty() ? nullptr : _pixels.data());
		}
		/** @brief Returns the horizontal texture-coordinate wrap mode */
		inline SamplerWrapping GetWrapS() const {
			return _wrap;
		}
		/** @brief Returns the vertical texture-coordinate wrap mode (single stored mode) */
		inline SamplerWrapping GetWrapT() const {
			return _wrap;
		}
		/** @brief Returns the four-channel sampling swizzle (identity by default; informational on the GS) */
		inline const SwizzleChannel* GetSwizzle() const {
			return _swizzle;
		}
		/** @brief Returns the magnification filter */
		inline nCine::SamplerFilter GetMagFiltering() const {
			return _magFilter;
		}
		/** @brief Alias of @ref GetMagFiltering() */
		inline nCine::SamplerFilter GetMagFilter() const {
			return _magFilter;
		}
		/** @brief Returns the minification filter */
		inline nCine::SamplerFilter GetMinFiltering() const {
			return _minFilter;
		}
		/** @brief Returns `true` if the texture is bound as a color render target */
		inline bool IsRenderTarget() const {
			return _isRenderTarget;
		}
		/** @brief Marks the texture as (or no longer as) a color render target; becoming one allocates its surface */
		void SetRenderTarget(bool isRenderTarget);
		/** @brief Returns a globally monotonic stamp of the texel store, advanced by every allocation or upload */
		inline std::uint32_t GetContentVersion() const {
			return _contentVersion;
		}

		/** @brief Returns `true` when the store is `PSMT8` (draws need a CLUT selected) */
		inline bool IsIndexed() const {
			return (_uploadFormat == PixelFormat::R8);
		}
		/** @brief Returns `true` when the texture needs the per-palette-row CPU bake (RG8 index + alpha) */
		inline bool NeedsPaletteBake() const {
			return (_uploadFormat == PixelFormat::RG8);
		}
		/** @brief Returns `true` when this is the intercepted shared palette texture (rows become CLUTs) */
		inline bool IsPaletteTexture() const {
			return _isPaletteTexture;
		}

		/** @brief Returns the first page of the local-memory store, or @ref GsVram::InvalidPage when none exists */
		inline std::uint32_t GetTexturePage() const {
			return _page;
		}
		/**
			@brief Returns the first page of the store, re-uploading it if it was reclaimed

			Should be used by the draw path: running out of local memory drops the least recently used stores
			(see @ref AllocatePages()), and a texture that is drawn again afterwards has to be transferred
			from its main-memory copy once more.
		*/
		std::uint32_t AcquireTexturePage();
		/** @brief Returns the pixel storage mode the local-memory store was uploaded in */
		inline GsPsm GetPsm() const {
			return _psm;
		}
		/** @brief Returns the store's buffer pitch in TEXELS, which is what `libdraw` wants in `texbuffer_t::width` */
		inline std::int32_t GetBufferPitch() const {
			return _bufferPitch;
		}
		/** @brief Returns the power-of-two sampled width (`TEX0.TW` is a log2 field) */
		inline std::int32_t GetPaddedWidth() const {
			return _paddedWidth;
		}
		/** @brief Returns the power-of-two sampled height (`TEX0.TH` is a log2 field) */
		inline std::int32_t GetPaddedHeight() const {
			return _paddedHeight;
		}
		/** @brief Returns the U compensation factor for the power-of-two sampled extent (`realW / paddedW`) */
		inline float GetUScale() const {
			return _uScale;
		}
		/** @brief Returns the V compensation factor for the power-of-two sampled extent (`realH / paddedH`) */
		inline float GetVScale() const {
			return _vScale;
		}

		/**
			@brief Returns the page of a `PSMCT32` copy of an RG8 store baked through one palette row

			Analogous to the PVR backend's ARGB4444 bake: the index is resolved through @p paletteRow (256
			RGBA8 entries) and the alpha comes from the texel's own alpha byte. One cached copy per slot,
			rebuilt when the row, generation or content changes. Returns @ref GsVram::InvalidPage on failure.
		*/
		std::uint32_t EnsureBakedColor(const std::uint32_t* paletteRow, std::uint32_t paletteRowIndex,
			std::uint32_t paletteGeneration, const void* palette);
		/** @brief Returns the buffer pitch in texels of the baked copies (they share the store's sampled extent) */
		inline std::int32_t GetBakedBufferPitch() const {
			return _paddedWidth;
		}

		/**
			@brief Always returns `nullptr` on the GS - local memory cannot be written by the CPU

			The PVR and GU backends answer this for content that is rebuilt every frame (the cinematics), so
			a frame can be produced straight where it will be sampled. The Graphics Synthesizer has no host
			mapping of its local memory at all: everything arrives through a GIF transfer, so there is no
			pointer to hand out. `RHI_CAP_STREAMING_TEXTURES` is deliberately left undefined for this backend
			so that no caller reaches here in the first place.
		*/
		void* MapStreamingTexels(std::int32_t& strideBytes);

		/** @brief Binds the texture to the specified texture unit on the device */
		bool Bind(std::uint32_t textureUnit) const;
		/** @brief Binds the texture to texture unit 0 */
		inline bool Bind() const {
			return Bind(0);
		}
		/** @brief Unbinds the texture from the unit it was last bound to */
		bool Unbind() const;
		/** @brief Unbinds any texture from the specified texture unit */
		static bool Unbind(std::uint32_t textureUnit);

		/** @brief Allocates level-0 storage of the given format/size and optionally uploads its texels */
		void TexImage2D(std::int32_t level, PixelFormat format, bool bgr, std::int32_t width, std::int32_t height, const void* data);
		/** @brief Updates a rectangular subregion of level 0 */
		void TexSubImage2D(std::int32_t level, std::int32_t xoffset, std::int32_t yoffset, std::int32_t width, std::int32_t height, PixelFormat format, bool bgr, const void* data);
		/** @brief Allocates immutable level-0 storage of the given format/size (no texels yet) */
		void TexStorage2D(std::int32_t levels, PixelFormat format, std::int32_t width, std::int32_t height);
		/** @brief Compressed upload (unsupported, accepted as a no-op) */
		void CompressedTexImage2D(std::int32_t level, PixelFormat format, std::int32_t width, std::int32_t height, std::int32_t imageSize, const void* data);
		/** @brief Compressed sub-upload (unsupported, accepted as a no-op) */
		void CompressedTexSubImage2D(std::int32_t level, std::int32_t xoffset, std::int32_t yoffset, std::int32_t width, std::int32_t height, PixelFormat format, std::int32_t imageSize, const void* data);
		/** @brief Reads back level-0 texels of the linear host store into client memory */
		void GetTexImage(std::int32_t level, PixelFormat format, bool bgr, void* pixels);

		/** @brief Sets the minification filter */
		void SetMinFiltering(nCine::SamplerFilter filter);
		/** @brief Sets the magnification filter */
		void SetMagFiltering(nCine::SamplerFilter filter);
		/** @brief Sets the wrap mode */
		void SetWrap(SamplerWrapping wrap);
		/** @brief Sets the sampling swizzle (stored, informational - the palette path keys off the upload format) */
		void SetSwizzle(SwizzleChannel r, SwizzleChannel g, SwizzleChannel b, SwizzleChannel a);
		/** @brief Sets the highest defined mipmap level (ignored) */
		void SetMaxLevel(std::int32_t maxLevel);
		/** @brief Sets the client pixel-row alignment of uploads (ignored, uploads are tightly packed) */
		static void SetUnpackAlignment(std::int32_t alignment);

		/** @brief Sets a debug label; "Palettes" marks the shared palette texture whose rows become CLUTs */
		void SetObjectLabel(StringView label);

		static bool SupportsImmutableStorage() {
			return false;
		}
		static bool SupportsTextureReadback() {
			return true;
		}
		static void ClearErrors() {}
		static bool CheckErrors() {
			return false;
		}
		static void CheckFormatSupport(PixelFormat format) {
			static_cast<void>(format);
		}

		/** @brief Returns the number of bytes occupied by one texel of the given format (0 if unsupported) */
		static std::int32_t BytesPerPixel(PixelFormat format);

		/**
			@brief Allocates local-memory pages, freeing the least recently used stores if necessary

			The 4 MB of local memory is shared by the display buffers, the render-target reserve, the CLUT
			slab and every texture, so a level with many tilesets (or a menu section that pulls in more
			graphics) exhausts it - the measured working set of a whole level is larger than the cache by
			design. The stores are a cache of the copies kept in main memory, so instead of failing the
			allocation the oldest ones are dropped and re-transferred when they are needed again.

			@p keepAlive is spared by the eviction walk, so a texture cannot evict itself while growing.
		*/
		static std::uint32_t AllocatePages(std::uint32_t pageCount, const GsTexture* keepAlive);

	private:
		static std::uint32_t _nextHandle;
		static std::uint32_t _nextContentVersion;

		std::uint32_t _handle;
		std::uint32_t _contentVersion;
		TextureTarget _target;
		PixelFormat _format;
		PixelFormat _uploadFormat;
		std::int32_t _width;
		std::int32_t _height;
		std::int32_t _strideBytes;
		std::int32_t _bytesPerPixel;
		nCine::SamplerFilter _minFilter;
		nCine::SamplerFilter _magFilter;
		SamplerWrapping _wrap;
		SwizzleChannel _swizzle[4];
		mutable std::uint32_t _textureUnit;
		SmallVector<std::uint8_t, 0> _pixels;
		bool _isRenderTarget;
		bool _isPaletteTexture;

		/** @brief First page of the local-memory store, or @ref GsVram::InvalidPage */
		std::uint32_t _page;
		/** @brief Pages the store occupies (kept so it can be freed without recomputing) */
		std::uint32_t _pageCount;
		/** @brief Whether @ref _page came from the render-target reserve rather than the streaming cache */
		bool _pageFromReserve;
		GsPsm _psm;
		/** @brief Buffer pitch of the store in texels (padded to the storage mode's page width) */
		std::int32_t _bufferPitch;
		std::int32_t _paddedWidth;
		std::int32_t _paddedHeight;
		float _uScale;
		float _vScale;

		// One baked copy is kept per palette row, for the same reason the PVR backend keeps three: several
		// rows are commonly alive within one frame (text and its shadow), and re-baking a row a submitted
		// primitive still references would corrupt it. Each copy costs as much local memory as the texture
		// itself, so the count stays a compromise between reuse and pressure on the cache.
		struct BakedSlot
		{
			std::uint32_t Page = GsVram::InvalidPage;
			std::uint32_t PageCount = 0;
			bool Valid = false;
			std::uint32_t PaletteRow = 0;
			std::uint32_t PaletteGeneration = 0;
			std::uint32_t ContentVersion = 0;
			std::uint32_t LastUsedFrame = 0;
			const void* Palette = nullptr;
		};
		BakedSlot _bakedSlots[BakedSlotCount];
		std::int32_t _nextBakedSlot;

		// Every texture with local memory attached is linked into this list, most recently used first, so an
		// allocation that runs out of pages can reclaim the least recently used stores
		static GsTexture* _liveHead;
		static GsTexture* _liveTail;
		GsTexture* _livePrev;
		GsTexture* _liveNext;
		// The frame this texture was last drawn in. NeverUsed marks one that has been uploaded but not drawn
		// yet, which must stay evictable - during level loading the frame counter does not advance, so an
		// untouched texture would otherwise look like part of the frame being assembled
		static constexpr std::uint32_t NeverUsed = ~std::uint32_t(0);
		std::uint32_t _lastUsedFrame;

		void Allocate(PixelFormat format, std::int32_t width, std::int32_t height);
		/** @brief Chooses the storage mode and pitch the current upload format wants */
		void ResolveStorage(GsPsm& psm, std::int32_t& bufferPitch, std::uint32_t& pageCount) const;
		/**
			@brief Rows the store occupies: the real height rounded up to @p psm's page height

			Shared by @ref ResolveStorage(), the upload and the RG8 bake so the three cannot disagree about how
			tall the allocation is - which would either waste pages or transfer past the end of one.
		*/
		std::int32_t ResolveStoreHeight(GsPsm psm) const;
		/** @brief Makes sure a store of the right size exists, reallocating it if the storage mode changed */
		bool EnsureStore();
		/** @brief Transfers the host store into local memory */
		void RefreshStore();
		void FreeStores();

		/** @brief Returns the size of the linear level-0 image in bytes */
		inline std::int32_t RawPixelsSize() const {
			return _strideBytes * (_height > 0 ? _height : 0);
		}

		/** @brief Moves this texture to the front of the live list and stamps it with the current frame */
		void Touch();
		/**
			@brief Links this texture at the back of the live list if it is not in it yet

			Attaching local memory has to make the texture visible to the eviction walk right away - a store
			uploaded during level loading would otherwise be unreachable (and its pages unreclaimable) until
			the first draw calls @ref Touch(). Linked least recently used, so untouched uploads are also the
			first to go when memory runs short.
		*/
		void LinkAsLeastRecent();
		/** @brief Unlinks this texture from the live list */
		void Unlink();
	};
}
