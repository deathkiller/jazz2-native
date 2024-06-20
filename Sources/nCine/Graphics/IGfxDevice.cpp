#define NCINE_INCLUDE_OPENGL
#include "../CommonHeaders.h"

#include "../../Common.h"
#include "IGfxDevice.h"
#include "../Primitives/Colorf.h"
#include "GL/GLDepthTest.h"
#include "GL/GLBlending.h"
#include "GL/GLClearColor.h"
#include "GL/GLViewport.h"

#if defined(DEATH_TARGET_EMSCRIPTEN)
#	include <emscripten/html5.h>
#	include "../Application.h"
#endif

namespace nCine
{
#if defined(DEATH_TARGET_EMSCRIPTEN)
	EM_BOOL IGfxDevice::emscriptenHandleResize(int eventType, const EmscriptenUiEvent* event, void* userData)
	{
#	if defined(DEATH_TRACE)
		double cssWidth = 0.0;
		double cssHeight = 0.0;
		emscripten_get_element_css_size("canvas", &cssWidth, &cssHeight);
		float pixelRatio2 = emscripten_get_device_pixel_ratio();
		LOGI("Canvas was resized to %ix%i (canvas size is %ix%i; ratio is %f)", (int)(event->windowInnerWidth * pixelRatio2), (int)(event->windowInnerHeight * pixelRatio2), (int)cssWidth, (int)cssHeight, pixelRatio2);
#	endif

		if (event->windowInnerWidth > 0 && event->windowInnerHeight > 0) {
			IGfxDevice* gfxDevice = static_cast<IGfxDevice*>(userData);
#if defined(EMSCRIPTEN_USE_PORT_CONTRIB_GLFW3)
			// `contrib.glfw3` should handle HiDPI automatically
			gfxDevice->setResolutionInternal(static_cast<int>(event->windowInnerWidth), static_cast<int>(event->windowInnerHeight));
#else
			float pixelRatio = emscripten_get_device_pixel_ratio();
			gfxDevice->setResolutionInternal(static_cast<int>(event->windowInnerWidth * pixelRatio), static_cast<int>(event->windowInnerHeight * pixelRatio));
#endif
		}
		return 1;
	}

	EM_BOOL IGfxDevice::emscriptenHandleFullscreen(int eventType, const EmscriptenFullscreenChangeEvent* event, void* userData)
	{
#	if defined(DEATH_TRACE)
		double cssWidth = 0.0;
		double cssHeight = 0.0;
		emscripten_get_element_css_size("canvas", &cssWidth, &cssHeight);
		float pixelRatio2 = emscripten_get_device_pixel_ratio();
		LOGI("Canvas was resized to %ix%i (canvas size is %ix%i; ratio is %f)", (int)(event->elementWidth * pixelRatio2), (int)(event->elementHeight * pixelRatio2), (int)cssWidth, (int)cssHeight, pixelRatio2);
#	endif
		IGfxDevice* gfxDevice = static_cast<IGfxDevice*>(userData);
		gfxDevice->isFullscreen_ = event->isFullscreen;

		if (event->elementWidth > 0 && event->elementHeight > 0) {
#if defined(EMSCRIPTEN_USE_PORT_CONTRIB_GLFW3)
			// `contrib.glfw3` should handle HiDPI automatically
			gfxDevice->setResolutionInternal(static_cast<int>(event->elementWidth), static_cast<int>(event->elementHeight));
#else
			float pixelRatio = emscripten_get_device_pixel_ratio();
			gfxDevice->setResolutionInternal(static_cast<int>(event->elementWidth * pixelRatio), static_cast<int>(event->elementHeight * pixelRatio));
#endif
		}

		return 1;
	}

#	if defined(WITH_GLFW)
	EM_BOOL IGfxDevice::emscriptenHandleFocus(int eventType, const EmscriptenFocusEvent* event, void* userData)
	{
		if (eventType == EMSCRIPTEN_EVENT_FOCUS) {
			theApplication().SetFocus(true);
		} else if (eventType == EMSCRIPTEN_EVENT_BLUR) {
			theApplication().SetFocus(false);
		}
		return 1;
	}
#	endif
#endif

	IGfxDevice::IGfxDevice(const WindowMode& windowMode, const GLContextInfo& glContextInfo, const DisplayMode& displayMode)
		: drawableWidth_(windowMode.width), drawableHeight_(windowMode.height), width_(windowMode.width), height_(windowMode.height),
			glContextInfo_(glContextInfo), isFullscreen_(windowMode.isFullscreen), displayMode_(displayMode), numMonitors_(0)
	{
#if defined(DEATH_TARGET_EMSCRIPTEN)
		double cssWidth = 0.0;
		double cssHeight = 0.0;
		// Referring to the first element of type <canvas> in the DOM
		emscripten_get_element_css_size("canvas", &cssWidth, &cssHeight);
#	if defined(EMSCRIPTEN_USE_PORT_CONTRIB_GLFW3)
		// `contrib.glfw3` should handle HiDPI automatically
		width_ = static_cast<int>(cssWidth);
		height_ = static_cast<int>(cssHeight);
#	else
		float pixelRatio = emscripten_get_device_pixel_ratio();
		width_ = static_cast<int>(cssWidth * pixelRatio);
		height_ = static_cast<int>(cssHeight * pixelRatio);
#	endif
		
		drawableWidth_ = width_;
		drawableHeight_ = height_;

		EmscriptenFullscreenChangeEvent fsce;
		emscripten_get_fullscreen_status(&fsce);
		if (isFullscreen_ != fsce.isFullscreen) {
			isFullscreen_ = fsce.isFullscreen;
			// TODO: Broadcast event here?
		}

		emscripten_set_resize_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, this, true, IGfxDevice::emscriptenHandleResize);
		emscripten_set_fullscreenchange_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, this, true, IGfxDevice::emscriptenHandleFullscreen);

#	if defined(WITH_GLFW)
		// GLFW does not seem to correctly handle Emscripten focus and blur events
		emscripten_set_blur_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, true, IGfxDevice::emscriptenHandleFocus);
		emscripten_set_focus_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, true, IGfxDevice::emscriptenHandleFocus);
#	endif
#endif

		currentVideoMode_.width = width_;
		currentVideoMode_.height = height_;
		currentVideoMode_.refreshRate = 0.0f;
		currentVideoMode_.redBits = displayMode.redBits();
		currentVideoMode_.greenBits = displayMode.greenBits();
		currentVideoMode_.blueBits = displayMode.blueBits();
	}

	/*! \internal Having this method inlined does not seem to work correctly with Qt5 on Linux */
	unsigned int IGfxDevice::numMonitors() const
	{
		return numMonitors_;
	}

	const IGfxDevice::Monitor& IGfxDevice::monitor(unsigned int index) const
	{
		ASSERT(index < numMonitors_);
		if (index >= numMonitors_) {
			index = 0;
		}
		return monitors_[index];
	}

	float IGfxDevice::windowScalingFactor() const
	{
#if defined(DEATH_TARGET_APPLE)
		const float factor = drawableWidth() / static_cast<float>(width());
#else
		const Vector2f& scale = monitor().scale;
		const float factor = (scale.X > scale.Y ? scale.X : scale.Y);
#endif

		return factor;
	}

	void IGfxDevice::initGLViewport()
	{
		GLViewport::initRect(0, 0, drawableWidth_, drawableHeight_);
	}

	void IGfxDevice::setupGL()
	{
		glDisable(GL_DITHER);
		GLBlending::setBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		GLDepthTest::enable();
	}
}
