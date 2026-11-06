#include "AuxiliaryWindowPresenter.h"

#if defined(NCINE_HAS_AUXILIARY_WINDOWS)

#include "Texture.h"
#include "RHI/Rhi.h"

namespace nCine
{
	bool AuxiliaryWindowPresenter::IsSupported()
	{
#if defined(RHI_CAP_AUXILIARY_SWAPCHAINS)
		return true;
#else
		return false;
#endif
	}

	std::unique_ptr<AuxiliaryWindowPresenter> AuxiliaryWindowPresenter::Create(RHI::AuxiliarySwapchainDesc&& desc, Vector2i pixelSize)
	{
#if defined(RHI_CAP_AUXILIARY_SWAPCHAINS)
		void* swapchain = RHI::Device::CreateAuxiliarySwapchain(Death::move(desc),
			std::max<std::int32_t>(pixelSize.X, 1), std::max<std::int32_t>(pixelSize.Y, 1));
		if (swapchain == nullptr) {
			return nullptr;
		}
		return std::unique_ptr<AuxiliaryWindowPresenter>(new AuxiliaryWindowPresenter(swapchain, pixelSize));
#else
		static_cast<void>(desc);
		static_cast<void>(pixelSize);
		return nullptr;
#endif
	}

	AuxiliaryWindowPresenter::AuxiliaryWindowPresenter(void* swapchain, Vector2i pixelSize)
		: _swapchain(swapchain), _size(pixelSize)
	{
	}

	AuxiliaryWindowPresenter::~AuxiliaryWindowPresenter()
	{
#if defined(RHI_CAP_AUXILIARY_SWAPCHAINS)
		RHI::Device::DestroyAuxiliarySwapchain(_swapchain);
#endif
	}

	void AuxiliaryWindowPresenter::Present(const Texture* contents, Vector2i pixelSize)
	{
#if defined(RHI_CAP_AUXILIARY_SWAPCHAINS)
		if (contents == nullptr || pixelSize.X <= 0 || pixelSize.Y <= 0) {
			return;
		}
		const RHI::Texture* texture = static_cast<const RHI::Texture*>(contents->GetGuiTexId());
		if (texture == nullptr) {
			return;
		}
		if (pixelSize != _size) {
			RHI::Device::ResizeAuxiliarySwapchain(_swapchain, pixelSize.X, pixelSize.Y);
			_size = pixelSize;
		}
		// Shown right away, or with the main window at the end of the frame on Vulkan and Metal
		RHI::Device::PresentAuxiliaryTexture(_swapchain, texture);
#else
		static_cast<void>(contents);
		static_cast<void>(pixelSize);
#endif
	}
}

#endif
