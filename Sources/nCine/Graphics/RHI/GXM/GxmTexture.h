#pragma once

#include "GxmMemory.h"
#include "../RhiTypes.h"

#include <cstddef>
#include <cstdint>

#include <Containers/SmallVector.h>
#include <Containers/StringView.h>

#include <psp2/gxm.h>

using namespace Death::Containers;

namespace nCine::RHI::GXM
{
	/**
		@brief Texture object of the sceGxm backend (aliased as `RHI::Texture`)

		Exposes the neutral upload surface `Texture.cpp` drives (`TexImage2D`, `TexSubImage2D`,
		`TexStorage2D`, filter/wrap/swizzle setters) and keeps the texels twice: once in a host store the
		uploads write into, and once in the GPU-visible copy the hardware samples, refreshed from the host
		store whenever it went stale. Every format is promoted to RGBA8 in both, which keeps one texture
		format on the GPU side and lets the sampling swizzle be baked into the texels (sceGxm can express a
		channel swizzle in the texture format, but only for a fixed set of patterns).

		A texture bound as a colour attachment is the exception: the GPU writes it, so its GPU-visible copy
		*is* the truth and the host store is only kept for a readback. That is also why the two live in
		separate allocations at all - the render target's surface has to stay put across frames while the
		host store may be reallocated by an upload.

		The GPU copy is laid out linearly, in one of the two layouts sceGxm offers for that:

		- `SCE_GXM_TEXTURE_LINEAR` when the width is a multiple of 8, which the hardware needs because a
		  linear texture has no stride of its own (see `SceGxmTexture`: its control words carry only a width
		  and a height) and derives the row pitch from the width. This is the fully featured layout - every
		  addressing mode, mip filtering - and is what the atlases and the power-of-two render targets get.
		- `SCE_GXM_TEXTURE_LINEAR_STRIDED` otherwise, which carries an explicit stride and so accepts any
		  width, at the cost of mip filtering and a separate minification filter (the hardware rejects both
		  with `SCE_GXM_ERROR_UNSUPPORTED` on a strided texture, so they are not programmed for one).

		A colour attachment needs its stride padded to a multiple of 8 texels on top of that, because
		`sceGxmColorSurfaceInit()` rejects anything else with `SCE_GXM_ERROR_INVALID_ALIGNMENT` - which is why
		a render target whose width is not already a multiple of 8 is always a strided texture.
	*/
	class GxmTexture
	{
	public:
		/** @brief Number of texture units tracked by the device */
		static constexpr std::uint32_t MaxTextureUnits = 8;

		explicit GxmTexture(TextureTarget target);
		~GxmTexture();

		GxmTexture(const GxmTexture&) = delete;
		GxmTexture& operator=(const GxmTexture&) = delete;

