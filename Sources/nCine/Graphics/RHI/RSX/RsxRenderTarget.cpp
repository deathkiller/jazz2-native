#if defined(WITH_RHI_RSX)

#include "RsxRenderTarget.h"
#include "RsxTexture.h"
#include "RsxDevice.h"

#include "../../../../Main.h"

#include <cstring>

namespace nCine::RHI::RSX
{
	RsxRenderTarget::RsxRenderTarget()
		: _numDrawBuffers(1), _surfaceTexture(nullptr), _surfaceData(nullptr), _surfaceOffset(0),
			_surfacePitch(0), _surfaceWidth(0), _surfaceHeight(0), _surfaceValid(false)
	{
		for (std::uint32_t i = 0; i < MaxColorAttachments; i++) {
			_colorTextures[i] = nullptr;
		}
	}

	RsxRenderTarget::~RsxRenderTarget()
	{
		// The device tracks the current target by pointer, so it has to forget this one before it goes
		RsxDevice::UnbindRenderTarget(this);

		// A texture that was a colour attachment goes back to being an ordinary sampled texture, so its
		// contents become the host store's business again
		for (std::uint32_t i = 0; i < MaxColorAttachments; i++) {
			if (_colorTextures[i] != nullptr) {
				_colorTextures[i]->SetRenderTarget(false);
				_colorTextures[i] = nullptr;
			}
		}
	}

	void RsxRenderTarget::AttachColorTexture(RsxTexture& texture, std::uint32_t index)
	{
		if (index >= MaxColorAttachments) {
			return;
		}
		if (_colorTextures[index] == &texture) {
			return;
		}
		if (_colorTextures[index] != nullptr) {
			_colorTextures[index]->SetRenderTarget(false);
		}
		_colorTextures[index] = &texture;
		// From now on the GPU writes these texels, so the texture stops uploading its host store over them
		texture.SetRenderTarget(true);
		_surfaceValid = false;
	}

	void RsxRenderTarget::DetachColorTexture(std::uint32_t index)
	{
		if (index >= MaxColorAttachments || _colorTextures[index] == nullptr) {
			return;
		}
		_colorTextures[index]->SetRenderTarget(false);
		_colorTextures[index] = nullptr;
		_surfaceValid = false;
	}

	void RsxRenderTarget::AttachDepthStencil(DepthStencilFormat format, std::int32_t width, std::int32_t height)
	{
		// Every target shares the device's one depth surface (see the class documentation), so there is
		// nothing to allocate - only the geometry is worth checking, because a target larger than the shared
		// surface would have depth testing read outside it
		static_cast<void>(format);
		if (width > RsxDevice::GetDisplayWidth() || height > RsxDevice::GetDisplayHeight()) {
			LOGW("A {}x{} render target is larger than the shared depth surface ({}x{}), so depth testing "
				"is disabled for it", width, height, RsxDevice::GetDisplayWidth(), RsxDevice::GetDisplayHeight());
		}
	}

	void RsxRenderTarget::DetachDepthStencil(DepthStencilFormat format)
	{
		static_cast<void>(format);
	}

	void RsxRenderTarget::BindDraw()
	{
		RsxDevice::SetRenderTarget(this);
	}

	void RsxRenderTarget::UnbindDraw()
	{
		RsxDevice::SetRenderTarget(nullptr);
	}

	bool RsxRenderTarget::SetDrawBuffers(std::uint32_t numColorAttachments)
	{
		if (numColorAttachments > MaxColorAttachments) {
			return false;
		}
		_numDrawBuffers = numColorAttachments;
		return true;
	}

	bool RsxRenderTarget::IsStatusComplete()
	{
		return (_colorTextures[0] != nullptr && _colorTextures[0]->GetSurfaceData() != nullptr);
	}

	void RsxRenderTarget::InvalidateDepthStencil(DepthStencilFormat format)
	{
		static_cast<void>(format);
	}

	void RsxRenderTarget::SetObjectLabel(StringView label)
	{
		static_cast<void>(label);
	}

	bool RsxRenderTarget::GetDrawSurface(gcmSurface& surface, std::int32_t& width, std::int32_t& height)
	{
		RsxTexture* texture = _colorTextures[0];
		if (texture == nullptr) {
			return false;
		}

		// Materializes the texture's GPU storage if this is the first use, and gives the address the surface
		// is described by. A texture that reallocated (an upload changed its size) hands back a different
		// one, which is exactly what invalidates the cached description below.
		void* surfaceData = texture->GetSurfaceData();
		if (surfaceData == nullptr) {
			return false;
		}

		if (!_surfaceValid || _surfaceTexture != texture || _surfaceData != surfaceData ||
			_surfaceWidth != texture->GetWidth() || _surfaceHeight != texture->GetHeight()) {
			_surfaceTexture = texture;
			_surfaceData = surfaceData;
			_surfacePitch = texture->GetSurfaceStride();
			_surfaceWidth = texture->GetWidth();
			_surfaceHeight = texture->GetHeight();

			// The offset is what the hardware addresses the surface by, and it comes from the same block the
			// texture samples - a render target and the texture reading it back are one allocation here,
			// which is why there is no resolve or copy step between a pass and the pass that samples it
			if (const gcmTexture* gcmTex = texture->GetGcmTexture()) {
				_surfaceOffset = gcmTex->offset;
				_surfaceValid = true;
			} else {
				_surfaceValid = false;
				return false;
			}
		}

		std::memset(&surface, 0, sizeof(surface));
		surface.colorFormat = GCM_SURFACE_A8R8G8B8;
		surface.colorTarget = GCM_SURFACE_TARGET_0;
		surface.colorLocation[0] = GCM_LOCATION_RSX;
		surface.colorOffset[0] = _surfaceOffset;
		surface.colorPitch[0] = _surfacePitch;
		// The three unused colour targets still need a legal pitch: the hardware validates all four pitch
		// fields whether or not the target is enabled, and rejects a zero
		for (std::uint32_t i = 1; i < 4; i++) {
			surface.colorLocation[i] = GCM_LOCATION_RSX;
			surface.colorOffset[i] = 0;
			surface.colorPitch[i] = 64;
		}
		surface.type = GCM_SURFACE_TYPE_LINEAR;
		surface.antiAlias = GCM_SURFACE_CENTER_1;
		surface.width = std::uint32_t(_surfaceWidth);
		surface.height = std::uint32_t(_surfaceHeight);
		surface.x = 0;
		surface.y = 0;

		width = _surfaceWidth;
		height = _surfaceHeight;
		return true;
	}
}

#endif
