#include "GxmRenderTarget.h"
#include "GxmTexture.h"
#include "GxmDevice.h"

#include "../../../../Main.h"

#include <cstring>

namespace nCine::RHI::GXM
{
	GxmRenderTarget::GxmRenderTarget()
		: numDrawBuffers_(MaxColorAttachments), gxmRenderTarget_(nullptr), syncObject_(nullptr), surfaceTexture_(nullptr),
			surfaceData_(nullptr), surfaceWidth_(0), surfaceHeight_(0), surfaceValid_(false)
	{
		for (std::uint32_t i = 0; i < MaxColorAttachments; i++) {
			colorTextures_[i] = nullptr;
		}
		std::memset(&colorSurface_, 0, sizeof(colorSurface_));
	}

	GxmRenderTarget::~GxmRenderTarget()
	{
		// Drop the device's pointer to this target before its sceGxm objects go away, and make sure no scene
		// still recording into it is left open
		GxmDevice::UnbindRenderTarget(this);
		ReleaseSceneTarget();

		for (std::uint32_t i = 0; i < MaxColorAttachments; i++) {
			if (colorTextures_[i] != nullptr) {
				colorTextures_[i]->SetRenderTarget(false);
				colorTextures_[i] = nullptr;
			}
		}
	}

	void GxmRenderTarget::ReleaseSceneTarget()
	{
		if (gxmRenderTarget_ != nullptr) {
			// The GPU must not still be binning into this target when its data structures are destroyed
			GxmDevice::FinishScene();
			if (SceGxmContext* context = GxmDevice::GetContext()) {
				sceGxmFinish(context);
			}
			sceGxmDestroyRenderTarget(gxmRenderTarget_);
			gxmRenderTarget_ = nullptr;
		}
		if (syncObject_ != nullptr) {
			sceGxmSyncObjectDestroy(syncObject_);
			syncObject_ = nullptr;
		}
		surfaceValid_ = false;
		surfaceTexture_ = nullptr;
		surfaceData_ = nullptr;
		surfaceWidth_ = 0;
		surfaceHeight_ = 0;
	}

	void GxmRenderTarget::AttachColorTexture(GxmTexture& texture, std::uint32_t index)
	{
		if (index >= MaxColorAttachments) {
			LOGW("sceGxm binds one colour surface per scene, so colour attachment {} is not available", index);
			return;
		}
		if (colorTextures_[index] == &texture) {
			return;
		}

		if (colorTextures_[index] != nullptr) {
			colorTextures_[index]->SetRenderTarget(false);
		}
		colorTextures_[index] = &texture;
		// The texture is written by the GPU from now on, which changes how its GPU-side copy is allocated
		texture.SetRenderTarget(true);
		ReleaseSceneTarget();
	}

	void GxmRenderTarget::DetachColorTexture(std::uint32_t index)
	{
		if (index >= MaxColorAttachments || colorTextures_[index] == nullptr) {
			return;
		}
		colorTextures_[index]->SetRenderTarget(false);
		colorTextures_[index] = nullptr;
		ReleaseSceneTarget();
	}

	void GxmRenderTarget::AttachDepthStencil(DepthStencilFormat format, std::int32_t width, std::int32_t height)
	{
		// Every scene shares the device's depth/stencil surface, so there is nothing to allocate here
		static_cast<void>(format);
		static_cast<void>(width);
		static_cast<void>(height);
	}

	void GxmRenderTarget::DetachDepthStencil(DepthStencilFormat format)
	{
		static_cast<void>(format);
	}

	void GxmRenderTarget::BindDraw()
	{
		GxmDevice::SetRenderTarget(this);
	}

	void GxmRenderTarget::UnbindDraw()
	{
		GxmDevice::SetRenderTarget(nullptr);
	}

	bool GxmRenderTarget::SetDrawBuffers(std::uint32_t numColorAttachments)
	{
		if (numColorAttachments > MaxColorAttachments) {
			return false;
		}
		numDrawBuffers_ = numColorAttachments;
		return true;
	}

	bool GxmRenderTarget::IsStatusComplete()
	{
		SceGxmRenderTarget* renderTarget;
		SceGxmColorSurface* colorSurface;
		SceGxmSyncObject* syncObject;
		std::int32_t width, height;
		return GetSceneTarget(renderTarget, colorSurface, syncObject, width, height);
	}

