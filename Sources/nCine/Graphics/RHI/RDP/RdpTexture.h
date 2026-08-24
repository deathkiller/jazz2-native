#pragma once

#include "../RhiTypes.h"

#include <cstddef>
#include <cstdint>

#include <Containers/StringView.h>

#include <surface.h>

using namespace Death::Containers;

namespace nCine::RHI::RDP
{
	/**
		@brief Fingerprint of one 256-entry palette row, used to tell a real palette change from a bump

		Every upload into the shared palette texture advances a GLOBAL generation counter, because a
		backend cannot know which rows a caller rewrote beyond the range it was told. Keying the derived
		copies (the TLUT slots, the RG8 bakes) on that counter alone therefore rebuilds all of them
		whenever anything touches the palette - and a bake is a whole-image conversion, which is far too
		expensive to repeat for a row whose bytes did not actually change. The fingerprint makes the
		distinction cheap: 256 words hashed (about a thousand cycles) instead of a rebuild.
	*/
	inline std::uint64_t HashPaletteRow(const std::uint32_t* row)
	{
		// FNV-1a over the row's 256 words
		std::uint64_t hash = 0xCBF29CE484222325ull;
		for (std::int32_t i = 0; i < 256; i++) {
			hash = (hash ^ row[i]) * 0x100000001B3ull;
		}
		return hash;
	}

	/**
		@brief Packs one 8-bit-per-channel colour into RGBA5551, the packing `FMT_RGBA16` stores and the TLUTs use

		The single alpha bit is thresholded from the 8-bit alpha - shared by the texture stores, the bakes
		and the device's TLUT conversion, so the threshold can never drift between them.
	*/
	inline std::uint16_t Pack5551(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a)
	{
		return std::uint16_t(((r >> 3) << 11) | ((g >> 3) << 6) | ((b >> 3) << 1) | (a >= 128 ? 1 : 0));
	}

	/**
		@brief Texture object of the RDP backend (aliased as `RHI::Texture`)

		The RDP samples textures out of TMEM, a 4 KB on-chip buffer the device loads per primitive from an
		ordinary RDRAM surface - so unlike the PowerVR there is no separate "video memory" store to manage,
		and unlike the GE there is no per-axis addressing limit to page around: the texture keeps ONE
		RDRAM store in a format the RDP's load commands accept, and the device uploads the sub-window a
		primitive samples into TMEM before drawing it. The store's requirements are libdragon's texture
		rules: an 8-byte aligned base (64 here, a full cache line), a row stride that is a multiple of
		8 bytes, and a CPU cache writeback after every CPU write - the RDP DMAs straight from RDRAM and
		never sees the data cache.

		Formats:
		- R8 index textures ARE their RDP store: the bytes of a `FMT_CI8` surface are the palette indices
		  themselves, so no second copy is kept and no conversion ever runs - the palette is resolved by
		  the TLUT the device loads into the upper half of TMEM per draw (the analogue of the PowerVR's
		  palette banks and the GE's CLUT). This is also what makes R8 streaming free (see
		  @ref MapStreamingTexels()).
		- RG8 (index + per-pixel alpha) has no paletted equivalent - a CI texel's alpha comes from the
		  TLUT entry - so it takes the same per-palette-row CPU bake the other consoles use, into
		  `FMT_RGBA16` (@ref EnsureBakedStore()). RGBA5551 keeps only one alpha bit, so the per-pixel
		  alpha is thresholded; content that needs the smooth gradient would have to bake to `FMT_RGBA32`
		  at double the memory and half the TMEM window (TODO if it ever shows).
		- RGBA8 / RGB8 / RGB565 convert to `FMT_RGBA16` (RGBA5551) at the first draw that samples them,
		  halving RDRAM against a 32-bit store. The linear host copy in the uploaded format exists only
		  until that conversion: the device reads a host copy back through @ref GetPixels() for ONE role,
		  the palette a draw resolves indices with (the shared palette texture, or the recolored preview
		  palettes of the profile menu bound to `uTexturePalette`), and a palette is never the texture a
		  primitive samples - so a texture that has produced its RDP store has proven it is not one, and
		  keeping its host copy alive would be two copies of every image in the game. Later sub-uploads
		  (a minimap line, an ImGui atlas patch) convert straight into the store from then on.
		The shared 256x256 palette texture (labelled "Palettes") is intercepted: it keeps only the linear
		RGBA8 store and the device converts its rows into RGBA5551 TLUTs on demand.

		Render targets allocate their `FMT_RGBA16` surface eagerly; the same surface is what
		`rdpq_attach()` renders into and what the sampling passes later upload windows from. Mip levels
		above 0 and compressed formats are accepted but not stored.
	*/
	class RdpTexture
	{
	public:
		/** @brief Number of texture units tracked by the device */
		static constexpr std::uint32_t MaxTextureUnits = 8;

