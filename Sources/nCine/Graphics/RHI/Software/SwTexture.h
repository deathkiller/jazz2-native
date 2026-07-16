#pragma once

#include "../RhiTypes.h"

#include <cstddef>
#include <cstdint>
#include <vector>

#include <Containers/StringView.h>

using namespace Death::Containers;

namespace nCine::RhiSoftware
{
	/**
		@brief CPU texture object of the software backend (aliased as `Rhi::Texture`)

		Wraps a single, tightly-packed level-0 pixel buffer in one of the formats the rasterizer can
		sample (@ref PixelFormat::RGBA8 for the textured path, @ref PixelFormat::R8 / @ref PixelFormat::RG8
		for the palette path). It exposes the neutral upload surface `Texture.cpp` drives (`TexImage2D`,
		`TexSubImage2D`, `TexStorage2D`, filter/wrap/swizzle setters); binding records the texture on the
		device so the effect running the draw can read its texels. Mip levels above 0 and compressed
		formats are accepted but not stored (the fast path samples level 0 only).
	*/
	class SwTexture
	{
	public:
		/** @brief Number of texture units tracked by the device */
		static constexpr std::uint32_t MaxTextureUnits = 8;

		explicit SwTexture(TextureTarget target);
		~SwTexture() = default;

		SwTexture(const SwTexture&) = delete;
		SwTexture& operator=(const SwTexture&) = delete;

		/** @brief Returns a synthetic handle uniquely identifying the texture (used by material sort keys) */
		inline std::uint32_t GetGLHandle() const {
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
		/** @brief Returns the pixel format of the stored texels (after any promotion to the RGBA8 store) */
		inline PixelFormat GetFormat() const {
			return format_;
		}
		/** @brief Returns the original upload format before promotion (R8/RG8/RGB8 kept, so the palette path can tell an R8 index texture from an RG8 index+alpha one) */
		inline PixelFormat GetUploadFormat() const {
			return uploadFormat_;
		}
		/** @brief Returns the byte distance between two consecutive rows of level 0 */
		inline std::int32_t GetStrideBytes() const {
			return strideBytes_;
		}
		/** @brief Returns the base pointer of the level-0 texel store (may be `nullptr` before an upload); the single-level store ignores @p level */
		inline const std::uint8_t* GetPixels(std::int32_t level = 0) const {
			static_cast<void>(level);
			return pixels_.empty() ? nullptr : pixels_.data();
		}
		/** @brief Returns the horizontal texture-coordinate wrap mode (used by the rasterizer sampler) */
		inline SamplerWrapping GetWrapS() const {
			return wrap_;
		}
		/** @brief Returns the vertical texture-coordinate wrap mode (single stored mode, same as @ref GetWrapS()) */
		inline SamplerWrapping GetWrapT() const {
			return wrap_;
		}
		/** @brief Returns the four-channel sampling swizzle (identity by default; the palette path maps `.a` to green for RG8 index textures) */
		inline const SwizzleChannel* GetSwizzle() const {
			return swizzle_;
		}
		/** @brief Returns the magnification filter (alias of @ref GetMagFiltering() the rasterizer samples with) */
		inline nCine::SamplerFilter GetMagFilter() const {
			return magFilter_;
		}
		/** @brief Returns `true` if the texture is bound as a color render target (its store is treated as bottom-up by the fast blit) */
		inline bool IsRenderTarget() const {
			return isRenderTarget_;
		}
		/** @brief Marks the texture as (or no longer as) a color render target */
		inline void SetRenderTarget(bool isRenderTarget) {
			isRenderTarget_ = isRenderTarget;
		}
		/** @brief Returns a writable base pointer of the level-0 texel store (for render-target output) */
		inline std::uint8_t* MutablePixels() {
			return pixels_.empty() ? nullptr : pixels_.data();
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
		std::vector<std::uint8_t> pixels_;
		bool isRenderTarget_;

		void Allocate(PixelFormat format, std::int32_t width, std::int32_t height);
	};
}
