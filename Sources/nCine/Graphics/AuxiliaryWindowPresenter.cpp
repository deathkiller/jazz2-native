#include "AuxiliaryWindowPresenter.h"

#if defined(NCINE_HAS_AUXILIARY_WINDOWS)

#include "Texture.h"
#include "RHI/Rhi.h"

// Desktop OpenGL, Direct3D 11, Vulkan and Metal can present into another window
#if (defined(WITH_RHI_GL) && !defined(RHI_GL_PROFILE_ES)) || defined(WITH_RHI_D3D11) || defined(WITH_RHI_VULKAN) || defined(WITH_RHI_METAL)
#	define NCINE_AUXILIARY_PRESENT
#endif

namespace nCine
{
	bool AuxiliaryWindowPresenter::IsSupported()
	{
#if defined(NCINE_AUXILIARY_PRESENT)
		return true;
#else
		return false;
#endif
	}

	std::unique_ptr<AuxiliaryWindowPresenter> AuxiliaryWindowPresenter::Create(AuxiliaryWindowNative&& native, Vector2i pixelSize)
	{
#if defined(NCINE_AUXILIARY_PRESENT)
		std::unique_ptr<AuxiliaryWindowPresenter> presenter(new AuxiliaryWindowPresenter(Death::move(native), pixelSize));
#	if defined(WITH_RHI_GL)
		if (!presenter->_native.MakeCurrent || !presenter->_native.SwapBuffers || !presenter->_native.RestoreMain) {
			return nullptr;
		}
#	else
		if (presenter->_swapchain == nullptr) {
			return nullptr;
		}
#	endif
		return presenter;
#else
		static_cast<void>(native);
		static_cast<void>(pixelSize);
		return nullptr;
#endif
	}

	AuxiliaryWindowPresenter::AuxiliaryWindowPresenter(AuxiliaryWindowNative&& native, Vector2i pixelSize)
		: _native(Death::move(native)), _size(pixelSize)
	{
#if defined(NCINE_AUXILIARY_PRESENT) && !defined(WITH_RHI_GL)
		if (_native.Handle != nullptr) {
			_swapchain = RHI::Device::CreateAuxiliarySwapchain(_native.Handle, std::max(pixelSize.X, 1), std::max(pixelSize.Y, 1));
		}
#endif
	}

	AuxiliaryWindowPresenter::~AuxiliaryWindowPresenter()
	{
#if defined(NCINE_AUXILIARY_PRESENT)
#	if defined(WITH_RHI_GL)
		if (_framebuffer != 0 && _native.MakeCurrent && _native.MakeCurrent()) {
			GLuint framebuffer = _framebuffer;
			glDeleteFramebuffers(1, &framebuffer);
			_native.RestoreMain();
		}
#	else
		if (_swapchain != nullptr) {
			RHI::Device::DestroyAuxiliarySwapchain(_swapchain);
		}
#	endif
#endif
	}

	void AuxiliaryWindowPresenter::Present(const Texture* contents, Vector2i pixelSize)
	{
#if defined(NCINE_AUXILIARY_PRESENT)
		if (contents == nullptr || pixelSize.X <= 0 || pixelSize.Y <= 0) {
			return;
		}
		const RHI::Texture* texture = static_cast<const RHI::Texture*>(contents->GetGuiTexId());
		if (texture == nullptr) {
			return;
		}

#	if defined(WITH_RHI_GL)
		if (!_native.MakeCurrent()) {
			return;
		}
		// The texture is shared, its framebuffer isn't, so this context has one of its own
		if (_framebuffer == 0) {
			GLuint framebuffer = 0;
			glGenFramebuffers(1, &framebuffer);
			_framebuffer = framebuffer;
		}
		glBindFramebuffer(GL_READ_FRAMEBUFFER, _framebuffer);
		glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture->GetGLHandle(), 0);
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
		glViewport(0, 0, pixelSize.X, pixelSize.Y);
		glDisable(GL_SCISSOR_TEST);
		glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);
		Vector2i size = contents->GetSize();
		std::int32_t width = std::min(size.X, pixelSize.X), height = std::min(size.Y, pixelSize.Y);
		// The first row of a render target is the top of it, the window's starts at the bottom
		glBlitFramebuffer(0, 0, width, height, 0, pixelSize.Y, width, pixelSize.Y - height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
		glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
		_native.SwapBuffers();
		_native.RestoreMain();
#	else
		if (_swapchain == nullptr) {
			return;
		}
		if (pixelSize != _size) {
			RHI::Device::ResizeAuxiliarySwapchain(_swapchain, pixelSize.X, pixelSize.Y);
			_size = pixelSize;
		}
#		if defined(WITH_RHI_D3D11)
		RHI::Device::PresentAuxiliaryTexture(_swapchain, texture);
#		else
		// Blitted into the window and presented with the main one, at the end of the frame
		RHI::Device::QueueAuxiliaryPresent(_swapchain, texture);
#		endif
#	endif
#else
		static_cast<void>(contents);
		static_cast<void>(pixelSize);
#endif
	}
}

#endif
