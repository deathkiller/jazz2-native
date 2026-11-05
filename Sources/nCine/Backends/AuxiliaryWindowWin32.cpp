#include "AuxiliaryWindowWin32.h"

#if defined(DEATH_TARGET_WINDOWS) && defined(NCINE_HAS_AUXILIARY_WINDOWS)

#include <CommonWindows.h>

namespace nCine::Backends
{
	namespace
	{
		// From <dwmapi.h>, loaded at run time, so older systems without the attributes run too
		constexpr DWORD DwmNcRenderingPolicy = 2;			// DWMWA_NCRENDERING_POLICY
		constexpr DWORD DwmNcRenderingEnabled = 2;			// DWMNCRP_ENABLED
		constexpr DWORD DwmWindowCornerPreference = 33;		// DWMWA_WINDOW_CORNER_PREFERENCE, Windows 11
		constexpr DWORD DwmCornerRound = 2;					// DWMWCP_ROUND
		constexpr DWORD DwmCornerRoundSmall = 3;			// DWMWCP_ROUNDSMALL

		struct DwmMargins
		{
			int Left, Right, Top, Bottom;
		};

		using DwmSetWindowAttributeFunc = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
		using DwmExtendFrameIntoClientAreaFunc = HRESULT(WINAPI*)(HWND, const DwmMargins*);

		struct DwmFunctions
		{
			DwmSetWindowAttributeFunc SetWindowAttribute = nullptr;
			DwmExtendFrameIntoClientAreaFunc ExtendFrameIntoClientArea = nullptr;
		};

		const DwmFunctions& GetDwm()
		{
			static DwmFunctions functions = []() {
				DwmFunctions result;
				if (HMODULE dwm = ::LoadLibraryW(L"dwmapi.dll")) {
					result.SetWindowAttribute = reinterpret_cast<DwmSetWindowAttributeFunc>(::GetProcAddress(dwm, "DwmSetWindowAttribute"));
					result.ExtendFrameIntoClientArea = reinterpret_cast<DwmExtendFrameIntoClientAreaFunc>(::GetProcAddress(dwm, "DwmExtendFrameIntoClientArea"));
				}
				return result;
			}();
			return functions;
		}

		struct WindowIcons
		{
			HICON Big = nullptr;
			HICON Small = nullptr;
		};

		const WindowIcons& GetIcons()
		{
			// Loaded once, they stay for as long as the application runs
			static WindowIcons icons = []() {
				WindowIcons result;
				HINSTANCE instance = ::GetModuleHandleW(nullptr);
				result.Big = static_cast<HICON>(::LoadImageW(instance, L"WINDOW_AUXILIARY_ICON", IMAGE_ICON,
					::GetSystemMetrics(SM_CXICON), ::GetSystemMetrics(SM_CYICON), LR_DEFAULTSIZE));
				result.Small = static_cast<HICON>(::LoadImageW(instance, L"WINDOW_AUXILIARY_ICON", IMAGE_ICON,
					::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON), LR_DEFAULTSIZE));
				return result;
			}();
			return icons;
		}

		constexpr const wchar_t* OriginalProcProperty = L"nCine.AuxiliaryWindow.Proc";

		// The pointer goes through to the windows of the thread below, and a click never activates the window
		LRESULT CALLBACK PassThroughWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
		{
			WNDPROC original = reinterpret_cast<WNDPROC>(::GetPropW(hwnd, OriginalProcProperty));
			switch (message) {
				case WM_NCHITTEST:
					return HTTRANSPARENT;
				case WM_MOUSEACTIVATE:
					return MA_NOACTIVATE;
				case WM_NCDESTROY:
					::SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(original));
					::RemovePropW(hwnd, OriginalProcProperty);
					break;
			}
			return (original != nullptr ? ::CallWindowProcW(original, hwnd, message, wParam, lParam) : ::DefWindowProcW(hwnd, message, wParam, lParam));
		}
	}

	bool SetupAuxiliaryWindowWin32(void* hwnd, void* mainHwnd, bool popup, bool passThrough)
	{
		HWND window = static_cast<HWND>(hwnd);
		if (window == nullptr) {
			return false;
		}
		if (passThrough && ::GetPropW(window, OriginalProcProperty) == nullptr) {
			LONG_PTR original = ::SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&PassThroughWindowProc));
			::SetPropW(window, OriginalProcProperty, reinterpret_cast<HANDLE>(original));
		}

		// Owned by the main window, it stays in front of it; an owned window isn't on the taskbar unless it's asked for
		if (mainHwnd != nullptr) {
			::SetWindowLongPtrW(window, GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(mainHwnd));
		}
		LONG_PTR exStyle = ::GetWindowLongPtrW(window, GWL_EXSTYLE);
		if (popup) {
			exStyle = (exStyle | WS_EX_TOOLWINDOW) & ~LONG_PTR(WS_EX_APPWINDOW);
		} else {
			exStyle = (exStyle | WS_EX_APPWINDOW) & ~LONG_PTR(WS_EX_TOOLWINDOW);
		}
		::SetWindowLongPtrW(window, GWL_EXSTYLE, exStyle);

		if (!popup) {
			const WindowIcons& icons = GetIcons();
			HICON big = icons.Big, small = icons.Small;
			if (big == nullptr && mainHwnd != nullptr) {
				big = reinterpret_cast<HICON>(::SendMessageW(static_cast<HWND>(mainHwnd), WM_GETICON, ICON_BIG, 0));
				small = reinterpret_cast<HICON>(::SendMessageW(static_cast<HWND>(mainHwnd), WM_GETICON, ICON_SMALL, 0));
			}
			if (big != nullptr) {
				::SendMessageW(window, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(big));
			}
			if (small != nullptr) {
				::SendMessageW(window, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(small));
			}
		}

		// The shadow of the system, a borderless window gets it with the frame of the compositor extended into it, and
		// rounded corners on Windows 11, which also cast it
		const DwmFunctions& dwm = GetDwm();
		bool rounded = false;
		if (dwm.SetWindowAttribute != nullptr) {
			DWORD policy = DwmNcRenderingEnabled;
			dwm.SetWindowAttribute(window, DwmNcRenderingPolicy, &policy, sizeof(policy));
			// Older systems don't know the attribute
			DWORD corners = (popup ? DwmCornerRoundSmall : DwmCornerRound);
			rounded = SUCCEEDED(dwm.SetWindowAttribute(window, DwmWindowCornerPreference, &corners, sizeof(corners)));
		}
		if (dwm.ExtendFrameIntoClientArea != nullptr) {
			DwmMargins margins = { 1, 1, 1, 1 };
			dwm.ExtendFrameIntoClientArea(window, &margins);
		}
		::SetWindowPos(window, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
		return rounded;
	}
}

#endif
