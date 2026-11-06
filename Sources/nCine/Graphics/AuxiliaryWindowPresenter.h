#pragma once

#include "AuxiliaryWindow.h"

#if defined(NCINE_HAS_AUXILIARY_WINDOWS) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../Primitives/Vector2.h"
#include "RHI/RhiAuxiliarySwapchain.h"

#include <memory>

namespace nCine
{
	class Texture;

	/**
		@brief Shows a texture in a window besides the main one with the graphics backend, see @ref AuxiliaryWindow

		A thin owner of an auxiliary swap chain of the RHI device. OpenGL copies the texture into the window in a context
		of the window that shares the textures of the main one, Direct3D 11 copies it into a swap chain of the window,
		Vulkan and Metal blit it into their swap chain of the window when the frame is presented.
	*/
	class AuxiliaryWindowPresenter
	{
	public:
		/** @brief Makes a presenter for a window of a size in pixels, `nullptr` if the graphics backend can't present into it */
		static std::unique_ptr<AuxiliaryWindowPresenter> Create(RHI::AuxiliarySwapchainDesc&& desc, Vector2i pixelSize);
		/** @brief Returns `true` if the graphics backend can present into another window */
		static bool IsSupported();

		~AuxiliaryWindowPresenter();

		AuxiliaryWindowPresenter(const AuxiliaryWindowPresenter&) = delete;
		AuxiliaryWindowPresenter& operator=(const AuxiliaryWindowPresenter&) = delete;

		/** @brief Shows a texture in the window of a size in pixels, black where it doesn't reach */
		void Present(const Texture* contents, Vector2i pixelSize);

	private:
		void* _swapchain;
		Vector2i _size;

		AuxiliaryWindowPresenter(void* swapchain, Vector2i pixelSize);
	};
}

#endif
