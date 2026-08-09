#pragma once

#include "RsxVram.h"
#include "../RhiTypes.h"

#include <cstddef>
#include <cstdint>

#include <Containers/SmallVector.h>
#include <Containers/StringView.h>

#include <rsx/gcm_sys.h>

using namespace Death::Containers;

namespace nCine::RHI::RSX
{
	/**
		@brief Texture object of the RSX backend (aliased as `RHI::Texture`)

		Exposes the neutral upload surface `Texture.cpp` drives (`TexImage2D`, `TexSubImage2D`,
		`TexStorage2D`, filter/wrap/swizzle setters) and keeps the texels twice: once in a host store the
		uploads write into, and once in the GPU-visible copy the hardware samples, refreshed from the host
		store whenever it went stale. Every format is promoted to RGBA8 in both, which keeps one texture
		format on the GPU side.

		A texture bound as a colour attachment is the exception: the GPU writes it, so its GPU-visible copy
		*is* the truth and the host store is only kept for a readback. That is also why the two live in
		separate allocations at all - the render target's surface has to stay put across frames while the
		host store may be reallocated by an upload.

		**The swizzle is free here, and that is the one real difference from the sceGxm backend.** sceGxm can
		only express a fixed set of channel patterns, so `GxmTexture` bakes the engine's swizzle into the
		texels and re-bakes them whenever it changes. The RSX has a general `remap` field in its texture
		control structure: each of the four output channels independently selects a source channel, or a
		constant zero or one. So the texels are uploaded once and never rewritten, and @ref SetSwizzle() only
		recomputes a word.

		That same field also absorbs the endianness problem the format list creates. The RSX's 32-bit colour
		format is `A8R8G8B8` - a word, not a byte order - and the engine's RGBA8 store read as a big-endian
		word lands as `0xRRGGBBAA`, so the hardware sees red where alpha belongs and so on round the cycle.
		Rather than byte-swapping every upload, @ref BuildRemap() composes that fixed rotation with whatever
		swizzle the engine asked for, and the GPU does both in the sampler for nothing.

		The GPU copy is laid out linearly (`GCM_TEXTURE_FORMAT_LIN`) with an explicit pitch, which the RSX
		carries in the control structure - so unlike sceGxm there is no width-dependent choice of layout and
		no addressing mode lost to it. The pitch is rounded up to 64 bytes, which is what a colour surface
		needs when the texture is also a render target, so one rule covers both uses.
	*/
	class RsxTexture
	{
	public:
		/** @brief Number of texture units tracked by the device */
		static constexpr std::uint32_t MaxTextureUnits = 8;

		explicit RsxTexture(TextureTarget target);
		~RsxTexture();

		RsxTexture(const RsxTexture&) = delete;
		RsxTexture& operator=(const RsxTexture&) = delete;

		/** @brief Returns the libgcm texture control structure, materializing the GPU copy if needed, or `nullptr` */
		const gcmTexture* GetGcmTexture() const;
		/** @brief Returns the GPU-visible base address of the texels, allocating the copy if needed (used by @ref RsxRenderTarget) */
		void* GetSurfaceData() const;
		/** @brief Returns the byte distance between two rows of the GPU copy (its surface stride) */
		inline std::uint32_t GetSurfaceStride() const {
			return _gpuStride;
		}
		/** @brief Releases the GPU-visible copy (on re-allocation and destruction) */
		void ReleaseGpu() const;
		/**
			@brief Programs this texture's filter and wrap modes onto a texture unit

			Unlike sceGxm, which keeps sampler state inside the texture structure, the RSX carries it in
			command-buffer methods - so it is re-issued per draw by @ref RsxDevice::DrawCommon() rather than
			baked in once, and this is part of the backend surface rather than an internal detail.
		*/
		void ApplySamplerState(std::uint32_t textureUnit) const;

