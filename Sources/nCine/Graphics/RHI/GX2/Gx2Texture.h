#pragma once

#include "Gx2Memory.h"
#include "../RhiTypes.h"

#include <cstddef>
#include <cstdint>

#include <Containers/SmallVector.h>
#include <Containers/StringView.h>

#include <gx2/sampler.h>
#include <gx2/texture.h>

using namespace Death::Containers;

namespace nCine::RHI::GX2
{
	class Gx2RenderTarget;

	/**
		@brief Texture object of the GX2 backend (aliased as `RHI::Texture`)

		Exposes the neutral upload surface `Texture.cpp` drives (`TexImage2D`, `TexSubImage2D`,
		`TexStorage2D`, filter/wrap/swizzle setters) and keeps the texels twice: once in a host store the
		uploads write into, and once in the GPU copy the hardware samples, refreshed from the host store
		whenever it went stale. Every format is promoted to RGBA8 in both, so the GPU side has one format,
		`GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8`, whose byte order in memory is the store's own.

		An ordinary texture is laid out `GX2_TILE_MODE_LINEAR_ALIGNED`, so an upload is a row copy at the
		surface pitch. A texture used as a colour attachment is tiled (`GX2_TILE_MODE_DEFAULT`), because the
		colour buffer that @ref Gx2RenderTarget builds on the same surface renders much faster that way; the
		GPU writes it, so its GPU copy is the truth and the host store is never uploaded over it.

		The swizzle costs nothing: `GX2Texture::compMap` selects a source channel or a constant for each
		output channel, so @ref SetSwizzle() only recomputes that word and the texels stay as uploaded. The
		filter and wrap modes go into a `GX2Sampler` the draw path binds next to the texture
		(@ref ApplySamplerState()).

		The GPU copy is refreshed only when a draw binds the texture, and a queued draw of an earlier frame may
		still sample it then. A draw stamps the texture with its batch token (@ref MarkUsed()); a refresh from
		the host store while that token has not retired moves the GPU copy to a fresh block and retires the old
		one, and @ref MapStreamingTexels() waits for the token instead (its caller rewrites the texels in place).
	*/
	class Gx2Texture
	{
	public:
		/** @brief Number of texture units tracked by the device */
		static constexpr std::uint32_t MaxTextureUnits = 8;

		explicit Gx2Texture(TextureTarget target);
		~Gx2Texture();

		Gx2Texture(const Gx2Texture&) = delete;
		Gx2Texture& operator=(const Gx2Texture&) = delete;

		/** @brief Returns the GX2 texture, materializing or refreshing the GPU copy if needed, or `nullptr` */
		const GX2Texture* GetGx2Texture() const;
		/** @brief Returns the surface of the GPU copy, allocating it if needed (used by @ref Gx2RenderTarget), or `nullptr` */
		const GX2Surface* GetSurface() const;
		/** @brief Releases the GPU copy (on re-allocation and destruction) */
		void ReleaseGpu() const;
		/** @brief Binds this texture and its sampler state to a pixel shader texture unit, returns `false` if it has no GPU copy */
		bool ApplySamplerState(std::uint32_t textureUnit) const;
		/** @brief Records that a draw of the batch with @p token samples the GPU copy (see @ref Gx2Device::GetUseToken()) */
		inline void MarkUsed(std::uint32_t token) const {
			_lastUse = token;
		}

		/**
			@brief Returns a writable pointer to the GPU-visible store, for content rebuilt every frame

			Lets the cinematics produce a frame straight where the GPU will sample it instead of writing the
			host store and copying it across. Answered only for an ordinary RGBA8 texture: a colour
			attachment is written by the GPU, so handing its texels to the CPU would race the frame.

			@p strideBytes receives the row pitch of the linear GPU copy, which can be wider than the texture.
			Nothing written here reaches the host store, so such a texture must be rewritten in full every
			frame. The caller's writes are flushed out of the CPU cache on the next bind.
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
		/**
			@brief Records that a render target has this texture as a colour attachment (called by @ref Gx2RenderTarget)

			A texture can be attached to several targets at once (the upscale pass draws into one texture through
			three viewports). A texture destroyed while still attached tells those targets to forget it, because
			the engine may destroy a viewport's texture before the viewport (`PlayerViewport` does).
		*/
		void AddAttachedTarget(Gx2RenderTarget* target);
		/** @brief Records that a render target no longer has this texture attached (called by @ref Gx2RenderTarget) */
		void RemoveAttachedTarget(Gx2RenderTarget* target);
		/**
			@brief Asks for the render target's texels to go in MEM1 (see @ref Gx2Memory)

			Applies from the next allocation of the GPU copy, and only while the texture is a render target;
			it falls back to MEM2 when MEM1 is full. Meant for the few targets drawn into and read every
			frame (the player scene targets, the compose target), which then cost about half the GPU time.
		*/
		void SetPreferFastMemory(bool value);
		/**
			@brief Frees every GPU copy that lives in MEM1, before SDL destroys its MEM1 heap (the GPU must be idle)

			Their owners allocate them again the next time they are drawn into; their contents are lost, which is
			fine for render targets that are redrawn every frame.
		*/
		static void ReleaseMem1Copies();
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
		bool _preferFastMemory;

		// GPU copy, created lazily on the first bind (mutable so the const bind-time accessors can
		// materialize it). `_contentsDirty` forces a refresh from the host store, `_streamingDirty` a cache
		// flush after MapStreamingTexels() handed the texels out
		mutable Gx2Memory::Block _gpuBlock;
		mutable GX2Texture _gpuTexture;
		mutable GX2Sampler _sampler;
		mutable bool _gpuValid;
		mutable bool _contentsDirty;
		mutable bool _streamingDirty;
		mutable bool _samplerDirty;
		// Token of the last batch with a draw that samples the GPU copy, 0 if none
		mutable std::uint32_t _lastUse;
		// Render targets this texture is a colour attachment of (see AddAttachedTarget())
		SmallVector<Gx2RenderTarget*, 2> _attachedTargets;

		void Allocate(PixelFormat format, std::int32_t width, std::int32_t height);
		/** @brief (Re)creates the GPU copy and its control structure when missing, then refreshes it if stale */
		bool EnsureGpuTexture() const;
		/** @brief Copies the host store into the GPU copy, applying the row padding */
		void UploadPixels() const;
		/** @brief Builds the `GX2Texture::compMap` word from the tracked swizzle */
		std::uint32_t BuildCompMap() const;
	};
}
