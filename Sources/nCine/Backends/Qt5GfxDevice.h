#pragma once

#if defined(WITH_QT5) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../Graphics/IGfxDevice.h"
#include "../Graphics/DisplayMode.h"
#include "../Primitives/Vector2.h"

namespace nCine::Backends
{
	class Qt5Widget;

	/**
		@brief Graphics device that wraps a Qt5 @ref Qt5Widget
		
		Drives an `IGfxDevice` whose surface and OpenGL context are owned by the
		hosting `QOpenGLWidget`, so most window operations are delegated to Qt5.
	*/
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
		/** @brief Initializes the GLEW OpenGL extension loader */
		void initGlew();
#endif
		/** @brief Clears the engine's cached texture binding so Qt5's GL state is not assumed */
		void resetTextureBinding();
		/** @brief Binds the widget's default draw framebuffer object */
		void bindDefaultDrawFramebufferObject();

	protected:
		void setResolutionInternal(int width, int height) override;

	private:
		Qt5Widget& widget_;
		bool isResizable_;

		/** @brief Deleted copy constructor */
		Qt5GfxDevice(const Qt5GfxDevice&) = delete;
		/** @brief Deleted assignment operator */
		Qt5GfxDevice& operator=(const Qt5GfxDevice&) = delete;

		/** @brief Initializes the OpenGL graphics context */
		void initDevice(bool isFullscreen);

		friend class Qt5InputManager;
	};

}

#endif
