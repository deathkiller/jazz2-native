#pragma once

#include "../RhiTypes.h"

#include <cstddef>
#include <cstdint>

#include <Containers/SmallVector.h>
#include <Containers/StringView.h>

#include <3ds/gpu/enums.h>

typedef struct C3D_RenderTarget_tag C3D_RenderTarget;

using namespace Death::Containers;

namespace nCine::RHI::PICA
{
	/**
		@brief Texture object of the PICA backend (aliased as `RHI::Texture`)

		Keeps a LINEAR host store of level 0 in the uploaded pixel format (the same native R8/RG8/RGBA8
		layout the software and PVR backends use) plus a second store the PICA200 can sample directly. That
		store lives in the linear heap - the part of main memory the GPU addresses physically, which libctru
		hands out through `linearAlloc()` - rather than in the 6 MB of VRAM, where only the framebuffers and
		the render targets are worth their space. It has to satisfy four hardware requirements the host store
		does not:

		- **Power-of-two dimensions between 8 and 1024 per axis.** Anything larger is split into
		  @ref Page "pages" of at most 1024x1024, and the draw path selects the page a primitive's texture
		  rectangle falls into and rebases its texture coordinates onto it (see @ref AcquirePage()). Every
		  sprite/tile draw samples one small sub-rect of an atlas, so a primitive practically never straddles a
		  page boundary; the game's own content is assembled against the 512 the chunking asks for anyway.
		- **A format the PICA200 samples.** The GPU has no colour lookup table of any kind - its 8-bit formats
		  are luminance and alpha - so BOTH indexed formats are baked through their palette row into colour on
		  the CPU (@ref EnsureBakedStore()): R8 with the palette entry's alpha, RG8 (index + per-pixel alpha)
		  with the texel's own. The bake goes into `GPU_RGBA4`, and so does true-colour RGB8/RGBA8 content,
		  which halves the store and the sampling bandwidth over RGBA8 for a 2D game whose colours are palette
		  entries to begin with; RGB565 stays `GPU_RGB565`, whose channel order matches the engine's.
		- **The 8x8 tiled layout.** The GPU reads every texture as consecutive 8x8 blocks in Morton (Z) order -
		  there is no linear texture mode - so every store is tiled as it is built (see @ref TilePage()).
		- **The CPU cache written back**, since the GPU reads the linear heap without seeing the ARM11's data
		  cache. Every store rebuild ends in a `GSPGPU_FlushDataCache()` of exactly the bytes it wrote.

		A render target's store is a colour buffer the GPU renders into (in VRAM when it fits, in the linear
		heap otherwise) wrapped in a `C3D_RenderTarget`; the framebuffer layout IS the tiled texture layout, so
		it is sampled afterwards without any conversion.

		Mip levels above 0 and compressed formats are accepted but not stored, exactly like on the other
		fixed-function backends: the game never uses either.
	*/
	class PicaTexture
	{
	public:
		/** @brief Number of texture units tracked by the device */
		static constexpr std::uint32_t MaxTextureUnits = 8;

		/** @brief The dimension a single PICA200 texture cannot exceed, per axis (a hardware limit) */
		static constexpr std::int32_t MaxPageDimension = 1024;
		/** @brief The dimension a single PICA200 texture cannot fall short of, per axis (a hardware limit) */
		static constexpr std::int32_t MinPageDimension = 8;

		/**
			@brief One GPU-addressable piece of the texture

			A texture no larger than @ref MaxPageDimension on either axis has exactly one page covering it
			whole, which is the case for practically all content; bigger images are tiled by pages (see the
			class documentation).
		*/
		struct Page
		{
			/** @brief Base of the page's tiled texels, in a format the GPU samples (linear heap or VRAM) */
			const void* Data = nullptr;
			/** @brief Position of the page's first texel within the source image */
			std::int32_t OriginX = 0, OriginY = 0;
			/** @brief Size of the source region the page covers */
			std::int32_t Width = 0, Height = 0;
			/** @brief Power-of-two size the GPU is told about (the region above padded up) */
			std::int32_t PaddedWidth = 0, PaddedHeight = 0;
		};

