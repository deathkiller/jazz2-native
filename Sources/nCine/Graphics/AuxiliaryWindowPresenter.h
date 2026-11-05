#pragma once

#include "AuxiliaryWindow.h"

#if defined(NCINE_HAS_AUXILIARY_WINDOWS) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../Primitives/Vector2.h"

#include <Containers/Function.h>

#include <memory>

namespace nCine
{
	class Texture;

	/** @brief What @ref AuxiliaryWindowPresenter needs of a window, given by the platform backend */
	struct AuxiliaryWindowNative
	{
		/**
		 * @brief Native window of the graphics backend
		 *
		 * The `HWND` for Direct3D 11, the `SDL_Window*` for Vulkan and Metal, unused for OpenGL.
		 */
		void* Handle = nullptr;
		/** @brief OpenGL: makes the context of the window current, it shares the objects of the main one */
		Death::Containers::Function<bool()> MakeCurrent;
		/** @brief OpenGL: shows what was drawn into the window */
		Death::Containers::Function<void()> SwapBuffers;
		/** @brief OpenGL: makes the context of the main window current again */
		Death::Containers::Function<void()> RestoreMain;
	};

	/**
		@brief Shows a texture in a window besides the main one with the graphics backend, see @ref AuxiliaryWindow

		OpenGL copies it into the window in a context of the window that shares the textures of the main one, Direct3D 11
		copies it into a swap chain of the window, Vulkan and Metal blit it into their swap chain of the window when the
		frame is presented.
	*/
	class AuxiliaryWindowPresenter
	{
	public:
		/** @brief Makes a presenter for a window of a size in pixels, `nullptr` if the graphics backend can't present into another window */
		static std::unique_ptr<AuxiliaryWindowPresenter> Create(AuxiliaryWindowNative&& native, Vector2i pixelSize);
		/** @brief Returns `true` if the graphics backend can present into another window */
		static bool IsSupported();

		~AuxiliaryWindowPresenter();

		AuxiliaryWindowPresenter(const AuxiliaryWindowPresenter&) = delete;
		AuxiliaryWindowPresenter& operator=(const AuxiliaryWindowPresenter&) = delete;

		/** @brief Shows a texture in the window of a size in pixels, black where it doesn't reach */
		void Present(const Texture* contents, Vector2i pixelSize);

	private:
		AuxiliaryWindowNative _native;
		// The swap chain of the window, or the framebuffer of its OpenGL context
		void* _swapchain = nullptr;
		unsigned int _framebuffer = 0;
		Vector2i _size;

		AuxiliaryWindowPresenter(AuxiliaryWindowNative&& native, Vector2i pixelSize);
	};
}

#endif
