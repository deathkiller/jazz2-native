#pragma once

#if defined(WITH_GLFW)

#include "../../Common.h"

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

namespace nCine
{
	/// The GLFW based graphics device
	class GlfwGfxDevice : public IGfxDevice
	{
	public:
		GlfwGfxDevice(const WindowMode& windowMode, const GLContextInfo& glContextInfo, const DisplayMode& displayMode);
		~GlfwGfxDevice() override;

		void setSwapInterval(int interval) override;

		void setResolution(bool fullscreen, int width = 0, int height = 0) override;

		inline void update() override {
			glfwSwapBuffers(windowHandle_);
		}

		void setWindowPosition(int x, int y) override;
		void setWindowSize(int width, int height) override;

		inline void setWindowTitle(const StringView& windowTitle) override {
			glfwSetWindowTitle(windowHandle_, String::nullTerminatedView(windowTitle).data());
		}
		void setWindowIcon(const StringView& windowIconFilename) override;

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
		/// GLFW3 window handle
		static GLFWwindow* windowHandle_;

		/// GLFW3 monitor pointers
		/*! \note Used to retrieve the index in the monitors array */
		static GLFWmonitor* monitorPointers_[MaxMonitors];

		/// Monitor index to use in full screen
		static int fsMonitorIndex_;
		/// Video mode index to use in full screen
		static int fsModeIndex_;

		int lastWindowWidth_;
		int lastWindowHeight_;

		/// Deleted copy constructor
		GlfwGfxDevice(const GlfwGfxDevice&) = delete;
		/// Deleted assignment operator
		GlfwGfxDevice& operator=(const GlfwGfxDevice&) = delete;

		/// Initilizes the video subsystem (GLFW)
		void initGraphics();
		/// Initilizes the OpenGL graphic context
		void initDevice(bool isResizable, bool enableWindowScaling);

		void updateMonitorScaling(unsigned int monitorIndex);

		int retrieveMonitorIndex(GLFWmonitor* monitor) const;
		void convertVideoModeInfo(const GLFWvidmode& glfwVideoMode, IGfxDevice::VideoMode& videoMode) const;

		/// Returns the window handle used by GLFW3
		static GLFWwindow* windowHandle() {
			return windowHandle_;
		}

		/// Callback for `glfwSetErrorCallback()`
		static void errorCallback(int error, const char* description);

		friend class GlfwInputManager;
		friend class GlfwMouseState;
		friend class GlfwKeyboardState;
	};

}

#endif