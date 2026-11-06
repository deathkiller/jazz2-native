#include "GlfwAuxiliaryWindow.h"

#if defined(WITH_GLFW) && defined(NCINE_HAS_AUXILIARY_WINDOWS)

#include "GlfwInputManager.h"
#include "AuxiliaryWindowWin32.h"
#include "../Graphics/AuxiliaryWindowPresenter.h"

#if defined(DEATH_TARGET_WINDOWS)
#	undef APIENTRY
#	if !defined(GLFW_EXPOSE_NATIVE_WIN32)
#		define GLFW_EXPOSE_NATIVE_WIN32
#	endif
#	if defined(__HAS_LOCAL_GLFW)
#		include "GL/glfw3native.h"
#	else
#		include <glfw3native.h>
#	endif
#endif

#include <algorithm>

#define GLFW_HAS_GET_PLATFORM (GLFW_VERSION_MAJOR > 3 || (GLFW_VERSION_MAJOR == 3 && GLFW_VERSION_MINOR >= 4))
#if GLFW_HAS_GET_PLATFORM && defined(__ELF__)
// The header can be newer than the linked library
#	pragma weak glfwGetPlatform
#endif

namespace nCine::Backends
{
	namespace
	{
		// GLFW has no identifiers of its own, they're counted from 1
		std::uint32_t NextWindowId = 1;
	}

	bool GlfwAuxiliaryWindow::IsSupported()
	{
		if (!AuxiliaryWindowPresenter::IsSupported()) {
			return false;
		}
#if GLFW_HAS_GET_PLATFORM
		// Wayland doesn't let an application put its windows where it wants
#	if defined(__ELF__)
		if (glfwGetPlatform != nullptr && glfwGetPlatform() == GLFW_PLATFORM_WAYLAND) {
#	else
		if (glfwGetPlatform() == GLFW_PLATFORM_WAYLAND) {
#	endif
			return false;
		}
#endif
		return true;
	}

	std::unique_ptr<GlfwAuxiliaryWindow> GlfwAuxiliaryWindow::Create(const AuxiliaryWindowDesc& desc)
	{
		if (!IsSupported()) {
			return nullptr;
		}
		std::unique_ptr<GlfwAuxiliaryWindow> window(new GlfwAuxiliaryWindow(desc));
		if (window->_window == nullptr || window->_presenter == nullptr) {
			return nullptr;
		}
		return window;
	}

	GlfwAuxiliaryWindow* GlfwAuxiliaryWindow::FromHandle(GLFWwindow* window)
	{
		for (AuxiliaryWindow* auxiliary : AuxiliaryWindow::GetAll()) {
			GlfwAuxiliaryWindow* glfwWindow = static_cast<GlfwAuxiliaryWindow*>(auxiliary);
			if (glfwWindow->_window == window) {
				return glfwWindow;
			}
		}
		return nullptr;
	}

	GlfwAuxiliaryWindow::GlfwAuxiliaryWindow(const AuxiliaryWindowDesc& desc)
		: _popup(desc.Popup)
	{
		GLFWwindow* mainWindow = GlfwGfxDevice::GetWindowHandle();
		// The hints of the context stay as the main window was made with, it shares its objects
		glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
		glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
		glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
		glfwWindowHint(GLFW_FOCUS_ON_SHOW, GLFW_FALSE);
		glfwWindowHint(GLFW_FOCUSED, GLFW_FALSE);
#if !defined(DEATH_TARGET_WINDOWS) && defined(GLFW_MOUSE_PASSTHROUGH)
		// On Windows the window is made to let the pointer through by SetupAuxiliaryWindowWin32()
		glfwWindowHint(GLFW_MOUSE_PASSTHROUGH, desc.PassThrough ? GLFW_TRUE : GLFW_FALSE);
#endif
		std::int32_t width = std::max<std::int32_t>(desc.Bounds.W, 1), height = std::max<std::int32_t>(desc.Bounds.H, 1);
		_window = glfwCreateWindow(width, height, "", nullptr, mainWindow);
#if !defined(DEATH_TARGET_WINDOWS) && defined(GLFW_MOUSE_PASSTHROUGH)
		glfwWindowHint(GLFW_MOUSE_PASSTHROUGH, GLFW_FALSE);
#endif
		glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);
		glfwWindowHint(GLFW_DECORATED, GLFW_TRUE);
		glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
		glfwWindowHint(GLFW_FOCUS_ON_SHOW, GLFW_TRUE);
		glfwWindowHint(GLFW_FOCUSED, GLFW_TRUE);
		if (_window == nullptr) {
			LOGE("A window besides the main one couldn't be created");
			return;
		}
		_id = NextWindowId++;
		_size = Vector2i(width, height);
		glfwSetWindowPos(_window, desc.Bounds.X, desc.Bounds.Y);

#if defined(DEATH_TARGET_WINDOWS)
		_roundedCorners = SetupAuxiliaryWindowWin32(glfwGetWin32Window(_window), glfwGetWin32Window(mainWindow), desc.Popup, desc.PassThrough);
#endif

		// Input over it goes where input over the main window goes
		GlfwInputManager::AttachAuxiliaryWindow(_window);

		// Its context doesn't wait for the display, the main window does already
		glfwMakeContextCurrent(_window);
		glfwSwapInterval(0);
		glfwMakeContextCurrent(mainWindow);

		RHI::AuxiliarySwapchainDesc native;
		GLFWwindow* window = _window;
		native.MakeCurrent = [window]() {
			glfwMakeContextCurrent(window);
			return true;
		};
		native.SwapBuffers = [window]() {
			glfwSwapBuffers(window);
		};
		native.RestoreMain = []() {
			glfwMakeContextCurrent(GlfwGfxDevice::GetWindowHandle());
		};
		_presenter = AuxiliaryWindowPresenter::Create(Death::move(native), GetPixelSize());
	}

