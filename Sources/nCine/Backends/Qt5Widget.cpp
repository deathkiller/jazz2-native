#if defined(WITH_QT5)

#include "Qt5Widget.h"
#include "Qt5InputManager.h"
#include "Qt5GfxDevice.h"
#include "../MainApplication.h"

#include <QCoreApplication>
#include <QResizeEvent>

namespace nCine::Backends
{
	Qt5Widget::Qt5Widget(QWidget* parent, CreateAppEventHandlerDelegate createAppEventHandler, int argc, NativeArgument* argv)
		: QOpenGLWidget(parent), _application(static_cast<MainApplication&>(theApplication())),
			_createAppEventHandler(createAppEventHandler), _isInitialized(false), _shouldUpdate(true)
	{
#if defined(DEATH_TRACE)
		// This widget drives Init() itself rather than going through MainApplication::Run(), so the sink is
		// attached here - before the assertion below, which would otherwise fire into nothing
		_application.InitializeTrace();
#endif

		DEATH_ASSERT(_createAppEventHandler != nullptr);

		setFocusPolicy(Qt::StrongFocus);
		setMouseTracking(true);
		QObject::connect(this, SIGNAL(frameSwapped()), this, SLOT(autoUpdate()));

		_application._qt5Widget = this;
		_application.Init(_createAppEventHandler, argc, argv);
		_application.SetAutoSuspension(false);

		const Vector2i resolution = _application.GetAppConfiguration().resolution;
		resize(resolution.X, resolution.Y);

		if (!_application.GetAppConfiguration().resizable) {
			setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
			setMinimumSize(resolution.X, resolution.Y);
			setMaximumSize(resolution.X, resolution.Y);
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
		Qt5InputManager* inputManager = static_cast<Qt5InputManager*>(&_application.GetInputManager());

		switch (event->type()) {
			case QEvent::FocusIn:
				_application.SetFocus(true);
				return QWidget::event(event);
			case QEvent::FocusOut:
				_application.SetFocus(false);
				return QWidget::event(event);
			case QEvent::KeyPress:
			case QEvent::KeyRelease:
			case QEvent::MouseButtonPress:
			case QEvent::MouseButtonRelease:
			case QEvent::MouseMove:
			case QEvent::TouchBegin:
			case QEvent::TouchUpdate:
			case QEvent::TouchEnd:
			case QEvent::Wheel:
				if (inputManager != nullptr) {
					// An event handler may draw (the ImGui overlay does), so the context has to be current
					if (Qt5InputManager::handler() != nullptr) {
						makeCurrent();
					}
					const bool result = inputManager->event(event);
					if (Qt5InputManager::handler() != nullptr) {
						doneCurrent();
					}
					return result;
				}
				return false;
			case QEvent::Close: {
				const bool shouldQuit = (inputManager == nullptr || inputManager->shouldQuitOnRequest());
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
		_application.InitCommon();
		gfxDevice.adoptWidgetFramebuffer();
		_isInitialized = true;
	}

	void Qt5Widget::resizeGL(int w, int h)
	{
		if (_isInitialized) {
			// Qt5 has the context current here, which both the viewport update and the viewport
			// reallocation the resolution change triggers need
			Qt5GfxDevice& gfxDevice = static_cast<Qt5GfxDevice&>(*_application._gfxDevice);
			// The widget allocates a new framebuffer object for the new size
			gfxDevice.adoptWidgetFramebuffer();
			gfxDevice.updateResolution(w, h);
		}
	}

	void Qt5Widget::paintGL()
	{
		if (_isInitialized) {
			if (!_application.ShouldQuit()) {
				// Qt5 has drawn its own widgets with this context since the last frame
				static_cast<Qt5GfxDevice&>(*_application._gfxDevice).adoptWidgetFramebuffer();
				_application.Step();
			} else {
				shutdown();
				QCoreApplication::quit();
			}
		}
	}

	QSize Qt5Widget::minimumSizeHint() const
	{
		if (_application.GetAppConfiguration().resizable) {
			return QSize(-1, -1);
		}

		return sizeHint();
	}

	QSize Qt5Widget::sizeHint() const
	{
		if (_isInitialized) {
			return QSize(_application.GetWidth(), _application.GetHeight());
		} else {
			const Vector2i resolution = _application.GetAppConfiguration().resolution;
			return QSize(resolution.X, resolution.Y);
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
			_application.ShutdownCommon();
			_application._qt5Widget = nullptr;
			_isInitialized = false;
		}
		disconnect(SIGNAL(frameSwapped()));
	}
}

#endif
