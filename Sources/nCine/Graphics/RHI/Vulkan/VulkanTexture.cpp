#include "VulkanTexture.h"
#include "VulkanDevice.h"
#include "VulkanCommon.h"

#include <cstring>

namespace nCine::RHI::Vulkan
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

	std::uint32_t VulkanTexture::_nextHandle = 1;

	VulkanTexture::VulkanTexture(TextureTarget target)
		: _handle(_nextHandle++), _target(target), _format(PixelFormat::Unknown), _uploadFormat(PixelFormat::Unknown),
			_width(0), _height(0), _strideBytes(0),
			_minFilter(nCine::SamplerFilter::Nearest), _magFilter(nCine::SamplerFilter::Nearest), _wrap(SamplerWrapping::ClampToEdge),
			_textureUnit(0), _isRenderTarget(false),
			_gpuImage(0), _gpuMemory(0), _gpuView(0), _gpuSampler(0),
			_currentLayout(VK_IMAGE_LAYOUT_UNDEFINED), _gpuFormat(VK_FORMAT_R8G8B8A8_UNORM),
			_contentsDirty(false), _viewDirty(false), _hasCpuData(false),
			_samplerFilter(nCine::SamplerFilter::Unknown), _samplerWrap(SamplerWrapping::Unknown)
	{
		_swizzle[0] = SwizzleChannel::Red;
		_swizzle[1] = SwizzleChannel::Green;
		_swizzle[2] = SwizzleChannel::Blue;
		_swizzle[3] = SwizzleChannel::Alpha;
	}

	VulkanTexture::~VulkanTexture()
	{
		// Unbind from the device first so a later draw can't dereference this freed texture via _boundTextures
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
		VkEnqueueDestroy(VkDeferredResource::ImageView, _gpuView);
		VkEnqueueDestroy(VkDeferredResource::Sampler, _gpuSampler);
		VkEnqueueDestroy(VkDeferredResource::Image, _gpuImage);
		VkEnqueueDestroy(VkDeferredResource::DeviceMemory, _gpuMemory);
		_gpuView = 0;
		_gpuSampler = 0;
		_gpuImage = 0;
		_gpuMemory = 0;
		_currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
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
		_uploadFormat = format;
		_format = (format == PixelFormat::RGB8 || format == PixelFormat::R8 || format == PixelFormat::RG8) ? PixelFormat::RGBA8 : format;
		_width = width;
		_height = height;
		const std::int32_t bpp = BytesPerPixel(_format);
		_strideBytes = width * bpp;
		_pixels.assign(std::size_t(_strideBytes) * std::size_t(height > 0 ? height : 0), std::uint8_t(0));
		// The size/format changed, so the GPU image must be rebuilt on the next bind
		ReleaseGpu();
		_contentsDirty = true;
		_viewDirty = true;
	}

	void VulkanTexture::EnsureGpu() const
	{
		VkDevice device = VkDeviceHandle();
		if (device == VK_NULL_HANDLE || _width <= 0 || _height <= 0) {
			return;
		}

		if (_gpuImage == 0) {
			_gpuFormat = std::uint32_t(MapVkFormat(_format));
			VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT;
			if (_isRenderTarget) {
				usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
			} else {
				usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
			}

			VkImageCreateInfo ici = {};
			ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
			ici.imageType = VK_IMAGE_TYPE_2D;
			ici.format = VkFormat(_gpuFormat);
			ici.extent = { std::uint32_t(_width), std::uint32_t(_height), 1 };
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
			_gpuImage = reinterpret_cast<std::uint64_t>(image);
			_gpuMemory = reinterpret_cast<std::uint64_t>(memory);
			_currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			_viewDirty = true;

			if (_isRenderTarget) {
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
					_currentLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
				}
			}
		}

		// NOTE: CPU texel uploads are NOT recorded here anymore. EnsureGpu() only materializes the image / view /
		// sampler; the staging copy is recorded into the current FRAME's command buffer by RecordStreamingUpload()
		// (driven from the device's pre-draw phase, outside any render pass), so per-frame texture streaming (the
		// palette) no longer forces a vkQueueWaitIdle stall every frame. _contentsDirty stays set until then.

		if (_viewDirty || _gpuView == 0) {
			if (_gpuView != 0) {
				// Defer: an in-flight frame's descriptor set may still sample through the old view (see ReleaseGpu)
				VkEnqueueDestroy(VkDeferredResource::ImageView, _gpuView);
				_gpuView = 0;
			}
			VkImageViewCreateInfo ivci = {};
			ivci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			ivci.image = reinterpret_cast<VkImage>(_gpuImage);
			ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
			ivci.format = VkFormat(_gpuFormat);
			// The sampling swizzle is applied here (no texel baking) - e.g. RG8 palette textures map A<-Green
			ivci.components.r = MapSwizzle(_swizzle[0], VK_COMPONENT_SWIZZLE_R);
			ivci.components.g = MapSwizzle(_swizzle[1], VK_COMPONENT_SWIZZLE_G);
			ivci.components.b = MapSwizzle(_swizzle[2], VK_COMPONENT_SWIZZLE_B);
			ivci.components.a = MapSwizzle(_swizzle[3], VK_COMPONENT_SWIZZLE_A);
			ivci.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
			VkImageView view = VK_NULL_HANDLE;
			if (vkCreateImageView(device, &ivci, nullptr, &view) == VK_SUCCESS) {
				_gpuView = reinterpret_cast<std::uint64_t>(view);
			}
			_viewDirty = false;
		}

		if (_gpuSampler == 0 || _samplerFilter != _magFilter || _samplerWrap != _wrap) {
			if (_gpuSampler != 0) {
				// Defer: an in-flight frame's descriptor set may still use the old sampler (see ReleaseGpu)
				VkEnqueueDestroy(VkDeferredResource::Sampler, _gpuSampler);
				_gpuSampler = 0;
			}
			const bool linear = (_magFilter == nCine::SamplerFilter::Linear ||
				_magFilter == nCine::SamplerFilter::LinearMipmapNearest || _magFilter == nCine::SamplerFilter::LinearMipmapLinear);
			VkSamplerAddressMode address;
			switch (_wrap) {
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
				_gpuSampler = reinterpret_cast<std::uint64_t>(sampler);
			}
			_samplerFilter = _magFilter;
			_samplerWrap = _wrap;
		}
	}

	void VulkanTexture::RecordStreamingUpload(std::uint64_t cmdBuffer) const
	{
		VkDevice device = VkDeviceHandle();
		if (device == VK_NULL_HANDLE || cmdBuffer == 0) {
			return;
		}
		EnsureGpu();	// materialize the image / view / sampler (no upload)
		if (_gpuImage == 0 || !_contentsDirty || !_hasCpuData || _isRenderTarget || _pixels.empty()) {
			return;
		}
		VkCommandBuffer cmd = reinterpret_cast<VkCommandBuffer>(cmdBuffer);
		VkImage image = reinterpret_cast<VkImage>(_gpuImage);

		// Host-visible staging buffer holding the whole level-0 store; the device frees it once the frame's fence signals
		VkBuffer staging = VK_NULL_HANDLE;
		VkDeviceMemory stagingMem = VK_NULL_HANDLE;
		VkBufferCreateInfo bci = {};
		bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bci.size = _pixels.size();
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
		std::memcpy(mapped, _pixels.data(), _pixels.size());
		vkUnmapMemory(device, stagingMem);

		// Barrier into TRANSFER_DST. On a re-upload of a texture the OTHER in-flight frame sampled
		// (_currentLayout == SHADER_READ_ONLY), the source scope is FRAGMENT_SHADER / SHADER_READ so this write
		// waits (via queue submission order) for that frame's samples - the shared-image WAR guard for streaming
		// textures like the palette. A fresh image is UNDEFINED (its contents are discarded, no wait needed).
		const VkImageLayout oldLayout = VkImageLayout(_currentLayout);
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
		region.imageExtent = { std::uint32_t(_width), std::uint32_t(_height), 1 };
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

		_currentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		_contentsDirty = false;
		VkRegisterFrameStaging(reinterpret_cast<std::uint64_t>(staging), reinterpret_cast<std::uint64_t>(stagingMem));
	}

	std::uint64_t VulkanTexture::GpuImage() const
	{
		EnsureGpu();
		return _gpuImage;
	}

	std::uint64_t VulkanTexture::GpuView() const
	{
		EnsureGpu();
		return _gpuView;
	}

	std::uint64_t VulkanTexture::GpuSampler() const
	{
		EnsureGpu();
		return _gpuSampler;
	}

	std::uint32_t VulkanTexture::GpuFormat() const
	{
		return _gpuFormat;
	}

	bool VulkanTexture::Bind(std::uint32_t textureUnit) const
	{
		_textureUnit = textureUnit;
		VulkanDevice::BindTexture(textureUnit, this);
		return true;
	}

	bool VulkanTexture::Unbind() const
	{
		VulkanDevice::BindTexture(_textureUnit, nullptr);
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
		if (data != nullptr && !_pixels.empty()) {
			_hasCpuData = true;
			const std::int32_t srcBpp = BytesPerPixel(format);
			const std::int32_t dstBpp = BytesPerPixel(_format);
			if (srcBpp == dstBpp) {
				std::memcpy(_pixels.data(), data, _pixels.size());
			} else {
				const std::uint8_t* src = static_cast<const std::uint8_t*>(data);
				for (std::int32_t y = 0; y < _height; y++) {
					CopyExpandRow(_pixels.data() + std::size_t(y) * _strideBytes,
						dstBpp, src + std::size_t(y) * std::size_t(_width) * srcBpp, srcBpp, _width);
				}
			}
		}
	}

	void VulkanTexture::TexSubImage2D(std::int32_t level, std::int32_t xoffset, std::int32_t yoffset, std::int32_t width, std::int32_t height, PixelFormat format, bool bgr, const void* data)
	{
		static_cast<void>(bgr);
		if (level != 0 || data == nullptr || _pixels.empty()) {
			return;
		}
		_hasCpuData = true;
		_contentsDirty = true;
		const std::int32_t srcBpp = BytesPerPixel(format);
		const std::int32_t dstBpp = BytesPerPixel(_format);
		for (std::int32_t y = 0; y < height; y++) {
			const std::int32_t dstY = yoffset + y;
			if (dstY < 0 || dstY >= _height) {
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
			if (dstX + copyW > _width) {
				copyW = _width - dstX;
			}
			if (copyW <= 0) {
				continue;
			}
			const std::uint8_t* srcRow = static_cast<const std::uint8_t*>(data) + std::size_t(y) * std::size_t(width) * srcBpp + std::size_t(srcX0) * srcBpp;
			std::uint8_t* dstRow = _pixels.data() + std::size_t(dstY) * _strideBytes + std::size_t(dstX) * dstBpp;
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
		if (pixels == nullptr) {
			return;
		}

		// A render target's contents only exist on the GPU (draws never touch the host store), so read them
		// back through a staging buffer with a one-time submit + queue-idle wait. Readback is a screenshot-rate
		// operation, so the synchronous wait is acceptable.
		if (_isRenderTarget && _gpuImage != 0 && _width > 0 && _height > 0 && vkCmdCopyImageToBuffer != nullptr) {
			VkDevice device = VkDeviceHandle();
			if (device != VK_NULL_HANDLE) {
				const VkDeviceSize byteSize = VkDeviceSize(_width) * VkDeviceSize(_height) * 4;
				VkBuffer staging = VK_NULL_HANDLE;
				VkDeviceMemory stagingMem = VK_NULL_HANDLE;
				VkBufferCreateInfo bci = {};
				bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
				bci.size = byteSize;
				bci.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
				bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
				if (vkCreateBuffer(device, &bci, nullptr, &staging) == VK_SUCCESS) {
					VkMemoryRequirements req;
					vkGetBufferMemoryRequirements(device, staging, &req);
					const std::uint32_t memType = VkFindMemoryType(req.memoryTypeBits,
						VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
					VkMemoryAllocateInfo mai = {};
					mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
					mai.allocationSize = req.size;
					mai.memoryTypeIndex = memType;
					if (memType != UINT32_MAX && vkAllocateMemory(device, &mai, nullptr, &stagingMem) == VK_SUCCESS) {
						vkBindBufferMemory(device, staging, stagingMem, 0);

						VkCommandBuffer cmd = VkBeginOneTimeCommands();
						if (cmd != VK_NULL_HANDLE) {
							VkImage image = reinterpret_cast<VkImage>(_gpuImage);
							const VkImageLayout oldLayout = VkImageLayout(_currentLayout);

							// Conservative full-scope barriers: this is a synchronous, out-of-frame submit
							VkImageMemoryBarrier toSrc = {};
							toSrc.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
							toSrc.oldLayout = oldLayout;
							toSrc.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
							toSrc.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
							toSrc.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
							toSrc.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
							toSrc.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
							toSrc.image = image;
							toSrc.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
							vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
								0, 0, nullptr, 0, nullptr, 1, &toSrc);

							VkBufferImageCopy region = {};
							region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
							region.imageExtent = { std::uint32_t(_width), std::uint32_t(_height), 1 };
							vkCmdCopyImageToBuffer(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging, 1, &region);

							VkImageMemoryBarrier restore = toSrc;
							restore.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
							restore.newLayout = oldLayout;
							restore.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
							restore.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
							vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
								0, 0, nullptr, 0, nullptr, 1, &restore);

							VkEndOneTimeCommands(cmd);	// submits + waits for the queue to go idle

							void* mapped = nullptr;
							if (vkMapMemory(device, stagingMem, 0, VK_WHOLE_SIZE, 0, &mapped) == VK_SUCCESS) {
								std::memcpy(pixels, mapped, std::size_t(byteSize));
								vkUnmapMemory(device, stagingMem);
								vkDestroyBuffer(device, staging, nullptr);
								vkFreeMemory(device, stagingMem, nullptr);
								return;
							}
						}
						vkFreeMemory(device, stagingMem, nullptr);
					}
					vkDestroyBuffer(device, staging, nullptr);
				}
			}
		}

		if (!_pixels.empty()) {
			std::memcpy(pixels, _pixels.data(), _pixels.size());
		}
	}

	void VulkanTexture::SetMinFiltering(nCine::SamplerFilter filter)
	{
		_minFilter = filter;
	}

	void VulkanTexture::SetMagFiltering(nCine::SamplerFilter filter)
	{
		_magFilter = filter;
	}

	void VulkanTexture::SetWrap(SamplerWrapping wrap)
	{
		_wrap = wrap;
	}

	void VulkanTexture::SetSwizzle(SwizzleChannel r, SwizzleChannel g, SwizzleChannel b, SwizzleChannel a)
	{
		_swizzle[0] = r;
		_swizzle[1] = g;
		_swizzle[2] = b;
		_swizzle[3] = a;
		// Applied through the image view's component mapping, so a change rebuilds the view (not the texels)
		_viewDirty = true;
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
