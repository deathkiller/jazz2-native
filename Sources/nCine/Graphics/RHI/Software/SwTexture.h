#pragma once

#include "../RhiTypes.h"

#include <cstddef>
#include <cstdint>
#include <vector>

#include <Containers/StringView.h>

using namespace Death::Containers;

namespace nCine::RHI::Software
{
	/**
		@brief CPU texture object of the software backend (aliased as `RHI::Texture`)

		Wraps a single, tightly-packed level-0 pixel buffer in one of the formats the rasterizer can
		sample (@ref PixelFormat::RGBA8 for the textured path, @ref PixelFormat::R8 / @ref PixelFormat::RG8
		for the palette path). R8 / RG8 uploads are stored NATIVELY at 1 / 2 bytes per texel (4x / 2x less
		memory than an RGBA8 store and correspondingly less gather bandwidth); the samplers expand each
		fetched texel to the 4-byte RGBA working form, filling the missing channels with 255. Only RGB8 is
		widened to RGBA8 at upload (3-byte texels are unaligned), and any store becomes RGBA8 the moment
		the texture is attached as a render target (the rasterizer composites 4 bytes per pixel). It
		exposes the neutral upload surface `Texture.cpp` drives (`TexImage2D`, `TexSubImage2D`,
		`TexStorage2D`, filter/wrap/swizzle setters); binding records the texture on the device so the
		effect running the draw can read its texels. Mip levels above 0 and compressed formats are
		accepted but not stored (the fast path samples level 0 only).
	*/
	class SwTexture
	{
	public:
		/** @brief Number of texture units tracked by the device */
		static constexpr std::uint32_t MaxTextureUnits = 8;

		explicit SwTexture(TextureTarget target);
		~SwTexture();

		SwTexture(const SwTexture&) = delete;
		SwTexture& operator=(const SwTexture&) = delete;

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
		/** @brief Returns the pixel format of the stored texels (native for R8/RG8; RGBA8 after the RGB8 or render-target widening) */
		inline PixelFormat GetFormat() const {
			return _format;
		}
		/** @brief Returns the original upload format before any widening (R8/RG8/RGB8 kept, so the palette path can tell an R8 index texture from an RG8 index+alpha one even after a render-target promotion) */
		inline PixelFormat GetUploadFormat() const {
			return _uploadFormat;
		}
		/** @brief Returns the byte distance between two consecutive rows of level 0 */
		inline std::int32_t GetStrideBytes() const {
			return _strideBytes;
		}
		/** @brief Returns the byte size of one stored texel (1 for R8, 2 for RG8, 4 for RGBA8 and widened stores) */
		inline std::int32_t GetBytesPerPixel() const {
			return _bytesPerPixel;
		}
		/** @brief Returns the base pointer of the level-0 texel store (may be `nullptr` before an upload); the single-level store ignores @p level */
		inline const std::uint8_t* GetPixels(std::int32_t level = 0) const {
			static_cast<void>(level);
			return _pixels.empty() ? nullptr : _pixels.data();
		}
		/** @brief Returns the horizontal texture-coordinate wrap mode (used by the rasterizer sampler) */
		inline SamplerWrapping GetWrapS() const {
			return _wrap;
		}
		/** @brief Returns the vertical texture-coordinate wrap mode (single stored mode, same as @ref GetWrapS()) */
		inline SamplerWrapping GetWrapT() const {
			return _wrap;
		}
		/** @brief Returns the four-channel sampling swizzle (identity by default; the palette path maps `.a` to green for RG8 index textures) */
		inline const SwizzleChannel* GetSwizzle() const {
			return _swizzle;
		}
		/** @brief Returns the magnification filter (alias of @ref GetMagFiltering() the rasterizer samples with) */
		inline nCine::SamplerFilter GetMagFilter() const {
			return _magFilter;
		}
		/** @brief Returns `true` if the texture is bound as a color render target (its store is treated as bottom-up by the fast blit) */
		inline bool IsRenderTarget() const {
			return _isRenderTarget;
		}
		/**
		 * @brief Marks the texture as (or no longer as) a color render target
		 *
		 * Becoming a render target widens a native R8/RG8 store to RGBA8 (discarding the texels - render
		 * targets are always fully drawn or cleared before being read): the rasterizer composites 4 bytes
		 * per pixel, so a narrower store would be overflowed by the first draw into it.
		 */
		void SetRenderTarget(bool isRenderTarget);
		/** @brief Returns a writable base pointer of the level-0 texel store (for render-target output) */
		inline std::uint8_t* MutablePixels() {
			return _pixels.empty() ? nullptr : _pixels.data();
		}
		/** @brief Returns a globally monotonic stamp of the texel store, advanced by every allocation or upload (used to key content-derived caches; render-target writes bypass it) */
		inline std::uint32_t GetContentVersion() const {
			return _contentVersion;
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
		/** @brief Compressed upload (unsupported by the fast path, accepted as a no-op) */
		void CompressedTexImage2D(std::int32_t level, PixelFormat format, std::int32_t width, std::int32_t height, std::int32_t imageSize, const void* data);
		/** @brief Compressed sub-upload (unsupported by the fast path, accepted as a no-op) */
		void CompressedTexSubImage2D(std::int32_t level, std::int32_t xoffset, std::int32_t yoffset, std::int32_t width, std::int32_t height, PixelFormat format, std::int32_t imageSize, const void* data);
		/** @brief Reads back level-0 texels into client memory */
		void GetTexImage(std::int32_t level, PixelFormat format, bool bgr, void* pixels);

		/** @brief Sets the minification filter (stored; the fast path currently samples nearest) */
		void SetMinFiltering(nCine::SamplerFilter filter);
		/** @brief Sets the magnification filter (stored; the fast path currently samples nearest) */
		void SetMagFiltering(nCine::SamplerFilter filter);
		/** @brief Sets the wrap mode (stored) */
		void SetWrap(SamplerWrapping wrap);
		/** @brief Sets the sampling swizzle (stored; used by the palette path) */
		void SetSwizzle(SwizzleChannel r, SwizzleChannel g, SwizzleChannel b, SwizzleChannel a);
		/** @brief Sets the highest defined mipmap level (ignored) */
		void SetMaxLevel(std::int32_t maxLevel);
		/** @brief Sets the client pixel-row alignment of uploads (ignored, uploads are tightly packed) */
		static void SetUnpackAlignment(std::int32_t alignment);

		/** @brief Sets a debug label for the texture (ignored) */
		void SetObjectLabel(StringView label);

		/** @brief Returns the magnification filter, for the effect to choose nearest vs. bilinear */
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
		std::vector<std::uint8_t> _pixels;
		bool _isRenderTarget;

		void Allocate(PixelFormat format, std::int32_t width, std::int32_t height);
	};
}
