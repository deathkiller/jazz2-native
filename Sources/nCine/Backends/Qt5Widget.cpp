#if defined(WITH_QT5)

#include "Qt5Widget.h"
#include "Qt5InputManager.h"
#include "../MainApplication.h"

#if defined(WITH_GLEW)
#	include "Qt5GfxDevice.h"
#endif

#include <QCoreApplication>
#include <QResizeEvent>

namespace nCine::Backends
{
	Qt5Widget::Qt5Widget(QWidget* parent, std::unique_ptr<IAppEventHandler>(*createAppEventHandler)(), int argc, char** argv)
		: QOpenGLWidget(parent), _application(static_cast<MainApplication&>(theApplication())),
			_createAppEventHandler(createAppEventHandler), _isInitialized(false), _shouldUpdate(true)
	{
		setFocusPolicy(Qt::StrongFocus);
		setMouseTracking(true);
		QObject::connect(this, SIGNAL(frameSwapped()), this, SLOT(autoUpdate()));

		//ASSERT(_createAppEventHandler);
		_application._qt5Widget = this;
		_application.init(_createAppEventHandler, argc, argv);
		_application.setAutoSuspension(false);

		const int width = _application.appConfiguration().resolution.x;
		const int height = _application.appConfiguration().resolution.y;
		QRect rect = geometry();
		rect.setWidth(_application.appConfiguration().resolution.x);
		rect.setHeight(_application.appConfiguration().resolution.y);
		setGeometry(rect);

		if (!_application._appCfg.isResizable) {
			setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
			setMinimumSize(width, height);
			setMaximumSize(width, height);
		}
	}

	Qt5Widget::~Qt5Widget()
	{
		shutdown();
	}

	IAppEventHandler& Qt5Widget::appEventHandler()
	{
		return *_application._appEventHandler;
	}

	bool Qt5Widget::event(QEvent* event)
	{
		Qt5InputManager* inputManager = static_cast<Qt5InputManager*>(&_application.inputManager());

		if (event->type() == QEvent::FocusIn) {
			_application.setFocus(true);
		} else if (event->type() == QEvent::FocusOut) {
			_application.setFocus(false);
		}

		switch (event->type()) {
			case QEvent::FocusIn:
			case QEvent::FocusOut:
			case QEvent::KeyPress:
			case QEvent::KeyRelease:
			case QEvent::MouseButtonPress:
			case QEvent::MouseButtonRelease:
			case QEvent::MouseMove:
			case QEvent::TouchBegin:
			case QEvent::TouchUpdate:
			case QEvent::TouchEnd:
			case QEvent::Wheel:
				if (inputManager) {
					if (inputManager->handler()) {
						makeCurrent();
					}
					const bool result = inputManager->event(event);
					if (inputManager->handler()) {
						doneCurrent();
					}
					return result;
				}
				return false;
			case QEvent::Resize: {
				makeCurrent();
				const QSize size = static_cast<QResizeEvent*>(event)->size();
				_application.resizeScreenViewport(size.width(), size.height());
				doneCurrent();
				return QWidget::event(event);
			}
			case QEvent::Close: {
				const bool shouldQuit = inputManager ? inputManager->shouldQuitOnRequest() : true;
				if (shouldQuit) {
					makeCurrent();
					shutdown();
					doneCurrent();
				} else {
					static_cast<QCloseEvent*>(event)->ignore();
				}
				return true;
			}
			default:
				return QWidget::event(event);
		}
	}

	void Qt5Widget::initializeGL()
	{
		Qt5GfxDevice& gfxDevice = static_cast<Qt5GfxDevice&>(*_application._gfxDevice);

#if defined(WITH_GLEW)
		gfxDevice.initGlew();
#endif
		_application.initCommon();
		gfxDevice.resetTextureBinding();
		_isInitialized = true;
	}

	void Qt5Widget::resizeGL(int w, int h)
	{
		if (_isInitialized) {
			Qt5GfxDevice& gfxDevice = static_cast<Qt5GfxDevice&>(*_application._gfxDevice);
			gfxDevice.setResolution(w, h);
			gfxDevice.resetTextureBinding();
		}
	}

	void Qt5Widget::paintGL()
	{
		if (_isInitialized) {
			if (!_application.shouldQuit()) {
				_application.run();
			} else {
				shutdown();
				QCoreApplication::quit();
			}
		}
	}

	QSize Qt5Widget::minimumSizeHint() const
	{
		if (_application.appConfiguration().isResizable) {
			return QSize(-1, -1);
		}

		if (_isInitialized) {
			return QSize(_application.width(), _application.height());
		} else {
			return QSize(_application._appCfg.resolution.x, _application._appCfg.resolution.y);
		}
	}

	QSize Qt5Widget::sizeHint() const
	{
		if (_isInitialized) {
			return QSize(_application.width(), _application.height());
		} else {
			return QSize(_application._appCfg.resolution.x, _application._appCfg.resolution.y);
		}
	}

	void Qt5Widget::autoUpdate()
	{
		if (_shouldUpdate) {
			update();
		}
	}

	void Qt5Widget::shutdown()
	{
		if (_isInitialized) {
			_application.shutdownCommon();
			_application._qt5Widget = nullptr;
			_isInitialized = false;
		}
		disconnect(SIGNAL(frameSwapped()));
	}
}

#endif