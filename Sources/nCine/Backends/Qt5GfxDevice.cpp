#if defined(WITH_QT5)

#if defined(WITH_GLEW)
#	define GLEW_NO_GLU
#	include <GL/glew.h>
#endif

#include "../../Main.h"
#include "../Graphics/RHI/Rhi.h"
#include "Qt5GfxDevice.h"
#include "../Application.h"
#include "Qt5Widget.h"

#include <QWindow>
#include <QApplication>
#include <QIcon>
#include <QScreen>

namespace nCine::Backends
{
	Qt5GfxDevice::Qt5GfxDevice(const WindowMode& windowMode, const ContextInfo& contextInfo, const DisplayMode& displayMode, Qt5Widget& widget)
		: IGfxDevice(windowMode, contextInfo, displayMode), _widget(widget), _isResizable(windowMode.isResizable)
	{
		initDevice(windowMode.isFullscreen);
	}

	void Qt5GfxDevice::setSwapInterval(int interval)
	{
		// `format()` returns a copy, so the interval has to be written back through `setFormat()` - and, as
		// with every surface format attribute, it only takes effect while the context is being created
		QSurfaceFormat format = _widget.format();
		format.setSwapInterval(interval);
		_widget.setFormat(format);
	}

	void Qt5GfxDevice::setResolution(bool fullscreen, int width, int height)
	{
		if (width <= 0 || height <= 0) {
			width = _width;
			height = _height;
		}

		_isFullscreen = fullscreen;

		if (fullscreen) {
			_widget.setWindowState(_widget.windowState() | Qt::WindowFullScreen);
		} else {
			_widget.setWindowState(_widget.windowState() & ~Qt::WindowFullScreen);
			if (!_isResizable) {
				_widget.setMinimumSize(width, height);
				_widget.setMaximumSize(width, height);
			}
		}

		setResolutionInternal(width, height);
	}

	void Qt5GfxDevice::setResolutionInternal(int width, int height)
	{
		// Resizing the widget is all that is done here: Qt5 answers it with a resize event, and
		// `Qt5Widget::resizeGL()` then adopts the new size through `updateResolution()` with the OpenGL
		// context current, which is where the viewport can actually be set
		_widget.resize(width, height);
	}

	void Qt5GfxDevice::updateResolution(int drawableWidth, int drawableHeight)
	{
		// `QOpenGLWidget::resizeGL()` reports the size of the framebuffer, which is what the engine calls
		// the drawable resolution; the window resolution is the logical one Qt5 lays the widget out in
		const float ratio = static_cast<float>(_widget.devicePixelRatioF());
		_drawableWidth = drawableWidth;
		_drawableHeight = drawableHeight;
		_width = (ratio > 0.0f ? static_cast<std::int32_t>(drawableWidth / ratio) : drawableWidth);
		_height = (ratio > 0.0f ? static_cast<std::int32_t>(drawableHeight / ratio) : drawableHeight);

		initDeviceViewport();
		theApplication().ResizeScreenViewport(_drawableWidth, _drawableHeight);
	}

	void Qt5GfxDevice::setWindowTitle(StringView windowTitle)
	{
		_widget.setWindowTitle(QString::fromUtf8(windowTitle.data(), static_cast<int>(windowTitle.size())));
	}

	void Qt5GfxDevice::setWindowIcon(StringView windowIconFilename)
	{
		_widget.setWindowIcon(QIcon(QString::fromUtf8(windowIconFilename.data(), static_cast<int>(windowIconFilename.size()))));
	}

	const Vector2i Qt5GfxDevice::windowPosition() const
	{
		return Vector2i(_widget.pos().x(), _widget.pos().y());
	}

	void Qt5GfxDevice::setWindowPosition(int x, int y)
	{
		_widget.move(x, y);
	}

	void Qt5GfxDevice::setWindowSize(int width, int height)
	{
		if (width > 0 && height > 0 && (width != _width || height != _height)) {
			setResolutionInternal(width, height);
		}
	}

	void Qt5GfxDevice::flashWindow() const
	{
		QApplication::alert(&_widget, 0);
	}

