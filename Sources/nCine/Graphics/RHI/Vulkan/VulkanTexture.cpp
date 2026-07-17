#include "VulkanTexture.h"
#include "VulkanDevice.h"
#include "VulkanCommon.h"

#include <cstring>

namespace nCine::RhiVulkan
{
	namespace
	{
		// Copies one packed row into the store, expanding a narrower source (RGB8) to a wider store
		// (RGBA8) by filling the extra channel with 255 (opaque). A same-width copy is a plain memcpy.
		void CopyExpandRow(std::uint8_t* dst, std::int32_t dstBpp, const std::uint8_t* src, std::int32_t srcBpp, std::int32_t width)
		{
			if (srcBpp == dstBpp) {
				std::memcpy(dst, src, std::size_t(width) * std::size_t(dstBpp));
				return;
			}
			const std::int32_t shared = (srcBpp < dstBpp ? srcBpp : dstBpp);
			for (std::int32_t x = 0; x < width; x++) {
				std::int32_t c = 0;
				for (; c < shared; c++) {
					dst[x * dstBpp + c] = src[x * srcBpp + c];
				}
				for (; c < dstBpp; c++) {
					dst[x * dstBpp + c] = 255;
				}
			}
		}

		VkFormat MapVkFormat(PixelFormat format)
		{
			switch (format) {
				case PixelFormat::RGBA8: return VK_FORMAT_R8G8B8A8_UNORM;
				case PixelFormat::RGBA16F: return VK_FORMAT_R16G16B16A16_SFLOAT;
				case PixelFormat::RGBA32F: return VK_FORMAT_R32G32B32A32_SFLOAT;
				default: return VK_FORMAT_R8G8B8A8_UNORM;	// the store is promoted to RGBA8 for R8/RG8/RGB8
			}
		}

		VkComponentSwizzle MapSwizzle(SwizzleChannel channel, VkComponentSwizzle identity)
		{
			switch (channel) {
				case SwizzleChannel::Red: return VK_COMPONENT_SWIZZLE_R;
				case SwizzleChannel::Green: return VK_COMPONENT_SWIZZLE_G;
				case SwizzleChannel::Blue: return VK_COMPONENT_SWIZZLE_B;
				case SwizzleChannel::Alpha: return VK_COMPONENT_SWIZZLE_A;
				case SwizzleChannel::Zero: return VK_COMPONENT_SWIZZLE_ZERO;
				case SwizzleChannel::One: return VK_COMPONENT_SWIZZLE_ONE;
				default: return identity;
			}
		}
	}

	std::uint32_t VulkanTexture::nextHandle_ = 1;

	VulkanTexture::VulkanTexture(TextureTarget target)
		: handle_(nextHandle_++), target_(target), format_(PixelFormat::Unknown), uploadFormat_(PixelFormat::Unknown),
			width_(0), height_(0), strideBytes_(0),
			minFilter_(nCine::SamplerFilter::Nearest), magFilter_(nCine::SamplerFilter::Nearest), wrap_(SamplerWrapping::ClampToEdge),
			textureUnit_(0), isRenderTarget_(false),
			gpuImage_(0), gpuMemory_(0), gpuView_(0), gpuSampler_(0),
			currentLayout_(VK_IMAGE_LAYOUT_UNDEFINED), gpuFormat_(VK_FORMAT_R8G8B8A8_UNORM),
			contentsDirty_(false), viewDirty_(false), hasCpuData_(false),
			samplerFilter_(nCine::SamplerFilter::Unknown), samplerWrap_(SamplerWrapping::Unknown)
	{
		swizzle_[0] = SwizzleChannel::Red;
		swizzle_[1] = SwizzleChannel::Green;
		swizzle_[2] = SwizzleChannel::Blue;
		swizzle_[3] = SwizzleChannel::Alpha;
	}

	VulkanTexture::~VulkanTexture()
	{
		// Unbind from the device first so a later draw can't dereference this freed texture via boundTextures_
		VulkanDevice::UnbindTexture(this);
		ReleaseGpu();
	}