		explicit PicaTexture(TextureTarget target);
		~PicaTexture();

		PicaTexture(const PicaTexture&) = delete;
		PicaTexture& operator=(const PicaTexture&) = delete;

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
		/** @brief Returns the four-channel sampling swizzle (identity by default; informational here) */
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
		/** @brief Marks the texture as (or no longer as) a color render target; becoming one allocates its colour buffer */
		void SetRenderTarget(bool isRenderTarget);
		/** @brief Returns a globally monotonic stamp of the texel store, advanced by every allocation or upload */
		inline std::uint32_t GetContentVersion() const {
			return _contentVersion;
		}

		/** @brief Returns `true` when the store holds palette indices (R8) - the GPU cannot sample those, see @ref NeedsPaletteBake() */
		inline bool IsIndexed() const {
			return (_uploadFormat == PixelFormat::R8);
		}
		/** @brief Returns `true` when the texture needs the per-palette-row CPU bake (both indexed formats, there is no palette hardware) */
		inline bool NeedsPaletteBake() const {
			return (_uploadFormat == PixelFormat::R8 || _uploadFormat == PixelFormat::RG8);
		}
		/** @brief Returns `true` when this is the intercepted shared palette texture (read by the bakes, never sampled) */
		inline bool IsPaletteTexture() const {
			return _isPaletteTexture;
		}

		// -- GPU store (read by the draw dispatch) --

		/**
			@brief Builds the GPU store if it is missing and returns the page holding the given source texel

			@p texelX / @p texelY are clamped into the image, so the min corner of any texture rectangle is a
			valid argument. Returns `nullptr` when there is nothing to sample (no upload yet, an unsupported
			format, or an indexed store whose bake has not been requested - see @ref EnsureBakedStore()).
		*/
		const Page* AcquirePage(std::int32_t texelX, std::int32_t texelY);
		/** @brief Returns the `GPU_TEXCOLOR` format of the GPU store (valid once a page exists) */
		inline GPU_TEXCOLOR GetPicaFormat() const {
			return _picaFormat;
		}
		/** @brief Number of pages the image is split into along each axis (1 x 1 for anything up to 1024x1024) */
		inline std::int32_t GetPageCountX() const {
			return _pagesX;
		}
		/** @brief Number of pages the image is split into along each axis */
		inline std::int32_t GetPageCountY() const {
			return _pagesY;
		}

		/**
			@brief Makes the GPU store hold this indexed texture baked through one palette row

			The row is taken as the 256 RGBA8 entries of @p palette starting at @p paletteOffset; the index is
			resolved through it, and the alpha comes from the texel's own alpha byte (RG8) or from the palette
			entry (R8), written as `GPU_RGBA4` - the analogue of @ref GU::GuTexture::EnsureBakedStore(),
			applied to both indexed formats here because the PICA200 has no colour lookup table. A small number
			of bakes is cached, so the common "one extra palette row" case (a sprite and its recolored twin)
			does not rebuild anything; @ref AcquirePage() then hands out the pages of the matching bake.
			Returns `false` when the bake could not be produced.

			The offset is taken rather than a ready row pointer so that the 256 entries this reads can be
			checked to lie inside @p palette here, once, instead of at each of the callers.
		*/
		bool EnsureBakedStore(const PicaTexture* palette, std::int32_t paletteOffset, std::uint32_t paletteGeneration);

		/**
			@brief Releases the decoded texels once the GPU store built from them is the only copy needed

			Both live in main memory, so a texture nothing writes again costs twice what it has to. Refused
			for content that still needs the texels - an indexed texture (its bakes are rebuilt from the
			indices on every palette change), streaming content, a render target or the shared palette
			texture. Does nothing on a texture that has already given them up.
		*/
		void ReleaseHostCopy();

