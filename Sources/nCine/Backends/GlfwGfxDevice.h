#pragma once

#if defined(WITH_GLFW) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../../Main.h"

#if defined(WITH_GLEW)
#	define GLEW_NO_GLU
#	include <GL/glew.h>
#endif

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
		friend class GlfwMouseState;
		friend class GlfwKeyboardState;
		friend class ImGuiDrawing;

	public:
		GlfwGfxDevice(const WindowMode& windowMode, const ContextInfo& contextInfo, const DisplayMode& displayMode);
		~GlfwGfxDevice() override;

		void setSwapInterval(int interval) override;

		void setResolution(bool fullscreen, int width = 0, int height = 0) override;

		void update() override;

		void setWindowPosition(int x, int y) override;
		void setWindowSize(int width, int height) override;

		inline void setWindowTitle(StringView windowTitle) override {
			glfwSetWindowTitle(windowHandle_, String::nullTerminatedView(windowTitle).data());
		}
		void setWindowIcon(StringView windowIconFilename) override;

		const Vector2i windowPosition() const override;

		void flashWindow() const override;

		unsigned int primaryMonitorIndex() const override;
		unsigned int windowMonitorIndex() const override;

		const VideoMode& currentVideoMode(unsigned int monitorIndex) const override;
		bool setVideoMode(unsigned int modeIndex) override;

	protected:
		void setResolutionInternal(int width, int height) override;

		void updateMonitors() override;

	private:
		/** @brief GLFW3 window handle */
		static GLFWwindow* windowHandle_;

		/**
		 * @brief GLFW3 monitor pointers
		 *
		 * @note Used to retrieve the index in the monitors array
		 */
		static GLFWmonitor* monitorPointers_[MaxMonitors];

		/** @brief Monitor index to use in full screen */
		static int fsMonitorIndex_;
		/** @brief Video mode index to use in full screen */
		static int fsModeIndex_;

		int lastWindowWidth_;
		int lastWindowHeight_;

		/** @brief Deleted copy constructor */
		GlfwGfxDevice(const GlfwGfxDevice&) = delete;
		/** @brief Deleted assignment operator */
		GlfwGfxDevice& operator=(const GlfwGfxDevice&) = delete;

		/** @brief Initializes the video subsystem (GLFW) */
		void initGraphics();
		/** @brief Initializes the OpenGL graphics context */
		void initDevice(int windowPosX, int windowPosY, bool isResizable, bool enableWindowScaling);

		void updateMonitorScaling(unsigned int monitorIndex);

		int retrieveMonitorIndex(GLFWmonitor* monitor) const;
		void convertVideoModeInfo(const GLFWvidmode& glfwVideoMode, IGfxDevice::VideoMode& videoMode) const;

		/** @brief Returns the window handle used by GLFW3 */
		static GLFWwindow* windowHandle() {
			return windowHandle_;
		}

		/** @brief Callback for `glfwSetErrorCallback()` */
		static void errorCallback(int error, const char* description);
	};

}

#endif