	void VulkanTexture::ReleaseGpu() const
	{
		// Defer the GPU frees instead of destroying immediately: an in-flight frame's command buffer or descriptor
		// set may still reference this image / view / sampler (up to MaxFramesInFlight frames run concurrently), so
		// freeing now would be a GPU use-after-free that loses the device (the level->menu freeze). The device frees
		// them once every frame that could reference them has completed; VkEnqueueDestroy no-ops if the device is
		// already gone (teardown) - vkDestroyDevice implicitly destroys these handles then.
		VkEnqueueDestroy(VkDeferredResource::ImageView, gpuView_);
		VkEnqueueDestroy(VkDeferredResource::Sampler, gpuSampler_);
		VkEnqueueDestroy(VkDeferredResource::Image, gpuImage_);
		VkEnqueueDestroy(VkDeferredResource::DeviceMemory, gpuMemory_);
		gpuView_ = 0;
		gpuSampler_ = 0;
		gpuImage_ = 0;
		gpuMemory_ = 0;
		currentLayout_ = VK_IMAGE_LAYOUT_UNDEFINED;
	}

	std::int32_t VulkanTexture::BytesPerPixel(PixelFormat format)
	{
		switch (format) {
			case PixelFormat::R8: return 1;
			case PixelFormat::RG8: return 2;
			case PixelFormat::RGB8: return 3;
			case PixelFormat::RGBA8: return 4;
			default: return 0;
		}
	}

	void VulkanTexture::Allocate(PixelFormat format, std::int32_t width, std::int32_t height)
	{
		// Promote the narrower runtime formats (RGB8 render targets, palette-index R8 / RG8) to an RGBA8 store
		uploadFormat_ = format;
		format_ = (format == PixelFormat::RGB8 || format == PixelFormat::R8 || format == PixelFormat::RG8) ? PixelFormat::RGBA8 : format;
		width_ = width;
		height_ = height;
		const std::int32_t bpp = BytesPerPixel(format_);
		strideBytes_ = width * bpp;
		pixels_.assign(std::size_t(strideBytes_) * std::size_t(height > 0 ? height : 0), std::uint8_t(0));
		// The size/format changed, so the GPU image must be rebuilt on the next bind
		ReleaseGpu();
		contentsDirty_ = true;
		viewDirty_ = true;
	}

	void VulkanTexture::EnsureGpu() const
	{
		VkDevice device = VkDeviceHandle();
		if (device == VK_NULL_HANDLE || width_ <= 0 || height_ <= 0) {
			return;
		}

		if (gpuImage_ == 0) {
			gpuFormat_ = std::uint32_t(MapVkFormat(format_));
			VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT;
			if (isRenderTarget_) {
				usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
			} else {
				usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
			}

			VkImageCreateInfo ici = {};
			ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
			ici.imageType = VK_IMAGE_TYPE_2D;
			ici.format = VkFormat(gpuFormat_);
			ici.extent = { std::uint32_t(width_), std::uint32_t(height_), 1 };
			ici.mipLevels = 1;
			ici.arrayLayers = 1;
			ici.samples = VK_SAMPLE_COUNT_1_BIT;
			ici.tiling = VK_IMAGE_TILING_OPTIMAL;
			ici.usage = usage;
			ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
			ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			VkImage image = VK_NULL_HANDLE;
			if (vkCreateImage(device, &ici, nullptr, &image) != VK_SUCCESS) {
				return;
			}

			VkMemoryRequirements req;
			vkGetImageMemoryRequirements(device, image, &req);
			const std::uint32_t memType = VkFindMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
			VkMemoryAllocateInfo mai = {};
			mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
			mai.allocationSize = req.size;
			mai.memoryTypeIndex = memType;
			VkDeviceMemory memory = VK_NULL_HANDLE;
			if (memType == UINT32_MAX || vkAllocateMemory(device, &mai, nullptr, &memory) != VK_SUCCESS) {
				vkDestroyImage(device, image, nullptr);
				return;
			}
			vkBindImageMemory(device, image, memory, 0);
			gpuImage_ = reinterpret_cast<std::uint64_t>(image);
			gpuMemory_ = reinterpret_cast<std::uint64_t>(memory);
			currentLayout_ = VK_IMAGE_LAYOUT_UNDEFINED;
			viewDirty_ = true;

			if (isRenderTarget_) {
				// Bring the fresh attachment to COLOR_ATTACHMENT_OPTIMAL so the device's LOAD-op render pass
				// (initialLayout == COLOR_ATTACHMENT_OPTIMAL) is valid on first use; contents stay undefined
				// until the engine clears / overwrites them.
				VkCommandBuffer cmd = VkBeginOneTimeCommands();
				if (cmd != VK_NULL_HANDLE) {
					VkImageMemoryBarrier b = {};
					b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
					b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
					b.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
					b.srcAccessMask = 0;
					b.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
					b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
					b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
					b.image = image;
					b.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
					vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
						0, 0, nullptr, 0, nullptr, 1, &b);
					VkEndOneTimeCommands(cmd);
					currentLayout_ = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
				}
			}
		}