		explicit RdpTexture(TextureTarget target);
		~RdpTexture();

		RdpTexture(const RdpTexture&) = delete;
		RdpTexture& operator=(const RdpTexture&) = delete;

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
		/** @brief Returns the pixel format of the linear host store (native, like the software backend) */
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
		/**
			@brief Returns the base pointer of the linear host store (may be `nullptr` before an upload)

			For R8 the RDP store IS the host store (identity bytes, padded stride); RG8 keeps a linear copy
			in the uploaded format for as long as it lives (the per-palette-row bakes read it); a
			direct-color texture keeps one until its RGBA5551 store has been built, which is enough for the
			palette path (a palette is read on the CPU, never sampled, so it never builds a store); a
			render target has none.
		*/
		const std::uint8_t* GetPixels(std::int32_t level = 0) const;
		/** @brief Returns a writable base pointer of the linear host store (`nullptr` when none exists) */
		std::uint8_t* MutablePixels();
		/** @brief Returns the horizontal texture-coordinate wrap mode */
		inline SamplerWrapping GetWrapS() const {
			return _wrap;
		}
		/** @brief Returns the vertical texture-coordinate wrap mode (single stored mode) */
		inline SamplerWrapping GetWrapT() const {
			return _wrap;
		}
		/** @brief Returns the four-channel sampling swizzle (identity by default; informational on RDP) */
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
		/**
			@brief Returns the stamp of the surface a draw would sample right now

			The content version for an ordinary store; for an RG8 texture, the stamp of the ACTIVE bake -
			every rebuild gets a fresh one, so a bake slot recycled in place for another palette row can
			never satisfy the device's TMEM residency check with the previous row's texels.
		*/
		inline std::uint32_t GetSurfaceStamp() const {
			return (NeedsPaletteBake() ? _activeBakeStamp : _contentVersion);
		}

		/** @brief Returns `true` when the store holds palette indices (draws resolve them through a TLUT) */
		inline bool IsIndexed() const {
			return (_uploadFormat == PixelFormat::R8);
		}
		/** @brief Returns `true` when the texture needs the per-palette-row CPU bake (RG8 index + alpha) */
		inline bool NeedsPaletteBake() const {
			return (_uploadFormat == PixelFormat::RG8);
		}
		/** @brief Returns `true` when this is the intercepted shared palette texture (rows become TLUTs) */
		inline bool IsPaletteTexture() const {
			return _isPaletteTexture;
		}

		// -- RDP store (read by the draw dispatch) --

		/**
			@brief Builds the RDP-sampleable surface if it is missing and returns it, or `nullptr`

			For an RG8 texture this returns the pages of the ACTIVE bake, so @ref EnsureBakedStore() must
			have selected one first; `nullptr` means there is nothing to sample (no upload yet, an
			unsupported format, or a bake that was never requested).
		*/
		const surface_t* AcquireSurface();

		/**
			@brief Makes the active RDP store this RG8 texture baked through one palette row

			Index resolved through @p paletteRow (256 RGBA8 entries), alpha thresholded from the texel's own
			alpha byte into RGBA5551's single bit - the analogue of @ref GU::GuTexture::EnsureBakedStore().
			Two bakes are cached, so the common "one extra palette row" case (a sprite and its recolored
			twin drawn in the same frame) rebuilds nothing; @ref AcquireSurface() then hands out the surface
			of the matching bake. Returns `false` when the bake could not be produced.
		*/
		bool EnsureBakedStore(const std::uint32_t* paletteRow, std::uint32_t paletteRowIndex,
			std::uint32_t paletteGeneration, const void* palette);

		/**
			@brief Returns a writable pointer to the RDP store, for content that is rebuilt every frame

			Answered only for R8, whose store bytes are the palette indices themselves - which is exactly
			the format the cinematics produce on the paletted consoles, so a frame is decoded straight where
			the RDP will read it. Direct-color formats return `nullptr` (their store is RGBA5551, not the
			engine layout the writer would produce), and the caller takes the copy-through-a-buffer upload
			path, which converts.

			@p strideBytes receives the row pitch, which is the 8-byte padded store stride rather than the
			texture's own width. The caller must rewrite the mapped content in full; the cache writeback for
			the RDP happens at the next @ref AcquireSurface() (i.e. the next draw that samples this texture).
		*/
		void* MapStreamingTexels(std::int32_t& strideBytes);