	GlfwAuxiliaryWindow::~GlfwAuxiliaryWindow()
	{
		// The presenter goes first, it may still use the context
		_presenter = nullptr;
		if (_window != nullptr) {
			glfwDestroyWindow(_window);
			glfwMakeContextCurrent(GlfwGfxDevice::GetWindowHandle());
		}
	}

	void GlfwAuxiliaryWindow::SetBounds(const Recti& bounds)
	{
		std::int32_t width = std::max<std::int32_t>(bounds.W, 1), height = std::max<std::int32_t>(bounds.H, 1);
		Vector2i position;
		glfwGetWindowPos(_window, &position.X, &position.Y);
		if (position.X != bounds.X || position.Y != bounds.Y) {
			glfwSetWindowPos(_window, bounds.X, bounds.Y);
		}
		if (width != _size.X || height != _size.Y) {
			glfwSetWindowSize(_window, width, height);
			_size = Vector2i(width, height);
		}
	}

	Recti GlfwAuxiliaryWindow::GetBounds() const
	{
		Recti bounds;
		glfwGetWindowPos(_window, &bounds.X, &bounds.Y);
		glfwGetWindowSize(_window, &bounds.W, &bounds.H);
		return bounds;
	}

	Vector2i GlfwAuxiliaryWindow::GetPixelSize() const
	{
		Vector2i size;
		glfwGetFramebufferSize(_window, &size.X, &size.Y);
		return size;
	}

	float GlfwAuxiliaryWindow::GetContentScale() const
	{
		// The units of windows are pixels on most systems, the scale of the display is told apart
		float xScale = 1.0f, yScale = 1.0f;
		glfwGetWindowContentScale(_window, &xScale, &yScale);
		return (xScale > 0.0f ? xScale : 1.0f);
	}

	bool GlfwAuxiliaryWindow::IsFocused() const
	{
		return (glfwGetWindowAttrib(_window, GLFW_FOCUSED) != 0);
	}

	void GlfwAuxiliaryWindow::Raise()
	{
		glfwFocusWindow(_window);
	}

	void GlfwAuxiliaryWindow::SetTitle(StringView title)
	{
		glfwSetWindowTitle(_window, String::nullTerminatedView(title).data());
	}

	void GlfwAuxiliaryWindow::SetVisible(bool visible, bool activate)
	{
		if (!visible) {
			glfwHideWindow(_window);
			return;
		}
		glfwShowWindow(_window);
		// A popup never takes the focus from the window it was shown from
		if (activate && !_popup) {
			glfwFocusWindow(_window);
		}
	}
}

#endif