		// NOTE: CPU texel uploads are NOT recorded here anymore. EnsureGpu() only materializes the image / view /
		// sampler; the staging copy is recorded into the current FRAME's command buffer by RecordStreamingUpload()
		// (driven from the device's pre-draw phase, outside any render pass), so per-frame texture streaming (the
		// palette) no longer forces a vkQueueWaitIdle stall every frame. contentsDirty_ stays set until then.

		if (viewDirty_ || gpuView_ == 0) {
			if (gpuView_ != 0) {
				// Defer: an in-flight frame's descriptor set may still sample through the old view (see ReleaseGpu)
				VkEnqueueDestroy(VkDeferredResource::ImageView, gpuView_);
				gpuView_ = 0;
			}
			VkImageViewCreateInfo ivci = {};
			ivci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			ivci.image = reinterpret_cast<VkImage>(gpuImage_);
			ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
			ivci.format = VkFormat(gpuFormat_);
			// The sampling swizzle is applied here (no texel baking) - e.g. RG8 palette textures map A<-Green
			ivci.components.r = MapSwizzle(swizzle_[0], VK_COMPONENT_SWIZZLE_R);
			ivci.components.g = MapSwizzle(swizzle_[1], VK_COMPONENT_SWIZZLE_G);
			ivci.components.b = MapSwizzle(swizzle_[2], VK_COMPONENT_SWIZZLE_B);
			ivci.components.a = MapSwizzle(swizzle_[3], VK_COMPONENT_SWIZZLE_A);
			ivci.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
			VkImageView view = VK_NULL_HANDLE;
			if (vkCreateImageView(device, &ivci, nullptr, &view) == VK_SUCCESS) {
				gpuView_ = reinterpret_cast<std::uint64_t>(view);
			}
			viewDirty_ = false;
		}

