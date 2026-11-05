#include "SdlAuxiliaryWindow.h"

#if (defined(WITH_SDL2) || defined(WITH_SDL3)) && defined(NCINE_HAS_AUXILIARY_WINDOWS)

#include "AuxiliaryWindowWin32.h"
#include "../Graphics/AuxiliaryWindowPresenter.h"

#if defined(WITH_SDL2) && defined(DEATH_TARGET_WINDOWS)
#	if defined(__HAS_LOCAL_SDL)
#		include "SDL2/SDL_syswm.h"
#	else
#		include <SDL_syswm.h>
#	endif
#endif

#include <algorithm>
#include <cstring>


#if defined(WITH_SDL3)
#	define NCINE_SDL_HIGHDPI SDL_WINDOW_HIGH_PIXEL_DENSITY
#else
#	define NCINE_SDL_HIGHDPI SDL_WINDOW_ALLOW_HIGHDPI
#endif

namespace nCine::Backends
{
	namespace
	{
		// Auxiliary windows that exist, while there's any, a click that activates a window isn't swallowed by SDL, the
		// pointer goes back and forth between them and the main window, which would lose every first click
		std::int32_t WindowCount = 0;

		void UpdateFocusClickThrough()
		{
			SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, WindowCount > 0 ? "1" : "0");
		}

#if defined(DEATH_TARGET_WINDOWS)
		void* GetNativeWindow(SDL_Window* window)
		{
#	if defined(WITH_SDL3)
			return SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
#	else
			SDL_SysWMinfo wmInfo;
			SDL_VERSION(&wmInfo.version);
			return (SDL_GetWindowWMInfo(window, &wmInfo) == SDL_TRUE ? reinterpret_cast<void*>(wmInfo.info.win.window) : nullptr);
#	endif
		}
#endif
	}

	bool SdlAuxiliaryWindow::IsSupported()
	{
		if (!AuxiliaryWindowPresenter::IsSupported()) {
			return false;
		}
		// Wayland doesn't let an application put its windows where it wants
		const char* driver = SDL_GetCurrentVideoDriver();
		return (driver == nullptr || std::strcmp(driver, "wayland") != 0);
	}

	std::unique_ptr<SdlAuxiliaryWindow> SdlAuxiliaryWindow::Create(const AuxiliaryWindowDesc& desc)
	{
		if (!IsSupported()) {
			return nullptr;
		}
		std::unique_ptr<SdlAuxiliaryWindow> window(new SdlAuxiliaryWindow(desc));
		if (window->_window == nullptr || window->_presenter == nullptr) {
			return nullptr;
		}
		return window;
	}

	SdlAuxiliaryWindow::SdlAuxiliaryWindow(const AuxiliaryWindowDesc& desc)
		: _popup(desc.Popup)
	{
		Uint32 flags = SDL_WINDOW_BORDERLESS | SDL_WINDOW_HIDDEN | NCINE_SDL_HIGHDPI;
#if !defined(DEATH_TARGET_WINDOWS)
		// On Windows the window is given its place by SetupAuxiliaryWindowWin32(), elsewhere a popup stays off the taskbar this way
		if (desc.Popup) {
#	if defined(WITH_SDL3)
			flags |= SDL_WINDOW_UTILITY;
#	else
			flags |= SDL_WINDOW_SKIP_TASKBAR;
#	endif
		}
#endif
#if defined(WITH_RHI_GL)
		flags |= SDL_WINDOW_OPENGL;
#elif defined(WITH_RHI_VULKAN)
		flags |= SDL_WINDOW_VULKAN;
#elif defined(WITH_RHI_METAL)
		flags |= SDL_WINDOW_METAL;
#endif
		std::int32_t width = std::max(desc.Bounds.W, 1), height = std::max(desc.Bounds.H, 1);
#if defined(WITH_SDL3)
		_window = SDL_CreateWindow("", width, height, flags);
		if (_window != nullptr) {
			SDL_SetWindowPosition(_window, desc.Bounds.X, desc.Bounds.Y);
		}
#else
		_window = SDL_CreateWindow("", desc.Bounds.X, desc.Bounds.Y, width, height, flags);
#endif
		if (_window == nullptr) {
			LOGE("A window besides the main one couldn't be created: {}", SDL_GetError());
			return;
		}
		_id = SDL_GetWindowID(_window);
		_size = Vector2i(width, height);
		WindowCount++;
		UpdateFocusClickThrough();

#if defined(DEATH_TARGET_WINDOWS)
		_roundedCorners = SetupAuxiliaryWindowWin32(GetNativeWindow(_window), GetNativeWindow(SdlGfxDevice::windowHandle()), desc.Popup, desc.PassThrough);
#elif defined(WITH_SDL3)
		// In front of the main window
		SDL_SetWindowParent(_window, SdlGfxDevice::windowHandle());
#endif

		AuxiliaryWindowNative native;
#if defined(WITH_RHI_GL)
		// A context of its own sharing the objects of the main one, so the swap interval of the main window stays
		SDL_Window* mainWindow = SdlGfxDevice::windowHandle();
		SDL_GLContext mainContext = SdlGfxDevice::glContextHandle();
		SDL_GL_MakeCurrent(mainWindow, mainContext);
		SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 1);
		_glContext = SDL_GL_CreateContext(_window);
		SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 0);
		if (_glContext == nullptr) {
			LOGE("A context of a window besides the main one couldn't be created: {}", SDL_GetError());
			SDL_GL_MakeCurrent(mainWindow, mainContext);
			return;
		}
		SDL_GL_SetSwapInterval(0);
		SDL_GL_MakeCurrent(mainWindow, mainContext);
		SDL_Window* window = _window;
		SDL_GLContext context = _glContext;
		native.MakeCurrent = [window, context]() {
#	if defined(WITH_SDL3)
			return SDL_GL_MakeCurrent(window, context);
#	else
			return (SDL_GL_MakeCurrent(window, context) == 0);
#	endif
		};
		native.SwapBuffers = [window]() {
			SDL_GL_SwapWindow(window);
		};
		native.RestoreMain = []() {
			SDL_GL_MakeCurrent(SdlGfxDevice::windowHandle(), SdlGfxDevice::glContextHandle());
		};
