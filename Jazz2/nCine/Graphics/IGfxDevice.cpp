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
#if defined(ENABLE_LOG)
		double cssWidth = 0.0;
		double cssHeight = 0.0;
		emscripten_get_element_css_size("canvas", &cssWidth, &cssHeight);
		float pixelRatio2 = emscripten_get_device_pixel_ratio();
		LOGI_X("Canvas was resized to %ix%i (canvas size is %ix%i; ratio is %f)", (int)(event->windowInnerWidth * pixelRatio2), (int)(event->windowInnerHeight * pixelRatio2), (int)cssWidth, (int)cssHeight, pixelRatio2);
#endif
		if (event->windowInnerWidth > 0 && event->windowInnerHeight > 0) {
			float pixelRatio = emscripten_get_device_pixel_ratio();
			IGfxDevice* gfxDevice = reinterpret_cast<IGfxDevice*>(userData);
			gfxDevice->setResolutionInternal(static_cast<int>(event->windowInnerWidth * pixelRatio), static_cast<int>(event->windowInnerHeight * pixelRatio));
		}
		return 1;
	}

	EM_BOOL IGfxDevice::emscriptenHandleFullscreen(int eventType, const EmscriptenFullscreenChangeEvent* event, void* userData)
	{
#if defined(ENABLE_LOG)
		double cssWidth = 0.0;
		double cssHeight = 0.0;
		emscripten_get_element_css_size("canvas", &cssWidth, &cssHeight);
		float pixelRatio2 = emscripten_get_device_pixel_ratio();
		LOGI_X("Canvas was resized to %ix%i (canvas size is %ix%i; ratio is %f)", (int)(event->elementWidth * pixelRatio2), (int)(event->elementHeight * pixelRatio2), (int)cssWidth, (int)cssHeight, pixelRatio2);
#endif
		if (event->elementWidth > 0 && event->elementHeight > 0) {
			float pixelRatio = emscripten_get_device_pixel_ratio();
			IGfxDevice* gfxDevice = reinterpret_cast<IGfxDevice*>(userData);
			gfxDevice->setResolutionInternal(static_cast<int>(event->elementWidth * pixelRatio), static_cast<int>(event->elementHeight * pixelRatio));
		}

		theApplication().handleFullscreenChanged(event->isFullscreen);
		return 1;
	}

#	if defined(WITH_GLFW)
	EM_BOOL IGfxDevice::emscriptenHandleFocus(int eventType, const EmscriptenFocusEvent* event, void* userData)
	{
		if (eventType == EMSCRIPTEN_EVENT_FOCUS) {
			theApplication().setFocus(true);
		} else if (eventType == EMSCRIPTEN_EVENT_BLUR) {
			theApplication().setFocus(false);
		}
		return 1;
	}
#	endif
#endif

	///////////////////////////////////////////////////////////
	// CONSTRUCTORS AND DESTRUCTOR
	///////////////////////////////////////////////////////////

	IGfxDevice::IGfxDevice(const WindowMode& windowMode, const GLContextInfo& glContextInfo, const DisplayMode& displayMode)
		: width_(windowMode.width), height_(windowMode.height),
		glContextInfo_(glContextInfo), displayMode_(displayMode)
	{
#if defined(DEATH_TARGET_EMSCRIPTEN)
		double cssWidth = 0.0;
		double cssHeight = 0.0;
		// Referring to the first element of type <canvas> in the DOM
		emscripten_get_element_css_size("canvas", &cssWidth, &cssHeight);
		float pixelRatio = emscripten_get_device_pixel_ratio();
		width_ = static_cast<int>(cssWidth * pixelRatio);
		height_ = static_cast<int>(cssHeight * pixelRatio);

		EmscriptenFullscreenChangeEvent fsce;
		emscripten_get_fullscreen_status(&fsce);
		if (windowMode.isFullscreen != fsce.isFullscreen) {
			theApplication().handleFullscreenChanged(fsce.isFullscreen);
		}

		emscripten_set_resize_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, this, true, IGfxDevice::emscriptenHandleResize);
		emscripten_set_fullscreenchange_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, this, true, IGfxDevice::emscriptenHandleFullscreen);

#	if defined(WITH_GLFW)
		// GLFW does not seem to correctly handle Emscripten focus and blur events
		emscripten_set_blur_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, true, IGfxDevice::emscriptenHandleFocus);
		emscripten_set_focus_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, true, IGfxDevice::emscriptenHandleFocus);
#	endif
#endif

		GLViewport::initRect(0, 0, width_, height_);

		currentVideoMode_.width = width_;
		currentVideoMode_.height = height_;
		currentVideoMode_.refreshRate = 60.0f;
		currentVideoMode_.redBits = displayMode.redBits();
		currentVideoMode_.greenBits = displayMode.greenBits();
		currentVideoMode_.blueBits = displayMode.blueBits();
		videoModes_.push_back(currentVideoMode_);
	}

	///////////////////////////////////////////////////////////
	// PUBLIC FUNCTIONS
	///////////////////////////////////////////////////////////

	const IGfxDevice::VideoMode& IGfxDevice::videoMode(unsigned int index) const
	{
		ASSERT(index < videoModes_.size());
		if (index >= videoModes_.size())
			return videoModes_[0];
		return videoModes_[index];
	}

	///////////////////////////////////////////////////////////
	// PRIVATE FUNCTIONS
	///////////////////////////////////////////////////////////

	void IGfxDevice::setupGL()
	{
		glDisable(GL_DITHER);
		GLBlending::setBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		GLDepthTest::enable();
	}

}