		/**
			@brief Declined here: always returns `nullptr` (the contract's streaming-texture fast path)

			The GPU samples only tiled stores, so there is no linear pitch a CPU writer could fill; the
			cinematics take the copy-through-a-buffer path and the store is tiled at upload.
		*/
		void* MapStreamingTexels(std::int32_t& strideBytes);

		/** @brief Returns the render target the GPU renders into when this texture is one, or `nullptr` */
		inline C3D_RenderTarget* GetRenderTarget() const {
			return _renderTarget;
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

		/** @brief Sets a debug label; "Palettes" marks the shared palette texture the bakes read */
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
			@brief Scatters eight linear rows of 16-bit texels into the tile row of a GPU store they belong to

			The GPU reads a texture as 8x8 tiles laid out left to right, tile rows running from the first texel
			row on: tile (tx, ty) starts at texel index (ty * tilesPerRow + tx) * 64 and holds its 64 texels in
			Morton (Z) order - the three low bits of x and y interleaved with x in the even positions. @p dst is
			the first tile of the band's tile row, @p band the eight linear rows (@p paddedWidth texels each).
			Shared with the device, which tiles the per-frame lightmap texture the same way.
		*/
		static void TileBand16(std::uint16_t* dst, const std::uint16_t* band, std::int32_t paddedWidth);

	private:
		// The bake cache of an indexed texture. Two entries, because a sprite drawn through two palette rows
		// in the same frame is a real pattern (a recolored twin next to the original) and rebuilding one bake
		// twice per frame would both cost the whole conversion twice and let the GPU - which runs behind the
		// CPU on its own command list - sample the second row's texels for the first row's draws. Anything
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

		// The GPU store: one linear-heap allocation holding every page back to back (pages are laid out
		// row-major). Rebuilt lazily from the host store, so an upload only invalidates it.
		std::uint8_t* _gpuStore;
		std::size_t _gpuStoreSize;
		GPU_TEXCOLOR _picaFormat;
		std::int32_t _gpuBytesPerTexel;
		std::int32_t _pagesX;
		std::int32_t _pagesY;
		SmallVector<Page, 1> _pages;
		bool _gpuStoreValid;
		// Which bake the GPU store currently holds (indexed formats only) and the two cached bakes it can be
		// switched between without a rebuild
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
		// So an upload arriving after ReleaseHostCopy() is reported instead of being dropped in silence
		bool _hostCopyReleased;

		// The colour buffer the GPU renders into when this texture is a render target (VRAM when it fits, the
		// linear heap otherwise), the citro3d target wrapping it, and the descriptor the target was created from
		void* _renderTargetSurface;
		bool _renderTargetSurfaceInVram;
		C3D_RenderTarget* _renderTarget;
		// Storage for the C3D_Tex the render target was created from (kept opaque here, so this header does
		// not pull citro3d into every translation unit that includes Rhi.h)
		alignas(8) std::uint8_t _renderTargetTex[32];

		void Allocate(PixelFormat format, std::int32_t width, std::int32_t height);
		/** @brief Recomputes the page grid and the GPU pixel format from the upload format and size */
		void PlanGpuStore();
		/** @brief Builds (or rebuilds) the GPU store from the host store; returns `false` on failure */
		bool RefreshGpuStore();
		/** @brief Converts and pads one page out of the host store into @p dst, tiled the way the GPU reads it */
		void BuildPage(const Page& page, std::uint8_t* dst);
		/** @brief Converts and pads one page out of the host store baked through a palette row, tiled */
		void BuildBakedPage(const Page& page, std::uint8_t* dst, const std::uint16_t* rgba4);
		/** @brief Drops the GPU store (and every bake), so the next draw rebuilds it */
		void ReleaseHostPixels();
		void InvalidateGpuStore();
		void FreeGpuStores();
		void FreeRenderTargetSurface();

		/** @brief Returns the size of the linear (uncompressed) level-0 image in bytes */
		inline std::int32_t RawPixelsSize() const {
			return _strideBytes * (_height > 0 ? _height : 0);
		}
	};
}
