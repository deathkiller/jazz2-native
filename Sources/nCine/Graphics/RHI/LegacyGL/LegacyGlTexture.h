#pragma once

#include "../RhiTypes.h"
#include "LegacyGlDevice.h"

#include <cstddef>
#include <cstdint>

#include <Containers/SmallVector.h>
#include <Containers/StringView.h>

using namespace Death::Containers;

namespace nCine::RHI::LegacyGL
{
	/**
		@brief Texture object of the legacy GL backend (aliased as `RHI::Texture`)

		Keeps a LINEAR host store of level 0 in the uploaded pixel format (the same native R8/RG8/RGBA8
		layout the software and PVR backends use) plus the GL texture objects the rasterizer samples. The
		host store is what every upload writes and what a readback answers from; the texture objects are
		built from it lazily, because the two differ in ways an upload should not have to care about:

		- **Power-of-two dimensions, where the GL insists on them.** Sampling a non-power-of-two texture
		  is a GL 2.0 feature (`GL_ARB_texture_non_power_of_two` before that), so where it is missing each
		  texture object is padded up and the draw path scales texel coordinates by the padded size. The
		  padding replicates the edge texel (`BuildPage()`) so a bilinear tap at the last real texel does
		  not fetch black. Where it is present (`LegacyGlDevice::SupportsNonPowerOfTwo()`) a page is
		  stored at its own size and nothing is padded.
		- **A bounded size.** An image larger than what the GL can hold - `GL_MAX_TEXTURE_SIZE`, capped at
		  @ref MaxPageDimension - is split into @ref Page "pages", and the draw path selects the page a
		  primitive's texture rectangle falls into and rebases its texture coordinates onto it (see
		  @ref AcquirePage()). Every sprite/tile draw samples one small sub-rect of an atlas, so a
		  primitive practically never straddles a page boundary.
		- **Colour rather than indices.** Legacy GL has no palette hardware to resolve an index with -
		  `GL_EXT_paletted_texture` is long gone from every driver and TinyGL never had it - so an indexed
		  texture is baked through its palette row into RGBA8 first (@ref EnsureBakedStore()), which is
		  the same machinery the consoles use for their index+alpha content, applied to both indexed
		  formats here. Everything else is uploaded as RGBA8 as well: one store format keeps the upload
		  path single, and a 16-bit one would only be expanded again by the driver.

		A render target is the exception to all of it: its texture object is created at the real size and
		GL renders into it (through a framebuffer object, or into the back buffer and back out - see
		@ref LegacyGlRenderTarget), so it has no host store and nothing is ever converted or uploaded.

		Mip levels above 0 and compressed formats are accepted but not stored, exactly like on the other
		fixed-function backends: the game never uses either.
	*/
	class LegacyGlTexture
	{
	public:
		/** @brief Number of texture units tracked by the device */
		static constexpr std::uint32_t MaxTextureUnits = 8;

		/**
			@brief The largest page this backend ever builds, per axis

			A page is actually split at what the device reports (`LegacyGlDevice::GetMaxTextureDimension()`,
			which is `GL_MAX_TEXTURE_SIZE` capped at `LegacyGlDevice::MaxTextureDimension`) - as low as 256
			on a Voodoo-class card. This constant only states the ceiling the padding arithmetic is written
			against.
		*/
		static constexpr std::int32_t MaxPageDimension = LegacyGlDevice::MaxTextureDimension;

		/**
			@brief One GL-addressable piece of the texture

			A texture no larger than @ref MaxPageDimension on either axis has exactly one page covering it
			whole, which is the case for practically all content; bigger images are tiled by pages (see the
			class documentation).
		*/
		struct Page
		{
			/** @brief The GL texture object holding this page's texels (RGBA8, padded to a power of two) */
			std::uint32_t GlTexture = 0;
			/** @brief Base of the page's texels in the CPU store the GL texture was uploaded from */
			const void* Data = nullptr;
			/** @brief Position of the page's first texel within the source image */
			std::int32_t OriginX = 0, OriginY = 0;
			/** @brief Size of the source region the page covers */
			std::int32_t Width = 0, Height = 0;
			/** @brief Power-of-two size of the texture object (the region above padded up) */
			std::int32_t PaddedWidth = 0, PaddedHeight = 0;
			/** @brief Whether this page's GL texture is up to date with the store */
			bool Uploaded = false;
		};

		explicit LegacyGlTexture(TextureTarget target);
		~LegacyGlTexture();

		LegacyGlTexture(const LegacyGlTexture&) = delete;
		LegacyGlTexture& operator=(const LegacyGlTexture&) = delete;

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
		/** @brief Marks the texture as (or no longer as) a color render target; becoming one allocates its surface */
		void SetRenderTarget(bool isRenderTarget);
		/** @brief Returns a globally monotonic stamp of the texel store, advanced by every allocation or upload */
		inline std::uint32_t GetContentVersion() const {
			return _contentVersion;
		}

		/** @brief Returns `true` when the store holds palette indices rather than colours */
		inline bool IsIndexed() const {
			return (_uploadFormat == PixelFormat::R8 || _uploadFormat == PixelFormat::RG8);
		}
		/**
			@brief Returns `true` when the texture needs the per-palette-row CPU bake

			Every indexed texture does on this backend, unlike the consoles: legacy GL has no palette
			hardware to resolve indices with (GL_EXT_paletted_texture is long gone and TinyGL never had
			it), so an indexed store is baked through its palette row into RGBA8 before it is uploaded.
			That is the same machinery the consoles use for their RG8 index+alpha content, applied to
			both indexed formats here.
		*/
		inline bool NeedsPaletteBake() const {
			return IsIndexed();
		}
		/** @brief Returns `true` when this is the intercepted shared palette texture (its rows drive the bakes) */
		inline bool IsPaletteTexture() const {
			return _isPaletteTexture;
		}