		/** @brief Returns the sceGxm texture control structure, materializing the GPU copy if needed, or `nullptr` */
		const SceGxmTexture* GetGxmTexture() const;
		/** @brief Returns the GPU-visible base address of the texels, allocating the copy if needed (used by @ref GxmRenderTarget) */
		void* GetSurfaceData() const;
		/** @brief Returns the byte distance between two rows of the GPU copy (its surface stride) */
		inline std::uint32_t GetSurfaceStride() const {
			return gpuStride_;
		}
		/** @brief Releases the GPU-visible copy (on re-allocation and destruction) */
		void ReleaseGpu() const;

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
		/** @brief Returns the pixel format of the stored texels (always RGBA8 after the promotion) */
		inline PixelFormat GetFormat() const {
			return format_;
		}
		/** @brief Returns the original upload format before promotion (R8/RG8/RGB8 kept) */
		inline PixelFormat GetUploadFormat() const {
			return uploadFormat_;
		}
		/** @brief Returns the byte distance between two consecutive rows of the host store */
		inline std::int32_t GetStrideBytes() const {
			return strideBytes_;
		}
		/** @brief Returns the base pointer of the level-0 host store (may be `nullptr` before an upload); the single-level store ignores @p level */
		inline const std::uint8_t* GetPixels(std::int32_t level = 0) const {
			static_cast<void>(level);
			return (pixels_.empty() ? nullptr : pixels_.data());
		}
		/** @brief Returns the horizontal texture-coordinate wrap mode */
		inline SamplerWrapping GetWrapS() const {
			return wrap_;
		}
		/** @brief Returns the vertical texture-coordinate wrap mode (single stored mode, same as @ref GetWrapS()) */
		inline SamplerWrapping GetWrapT() const {
			return wrap_;
		}
		/** @brief Returns the four-channel sampling swizzle (identity by default) */
		inline const SwizzleChannel* GetSwizzle() const {
			return swizzle_;
		}
		/** @brief Returns the magnification filter (alias of @ref GetMagFiltering()) */
		inline nCine::SamplerFilter GetMagFilter() const {
			return magFilter_;
		}
		/** @brief Returns `true` if the texture is bound as a color render target */
		inline bool IsRenderTarget() const {
			return isRenderTarget_;
		}
		/** @brief Marks the texture as (or no longer as) a color render target (the GPU copy is rebuilt, since the GPU writes it from then on) */
		void SetRenderTarget(bool isRenderTarget);
		/** @brief Returns a writable base pointer of the level-0 host store */
		inline std::uint8_t* MutablePixels() {
			return (pixels_.empty() ? nullptr : pixels_.data());
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
		/** @brief Compressed upload (unsupported by this backend, accepted as a no-op) */
		void CompressedTexImage2D(std::int32_t level, PixelFormat format, std::int32_t width, std::int32_t height, std::int32_t imageSize, const void* data);
		/** @brief Compressed sub-upload (unsupported by this backend, accepted as a no-op) */
		void CompressedTexSubImage2D(std::int32_t level, std::int32_t xoffset, std::int32_t yoffset, std::int32_t width, std::int32_t height, PixelFormat format, std::int32_t imageSize, const void* data);
		/** @brief Reads back level-0 texels into client memory (from the GPU copy when the texture is a render target) */
		void GetTexImage(std::int32_t level, PixelFormat format, bool bgr, void* pixels);

		/** @brief Sets the minification filter */
		void SetMinFiltering(nCine::SamplerFilter filter);
		/** @brief Sets the magnification filter */
		void SetMagFiltering(nCine::SamplerFilter filter);
		/** @brief Sets the wrap mode */
		void SetWrap(SamplerWrapping wrap);
		/** @brief Sets the sampling swizzle (baked into the GPU copy's texels) */
		void SetSwizzle(SwizzleChannel r, SwizzleChannel g, SwizzleChannel b, SwizzleChannel a);
		/** @brief Sets the highest defined mipmap level (ignored, the store is single-level) */
		void SetMaxLevel(std::int32_t maxLevel);
		/** @brief Sets the client pixel-row alignment of uploads (ignored, uploads are tightly packed) */
		static void SetUnpackAlignment(std::int32_t alignment);

		/** @brief Sets a debug label for the texture (ignored) */
		void SetObjectLabel(StringView label);

		/** @brief Returns the magnification filter */
		inline nCine::SamplerFilter GetMagFiltering() const {
			return magFilter_;
		}

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
		static std::uint32_t nextHandle_;

		std::uint32_t handle_;
		TextureTarget target_;
		PixelFormat format_;
		PixelFormat uploadFormat_;
		std::int32_t width_;
		std::int32_t height_;
		std::int32_t strideBytes_;
		nCine::SamplerFilter minFilter_;
		nCine::SamplerFilter magFilter_;
		SamplerWrapping wrap_;
		SwizzleChannel swizzle_[4];
		mutable std::uint32_t textureUnit_;
		SmallVector<std::uint8_t, 0> pixels_;
		bool isRenderTarget_;

		// GPU-visible copy, created lazily on the first bind (mutable so the const bind-time accessors can
		// materialize it). `contentsDirty_` forces a refresh from the host store, `samplerDirty_` only a
		// filter/wrap update of the control structure
		mutable GxmMemory::Block gpuBlock_;
		mutable SceGxmTexture gpuTexture_;
		mutable std::uint32_t gpuStride_;
		mutable bool gpuStrided_;			// the GPU copy uses SCE_GXM_TEXTURE_LINEAR_STRIDED
		mutable bool gpuValid_;
		mutable bool contentsDirty_;
		mutable bool samplerDirty_;

		void Allocate(PixelFormat format, std::int32_t width, std::int32_t height);
		/** @brief (Re)creates the GPU copy and its control structure when missing, then refreshes it if stale */
		bool EnsureGpuTexture() const;
		/** @brief Applies the tracked filter and wrap modes to the control structure */
		void ApplySamplerState() const;
		/** @brief Copies the host store into the GPU copy, baking the swizzle and the row padding in */
		void UploadPixels() const;
		/** @brief Returns `true` if @ref swizzle_ is the identity mapping (R,G,B,A) */
		bool IsIdentitySwizzle() const;
	};
}
