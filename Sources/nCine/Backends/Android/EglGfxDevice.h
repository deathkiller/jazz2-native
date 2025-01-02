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

	/// The EGL based graphics device
	class EglGfxDevice : public IGfxDevice
	{
	public:
		/// Constructor taking a `DisplayMode` object
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
	
		/// Recreates a surface from a native window
		void createSurface();
		/// Binds the current context
		void bindContext();
		/// Unbinds the current context
		void unbindContext();
		/// Queries the size of the current surface
		void querySurfaceSize();

		/// Checks if the desired pixel format is supported
		static bool isModeSupported(struct android_app* state, const GLContextInfo& glContextInfo, const DisplayMode& mode);
		
#if defined(DEATH_TARGET_ANDROID)
		/// Used only for the JNI native function
		static void updateMonitorsFromJni();
#endif

	protected:
		void setResolutionInternal(int width, int height) override { }

	private:
		/// The EGL display connection
		EGLDisplay display_;
		/// The EGL drawing surface
		EGLSurface surface_;
		/// The EGL context
		EGLContext context_;
		/// The EGL config used to create the first surface
		EGLConfig config_;

		struct android_app* state_;

		static const unsigned int MaxMonitorNameLength = 128;
		static char monitorNames_[MaxMonitors][MaxMonitorNameLength];

		/// Initializes the OpenGL graphic context
		void initDevice();

		void updateMonitors() override;

		void convertVideoModeInfo(const AndroidJniClass_DisplayMode& javaDisplayMode, IGfxDevice::VideoMode& videoMode) const;
	};
}
