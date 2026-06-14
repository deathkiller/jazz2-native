#pragma once

#include "../../Graphics/IGfxDevice.h"
#include "../../Graphics/DisplayMode.h"
#include "../../Primitives/Vector2.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>

struct android_app;

namespace nCine::Backends
{
	class AndroidJniClass_DisplayMode;

	/**
		@brief The EGL-based graphics device
		
		Manages the EGL display connection, drawing surface and rendering context for the Android backend.
	*/
	class EglGfxDevice : public IGfxDevice
	{
	public:
		/** @brief Constructor taking a `DisplayMode` object */
		EglGfxDevice(struct android_app* state, const GLContextInfo& glContextInfo, const DisplayMode& displayMode);
		~EglGfxDevice() override;

		void setSwapInterval(int interval) override { }
		void setResolution(bool fullscreen, int width = 0, int height = 0) override { }
		void setWindowPosition(int x, int y) override { }
		void setWindowSize(int width, int height) override { }

		void update() override;

		void setWindowTitle(StringView windowTitle) override { }
		void setWindowIcon(StringView windowIconFilename) override { }

		const VideoMode& currentVideoMode(unsigned int monitorIndex) const override;
		bool setVideoMode(unsigned int modeIndex) override;
	
		/** @brief Recreates the drawing surface from the native window */
		void createSurface();
		/** @brief Makes the EGL context current on the calling thread */
		void bindContext();
		/** @brief Releases the EGL context from the calling thread */
		void unbindContext();
		/** @brief Queries the size of the current drawing surface */
		Vector2i querySurfaceSize();

		/** @brief Checks if the desired pixel format is supported */
		static bool isModeSupported(struct android_app* state, const GLContextInfo& glContextInfo, const DisplayMode& mode);

#if defined(DEATH_TARGET_ANDROID)
		/** @brief Refreshes the monitor list from JNI, called only by the JNI native function */
		static void updateMonitorsFromJni();
#endif

	protected:
		void setResolutionInternal(int width, int height) override { }

	private:
		/** @brief The EGL display connection */
		EGLDisplay display_;
		/** @brief The EGL drawing surface */
		EGLSurface surface_;
		/** @brief The EGL rendering context */
		EGLContext context_;
		/** @brief The EGL config used to create the first surface */
		EGLConfig config_;

		struct android_app* state_;

		static const unsigned int MaxMonitorNameLength = 128;
		static char monitorNames_[MaxMonitors][MaxMonitorNameLength];

		/** @brief Initializes the OpenGL graphics context */
		void initDevice();

		void updateMonitors() override;

		void convertVideoModeInfo(const AndroidJniClass_DisplayMode& javaDisplayMode, IGfxDevice::VideoMode& videoMode) const;
	};
}