#elif defined(WITH_RHI_D3D11)
		native.Handle = GetNativeWindow(_window);
#else
		// Vulkan and Metal make the surface or the layer of the SDL window itself
		native.Handle = _window;
#endif
		_presenter = AuxiliaryWindowPresenter::Create(Death::move(native), GetPixelSize());
	}

	SdlAuxiliaryWindow::~SdlAuxiliaryWindow()
	{
		// The presenter goes first, it may still use the context or the window
		_presenter = nullptr;
#if defined(WITH_RHI_GL)
		if (_glContext != nullptr) {
#	if defined(WITH_SDL3)
			SDL_GL_DestroyContext(_glContext);
#	else
			SDL_GL_DeleteContext(_glContext);
#	endif
			SDL_GL_MakeCurrent(SdlGfxDevice::windowHandle(), SdlGfxDevice::glContextHandle());
		}
#endif
		if (_window != nullptr) {
			SDL_DestroyWindow(_window);
			WindowCount--;
			UpdateFocusClickThrough();
		}
	}

	void SdlAuxiliaryWindow::SetBounds(const Recti& bounds)
	{
		std::int32_t width = std::max(bounds.W, 1), height = std::max(bounds.H, 1);
		Vector2i position;
		SDL_GetWindowPosition(_window, &position.X, &position.Y);
		if (position.X != bounds.X || position.Y != bounds.Y) {
			SDL_SetWindowPosition(_window, bounds.X, bounds.Y);
		}
		if (width != _size.X || height != _size.Y) {
			SDL_SetWindowSize(_window, width, height);
			_size = Vector2i(width, height);
		}
	}

	Recti SdlAuxiliaryWindow::GetBounds() const
	{
		Recti bounds;
		SDL_GetWindowPosition(_window, &bounds.X, &bounds.Y);
		SDL_GetWindowSize(_window, &bounds.W, &bounds.H);
		return bounds;
	}

	Vector2i SdlAuxiliaryWindow::GetPixelSize() const
	{
		Vector2i size;
		SdlGfxDevice::queryDrawableSize(_window, _size.X, _size.Y, size.X, size.Y);
		return size;
	}

	float SdlAuxiliaryWindow::GetContentScale() const
	{
#if defined(WITH_SDL3)
		// The units of windows are pixels on some systems, the scale of the display is told apart
		float scale = SDL_GetWindowDisplayScale(_window);
		return (scale > 0.0f ? scale : 1.0f);
#else
		// The units are scaled by the display where SDL scales them (see SDL_HINT_WINDOWS_DPI_SCALING) and on macOS
		return AuxiliaryWindow::GetContentScale();
#endif
	}

	bool SdlAuxiliaryWindow::IsFocused() const
	{
		return (SDL_GetKeyboardFocus() == _window);
	}

	void SdlAuxiliaryWindow::Raise()
	{
		SDL_RaiseWindow(_window);
	}

	void SdlAuxiliaryWindow::SetTitle(StringView title)
	{
		SDL_SetWindowTitle(_window, String::nullTerminatedView(title).data());
	}

	void SdlAuxiliaryWindow::SetVisible(bool visible, bool activate)
	{
		if (!visible) {
			SDL_HideWindow(_window);
			return;
		}
		// A popup never takes the focus from the window it was shown from
		activate = (activate && !_popup);
#if defined(WITH_SDL3)
		SDL_SetHint(SDL_HINT_WINDOW_ACTIVATE_WHEN_SHOWN, activate ? "1" : "0");
#elif defined(SDL_HINT_WINDOW_NO_ACTIVATION_WHEN_SHOWN)
		SDL_SetHint(SDL_HINT_WINDOW_NO_ACTIVATION_WHEN_SHOWN, activate ? "0" : "1");
#endif
		SDL_ShowWindow(_window);
		if (activate) {
			SDL_RaiseWindow(_window);
		}
	}
}

#endif