	void GxmRenderTarget::InvalidateDepthStencil(DepthStencilFormat format)
	{
		static_cast<void>(format);
	}

	void GxmRenderTarget::SetObjectLabel(StringView label)
	{
		static_cast<void>(label);
	}

	bool GxmRenderTarget::GetSceneTarget(SceGxmRenderTarget*& renderTarget, SceGxmColorSurface*& colorSurface,
		SceGxmSyncObject*& syncObject, std::int32_t& width, std::int32_t& height)
	{
		GxmTexture* texture = colorTextures_[0];
		if (texture == nullptr || numDrawBuffers_ == 0) {
			return false;
		}

		void* surfaceData = texture->GetSurfaceData();
		if (surfaceData == nullptr) {
			return false;
		}

		// Rebuild when the attachment, its size or its GPU allocation changed under us (an upload that
		// resizes the texture reallocates the block the surface points at)
		if (surfaceValid_ && (surfaceTexture_ != texture || surfaceData_ != surfaceData ||
				surfaceWidth_ != texture->GetWidth() || surfaceHeight_ != texture->GetHeight())) {
			ReleaseSceneTarget();
			surfaceData = texture->GetSurfaceData();
			if (surfaceData == nullptr) {
				return false;
			}
		}

		if (!surfaceValid_) {
			const std::int32_t targetWidth = texture->GetWidth();
			const std::int32_t targetHeight = texture->GetHeight();
			if (targetWidth <= 0 || targetHeight <= 0) {
				return false;
			}

			SceGxmRenderTargetParams params = {};
			params.flags = 0;
			params.width = std::uint16_t(targetWidth);
			params.height = std::uint16_t(targetHeight);
			// The pipeline can render into the same target more than once in a frame (a blur chain ping-pongs
			// between its levels); 8 is the largest sceGxm accepts and only sizes the driver's own bookkeeping
			params.scenesPerFrame = 8;
			params.multisampleMode = SCE_GXM_MULTISAMPLE_NONE;
			params.multisampleLocations = 0;
			params.driverMemBlock = GxmMemory::InvalidUid;

			std::int32_t result = sceGxmSyncObjectCreate(&syncObject_);
			if (result < 0) {
				LOGE("sceGxmSyncObjectCreate() for a {}x{} render target failed with 0x{:.8x}",
					targetWidth, targetHeight, std::uint32_t(result));
				syncObject_ = nullptr;
				return false;
			}

			result = sceGxmCreateRenderTarget(&params, &gxmRenderTarget_);
			if (result < 0) {
				LOGE("sceGxmCreateRenderTarget({}x{}) failed with 0x{:.8x}", targetWidth, targetHeight, std::uint32_t(result));
				gxmRenderTarget_ = nullptr;
				return false;
			}

			// The colour surface's stride is the texture's own row pitch, so the GPU renders straight into the
			// texels the shaders will sample afterwards - no resolve step in between
			// Tiled, to match the layout the attachment's texture describes (see GxmTexture::EnsureGpuTexture()):
			// a linear texture cannot sample outside [0, 1], and every one of these targets is later read by a
			// pass that does. The stride is the tile-padded width the texture allocated.
			result = sceGxmColorSurfaceInit(&colorSurface_, SCE_GXM_COLOR_FORMAT_U8U8U8U8_ABGR,
				SCE_GXM_COLOR_SURFACE_TILED, SCE_GXM_COLOR_SURFACE_SCALE_NONE, SCE_GXM_OUTPUT_REGISTER_SIZE_32BIT,
				std::uint32_t(targetWidth), std::uint32_t(targetHeight), texture->GetSurfaceStride() / 4u, surfaceData);
			if (result < 0) {
				LOGE("sceGxmColorSurfaceInit({}x{}) failed with 0x{:.8x}", targetWidth, targetHeight, std::uint32_t(result));
				sceGxmDestroyRenderTarget(gxmRenderTarget_);
				gxmRenderTarget_ = nullptr;
				return false;
			}

			surfaceTexture_ = texture;
			surfaceData_ = surfaceData;
			surfaceWidth_ = targetWidth;
			surfaceHeight_ = targetHeight;
			surfaceValid_ = true;
		}

		renderTarget = gxmRenderTarget_;
		colorSurface = &colorSurface_;
		syncObject = syncObject_;
		width = surfaceWidth_;
		height = surfaceHeight_;
		return true;
	}
}