		if (gpuSampler_ == 0 || samplerFilter_ != magFilter_ || samplerWrap_ != wrap_) {
			if (gpuSampler_ != 0) {
				// Defer: an in-flight frame's descriptor set may still use the old sampler (see ReleaseGpu)
				VkEnqueueDestroy(VkDeferredResource::Sampler, gpuSampler_);
				gpuSampler_ = 0;
			}
			const bool linear = (magFilter_ == nCine::SamplerFilter::Linear ||
				magFilter_ == nCine::SamplerFilter::LinearMipmapNearest || magFilter_ == nCine::SamplerFilter::LinearMipmapLinear);
			VkSamplerAddressMode address;
			switch (wrap_) {
				case SamplerWrapping::Repeat: address = VK_SAMPLER_ADDRESS_MODE_REPEAT; break;
				case SamplerWrapping::MirroredRepeat: address = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT; break;
				default: address = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE; break;
			}
			VkSamplerCreateInfo sci = {};
			sci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
			sci.magFilter = linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
			sci.minFilter = linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
			sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
			sci.addressModeU = address;
			sci.addressModeV = address;
			sci.addressModeW = address;
			sci.maxLod = VK_LOD_CLAMP_NONE;
			sci.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
			VkSampler sampler = VK_NULL_HANDLE;
			if (vkCreateSampler(device, &sci, nullptr, &sampler) == VK_SUCCESS) {
				gpuSampler_ = reinterpret_cast<std::uint64_t>(sampler);
			}
			samplerFilter_ = magFilter_;
			samplerWrap_ = wrap_;
		}
	}

	void VulkanTexture::RecordStreamingUpload(std::uint64_t cmdBuffer) const
	{
		VkDevice device = VkDeviceHandle();
		if (device == VK_NULL_HANDLE || cmdBuffer == 0) {
			return;
		}
		EnsureGpu();	// materialize the image / view / sampler (no upload)
		if (gpuImage_ == 0 || !contentsDirty_ || !hasCpuData_ || isRenderTarget_ || pixels_.empty()) {
			return;
		}
		VkCommandBuffer cmd = reinterpret_cast<VkCommandBuffer>(cmdBuffer);
		VkImage image = reinterpret_cast<VkImage>(gpuImage_);

		// Host-visible staging buffer holding the whole level-0 store; the device frees it once the frame's fence signals
		VkBuffer staging = VK_NULL_HANDLE;
		VkDeviceMemory stagingMem = VK_NULL_HANDLE;
		VkBufferCreateInfo bci = {};
		bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bci.size = pixels_.size();
		bci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
		bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		if (vkCreateBuffer(device, &bci, nullptr, &staging) != VK_SUCCESS) {
			return;
		}
		VkMemoryRequirements req;
		vkGetBufferMemoryRequirements(device, staging, &req);
		const std::uint32_t memType = VkFindMemoryType(req.memoryTypeBits,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		VkMemoryAllocateInfo mai = {};
		mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		mai.allocationSize = req.size;
		mai.memoryTypeIndex = memType;
		if (memType == UINT32_MAX || vkAllocateMemory(device, &mai, nullptr, &stagingMem) != VK_SUCCESS) {
			vkDestroyBuffer(device, staging, nullptr);
			return;
		}
		vkBindBufferMemory(device, staging, stagingMem, 0);
		void* mapped = nullptr;
		vkMapMemory(device, stagingMem, 0, VK_WHOLE_SIZE, 0, &mapped);
		std::memcpy(mapped, pixels_.data(), pixels_.size());
		vkUnmapMemory(device, stagingMem);

		// Barrier into TRANSFER_DST. On a re-upload of a texture the OTHER in-flight frame sampled
		// (currentLayout_ == SHADER_READ_ONLY), the source scope is FRAGMENT_SHADER / SHADER_READ so this write
		// waits (via queue submission order) for that frame's samples - the shared-image WAR guard for streaming
		// textures like the palette. A fresh image is UNDEFINED (its contents are discarded, no wait needed).
		const VkImageLayout oldLayout = VkImageLayout(currentLayout_);
		VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
		VkAccessFlags srcAccess = 0;
		if (oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
			srcStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
			srcAccess = VK_ACCESS_SHADER_READ_BIT;
		}
		VkImageMemoryBarrier toDst = {};
		toDst.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		toDst.oldLayout = oldLayout;
		toDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		toDst.srcAccessMask = srcAccess;
		toDst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		toDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		toDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		toDst.image = image;
		toDst.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
		vkCmdPipelineBarrier(cmd, srcStage, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toDst);

		VkBufferImageCopy region = {};
		region.bufferOffset = 0;
		region.bufferRowLength = 0;
		region.bufferImageHeight = 0;
		region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
		region.imageOffset = { 0, 0, 0 };
		region.imageExtent = { std::uint32_t(width_), std::uint32_t(height_), 1 };
		vkCmdCopyBufferToImage(cmd, staging, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

		VkImageMemoryBarrier toRead = {};
		toRead.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		toRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		toRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		toRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		toRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		toRead.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		toRead.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		toRead.image = image;
		toRead.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
		vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			0, 0, nullptr, 0, nullptr, 1, &toRead);

		currentLayout_ = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		contentsDirty_ = false;
		VkRegisterFrameStaging(reinterpret_cast<std::uint64_t>(staging), reinterpret_cast<std::uint64_t>(stagingMem));
	}

	std::uint64_t VulkanTexture::GpuImage() const
	{
		EnsureGpu();
		return gpuImage_;
	}

	std::uint64_t VulkanTexture::GpuView() const
	{
		EnsureGpu();
		return gpuView_;
	}

	std::uint64_t VulkanTexture::GpuSampler() const
	{
		EnsureGpu();
		return gpuSampler_;
	}

	std::uint32_t VulkanTexture::GpuFormat() const
	{
		return gpuFormat_;
	}

	bool VulkanTexture::Bind(std::uint32_t textureUnit) const
	{
		textureUnit_ = textureUnit;
		VulkanDevice::BindTexture(textureUnit, this);
		return true;
	}

	bool VulkanTexture::Unbind() const
	{
		VulkanDevice::BindTexture(textureUnit_, nullptr);
		return true;
	}

	bool VulkanTexture::Unbind(std::uint32_t textureUnit)
	{
		VulkanDevice::BindTexture(textureUnit, nullptr);
		return true;
	}

	void VulkanTexture::TexImage2D(std::int32_t level, PixelFormat format, bool bgr, std::int32_t width, std::int32_t height, const void* data)
	{
		static_cast<void>(bgr);
		if (level != 0) {
			return;
		}
		Allocate(format, width, height);
		if (data != nullptr && !pixels_.empty()) {
			hasCpuData_ = true;
			const std::int32_t srcBpp = BytesPerPixel(format);
			const std::int32_t dstBpp = BytesPerPixel(format_);
			if (srcBpp == dstBpp) {
				std::memcpy(pixels_.data(), data, pixels_.size());
			} else {
				const std::uint8_t* src = static_cast<const std::uint8_t*>(data);
				for (std::int32_t y = 0; y < height_; y++) {
					CopyExpandRow(pixels_.data() + std::size_t(y) * strideBytes_,
						dstBpp, src + std::size_t(y) * std::size_t(width_) * srcBpp, srcBpp, width_);
				}
			}
		}
	}

	void VulkanTexture::TexSubImage2D(std::int32_t level, std::int32_t xoffset, std::int32_t yoffset, std::int32_t width, std::int32_t height, PixelFormat format, bool bgr, const void* data)
	{
		static_cast<void>(bgr);
		if (level != 0 || data == nullptr || pixels_.empty()) {
			return;
		}
		hasCpuData_ = true;
		contentsDirty_ = true;
		const std::int32_t srcBpp = BytesPerPixel(format);
		const std::int32_t dstBpp = BytesPerPixel(format_);
		for (std::int32_t y = 0; y < height; y++) {
			const std::int32_t dstY = yoffset + y;
			if (dstY < 0 || dstY >= height_) {
				continue;
			}
			std::int32_t dstX = xoffset;
			std::int32_t copyW = width;
			std::int32_t srcX0 = 0;
			if (dstX < 0) {
				srcX0 = -dstX;
				copyW += dstX;
				dstX = 0;
			}
			if (dstX + copyW > width_) {
				copyW = width_ - dstX;
			}
			if (copyW <= 0) {
				continue;
			}
			const std::uint8_t* srcRow = static_cast<const std::uint8_t*>(data) + std::size_t(y) * std::size_t(width) * srcBpp + std::size_t(srcX0) * srcBpp;
			std::uint8_t* dstRow = pixels_.data() + std::size_t(dstY) * strideBytes_ + std::size_t(dstX) * dstBpp;
			CopyExpandRow(dstRow, dstBpp, srcRow, srcBpp, copyW);
		}
	}

	void VulkanTexture::TexStorage2D(std::int32_t levels, PixelFormat format, std::int32_t width, std::int32_t height)
	{
		static_cast<void>(levels);
		Allocate(format, width, height);
	}

	void VulkanTexture::CompressedTexImage2D(std::int32_t level, PixelFormat format, std::int32_t width, std::int32_t height, std::int32_t imageSize, const void* data)
	{
		static_cast<void>(level);
		static_cast<void>(format);
		static_cast<void>(width);
		static_cast<void>(height);
		static_cast<void>(imageSize);
		static_cast<void>(data);
	}

	void VulkanTexture::CompressedTexSubImage2D(std::int32_t level, std::int32_t xoffset, std::int32_t yoffset, std::int32_t width, std::int32_t height, PixelFormat format, std::int32_t imageSize, const void* data)
	{
		static_cast<void>(level);
		static_cast<void>(xoffset);
		static_cast<void>(yoffset);
		static_cast<void>(width);
		static_cast<void>(height);
		static_cast<void>(format);
		static_cast<void>(imageSize);
		static_cast<void>(data);
	}

	void VulkanTexture::GetTexImage(std::int32_t level, PixelFormat format, bool bgr, void* pixels)
	{
		static_cast<void>(level);
		static_cast<void>(format);
		static_cast<void>(bgr);
		if (pixels != nullptr && !pixels_.empty()) {
			std::memcpy(pixels, pixels_.data(), pixels_.size());
		}
	}

	void VulkanTexture::SetMinFiltering(nCine::SamplerFilter filter)
	{
		minFilter_ = filter;
	}

	void VulkanTexture::SetMagFiltering(nCine::SamplerFilter filter)
	{
		magFilter_ = filter;
	}

	void VulkanTexture::SetWrap(SamplerWrapping wrap)
	{
		wrap_ = wrap;
	}

	void VulkanTexture::SetSwizzle(SwizzleChannel r, SwizzleChannel g, SwizzleChannel b, SwizzleChannel a)
	{
		swizzle_[0] = r;
		swizzle_[1] = g;
		swizzle_[2] = b;
		swizzle_[3] = a;
		// Applied through the image view's component mapping, so a change rebuilds the view (not the texels)
		viewDirty_ = true;
	}

	void VulkanTexture::SetMaxLevel(std::int32_t maxLevel)
	{
		static_cast<void>(maxLevel);
	}

	void VulkanTexture::SetUnpackAlignment(std::int32_t alignment)
	{
		static_cast<void>(alignment);
	}

	void VulkanTexture::SetObjectLabel(StringView label)
	{
		static_cast<void>(label);
	}
}
