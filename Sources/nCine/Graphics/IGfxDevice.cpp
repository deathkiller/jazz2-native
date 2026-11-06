#if defined(WITH_RHI_GL)
#	define NCINE_INCLUDE_OPENGL
#	include "../CommonHeaders.h"
#endif

#include "../../Main.h"
#include "IGfxDevice.h"
#include "../Primitives/Colorf.h"
#include "RHI/Rhi.h"

#if defined(DEATH_TARGET_EMSCRIPTEN)
#	include <emscripten/html5.h>
#	include "../Application.h"
#endif

namespace nCine
{
#if defined(DEATH_TARGET_EMSCRIPTEN)
	bool IGfxDevice::EmscriptenHandleResize(int eventType, const EmscriptenUiEvent* event, void* userData)
	{
#	if defined(DEATH_TRACE)
		double cssWidth = 0.0;
		double cssHeight = 0.0;
		emscripten_get_element_css_size("canvas", &cssWidth, &cssHeight);
		float pixelRatio2 = emscripten_get_device_pixel_ratio();
		LOGI("Canvas was resized to {}x{} (canvas size is {}x{}; ratio is {})", (int)(event->windowInnerWidth * pixelRatio2), (int)(event->windowInnerHeight * pixelRatio2), (int)cssWidth, (int)cssHeight, pixelRatio2);
#	endif

		if (event->windowInnerWidth > 0 && event->windowInnerHeight > 0) {
			IGfxDevice* gfxDevice = static_cast<IGfxDevice*>(userData);
#	if defined(EMSCRIPTEN_USE_PORT_CONTRIB_GLFW3)
			// `contrib.glfw3` should handle HiDPI automatically
			gfxDevice->SetResolutionInternal(static_cast<int>(event->windowInnerWidth), static_cast<int>(event->windowInnerHeight));
#	else
			float pixelRatio = emscripten_get_device_pixel_ratio();
			gfxDevice->SetResolutionInternal(static_cast<int>(event->windowInnerWidth * pixelRatio), static_cast<int>(event->windowInnerHeight * pixelRatio));
#	endif
		}
		return true;
	}

	bool IGfxDevice::EmscriptenHandleFullscreen(int eventType, const EmscriptenFullscreenChangeEvent* event, void* userData)
	{
#	if defined(DEATH_TRACE)
		double cssWidth = 0.0;
		double cssHeight = 0.0;
		emscripten_get_element_css_size("canvas", &cssWidth, &cssHeight);
		float pixelRatio2 = emscripten_get_device_pixel_ratio();
		LOGI("Canvas was resized to {}x{} (canvas size is {}x{}; ratio is {})", (int)(event->elementWidth * pixelRatio2), (int)(event->elementHeight * pixelRatio2), (int)cssWidth, (int)cssHeight, pixelRatio2);
#	endif
		IGfxDevice* gfxDevice = static_cast<IGfxDevice*>(userData);
		gfxDevice->_isFullscreen = event->isFullscreen;

		if (event->elementWidth > 0 && event->elementHeight > 0) {
#	if defined(EMSCRIPTEN_USE_PORT_CONTRIB_GLFW3)
			// `contrib.glfw3` should handle HiDPI automatically
			gfxDevice->SetResolutionInternal(static_cast<int>(event->elementWidth), static_cast<int>(event->elementHeight));
#	else
			float pixelRatio = emscripten_get_device_pixel_ratio();
			gfxDevice->SetResolutionInternal(static_cast<int>(event->elementWidth * pixelRatio), static_cast<int>(event->elementHeight * pixelRatio));
#	endif
		}

		return true;
	}

#	if defined(WITH_GLFW)
	bool IGfxDevice::EmscriptenHandleFocus(int eventType, const EmscriptenFocusEvent* event, void* userData)
	{
		if (eventType == EMSCRIPTEN_EVENT_FOCUS) {
			theApplication().SetFocus(true);
		} else if (eventType == EMSCRIPTEN_EVENT_BLUR) {
			theApplication().SetFocus(false);
		}
		return true;
	}
#	endif
#endif

	IGfxDevice::IGfxDevice(const WindowMode& windowMode, const ContextInfo& contextInfo, const DisplayMode& displayMode)
		: _drawableWidth(windowMode.Width), _drawableHeight(windowMode.Height), _width(windowMode.Width), _height(windowMode.Height),
			_contextInfo(contextInfo), _isFullscreen(windowMode.IsFullscreen), _displayMode(displayMode), _numMonitors(0)
	{
#if defined(DEATH_TARGET_EMSCRIPTEN)
		double cssWidth = 0.0;
		double cssHeight = 0.0;
		// Referring to the first element of type <canvas> in the DOM
		emscripten_get_element_css_size("canvas", &cssWidth, &cssHeight);
#	if defined(EMSCRIPTEN_USE_PORT_CONTRIB_GLFW3)
		// `contrib.glfw3` should handle HiDPI automatically
		_width = static_cast<int>(cssWidth);
		_height = static_cast<int>(cssHeight);
#	else
		float pixelRatio = emscripten_get_device_pixel_ratio();
		_width = static_cast<int>(cssWidth * pixelRatio);
		_height = static_cast<int>(cssHeight * pixelRatio);
#	endif
		
		_drawableWidth = _width;
		_drawableHeight = _height;

		EmscriptenFullscreenChangeEvent fsce;
		emscripten_get_fullscreen_status(&fsce);
		if (_isFullscreen != fsce.isFullscreen) {
			_isFullscreen = fsce.isFullscreen;
			// TODO: Broadcast event here?
		}

		emscripten_set_resize_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, this, true, IGfxDevice::EmscriptenHandleResize);
		emscripten_set_fullscreenchange_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, this, true, IGfxDevice::EmscriptenHandleFullscreen);

#	if defined(WITH_GLFW)
		// GLFW does not seem to correctly handle Emscripten focus and blur events
		emscripten_set_blur_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, true, IGfxDevice::EmscriptenHandleFocus);
		emscripten_set_focus_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, true, IGfxDevice::EmscriptenHandleFocus);
#	endif
#endif

		_currentVideoMode.Width = _width;
		_currentVideoMode.Height = _height;
		_currentVideoMode.RefreshRate = 0.0f;
		_currentVideoMode.RedBits = displayMode.GetRedBits();
		_currentVideoMode.GreenBits = displayMode.GetGreenBits();
		_currentVideoMode.BlueBits = displayMode.GetBlueBits();
	}

	/**
	 * @brief Returns the number of connected monitors
	 *
	 * @internal Having this method inlined does not seem to work correctly with Qt5 on Linux.
	 */
	unsigned int IGfxDevice::GetMonitorCount() const
	{
		return _numMonitors;
	}

	const IGfxDevice::Monitor& IGfxDevice::GetMonitor(unsigned int index) const
	{
		DEATH_ASSERT(index < _numMonitors);
		if (index >= _numMonitors) {
			index = 0;
		}
		return _monitors[index];
	}

	float IGfxDevice::GetWindowScalingFactor() const
	{
#if defined(DEATH_TARGET_APPLE)
		float factor = GetDrawableWidth() / static_cast<float>(GetWidth());
#else
		Vector2f scale = GetMonitor().Scale;
		float factor = (scale.X > scale.Y ? scale.X : scale.Y);
#endif

		return factor;
	}

	void IGfxDevice::InitDeviceViewport()
	{
		RHI::Device::InitViewport(0, 0, _drawableWidth, _drawableHeight);
	}

	void IGfxDevice::SetupDevice()
	{
		RHI::Device::SetupInitialState();
	}
}
