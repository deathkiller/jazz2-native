#pragma once

#if defined(WITH_GLFW) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../../Main.h"

#if defined(WITH_GLEW)
#	define GLEW_NO_GLU
#	include <GL/glew.h>
#endif

// GLFW must not include a GL header of its own: which one is correct depends on the profile
// (`CommonHeaders.h` picks desktop GL, GLES2 or GLES3 headers for it), and letting GLFW pull in the desktop
// <GL/gl.h> + <GL/glext.h> on an ES build breaks the ES headers - glext.h defines `GL_KHR_debug` with the
// unsuffixed spellings, which then suppresses the `*_KHR` block of <GLES2/gl2ext.h> the engine reads.
#define GLFW_INCLUDE_NONE

#if !defined(CMAKE_BUILD) && defined(__has_include)
#	if __has_include("GL/glfw3.h")
#		define __HAS_LOCAL_GLFW
#	endif
#endif
#if defined(__HAS_LOCAL_GLFW)
#	include "GL/glfw3.h"
#elif defined(DEATH_TARGET_EMSCRIPTEN)
#	include <GLFW/glfw3.h>
#else
#	include <glfw3.h>
#endif

#include "../Primitives/Vector2.h"
#include "../Graphics/IGfxDevice.h"
#include "../Graphics/DisplayMode.h"

namespace nCine::Backends
{
	/**
		@brief GLFW-based graphics device
		
		Creates and manages the application window and OpenGL context using the cross-platform
		GLFW library, and handles resolution, video modes and monitor enumeration.
	*/
	class GlfwGfxDevice : public IGfxDevice
	{
		friend class GlfwInputManager;
		friend class GlfwAuxiliaryWindow;
		friend class GlfwMouseState;
		friend class GlfwKeyboardState;
		friend class ImGuiDrawing;

	public:
		GlfwGfxDevice(const WindowMode& windowMode, const ContextInfo& contextInfo, const DisplayMode& displayMode);
		~GlfwGfxDevice() override;

		void SetSwapInterval(int interval) override;

		void SetResolution(bool fullscreen, int width = 0, int height = 0) override;

		void Update() override;

		void SetWindowPosition(int x, int y) override;
		void SetWindowSize(int width, int height) override;

		inline void SetWindowTitle(StringView windowTitle) override {
			glfwSetWindowTitle(_windowHandle, String::nullTerminatedView(windowTitle).data());
		}
		void SetWindowIcon(StringView windowIconFilename) override;

		const Vector2i GetWindowPosition() const override;

		void FlashWindow() const override;

		unsigned int GetPrimaryMonitorIndex() const override;
		unsigned int GetWindowMonitorIndex() const override;

		const VideoMode& GetCurrentVideoMode(unsigned int monitorIndex) const override;
		bool SetVideoMode(unsigned int modeIndex) override;

#if defined(NCINE_HAS_AUXILIARY_WINDOWS) || defined(DOXYGEN_GENERATING_OUTPUT)
		bool SupportsAuxiliaryWindows() const override;
		std::unique_ptr<AuxiliaryWindow> CreateAuxiliaryWindow(const AuxiliaryWindowDesc& desc) override;
		bool GetGlobalMousePosition(Vector2i& position) const override;
		float GetWindowContentScale() const override;
		bool GetGlobalMouseButtons(std::uint32_t& buttons) const override;
		bool GetDesktopWorkArea(Vector2i point, Recti& area) const override;
#endif

	protected:
		void SetResolutionInternal(int width, int height) override;

		void UpdateMonitors() override;

	private:
		/** @brief GLFW3 window handle */
		static GLFWwindow* _windowHandle;

		/**
		 * @brief GLFW3 monitor pointers
		 *
		 * @note Used to retrieve the index in the monitors array
		 */
		static GLFWmonitor* _monitorPointers[MaxMonitors];

		/** @brief Monitor index to use in full screen */
		static int _fsMonitorIndex;
		/** @brief Video mode index to use in full screen */
		static int _fsModeIndex;

		int _lastWindowWidth;
		int _lastWindowHeight;

		/** @brief Deleted copy constructor */
		GlfwGfxDevice(const GlfwGfxDevice&) = delete;
		/** @brief Deleted assignment operator */
		GlfwGfxDevice& operator=(const GlfwGfxDevice&) = delete;

		/** @brief Initializes the video subsystem (GLFW) */
		void InitGraphics();
		/** @brief Initializes the OpenGL graphics context */
		void InitDevice(int windowPosX, int windowPosY, bool isResizable, bool enableWindowScaling);

		void UpdateMonitorScaling(unsigned int monitorIndex);

		int RetrieveMonitorIndex(GLFWmonitor* monitor) const;
		void ConvertVideoModeInfo(const GLFWvidmode& glfwVideoMode, IGfxDevice::VideoMode& videoMode) const;

		/** @brief Returns the window handle used by GLFW3 */
		static GLFWwindow* GetWindowHandle() {
			return _windowHandle;
		}

		/** @brief Callback for `glfwSetErrorCallback()` */
		static void ErrorCallback(int error, const char* description);
	};

}

#endif