	const IGfxDevice::VideoMode& Qt5GfxDevice::currentVideoMode(unsigned int monitorIndex) const
	{
		if (monitorIndex >= _numMonitors) {
			monitorIndex = 0;
		}
		_currentVideoMode = _monitors[monitorIndex].videoModes[0];
		return _currentVideoMode;
	}

	void Qt5GfxDevice::updateMonitors()
	{
		// Qt5 exposes the mode a screen is currently in, not the list of modes it supports, so every
		// monitor publishes exactly that one mode
		const QList<QScreen*> screens = QApplication::screens();
		_numMonitors = std::min<std::uint32_t>(static_cast<std::uint32_t>(screens.size()), MaxMonitors);

		for (std::uint32_t i = 0; i < _numMonitors; i++) {
			const QScreen* screen = screens.at(static_cast<int>(i));
			Monitor& monitor = _monitors[i];

			monitor.name = nullptr;
			monitor.position = Vector2i(screen->geometry().x(), screen->geometry().y());
			monitor.scale = Vector2f(static_cast<float>(screen->devicePixelRatio()), static_cast<float>(screen->devicePixelRatio()));

			monitor.numVideoModes = 1;
			VideoMode& videoMode = monitor.videoModes[0];
			videoMode.width = static_cast<std::uint32_t>(screen->size().width());
			videoMode.height = static_cast<std::uint32_t>(screen->size().height());
			videoMode.refreshRate = static_cast<float>(screen->refreshRate());
			const std::uint32_t bitsPerChannel = (screen->depth() >= 24 ? 8 : 5);
			videoMode.redBits = bitsPerChannel;
			videoMode.greenBits = bitsPerChannel;
			videoMode.blueBits = bitsPerChannel;
		}

		if (_numMonitors == 0) {
			// Never leave the array empty, `currentVideoMode()` and the fullscreen path index into it
			_numMonitors = 1;
			_monitors[0] = {};
			_monitors[0].numVideoModes = 1;
		}
	}

#if defined(WITH_GLEW)
	void Qt5GfxDevice::initGlew()
	{
		const GLenum err = glewInit();
		FATAL_ASSERT_MSG(err == GLEW_OK, "GLEW error: {}", (const char*)glewGetErrorString(err));

		_contextInfo.debugContext = _contextInfo.debugContext && glewIsSupported("GL_ARB_debug_output");
	}
#endif

	void Qt5GfxDevice::adoptWidgetFramebuffer()
	{
		RHI::GL::GLFramebuffer::SetDefaultHandle(_widget.defaultFramebufferObject());
		RHI::GL::GLDevice::ResyncExternalStateChanges();
	}

	void Qt5GfxDevice::initDevice(bool isFullscreen)
	{
		QSurfaceFormat format;
		format.setRedBufferSize(_displayMode.redBits());
		format.setGreenBufferSize(_displayMode.greenBits());
		format.setBlueBufferSize(_displayMode.blueBits());
		//format.setAlphaBufferSize(_displayMode.alphaBits());
		format.setSwapBehavior(_displayMode.isDoubleBuffered() ? QSurfaceFormat::DoubleBuffer : QSurfaceFormat::SingleBuffer);
		format.setDepthBufferSize(_displayMode.depthBits());
		format.setStencilBufferSize(_displayMode.stencilBits());
		format.setVersion(_contextInfo.majorVersion, _contextInfo.minorVersion);
#if defined(RHI_GL_PROFILE_ES)
		format.setRenderableType(QSurfaceFormat::OpenGLES);
#endif
		format.setProfile(_contextInfo.coreProfile ? QSurfaceFormat::CoreProfile : QSurfaceFormat::CompatibilityProfile);
		if (_contextInfo.debugContext) {
			format.setOptions(QSurfaceFormat::DebugContext);
		}

		if (isFullscreen) {
			_widget.setWindowState(_widget.windowState() | Qt::WindowFullScreen);
		}

		format.setSwapInterval(_displayMode.hasVSync() ? 1 : 0);

		_widget.setFormat(format);
		QSurfaceFormat::setDefaultFormat(format);

		updateMonitors();
	}
}

#endif
