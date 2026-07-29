#pragma once

#include "../RhiTypes.h"

#include <cstddef>
#include <cstdint>
#include <vector>

#include <Containers/StringView.h>

#include <gccore.h>

using namespace Death::Containers;

namespace nCine::RHI::GX
{
	/**
		@brief Texture object of the GX backend (aliased as `RHI::Texture`)

		Keeps two representations of level 0: a LINEAR host store in the uploaded pixel format (the same
		native R8/RG8/RGBA8 layout the software backend uses - needed for sub-updates, readback and the
		palette interception below), and a TILED GX store in main memory referenced by a `GXTexObj`:
		- R8 index textures tile to `GX_TF_CI8`; the palette (TLUT) is selected per draw by the device
		  from the instance's `palOffset`, which is what makes runtime palette remaps work fixed-function.
		- RGBA8 / RGB8 textures tile to `GX_TF_RGBA8`.
		- RG8 (index + per-pixel alpha) has no GX equivalent - a CI8 texel's alpha can only come from the
		  palette entry. Those textures keep only the linear store; the device CPU-bakes an RGBA8 tiled
		  copy per palette row on first use (see `EnsureBakedRgba()`), cached until the palette row or the
		  texel content changes. Only a small set of translucent indexed sprites uses RG8.
		The shared 256x256 palette texture (labelled "Palettes") is intercepted: it keeps only the linear
		store, and the device turns its rows into TLUTs on demand.

		Mip levels above 0 and compressed formats are accepted but not stored.
	*/
	class GxTexture
	{
	public:
		/** @brief Number of texture units tracked by the device */
		static constexpr std::uint32_t MaxTextureUnits = 8;

		explicit GxTexture(TextureTarget target);
		~GxTexture();

		GxTexture(const GxTexture&) = delete;
		GxTexture& operator=(const GxTexture&) = delete;

		/** @brief Returns a backend-neutral identifier uniquely identifying the texture (feeds material sort keys) */
		inline std::uint32_t GetUniqueId() const {
			return handle_;
		}
		/** @brief Returns the texture target */
		inline TextureTarget GetTarget() const {
			return target_;
		}

		/** @brief Returns the width of level 0 in texels */
		inline std::int32_t GetWidth() const {
			return width_;
		}
		/** @brief Returns the height of level 0 in texels */
		inline std::int32_t GetHeight() const {
			return height_;
		}
		/** @brief Returns the pixel format of the linear host store (native, like the software backend) */
		inline PixelFormat GetFormat() const {
			return format_;
		}
		/** @brief Returns the original upload format (R8/RG8 kept so the palette path can tell them apart) */
		inline PixelFormat GetUploadFormat() const {
			return uploadFormat_;
		}
		/** @brief Returns the byte distance between two consecutive rows of the linear host store */
		inline std::int32_t GetStrideBytes() const {
			return strideBytes_;
		}
		/** @brief Returns the base pointer of the linear host store (may be `nullptr` before an upload) */
		inline const std::uint8_t* GetPixels(std::int32_t level = 0) const {
			static_cast<void>(level);
			return pixels_.empty() ? nullptr : pixels_.data();
		}
		/** @brief Returns a writable base pointer of the linear host store (for render-target readback) */
		inline std::uint8_t* MutablePixels() {
			return pixels_.empty() ? nullptr : pixels_.data();
		}
		/** @brief Returns the horizontal texture-coordinate wrap mode */
		inline SamplerWrapping GetWrapS() const {
			return wrap_;
		}
		/** @brief Returns the vertical texture-coordinate wrap mode (single stored mode) */
		inline SamplerWrapping GetWrapT() const {
			return wrap_;
		}
		/** @brief Returns the four-channel sampling swizzle (identity by default; informational on GX) */
		inline const SwizzleChannel* GetSwizzle() const {
			return swizzle_;
		}
		/** @brief Returns the magnification filter */
		inline nCine::SamplerFilter GetMagFiltering() const {
			return magFilter_;
		}
		/** @brief Alias of @ref GetMagFiltering() */
		inline nCine::SamplerFilter GetMagFilter() const {
			return magFilter_;
		}
		/** @brief Returns `true` if the texture is bound as a color render target (EFB copy destination) */
		inline bool IsRenderTarget() const {
			return isRenderTarget_;
		}
		/** @brief Marks the texture as (or no longer as) a color render target; becoming one allocates an RGBA8 tiled store to copy the EFB into */
		void SetRenderTarget(bool isRenderTarget);
		/** @brief Returns a globally monotonic stamp of the texel store, advanced by every allocation or upload */
		inline std::uint32_t GetContentVersion() const {
			return contentVersion_;
		}