		/**
			@brief Returns a writable pointer to the GPU-visible store, for content rebuilt every frame

			Lets the cinematics produce a frame straight where the GPU will sample it instead of writing the
			host store and copying it across. Answered only for an ordinary RGBA8 texture: a colour
			attachment is written by the GPU, so handing its texels to the CPU would race the frame.

			@p strideBytes receives the row pitch, which is the 64-byte aligned one the GPU copy was
			allocated with rather than the texture's own width. Nothing written here reaches the host store,
			so such a texture must be rewritten in full every frame - and never read back, because this
			memory is local video memory the PPE reads at a crawl (see @ref RsxVram).
		*/
		void* MapStreamingTexels(std::int32_t& strideBytes);

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
		/** @brief Returns the pixel format of the stored texels (always RGBA8 after the promotion) */
		inline PixelFormat GetFormat() const {
			return _format;
		}
		/** @brief Returns the original upload format before promotion (R8/RG8/RGB8 kept) */
		inline PixelFormat GetUploadFormat() const {
			return _uploadFormat;
		}
		/** @brief Returns the byte distance between two consecutive rows of the host store */
		inline std::int32_t GetStrideBytes() const {
			return _strideBytes;
		}
		/** @brief Returns the base pointer of the level-0 host store (may be `nullptr` before an upload); the single-level store ignores @p level */
		inline const std::uint8_t* GetPixels(std::int32_t level = 0) const {
			static_cast<void>(level);
			return (_pixels.empty() ? nullptr : _pixels.data());
		}
		/** @brief Returns the horizontal texture-coordinate wrap mode */
		inline SamplerWrapping GetWrapS() const {
			return _wrap;
		}
		/** @brief Returns the vertical texture-coordinate wrap mode (single stored mode, same as @ref GetWrapS()) */
		inline SamplerWrapping GetWrapT() const {
			return _wrap;
		}
		/** @brief Returns the four-channel sampling swizzle (identity by default) */
		inline const SwizzleChannel* GetSwizzle() const {
			return _swizzle;
		}
		/** @brief Returns the magnification filter (alias of @ref GetMagFiltering()) */
		inline nCine::SamplerFilter GetMagFilter() const {
			return _magFilter;
		}
		/** @brief Returns `true` if the texture is bound as a color render target */
		inline bool IsRenderTarget() const {
			return _isRenderTarget;
		}
		/** @brief Marks the texture as (or no longer as) a color render target (the GPU copy is rebuilt, since the GPU writes it from then on) */
		void SetRenderTarget(bool isRenderTarget);
		/** @brief Returns a writable base pointer of the level-0 host store */
		inline std::uint8_t* MutablePixels() {
			return (_pixels.empty() ? nullptr : _pixels.data());
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
		/** @brief Sets the sampling swizzle (recomputes the remap word; the texels are not touched) */
		void SetSwizzle(SwizzleChannel r, SwizzleChannel g, SwizzleChannel b, SwizzleChannel a);
		/** @brief Sets the highest defined mipmap level (ignored, the store is single-level) */
		void SetMaxLevel(std::int32_t maxLevel);
		/** @brief Sets the client pixel-row alignment of uploads (ignored, uploads are tightly packed) */
		static void SetUnpackAlignment(std::int32_t alignment);

		/** @brief Sets a debug label for the texture (ignored) */
		void SetObjectLabel(StringView label);

		/** @brief Returns the magnification filter */
		inline nCine::SamplerFilter GetMagFiltering() const {
			return _magFilter;
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
		static std::uint32_t _nextHandle;

		std::uint32_t _handle;
		TextureTarget _target;
		PixelFormat _format;
		PixelFormat _uploadFormat;
		std::int32_t _width;
		std::int32_t _height;
		std::int32_t _strideBytes;
		nCine::SamplerFilter _minFilter;
		nCine::SamplerFilter _magFilter;
		SamplerWrapping _wrap;
		SwizzleChannel _swizzle[4];
		mutable std::uint32_t _textureUnit;
		SmallVector<std::uint8_t, 0> _pixels;
		bool _isRenderTarget;

		// GPU-visible copy, created lazily on the first bind (mutable so the const bind-time accessors can
		// materialize it). `_contentsDirty` forces a refresh from the host store; the sampler state is
		// programmed per bind rather than cached, because it lives in command-buffer methods rather than in
		// the texture structure the way sceGxm keeps it
		mutable RsxVram::Block _gpuBlock;
		mutable gcmTexture _gpuTexture;
		mutable std::uint32_t _gpuStride;
		mutable bool _gpuValid;
		mutable bool _contentsDirty;

		void Allocate(PixelFormat format, std::int32_t width, std::int32_t height);
		/** @brief (Re)creates the GPU copy and its control structure when missing, then refreshes it if stale */
		bool EnsureGpuTexture() const;
		/** @brief Copies the host store into the GPU copy, applying the row padding */
		void UploadPixels() const;
		/**
			@brief Builds the `gcmTexture::remap` word from the tracked swizzle

			Composes two mappings: the engine's swizzle (which logical channel, or a constant, feeds each
			output) and the fixed rotation that reinterprets the RGBA8 store as the hardware's A8R8G8B8 word
			(see the class documentation).
		*/
		std::uint32_t BuildRemap() const;
	};
}
