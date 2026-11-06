#pragma once

#include <Containers/Function.h>

namespace nCine::RHI
{
	/**
		@brief What `Device::CreateAuxiliarySwapchain()` needs of a window besides the main one

		Filled by the window backend, see @ref nCine::AuxiliaryWindow. Each graphics backend uses only its part: Direct3D 11,
		Vulkan and Metal create their swap chain from @ref WindowHandle, OpenGL draws into a context of the window that
		shares the objects of the main one through the three functions.
	*/
	struct AuxiliarySwapchainDesc
	{
		/**
		 * @brief Native window of the graphics backend
		 *
		 * The `HWND` for Direct3D 11, the `SDL_Window*` for Vulkan and Metal, unused for OpenGL.
		 */
		void* WindowHandle = nullptr;
		/** @brief OpenGL: makes the context of the window current, it shares the objects of the main one */
		Death::Containers::Function<bool()> MakeCurrent;
		/** @brief OpenGL: shows what was drawn into the window */
		Death::Containers::Function<void()> SwapBuffers;
		/** @brief OpenGL: makes the context of the main window current again */
		Death::Containers::Function<void()> RestoreMain;
	};
}
