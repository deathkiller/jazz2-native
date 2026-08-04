#pragma once

#include "../RhiTypes.h"

#include <cstddef>
#include <cstdint>

#include <Containers/SmallVector.h>
#include <Containers/StringView.h>

using namespace Death::Containers;

namespace nCine::RHI::GU
{
	/**
		@brief Texture object of the GU backend (aliased as `RHI::Texture`)

		Keeps a LINEAR host store of level 0 in the uploaded pixel format (the same native R8/RG8/RGBA8
		layout the software and PVR backends use) plus a second store the Allegrex GE can sample directly.
		The GE reads textures out of ordinary addressable memory, so that store lives in main memory rather
		than in the 2 MB of video memory - only render targets and the display buffers are worth putting
		there - but it still has to satisfy three hardware requirements the host store does not:

		- **Power-of-two dimensions, at most 512 per axis** (the texture size register holds log2 of each
		  axis in 4 bits, and u/v addressing is 9-bit). Anything larger is split into @ref Page "pages" of
		  at most 512x512, and the draw path selects the page a primitive's texture rectangle falls into and
		  rebases its texture coordinates onto it (see @ref AcquirePage()). Every sprite/tile draw samples
		  one small sub-rect of an atlas, so a primitive practically never straddles a page boundary.
		- **A format the GE samples.** R8 index textures become `GU_PSM_T8` read through a hardware CLUT
		  (the direct analogue of the PowerVR's palette banks and the GX's TLUTs) - the CLUT itself is
		  loaded per draw by the device from whatever palette texture the material bound. RG8 (index +
		  per-pixel alpha) has no paletted equivalent, because a paletted texel's alpha comes from the CLUT
		  entry, so those get the same per-palette-row CPU bake the other consoles use, into `GU_PSM_4444`
		  (@ref EnsureBakedStore()). RGB8/RGBA8 convert to `GU_PSM_4444` as well, and RGB565 to
		  `GU_PSM_5650` (whose channel order is the reverse of the engine's).
		- **The CPU cache written back**, since the GE reads main memory without seeing the data cache. Every
		  store rebuild ends in a writeback of exactly the bytes it wrote.

		Static content is additionally stored *swizzled* (the GE's 16-byte x 8-row block interleave), which
		is substantially cheaper to sample; content that is replaced repeatedly (the cinematic frames)
		drops the swizzle after its second upload, because interleaving it every frame would cost more than
		it saves. @ref MapStreamingTexels() lets such a texture be built straight in the GE store.

		Mip levels above 0 and compressed formats are accepted but not stored, exactly like on the other
		fixed-function backends: the game never uses either.
	*/
	class GuTexture
	{
	public:
		/** @brief Number of texture units tracked by the device */
		static constexpr std::uint32_t MaxTextureUnits = 8;

		/** @brief The dimension a single GE texture cannot exceed, per axis (a hardware limit) */
		static constexpr std::int32_t MaxPageDimension = 512;

		/**
			@brief One GE-addressable piece of the texture

			A texture no larger than @ref MaxPageDimension on either axis has exactly one page covering it
			whole, which is the case for practically all content; bigger images are tiled by pages (see the
			class documentation).
		*/
		struct Page
		{
			/** @brief 16-byte aligned base of the page's texels, in a format the GE samples */
			const void* Data = nullptr;
			/** @brief Position of the page's first texel within the source image */
			std::int32_t OriginX = 0, OriginY = 0;
			/** @brief Size of the source region the page covers */
			std::int32_t Width = 0, Height = 0;
			/** @brief Power-of-two size the GE is told about (the region above padded up) */
			std::int32_t PaddedWidth = 0, PaddedHeight = 0;
			/** @brief Whether the texels are stored in the GE's block interleave */
			bool Swizzled = false;
		};

		explicit GuTexture(TextureTarget target);
		~GuTexture();

		GuTexture(const GuTexture&) = delete;
		GuTexture& operator=(const GuTexture&) = delete;

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
		/** @brief Returns the base pointer of the linear host store (may be `nullptr` before an upload) */
		inline const std::uint8_t* GetPixels(std::int32_t level = 0) const {
			static_cast<void>(level);
			return (_pixels.empty() ? nullptr : _pixels.data());
		}
		/** @brief Returns a writable base pointer of the linear host store (`nullptr` before an upload) */
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
		/** @brief Returns the four-channel sampling swizzle (identity by default; informational on GU) */
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

		/** @brief Returns `true` when the store holds palette indices (draws resolve them through a CLUT) */
		inline bool IsIndexed() const {
			return (_uploadFormat == PixelFormat::R8);
		}
		/** @brief Returns `true` when the texture needs the per-palette-row CPU bake (RG8 index + alpha) */
		inline bool NeedsPaletteBake() const {
			return (_uploadFormat == PixelFormat::RG8);
		}
		/** @brief Returns `true` when this is the intercepted shared palette texture (its rows become CLUTs) */
		inline bool IsPaletteTexture() const {
			return _isPaletteTexture;
		}

		// -- GE store (read by the draw dispatch) --

		/**
			@brief Builds the GE store if it is missing and returns the page holding the given source texel

			@p texelX / @p texelY are clamped into the image, so the min corner of any texture rectangle is a
			valid argument. Returns `nullptr` when there is nothing to sample (no upload yet, an unsupported
			format, or an RG8 store whose bake has not been requested - see @ref EnsureBakedStore()).
		*/
		const Page* AcquirePage(std::int32_t texelX, std::int32_t texelY);
		/** @brief Returns the `GU_PSM_*` pixel format of the GE store (valid once a page exists) */
		inline std::int32_t GetGuPixelFormat() const {
			return _guFormat;
		}
		/** @brief Number of pages the image is split into along each axis (1 x 1 for anything up to 512x512) */
		inline std::int32_t GetPageCountX() const {
			return _pagesX;
		}
		/** @brief Number of pages the image is split into along each axis */
		inline std::int32_t GetPageCountY() const {
			return _pagesY;
		}

