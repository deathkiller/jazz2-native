#pragma once

#if defined(WITH_QT5)

#include "../../Main.h"
#include <QOpenGLWidget>

#if defined(DEATH_TARGET_WINDOWS) && defined(ERROR)
#	undef ERROR
#endif

namespace nCine
{
	class MainApplication;
	class IAppEventHandler;
}

class QWindow;

namespace nCine::Backends
{
	/**
		@brief `QOpenGLWidget`-derived widget that hosts the engine
		
		Embeds an nCine `MainApplication` inside a Qt5 OpenGL widget, driving the
		engine from the widget's GL callbacks and forwarding Qt5 events to it.
	*/
	class Qt5Widget : public QOpenGLWidget
	{
		Q_OBJECT

	public:
		explicit Qt5Widget(std::unique_ptr<IAppEventHandler>(*createAppEventHandler)())
			: Qt5Widget(nullptr, createAppEventHandler, 0, nullptr) {}
		Qt5Widget(std::unique_ptr<IAppEventHandler>(*createAppEventHandler)(), int argc, char** argv)
			: Qt5Widget(nullptr, createAppEventHandler, argc, argv) {}
		Qt5Widget(QWidget* parent, std::unique_ptr<IAppEventHandler>(*createAppEventHandler)(), int argc, char** argv);
		~Qt5Widget();

		/** @brief If set to `false` the widget will stop automatically updating each frame */
		inline void setShouldUpdate(bool shouldUpdate) {
			shouldUpdate_ = shouldUpdate;
		}

		/** @brief Returns the application event handler driving the embedded engine */
		IAppEventHandler& appEventHandler();

	protected:
		bool event(QEvent* event) override;

		void initializeGL() override;
		void resizeGL(int w, int h) override;
		void paintGL() override;

		QSize minimumSizeHint() const override;
		QSize sizeHint() const override;

	private slots:
		/** @brief The function slot called at each `frameSwapped` signal */
		void autoUpdate();

	private:
		MainApplication& application_;
		std::unique_ptr<IAppEventHandler>(*createAppEventHandler_)();
		bool isInitialized_;
		bool shouldUpdate_;

		void shutdown();

		/** @brief Deleted copy constructor */
		Qt5Widget(const Qt5Widget&) = delete;
		/** @brief Deleted assignment operator */
		Qt5Widget& operator=(const Qt5Widget&) = delete;
	};
}

#endif