		/** @brief Returns the surface the RDP renders into when this texture is a render target, or `nullptr` */
		const surface_t* GetRenderTargetSurface() const;

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
		/** @brief Sets the sampling swizzle (stored, informational - the palette path keys off the upload format instead) */
		void SetSwizzle(SwizzleChannel r, SwizzleChannel g, SwizzleChannel b, SwizzleChannel a);
		/** @brief Sets the highest defined mipmap level (ignored) */
		void SetMaxLevel(std::int32_t maxLevel);
		/** @brief Sets the client pixel-row alignment of uploads (ignored, uploads are tightly packed) */
		static void SetUnpackAlignment(std::int32_t alignment);

		/** @brief Sets a debug label; "Palettes" marks the shared palette texture whose rows become TLUTs */
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

	private:
		// The bake cache of an RG8 texture. Two entries, because a sprite drawn through two palette rows in
		// the same frame is a real pattern (a recolored twin next to the original), and rebuilding a bake the
		// RDP may still DMA from within the frame would corrupt the earlier draw. Anything beyond two rows
		// falls back to rebuilding, with a one-time warning.
		static constexpr std::int32_t BakedStoreCount = 2;

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
		// Linear host copy in the uploaded format, owned outright (a growable container would keep the
		// block alive across the clear() this used to do, which is a whole extra image for every render
		// target). Never allocated for R8 (the RDP store is identity bytes and stands in for it) or
		// render targets; kept for the lifetime of an RG8 texture (the bakes read it) and, for the
		// direct-color formats, until the RGBA5551 store has been built - see the class documentation.
		std::uint8_t* _pixels;
		std::size_t _pixelsSize;
		bool _isRenderTarget;
		bool _isPaletteTexture;

		// The RDP store: one cache-line aligned RDRAM allocation the device uploads TMEM windows from
		// (for RG8 it holds the ACTIVE bake and the slots below own their own allocations)
		std::uint8_t* _store;
		std::size_t _storeSize;
		std::int32_t _storeStride;
		std::int32_t _texFormat;			// tex_format_t of the store (int to keep libdragon out of most includes)
		surface_t _surface;
		bool _storeValid;
		// Whether MapStreamingTexels() handed the store out and its cache lines still have to be written
		// back before the RDP can read it (done lazily at the next AcquireSurface())
		bool _streamingWritebackPending;

		struct BakedStore
		{
			std::uint8_t* Data = nullptr;
			std::uint32_t PaletteRow = 0;
			std::uint32_t PaletteGeneration = 0;
			std::uint32_t ContentVersion = 0;
			std::uint32_t LastUsedFrame = 0;
			std::uint64_t PaletteHash = 0;
			// Fresh on every rebuild, even when the slot's Data pointer is recycled in place - what the
			// device's TMEM residency dedup keys on (see GetSurfaceStamp())
			std::uint32_t BakeStamp = 0;
			const void* Palette = nullptr;
			bool Valid = false;
		};
		BakedStore _bakedStores[BakedStoreCount];
		std::int32_t _nextBakedStore;
		// Stamp of the bake the surface currently points at, set by EnsureBakedStore()
		std::uint32_t _activeBakeStamp;
		// The frame this texture's RDP store was last sampled in - presentation keeps a frame of commands
		// in flight, so a store rewrite or free drains the queue first when the stamp says the in-flight
		// frame could still DMA it (see the WaitIfInFlight guards in RdpTexture.cpp)
		std::uint32_t _lastSampledFrame;

		void Allocate(PixelFormat format, std::int32_t width, std::int32_t height);
		/** @brief Makes sure the RDP store allocation matches the current format/size; returns `false` on failure */
		bool EnsureStore();
		/**
			@brief Converts the host store into the RDP store (5551 packing) and writes the cache back

			Releases the host copy afterwards for the direct-color formats, which is what makes the store
			the single copy of a sampled image (see the class documentation).
		*/
		void RefreshStore();
		void FreeStores();
		/** @brief Allocates a zeroed host copy of @p size bytes, replacing any previous one */
		bool AllocatePixels(std::size_t size);
		/** @brief Releases the host copy */
		void FreePixels();
		/**
			@brief Packs one texel, laid out in the uploaded format, into the store's RGBA5551

			Used by the sub-upload path that writes straight into the store; the whole-image conversion in
			@ref RefreshStore() keeps its per-format loops so it does not branch per texel.
		*/
		std::uint16_t PackStoreTexel(const std::uint8_t* texel) const;

		/** @brief Returns the size of the linear (uncompressed) level-0 image in bytes */
		inline std::int32_t RawPixelsSize() const {
			return _strideBytes * (_height > 0 ? _height : 0);
		}
	};
}