		// -- page store (read by the draw dispatch) --

		/**
			@brief Uploads what is missing and returns the page holding the given source texel

			@p texelX / @p texelY are clamped into the image, so the min corner of any texture rectangle is a
			valid argument. Returns `nullptr` when there is nothing to sample (no upload yet, an unsupported
			format, or an indexed texture whose bake has not been requested - see @ref EnsureBakedStore()).
		*/
		const Page* AcquirePage(std::int32_t texelX, std::int32_t texelY);
		/** @brief Number of pages the image is split into along each axis (1 x 1 for practically all content) */
		inline std::int32_t GetPageCountX() const {
			return _pagesX;
		}
		/** @brief Number of pages the image is split into along each axis */
		inline std::int32_t GetPageCountY() const {
			return _pagesY;
		}

		/**
			@brief Makes the texture objects hold this indexed texture baked through one palette row

			Index resolved through @p paletteRow (256 RGBA8 entries); an RG8 texel's alpha comes from its own
			second byte, an R8 texel's from the palette entry. A small number of bakes is cached, so the
			common "one extra palette row" case (a sprite and its recolored twin) does not rebuild anything -
			each bake keeps its own texture objects, so switching between two resident ones uploads nothing
			and @ref AcquirePage() simply hands out the matching set. Returns `false` when the bake could not
			be produced.
		*/
		bool EnsureBakedStore(const std::uint32_t* paletteRow, std::uint32_t paletteRowIndex,
			std::uint32_t paletteGeneration, const void* palette);

		/**
			@brief Declined here: always returns `nullptr` (the contract's streaming-texture fast path)

			The consoles answer this with a pointer into the very memory their rasterizer samples, so content
			that is regenerated every frame (the cinematics) can be produced straight into it. A GL texture is
            a driver-owned object instead, so there is nothing to hand out and such content is uploaded like
			any other - which is also why the backend leaves `RHI_CAP_STREAMING_TEXTURES` undefined.
		*/
		void* MapStreamingTexels(std::int32_t& strideBytes);

		/** @brief Returns the texture object a render target draws into, or zero */
		inline std::uint32_t GetRenderTargetTexture() const {
			return (_isRenderTarget && !_pages.empty() ? _pages[0].GlTexture : 0);
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

		/** @brief Sets a debug label; "Palettes" marks the shared palette texture the bakes resolve through */
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
		// The bake cache of an indexed texture. Two entries, because a sprite drawn through two palette rows
		// in the same frame is a real pattern (a recolored twin next to the original) and rebuilding one bake
		// twice per frame would cost the whole conversion AND a full re-upload twice. Anything beyond two
		// rows falls back to rebuilding, with a one-time warning.
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

		// The page store: one allocation holding every page back to back (pages are laid out row-major).
		// Rebuilt lazily from the host store, so an upload only invalidates it.
		std::uint8_t* _pageStore;
		std::size_t _pageStoreSize;
		std::int32_t _pageBytesPerTexel;
		// The bound this store was split at, kept per texture: it is read from the device when the store
		// is planned and everything that maps a texel onto a page has to keep using that same value
		std::int32_t _pageDimension;
		std::int32_t _pagesX;
		std::int32_t _pagesY;
		SmallVector<Page, 1> _pages;
		bool _pageStoreValid;
		// Which bake the page store currently holds (RG8 only) and the two cached bakes it can be switched
		// between without a rebuild
		struct BakedStore
		{
			std::uint8_t* Data = nullptr;
			/** @brief One texture object per page, so switching between resident bakes uploads nothing */
			SmallVector<std::uint32_t, 1> PageTextures;
			std::uint32_t PaletteRow = 0;
			std::uint32_t PaletteGeneration = 0;
			std::uint32_t ContentVersion = 0;
			const void* Palette = nullptr;
			bool Valid = false;
		};
		BakedStore _bakedStores[BakedStoreCount];
		std::int32_t _activeBakedStore;
		std::int32_t _nextBakedStore;
		void Allocate(PixelFormat format, std::int32_t width, std::int32_t height);
		/** @brief Recomputes the page grid and the store format from the upload format and size */
		void PlanPages();
		/** @brief Builds (or rebuilds) the page store from the host store; returns `false` on failure */
		bool RefreshPageStore();
		/** @brief Converts and pads one page out of the host store into @p dst as RGBA8 */
		void BuildPage(const Page& page, std::uint8_t* dst);
		/** @brief Converts and pads one page out of the host store baked through a palette row */
		void BuildBakedPage(const Page& page, std::uint8_t* dst, const std::uint32_t* paletteRgba);
		/** @brief (Re)uploads one page's texels into its GL texture, creating the object on first use */
		bool UploadPage(Page& page);
		/** @brief Drops the page store (and every bake), so the next draw rebuilds it */
		void InvalidatePageStore();
		void FreePageStores();


		/** @brief Returns the size of the linear (uncompressed) level-0 image in bytes */
		inline std::int32_t RawPixelsSize() const {
			return _strideBytes * (_height > 0 ? _height : 0);
		}
	};
}
