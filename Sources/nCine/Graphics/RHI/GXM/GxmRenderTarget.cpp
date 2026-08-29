#include "GxmRenderTarget.h"
#include "GxmTexture.h"
#include "GxmDevice.h"

#include "../../../../Main.h"

#include <cstring>

namespace nCine::RHI::GXM
{
	GxmRenderTarget::GxmRenderTarget()
		: _numDrawBuffers(MaxColorAttachments), _gxmRenderTarget(nullptr), _syncObject(nullptr), _surfaceTexture(nullptr),
			_surfaceData(nullptr), _surfaceWidth(0), _surfaceHeight(0), _surfaceValid(false)
	{
		for (std::uint32_t i = 0; i < MaxColorAttachments; i++) {
			_colorTextures[i] = nullptr;
		}
		std::memset(&_colorSurface, 0, sizeof(_colorSurface));
	}

	GxmRenderTarget::~GxmRenderTarget()
	{
		// Drop the device's pointer to this target before its sceGxm objects go away, and make sure no scene
		// still recording into it is left open
		GxmDevice::UnbindRenderTarget(this);
		ReleaseSceneTarget();

		for (std::uint32_t i = 0; i < MaxColorAttachments; i++) {
			if (_colorTextures[i] != nullptr) {
				_colorTextures[i]->SetRenderTarget(false);
				_colorTextures[i] = nullptr;
			}
		}
	}

	void GxmRenderTarget::ReleaseSceneTarget()
	{
		if (_gxmRenderTarget != nullptr) {
			// The GPU must not still be binning into this target when its data structures are destroyed
			GxmDevice::FinishScene();
			if (SceGxmContext* context = GxmDevice::GetContext()) {
				sceGxmFinish(context);
			}
			sceGxmDestroyRenderTarget(_gxmRenderTarget);
			_gxmRenderTarget = nullptr;
		}
		if (_syncObject != nullptr) {
			sceGxmSyncObjectDestroy(_syncObject);
			_syncObject = nullptr;
		}
		_surfaceValid = false;
		_surfaceTexture = nullptr;
		_surfaceData = nullptr;
		_surfaceWidth = 0;
		_surfaceHeight = 0;
	}

	void GxmRenderTarget::AttachColorTexture(GxmTexture& texture, std::uint32_t index)
	{
		if (index >= MaxColorAttachments) {
			LOGW("sceGxm binds one colour surface per scene, so colour attachment {} is not available", index);
			return;
		}
		if (_colorTextures[index] == &texture) {
			return;
		}

		if (_colorTextures[index] != nullptr) {
			_colorTextures[index]->SetRenderTarget(false);
		}
		_colorTextures[index] = &texture;
		// The texture is written by the GPU from now on, which changes how its GPU-side copy is allocated
		texture.SetRenderTarget(true);
		ReleaseSceneTarget();
	}

	void GxmRenderTarget::DetachColorTexture(std::uint32_t index)
	{
		if (index >= MaxColorAttachments || _colorTextures[index] == nullptr) {
			return;
		}
		_colorTextures[index]->SetRenderTarget(false);
		_colorTextures[index] = nullptr;
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
		_numDrawBuffers = numColorAttachments;
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
		GxmTexture* texture = _colorTextures[0];
		if (texture == nullptr || _numDrawBuffers == 0) {
			return false;
		}

		void* surfaceData = texture->GetSurfaceData();
		if (surfaceData == nullptr) {
			return false;
		}

		// Rebuild when the attachment, its size or its GPU allocation changed under us (an upload that
		// resizes the texture reallocates the block the surface points at)
		if (_surfaceValid && (_surfaceTexture != texture || _surfaceData != surfaceData ||
				_surfaceWidth != texture->GetWidth() || _surfaceHeight != texture->GetHeight())) {
			ReleaseSceneTarget();
			surfaceData = texture->GetSurfaceData();
			if (surfaceData == nullptr) {
				return false;
			}
		}

		if (!_surfaceValid) {
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

			std::int32_t result = sceGxmSyncObjectCreate(&_syncObject);
			if (result < 0) {
				LOGE("sceGxmSyncObjectCreate() for a {}x{} render target failed with 0x{:.8x}",
					targetWidth, targetHeight, std::uint32_t(result));
				_syncObject = nullptr;
				return false;
			}

			result = sceGxmCreateRenderTarget(&params, &_gxmRenderTarget);
			if (result < 0) {
				LOGE("sceGxmCreateRenderTarget({}x{}) failed with 0x{:.8x}", targetWidth, targetHeight, std::uint32_t(result));
				_gxmRenderTarget = nullptr;
				sceGxmSyncObjectDestroy(_syncObject);
				_syncObject = nullptr;
				return false;
			}

			// The colour surface's stride is the texture's own row pitch, so the GPU renders straight into the
			// texels the shaders will sample afterwards - no resolve step in between
			// Tiled, to match the layout the attachment's texture describes (see GxmTexture::EnsureGpuTexture()):
			// a linear texture cannot sample outside [0, 1], and every one of these targets is later read by a
			// pass that does. The stride is the tile-padded width the texture allocated.
			result = sceGxmColorSurfaceInit(&_colorSurface, SCE_GXM_COLOR_FORMAT_U8U8U8U8_ABGR,
				SCE_GXM_COLOR_SURFACE_TILED, SCE_GXM_COLOR_SURFACE_SCALE_NONE, SCE_GXM_OUTPUT_REGISTER_SIZE_32BIT,
				std::uint32_t(targetWidth), std::uint32_t(targetHeight), texture->GetSurfaceStride() / 4u, surfaceData);
			if (result < 0) {
				LOGE("sceGxmColorSurfaceInit({}x{}) failed with 0x{:.8x}", targetWidth, targetHeight, std::uint32_t(result));
				sceGxmDestroyRenderTarget(_gxmRenderTarget);
				_gxmRenderTarget = nullptr;
				sceGxmSyncObjectDestroy(_syncObject);
				_syncObject = nullptr;
				return false;
			}

			_surfaceTexture = texture;
			_surfaceData = surfaceData;
			_surfaceWidth = targetWidth;
			_surfaceHeight = targetHeight;
			_surfaceValid = true;
		}

		renderTarget = _gxmRenderTarget;
		colorSurface = &_colorSurface;
		syncObject = _syncObject;
		width = _surfaceWidth;
		height = _surfaceHeight;
		return true;
	}
}
