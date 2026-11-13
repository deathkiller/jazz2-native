#if defined(WITH_RHI_GX2)

#include "Gx2RenderTarget.h"
#include "Gx2Texture.h"
#include "Gx2Device.h"

#include "../../../../Main.h"

#include <cstring>

namespace nCine::RHI::GX2
{
	Gx2RenderTarget::Gx2RenderTarget()
		: _numDrawBuffers(1), _surfaceTexture(nullptr), _surfaceImage(nullptr), _surfaceWidth(0), _surfaceHeight(0),
			_surfaceValid(false)
	{
		std::memset(&_colorBuffer, 0, sizeof(_colorBuffer));
		for (std::uint32_t i = 0; i < MaxColorAttachments; i++) {
			_colorTextures[i] = nullptr;
		}
	}

	Gx2RenderTarget::~Gx2RenderTarget()
	{
		// The device tracks the current target by pointer, so it has to forget this one before it goes
		Gx2Device::UnbindRenderTarget(this);

		// A texture that was a colour attachment goes back to being an ordinary sampled texture, so its
		// contents become the host store's business again
		for (std::uint32_t i = 0; i < MaxColorAttachments; i++) {
			if (_colorTextures[i] != nullptr) {
				_colorTextures[i]->RemoveAttachedTarget(this);
				_colorTextures[i]->SetRenderTarget(false);
				_colorTextures[i] = nullptr;
			}
		}
	}

	void Gx2RenderTarget::AttachColorTexture(Gx2Texture& texture, std::uint32_t index)
	{
		if (index >= MaxColorAttachments) {
			return;
		}
		if (_colorTextures[index] == &texture) {
			return;
		}
		if (_colorTextures[index] != nullptr) {
			_colorTextures[index]->RemoveAttachedTarget(this);
			_colorTextures[index]->SetRenderTarget(false);
		}
		_colorTextures[index] = &texture;
		texture.AddAttachedTarget(this);
		// From now on the GPU writes these texels, so the texture stops uploading its host store over them
		texture.SetRenderTarget(true);
		_surfaceValid = false;
	}

	void Gx2RenderTarget::DetachColorTexture(std::uint32_t index)
	{
		if (index >= MaxColorAttachments || _colorTextures[index] == nullptr) {
			return;
		}
		_colorTextures[index]->RemoveAttachedTarget(this);
		_colorTextures[index]->SetRenderTarget(false);
		_colorTextures[index] = nullptr;
		_surfaceValid = false;
	}

	void Gx2RenderTarget::ForgetColorTexture(const Gx2Texture& texture)
	{
		for (std::uint32_t i = 0; i < MaxColorAttachments; i++) {
			if (_colorTextures[i] == &texture) {
				_colorTextures[i] = nullptr;
			}
		}
		if (_surfaceTexture == &texture) {
			_surfaceTexture = nullptr;
		}
		_surfaceValid = false;
	}

	void Gx2RenderTarget::AttachDepthStencil(DepthStencilFormat format, std::int32_t width, std::int32_t height)
	{
		// Every target shares the device's one depth surface (see the class documentation), so there is
		// nothing to allocate - only the geometry is worth checking, because a target larger than the shared
		// surface would have depth testing read outside it
		static_cast<void>(format);
		if (width > Gx2Device::GetDisplayWidth() || height > Gx2Device::GetDisplayHeight()) {
			LOGW("A {}x{} render target is larger than the shared depth surface ({}x{}), so depth testing "
				"is disabled for it", width, height, Gx2Device::GetDisplayWidth(), Gx2Device::GetDisplayHeight());
		}
	}

	void Gx2RenderTarget::DetachDepthStencil(DepthStencilFormat format)
	{
		static_cast<void>(format);
	}

	void Gx2RenderTarget::BindDraw()
	{
		Gx2Device::SetRenderTarget(this);
	}

	void Gx2RenderTarget::UnbindDraw()
	{
		Gx2Device::SetRenderTarget(nullptr);
	}

	bool Gx2RenderTarget::SetDrawBuffers(std::uint32_t numColorAttachments)
	{
		if (numColorAttachments > MaxColorAttachments) {
			return false;
		}
		_numDrawBuffers = numColorAttachments;
		return true;
	}

	bool Gx2RenderTarget::IsStatusComplete()
	{
		return (_colorTextures[0] != nullptr && _colorTextures[0]->GetSurface() != nullptr);
	}

	void Gx2RenderTarget::InvalidateDepthStencil(DepthStencilFormat format)
	{
		static_cast<void>(format);
	}

	void Gx2RenderTarget::SetObjectLabel(StringView label)
	{
		static_cast<void>(label);
	}

	const GX2ColorBuffer* Gx2RenderTarget::GetColorBuffer(std::int32_t& width, std::int32_t& height)
	{
		const Gx2Texture* texture = _colorTextures[0];
		if (texture == nullptr) {
			return nullptr;
		}
		const GX2Surface* surface = texture->GetSurface();
		if (surface == nullptr) {
			return nullptr;
		}

		if (!_surfaceValid || _surfaceTexture != texture || _surfaceImage != surface->image ||
			_surfaceWidth != texture->GetWidth() || _surfaceHeight != texture->GetHeight()) {
			std::memset(&_colorBuffer, 0, sizeof(_colorBuffer));
			_colorBuffer.surface = *surface;
			_colorBuffer.viewMip = 0;
			_colorBuffer.viewFirstSlice = 0;
			_colorBuffer.viewNumSlices = 1;
			GX2InitColorBufferRegs(&_colorBuffer);

			_surfaceTexture = texture;
			_surfaceImage = surface->image;
			_surfaceWidth = texture->GetWidth();
			_surfaceHeight = texture->GetHeight();
			_surfaceValid = true;
		}

		width = _surfaceWidth;
		height = _surfaceHeight;
		return &_colorBuffer;
	}
}

#endif
