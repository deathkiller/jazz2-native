#pragma once

#if defined(WITH_QT5) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../Graphics/IGfxDevice.h"
#include "../Graphics/DisplayMode.h"
#include "../Primitives/Vector2.h"

namespace nCine::Backends
{
	class Qt5Widget;

	/// The Qt5 based graphics device
	class Qt5GfxDevice : public IGfxDevice
	{
	public:
		Qt5GfxDevice(const WindowMode& windowMode, const GLContextInfo& glContextInfo, const DisplayMode& displayMode, Qt5Widget& widget);

		void setSwapInterval(int interval) override;

		void setResolution(bool fullscreen, int width = 0, int height = 0) override;

		inline void update() override {}

		void setWindowPosition(int x, int y) override;
		void setWindowTitle(const char* windowTitle) override;
		void setWindowIcon(const char* windowIconFilename) override;

		int windowPositionX() const override;
		int windowPositionY() const override;
		const Vector2i windowPosition() const override;

		void flashWindow() const override;

		const VideoMode& currentVideoMode() const override;
		bool setVideoMode(unsigned int index) override {
			return false;
		}
		void updateVideoModes() override;

#if defined(WITH_GLEW)
		void initGlew();
#endif
		void resetTextureBinding();
		void bindDefaultDrawFramebufferObject();

	protected:
		void setResolutionInternal(int width, int height) override;

	private:
		Qt5Widget& widget_;
		bool isResizable_;

		/// Deleted copy constructor
		Qt5GfxDevice(const Qt5GfxDevice&) = delete;
		/// Deleted assignment operator
		Qt5GfxDevice& operator=(const Qt5GfxDevice&) = delete;

		/// Initilizes the OpenGL graphic context
		void initDevice(bool isFullscreen);

		friend class Qt5InputManager;
	};

}

#endif