		/** @brief Returns `true` when the tiled store is a CI8 index texture (draws need a TLUT bound) */
		inline bool IsIndexed() const {
			return (uploadFormat_ == PixelFormat::R8);
		}
		/** @brief Returns `true` when the texture needs the per-palette-row CPU bake (RG8 index + alpha) */
		inline bool NeedsPaletteBake() const {
			return (uploadFormat_ == PixelFormat::RG8);
		}
		/** @brief Returns `true` when this is the intercepted shared palette texture (rows become TLUTs) */
		inline bool IsPaletteTexture() const {
			return isPaletteTexture_;
		}

		/**
		 * @brief Returns the GX texture object of the tiled store, or `nullptr` when none exists
		 *
		 * Valid for CI8 and RGBA8 textures and for render targets; `nullptr` for RG8 (use
		 * @ref EnsureBakedRgba()) and for the palette texture (rows are TLUTs, not a texture).
		 */
		GXTexObj* GetTexObj();

		/**
		 * @brief Returns a GX texture object of an RGBA8 copy of an RG8 store baked through one palette row
		 *
		 * The bake resolves each texel's palette index through @p paletteRow (256 RGBA8 entries) and takes
		 * alpha from the texel's own alpha byte. One baked copy is cached; it is rebuilt when a different
		 * palette row, a newer palette generation or newer texel content is requested.
		 */
		GXTexObj* EnsureBakedRgba(const std::uint32_t* paletteRow, std::uint32_t paletteRowIndex, std::uint32_t paletteGeneration);

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

		/** @brief Returns the base pointer of the tiled render-target store the EFB is copied into (render targets only) */
		inline std::uint8_t* GetRenderTargetStore() {
			return tiledStore_;
		}

	private:
		static std::uint32_t nextHandle_;
		static std::uint32_t nextContentVersion_;

		std::uint32_t handle_;
		std::uint32_t contentVersion_;
		TextureTarget target_;
		PixelFormat format_;
		PixelFormat uploadFormat_;
		std::int32_t width_;
		std::int32_t height_;
		std::int32_t strideBytes_;
		std::int32_t bytesPerPixel_;
		nCine::SamplerFilter minFilter_;
		nCine::SamplerFilter magFilter_;
		SamplerWrapping wrap_;
		SwizzleChannel swizzle_[4];
		mutable std::uint32_t textureUnit_;
		std::vector<std::uint8_t> pixels_;
		bool isRenderTarget_;
		bool isPaletteTexture_;

		// Tiled GX store (32-byte aligned main memory) + its texture object, valid when tiledStore_ != nullptr
		std::uint8_t* tiledStore_;
		std::size_t tiledStoreSize_;
		GXTexObj texObj_;
		bool texObjValid_;

		// Per-palette-row baked RGBA8 copy of an RG8 store (see EnsureBakedRgba)
		std::uint8_t* bakedStore_;
		GXTexObj bakedTexObj_;
		bool bakedValid_;
		std::uint32_t bakedPaletteRow_;
		std::uint32_t bakedPaletteGeneration_;
		std::uint32_t bakedContentVersion_;

		void Allocate(PixelFormat format, std::int32_t width, std::int32_t height);
		void RefreshTiledStore();
		void InitTexObj(GXTexObj& obj, void* store, std::uint8_t gxFormat, bool ci);
		void FreeTiledStores();
	};
}
