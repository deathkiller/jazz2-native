#pragma once

#include "../RhiTypes.h"

#include <cstddef>
#include <cstdint>
#include <vector>

#include <Containers/StringView.h>

using namespace Death::Containers;

namespace nCine::RhiVulkan
{
	/**
		@brief Texture object of the Vulkan backend (aliased as `Rhi::Texture`)

		Wraps a single, tightly-packed level-0 pixel buffer in host memory and exposes the neutral upload
		surface `Texture.cpp` drives (`TexImage2D`, `TexSubImage2D`, `TexStorage2D`, filter/wrap/swizzle
		setters). It materializes the real `VkImage` + `VkImageView` + `VkSampler` from this surface: a
		staging-buffer upload for CPU textures, or a colour-attachment image for render targets. The sampling
		swizzle is applied through the image view's `VkComponentMapping` (no texel baking, unlike D3D11). Mip
		levels above 0 and compressed formats are accepted but not stored.
	*/
	class VulkanTexture
	{
	public:
		/** @brief Number of texture units tracked by the device */
		static constexpr std::uint32_t MaxTextureUnits = 8;

		explicit VulkanTexture(TextureTarget target);
		~VulkanTexture();

		VulkanTexture(const VulkanTexture&) = delete;
		VulkanTexture& operator=(const VulkanTexture&) = delete;

		// -- GPU accessors (used by the device draw / render-pass path). Handles are opaque
		// integers so this contract header stays free of <vulkan/vulkan.h>; the .cpp reinterprets them. --

		/** @brief Materializes the `VkImage`/view/sampler if missing (does NOT upload texels; see @ref RecordStreamingUpload()) */
		void EnsureGpu() const;
		/** @brief `true` if CPU texels are pending upload to the GPU image (a streaming update not yet recorded) */
		inline bool HasPendingUpload() const {
			return contentsDirty_ && hasCpuData_ && !isRenderTarget_ && !pixels_.empty();
		}
		/** @brief Records the pending CPU->GPU texel upload (staging copy + layout barriers) into @p cmdBuffer (an opaque `VkCommandBuffer`); the device frees the staging buffer once the frame completes */
		void RecordStreamingUpload(std::uint64_t cmdBuffer) const;
		/** @brief Returns the `VkImage` (as `std::uint64_t`), creating it on demand; `0` if unavailable */
		std::uint64_t GpuImage() const;
		/** @brief Returns the sampling `VkImageView` (with the swizzle component mapping) */
		std::uint64_t GpuView() const;
		/** @brief Returns the `VkSampler` matching the current filter/wrap */
		std::uint64_t GpuSampler() const;
		/** @brief Returns the `VkFormat` of the image */
		std::uint32_t GpuFormat() const;
		/** @brief Returns the last-known `VkImageLayout` of the image (device layout tracking) */
		std::uint32_t GetCurrentLayout() const {
			return currentLayout_;
		}
		/** @brief Records the current `VkImageLayout` (the device updates this after each transition) */
		void SetCurrentLayout(std::uint32_t layout) const {
			currentLayout_ = layout;
		}
		/** @brief Releases the GPU image / view / sampler (on re-allocation and destruction) */
		void ReleaseGpu() const;

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
		/** @brief Returns the original upload format before promotion (R8/RG8/RGB8 kept) */
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
		/** @brief Marks the texture as (or no longer as) a color render target (rebuilds the GPU image with the color-attachment usage when it changes) */
		inline void SetRenderTarget(bool isRenderTarget) {
			if (isRenderTarget != isRenderTarget_) {
				isRenderTarget_ = isRenderTarget;
				ReleaseGpu();
			}
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
		/** @brief Compressed upload (unsupported by this backend, accepted as a no-op) */
		void CompressedTexImage2D(std::int32_t level, PixelFormat format, std::int32_t width, std::int32_t height, std::int32_t imageSize, const void* data);
		/** @brief Compressed sub-upload (unsupported by this backend, accepted as a no-op) */
		void CompressedTexSubImage2D(std::int32_t level, std::int32_t xoffset, std::int32_t yoffset, std::int32_t width, std::int32_t height, PixelFormat format, std::int32_t imageSize, const void* data);
		/** @brief Reads back level-0 texels into client memory */
		void GetTexImage(std::int32_t level, PixelFormat format, bool bgr, void* pixels);

		/** @brief Sets the minification filter (stored) */
		void SetMinFiltering(nCine::SamplerFilter filter);
		/** @brief Sets the magnification filter (stored) */
		void SetMagFiltering(nCine::SamplerFilter filter);
		/** @brief Sets the wrap mode (stored) */
		void SetWrap(SamplerWrapping wrap);
		/** @brief Sets the sampling swizzle (stored) */
		void SetSwizzle(SwizzleChannel r, SwizzleChannel g, SwizzleChannel b, SwizzleChannel a);
		/** @brief Sets the highest defined mipmap level (ignored) */
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
		std::vector<std::uint8_t> pixels_;
		bool isRenderTarget_;

		// GPU objects, created lazily from the host store (mutable so the const bind-time accessors can
		// materialize them). Opaque integers so this header needs no <vulkan/vulkan.h>. contentsDirty_ forces
		// a texel re-upload; viewDirty_ rebuilds the view after a swizzle change; currentLayout_ tracks the
		// image's VkImageLayout for the device's render-pass / sampling transitions.
		mutable std::uint64_t gpuImage_;
		mutable std::uint64_t gpuMemory_;
		mutable std::uint64_t gpuView_;
		mutable std::uint64_t gpuSampler_;
		mutable std::uint32_t currentLayout_;
		mutable std::uint32_t gpuFormat_;
		mutable bool contentsDirty_;
		mutable bool viewDirty_;
		mutable bool hasCpuData_;
		mutable nCine::SamplerFilter samplerFilter_;
		mutable SamplerWrapping samplerWrap_;

		void Allocate(PixelFormat format, std::int32_t width, std::int32_t height);
	};
}
