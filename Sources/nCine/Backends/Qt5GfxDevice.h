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
		Qt5GfxDevice(const WindowMode& windowMode, const ContextInfo& contextInfo, const DisplayMode& displayMode, Qt5Widget& widget);

		void setSwapInterval(int interval) override;

		void setResolution(bool fullscreen, int width = 0, int height = 0) override;

		/** @brief No-op, the widget swaps its own buffers */
		inline void update() override {}

		void setWindowPosition(int x, int y) override;
		void setWindowSize(int width, int height) override;
		void setWindowTitle(StringView windowTitle) override;
		void setWindowIcon(StringView windowIconFilename) override;

		const Vector2i windowPosition() const override;

		void flashWindow() const override;

		const VideoMode& currentVideoMode(unsigned int monitorIndex) const override;

#if defined(WITH_GLEW)
		/** @brief Initializes the GLEW OpenGL extension loader */
		void initGlew();
#endif
		/**
			@brief Points the engine's "screen" at the widget's framebuffer and re-syncs cached OpenGL state

			`QOpenGLWidget` renders into a framebuffer object of its own rather than into the window, and Qt5
			draws the rest of the interface with the same context in between two engine frames. So the handle
			(which changes whenever the widget is resized) is republished and every cached binding dropped,
			exactly like the libretro backend does for the frontend's framebuffer.
		*/
		void adoptWidgetFramebuffer();
		/**
			@brief Adopts a framebuffer size the widget was resized to by Qt5 itself

			To be called from @ref Qt5Widget::resizeGL() only, which is where the OpenGL context is current.
		*/
		void updateResolution(int drawableWidth, int drawableHeight);

	protected:
		void updateMonitors() override;
		void setResolutionInternal(int width, int height) override;

	private:
		Qt5Widget& _widget;
		bool _isResizable;

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
