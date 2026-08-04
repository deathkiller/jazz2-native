#if defined(WITH_QT5)

#if defined(WITH_GLEW)
#	define GLEW_NO_GLU
#	include <GL/glew.h>
#endif

#include "../../Main.h"
#include "../Graphics/RHI/Rhi.h"
#include "Qt5GfxDevice.h"
#include "../MainApplication.h"
#include "Qt5Widget.h"

#include <QWindow>
#include <QApplication>
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
		_widget.format().setSwapInterval(interval);
	}

	void Qt5GfxDevice::setResolution(bool fullscreen, int width, int height)
	{
		_width = width;
		_height = height;
		theApplication().resizeRootViewport(width, height);

		QRect rect = _widget.GetGeometry();
		rect.setWidth(width);
		rect.setHeight(height);
		_widget.setGeometry(rect);

		if (fullscreen) {
			_widget.setWindowState(_widget.windowState() | Qt::WindowFullScreen);
		} else {
			_widget.setWindowState(_widget.windowState() & ~Qt::WindowFullScreen);
			if (!_isResizable) {
				_widget.setMinimumSize(width, height);
				_widget.setMaximumSize(width, height);
			}
		}
	}

	void Qt5GfxDevice::setResolutionInternal(int width, int height)
	{
		_width = width;
		_height = height;

		theApplication().resizeRootViewport(width, height);

		QRect rect = _widget.GetGeometry();
		rect.setWidth(width);
		rect.setHeight(height);
		_widget.setGeometry(rect);
	}

	void Qt5GfxDevice::setWindowTitle(const char* windowTitle)
	{
		_widget.setWindowTitle(windowTitle);
	}

	void Qt5GfxDevice::setWindowIcon(const char* windowIconFilename)
	{
		_widget.setWindowIcon(QIcon(windowIconFilename));
	}

	int Qt5GfxDevice::windowPositionX() const
	{
		return _widget.pos().x();
	}

	int Qt5GfxDevice::windowPositionY() const
	{
		return _widget.pos().y();
	}

	const Vector2i Qt5GfxDevice::windowPosition() const
	{
		return Vector2i(_widget.pos().x(), _widget.pos().y());
	}

	void Qt5GfxDevice::setWindowPosition(int x, int y)
	{
		QRect geometry = _widget.GetGeometry();
		geometry.setX(x);
		geometry.setY(y);
		_widget.setGeometry(geometry);
	}

	void Qt5GfxDevice::flashWindow() const
	{
		QApplication::alert(&_widget, 0);
	}

	const Qt5GfxDevice::VideoMode& Qt5GfxDevice::currentVideoMode() const
	{
		return _videoModes[0];
	}

	void Qt5GfxDevice::updateVideoModes()
	{
		QScreen* screen = nullptr;
		if (_widget.window() && _widget.window()->windowHandle()) {
			screen = _widget.window()->windowHandle()->screen();
		} else {
			screen = QApplication::primaryScreen();
		}

		_videoModes.resize_for_overwrite(1);
		if (screen) {
			_videoModes[0].width = screen->size().width();
			_videoModes[0].height = screen->size().height();
			_videoModes[0].refreshRate = screen->refreshRate();

			if (screen->depth() >= 24) {
				_videoModes[0].redBits = 8;
				_videoModes[0].greenBits = 8;
				_videoModes[0].blueBits = 8;
			}
		}
	}

#if defined(WITH_GLEW)
	void Qt5GfxDevice::initGlew()
	{
		const GLenum err = glewInit();
		FATAL_ASSERT_MSG(err == GLEW_OK, "GLEW error: {}", glewGetErrorString(err));

		_contextInfo.debugContext = _contextInfo.debugContext && glewIsSupported("GL_ARB_debug_output");
	}
#endif

	void Qt5GfxDevice::resetTextureBinding()
	{
		RHI::Texture::bindHandle(GL_TEXTURE_2D, 0);
	}

	void Qt5GfxDevice::bindDefaultDrawFramebufferObject()
	{
		const GLuint glHandle = _widget.defaultFramebufferObject();
		RHI::Framebuffer::bindHandle(GL_DRAW_FRAMEBUFFER, glHandle);
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
		if (_contextInfo.debugContext)
			format.setOptions(QSurfaceFormat::DebugContext);

		if (isFullscreen) {
			_widget.setWindowState(_widget.windowState() | Qt::WindowFullScreen);
		}

		const int interval = _displayMode.hasVSync() ? 1 : 0;
		format.setSwapInterval(interval);

		_widget.setFormat(format);
		QSurfaceFormat::setDefaultFormat(format);

		updateVideoModes();
	}
}

#endif