		/**
			@brief Makes the GE store hold this RG8 texture baked through one palette row

			Index resolved through @p paletteRow (256 RGBA8 entries), alpha from the texel's own alpha byte,
			written as `GU_PSM_4444` - the analogue of @ref PVR::PvrTexture::EnsureBakedArgb4444(). A small
			number of bakes is cached, so the common "one extra palette row" case (a sprite and its
			recolored twin) does not rebuild anything; @ref AcquirePage() then hands out the pages of the
			matching bake. Returns `false` when the bake could not be produced.
		*/
		bool EnsureBakedStore(const std::uint32_t* paletteRow, std::uint32_t paletteRowIndex,
			std::uint32_t paletteGeneration, const void* palette);

		/**
			@brief Returns a writable pointer to the GE store, for content that is rebuilt every frame

			Answered only for the format the hardware samples verbatim out of memory (RGB565), which is what
			lets the cinematics build a frame straight where the GE will read it instead of into a buffer that
			is then copied into the host store and converted a second time. Returns `nullptr` for anything
			else, and whenever the store cannot be allocated.

			@p strideBytes receives the row pitch, which is the power-of-two padded page width rather than the
			texture's own. Nothing written here reaches the host copy, so such a texture must be rewritten in
			full every frame and never read back. The channel order is the GE's (`GU_PSM_5650`, red in the low
			bits) rather than the engine's, so the bytes are swapped in place once per frame before the GE
			sees them (@ref AcquirePage()).
		*/
		void* MapStreamingTexels(std::int32_t& strideBytes);

		/** @brief Returns the surface the GE renders into when this texture is a render target, or `nullptr` */
		inline void* GetRenderTargetSurface() const {
			return _renderTargetSurface;
		}
		/** @brief Returns the row pitch in texels of @ref GetRenderTargetSurface() */
		inline std::int32_t GetRenderTargetStride() const {
			return _renderTargetStride;
		}

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

	private:
		// The bake cache of an RG8 texture. Two entries, because a sprite drawn through two palette rows in
		// the same frame is a real pattern (a recolored twin next to the original) and rebuilding one bake
		// twice per frame would both cost the whole conversion twice and let the GE - which runs behind the
		// CPU on its own display list - sample the second row's texels for the first row's draws. Anything
		// beyond two rows falls back to rebuilding, with a one-time warning.
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
		SmallVector<std::uint8_t, 0> _pixels;
		bool _isRenderTarget;
		bool _isPaletteTexture;

		// The GE store: one allocation holding every page back to back (pages are laid out row-major).
		// Rebuilt lazily from the host store, so an upload only invalidates it.
		std::uint8_t* _geStore;
		std::size_t _geStoreSize;
		std::int32_t _guFormat;
		std::int32_t _geBytesPerTexel;
		std::int32_t _pagesX;
		std::int32_t _pagesY;
		SmallVector<Page, 1> _pages;
		bool _geStoreValid;
		// Which bake the GE store currently holds (RG8 only) and the two cached bakes it can be switched
		// between without a rebuild
		struct BakedStore
		{
			std::uint8_t* Data = nullptr;
			std::uint32_t PaletteRow = 0;
			std::uint32_t PaletteGeneration = 0;
			std::uint32_t ContentVersion = 0;
			const void* Palette = nullptr;
			bool Valid = false;
		};
		BakedStore _bakedStores[BakedStoreCount];
		std::int32_t _activeBakedStore;
		std::int32_t _nextBakedStore;
		// Number of full uploads the texture has received. Content that is replaced again (video frames)
		// stops being swizzled, because interleaving it every frame costs more than the sampling saves.
		std::uint32_t _uploadCount;
		// Whether MapStreamingTexels() handed the GE store out and its channel order still has to be
		// flipped into the GE's before a draw can read it
		bool _streamingSwapPending;

		// The render-target surface the GE draws into (video memory when it fits, main memory otherwise);
		// it doubles as the texture's single page when the target is sampled afterwards
		void* _renderTargetSurface;
		std::int32_t _renderTargetStride;
		bool _renderTargetSurfaceInVram;

		void Allocate(PixelFormat format, std::int32_t width, std::int32_t height);
		/** @brief Recomputes the page grid and the GE pixel format from the upload format and size */
		void PlanGeStore();
		/** @brief Builds (or rebuilds) the GE store from the host store; returns `false` on failure */
		bool RefreshGeStore();
		/** @brief Converts and pads one page out of the host store into @p dst, then swizzles it if wanted */
		void BuildPage(const Page& page, std::uint8_t* dst, bool swizzle);
		/** @brief Converts and pads one page out of the host store baked through a palette row */
		void BuildBakedPage(const Page& page, std::uint8_t* dst, const std::uint16_t* rgb444, bool swizzle);
		/** @brief Drops the GE store (and every bake), so the next draw rebuilds it */
		void InvalidateGeStore();
		void FreeGeStores();
		void FreeRenderTargetSurface();

		/** @brief Returns the size of the linear (uncompressed) level-0 image in bytes */
		inline std::int32_t RawPixelsSize() const {
			return _strideBytes * (_height > 0 ? _height : 0);
		}
	};
}
