#define NCINE_INCLUDE_OPENGL
#include "../CommonHeaders.h"

#include "../../Common.h"
#include "IGfxDevice.h"
#include "../Primitives/Colorf.h"
#include "GL/GLDepthTest.h"
#include "GL/GLBlending.h"
#include "GL/GLClearColor.h"
#include "GL/GLViewport.h"

#ifdef DEATH_TARGET_EMSCRIPTEN
#	include <emscripten/html5.h>
#	include "../Application.h"
#endif

namespace nCine
{
#ifdef DEATH_TARGET_EMSCRIPTEN
	EM_BOOL IGfxDevice::emscriptenHandleResize(int eventType, const EmscriptenUiEvent* event, void* userData)
	{
#if defined(ENABLE_LOG)
		double cssWidth = 0.0;
		double cssHeight = 0.0;
		emscripten_get_element_css_size("canvas", &cssWidth, &cssHeight);
		LOGI_X("Canvas was resized to %ix%i (canvas size is %ix%i)", event->windowInnerWidth, event->windowInnerHeight, (int)cssWidth, (int)cssHeight);
#endif
		if (event->windowInnerWidth > 0 && event->windowInnerHeight > 0) {
			IGfxDevice* gfxDevice = reinterpret_cast<IGfxDevice*>(userData);
			gfxDevice->setResolution(event->windowInnerWidth, event->windowInnerHeight);
		}

		return 1;
	}

	EM_BOOL IGfxDevice::emscriptenHandleFullscreen(int eventType, const EmscriptenFullscreenChangeEvent* event, void* userData)
	{
		IGfxDevice* gfxDevice = reinterpret_cast<IGfxDevice*>(userData);
		gfxDevice->setResolution(event->elementWidth, event->elementHeight);
		gfxDevice->setFullScreen(event->isFullscreen);
		return 1;
	}

	EM_BOOL IGfxDevice::emscriptenHandleFocus(int eventType, const EmscriptenFocusEvent* event, void* userData)
	{
		if (eventType == EMSCRIPTEN_EVENT_FOCUS)
			theApplication().setFocus(true);
		else if (eventType == EMSCRIPTEN_EVENT_BLUR)
			theApplication().setFocus(false);
		return 1;
	}

#endif

	///////////////////////////////////////////////////////////
	// CONSTRUCTORS AND DESTRUCTOR
	///////////////////////////////////////////////////////////

	IGfxDevice::IGfxDevice(const WindowMode& windowMode, const GLContextInfo& glContextInfo, const DisplayMode& displayMode)
		: width_(windowMode.width), height_(windowMode.height),
		isFullScreen_(windowMode.isFullScreen), isResizable_(windowMode.isResizable),
		glContextInfo_(glContextInfo), displayMode_(displayMode), numVideoModes_(0)
	{
#ifdef DEATH_TARGET_EMSCRIPTEN
		double cssWidth = 0.0;
		double cssHeight = 0.0;
		// Referring to the first element of type <canvas> in the DOM
		emscripten_get_element_css_size("canvas", &cssWidth, &cssHeight);

		EmscriptenFullscreenChangeEvent fsce;
		emscripten_get_fullscreen_status(&fsce);

		width_ = static_cast<int>(cssWidth);
		height_ = static_cast<int>(cssHeight);
		isFullScreen_ = fsce.isFullscreen;
		isResizable_ = true;

		emscripten_set_resize_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, this, true, IGfxDevice::emscriptenHandleResize);
		emscripten_set_fullscreenchange_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, this, true, IGfxDevice::emscriptenHandleFullscreen);

		// GLFW does not seem to correctly handle Emscripten focus and blur events
#	ifdef WITH_GLFW
		emscripten_set_blur_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, true, IGfxDevice::emscriptenHandleFocus);
		emscripten_set_focus_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, true, IGfxDevice::emscriptenHandleFocus);
#	endif
#endif

		GLViewport::initRect(0, 0, width_, height_);
		currentVideoMode_.width = width_;
		currentVideoMode_.height = height_;
		currentVideoMode_.redBits = displayMode.redBits();
		currentVideoMode_.greenBits = displayMode.greenBits();
		currentVideoMode_.blueBits = displayMode.blueBits();
		currentVideoMode_.refreshRate = 60;
		videoModes_[0] = currentVideoMode_;
		numVideoModes_ = 1;
	}

	///////////////////////////////////////////////////////////
	// PUBLIC FUNCTIONS
	///////////////////////////////////////////////////////////

	const IGfxDevice::VideoMode& IGfxDevice::videoMode(unsigned int index) const
	{
		ASSERT(index < numVideoModes_);
		if (index >= numVideoModes_)
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
