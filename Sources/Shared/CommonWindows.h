#pragma once

#include "Common.h"

#if defined(DEATH_TARGET_WINDOWS)

#if defined(_INC_WINDOWS)
#	error <windows.h> cannot be included directly, include <CommonWindows.h> instead
#endif

// Undefine `min`/`max` macros, use `std::min`/`std::max` instead
#if !defined(NOMINMAX)
#	define NOMINMAX
#endif

#define WIN32_LEAN_AND_MEAN
#define WINRT_LEAN_AND_MEAN
#include <windows.h>

// Retrieves the address of an exported function using the type information from the specified function
#define GetProcAddressByFunctionDeclaration(hinst, fn) reinterpret_cast<decltype(::fn)*>(::GetProcAddress(hinst, #fn))

// Undefine `far` and `near` keywords, not used anymore (but still exist in some headers)
#if defined(far)
#	undef far
#	if defined(FAR)
#		undef FAR
#		define FAR
#	endif
#endif
#if defined(near)
#	undef near
#	if defined(NEAR)
#		undef NEAR
#		define NEAR
#	endif
#endif

// Undefine obsolete redirections
#if defined(GetCurrentTime)
#	undef GetCurrentTime
#endif

// Replace redirections with inline functions
#if defined(CreateDialog)
#	undef CreateDialog
DEATH_ALWAYS_INLINE HWND WINAPI CreateDialog(HINSTANCE hInstance, LPCTSTR pTemplate, HWND hwndParent, DLGPROC lpDialogFunc) {
	return ::CreateDialogW(hInstance, pTemplate, hwndParent, lpDialogFunc);
}
#endif

#if defined(CreateDirectory)
#	undef CreateDirectory
DEATH_ALWAYS_INLINE BOOL WINAPI CreateDirectory(LPCWSTR lpPathName, LPSECURITY_ATTRIBUTES lpSecurityAttributes) {
	return ::CreateDirectoryW(lpPathName, lpSecurityAttributes);
}
#endif

#if defined(CreateEvent)
#	undef CreateEvent
DEATH_ALWAYS_INLINE HANDLE WINAPI CreateEvent(LPSECURITY_ATTRIBUTES lpEventAttributes, BOOL bManualReset, BOOL bInitialState, LPCWSTR lpName) {
	return ::CreateEventW(lpEventAttributes, bManualReset, bInitialState, lpName);
}
#endif

#if defined(CreateFile)
#	undef CreateFile
DEATH_ALWAYS_INLINE HANDLE WINAPI CreateFile(LPCWSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode, LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes, HANDLE hTemplateFile) {
	return ::CreateFileW(lpFileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
}
#endif

#if defined(CreateFont)
#	undef CreateFont
DEATH_ALWAYS_INLINE HFONT WINAPI CreateFont(int cHeight, int cWidth, int cEscapement, int cOrientation, int cWeight, DWORD bItalic, DWORD bUnderline, DWORD bStrikeOut, DWORD iCharSet, DWORD iOutPrecision, DWORD iClipPrecision, DWORD iQuality, DWORD iPitchAndFamily, LPCTSTR pszFaceName) {
	return ::CreateFontW(cHeight, cWidth, cEscapement, cOrientation, cWeight, bItalic, bUnderline, bStrikeOut, iCharSet, iOutPrecision, iClipPrecision, iQuality, iPitchAndFamily, pszFaceName);
}
#endif

#if defined(CreateNamedPipe)
#	undef CreateNamedPipe
DEATH_ALWAYS_INLINE HANDLE WINAPI CreateNamedPipe(LPCWSTR lpName, DWORD dwOpenMode, DWORD dwPipeMode, DWORD nMaxInstances, DWORD nOutBufferSize, DWORD nInBufferSize, DWORD nDefaultTimeOut, LPSECURITY_ATTRIBUTES lpSecurityAttributes) {
	return ::CreateNamedPipeW(lpName, dwOpenMode, dwPipeMode, nMaxInstances, nOutBufferSize, nInBufferSize, nDefaultTimeOut, lpSecurityAttributes);
}
#endif

#if defined(CreateWindow)
#	undef CreateWindow
#	if !defined(DEATH_TARGET_WINDOWS_RT)
DEATH_ALWAYS_INLINE HWND WINAPI CreateWindow(LPCTSTR lpClassName, LPCTSTR lpWndClass, DWORD dwStyle, int x, int y, int w, int h, HWND hWndParent, HMENU hMenu, HINSTANCE hInstance, LPVOID lpParam) {
	return ::CreateWindowW(lpClassName, lpWndClass, dwStyle, x, y, w, h, hWndParent, hMenu, hInstance, lpParam);
}
#	endif
#endif

#if defined(CreateWindowEx)
#	undef CreateWindowEx
#	if !defined(DEATH_TARGET_WINDOWS_RT)
DEATH_ALWAYS_INLINE HWND WINAPI CreateWindowEx(DWORD dwExStyle, LPCWSTR lpClassName, LPCWSTR lpWindowName, DWORD dwStyle, int X, int Y, int nWidth, int nHeight, HWND hWndParent, HMENU hMenu, HINSTANCE hInstance, LPVOID lpParam) {
	return ::CreateWindowExW(dwExStyle, lpClassName, lpWindowName, dwStyle, X, Y, nWidth, nHeight, hWndParent, hMenu, hInstance, lpParam);
}
#	endif
#endif

#if defined(DeleteFile)
#	undef DeleteFile
DEATH_ALWAYS_INLINE BOOL WINAPI DeleteFile(LPCWSTR lpFileName) {
	return ::DeleteFileW(lpFileName);
}
#endif

#if defined(DrawText)
#	undef DrawText
DEATH_ALWAYS_INLINE int WINAPI DrawText(HDC hdc, LPCWSTR lpchText, int cchText, LPRECT lprc, UINT format) {
	return ::DrawTextW(hdc, lpchText, cchText, lprc, format);
}
#endif

#if defined(FindResource)
#	undef FindResource
DEATH_ALWAYS_INLINE HRSRC WINAPI FindResource(HMODULE hModule, LPCWSTR lpName, LPCWSTR lpType) {
	return ::FindResourceW(hModule, lpName, lpType);
}
#endif

#if defined(FindText)
#	undef FindText
DEATH_ALWAYS_INLINE HWND WINAPI FindText(LPFINDREPLACE lpFindReplace) {
	return ::FindTextW(lpFindReplace);
}
#endif

#if defined(FindWindow)
#	undef FindWindow
DEATH_ALWAYS_INLINE HWND WINAPI FindWindow(LPCWSTR lpClassName, LPCWSTR lpWindowName) {
	return ::FindWindowW(lpClassName, lpWindowName);
}
#endif

#if defined(FormatMessage)
#	undef FormatMessage
DEATH_ALWAYS_INLINE DWORD WINAPI FormatMessage(DWORD dwFlags, LPCVOID lpSource, DWORD dwMessageId, DWORD dwLanguageId, LPTSTR lpBuffer, DWORD nSize, va_list* Arguments) {
	return ::FormatMessageW(dwFlags, lpSource, dwMessageId, dwLanguageId, lpBuffer, nSize, Arguments);
}
#endif

#if defined(GetCharWidth)
#	undef GetCharWidth
DEATH_ALWAYS_INLINE BOOL WINAPI GetCharWidth(HDC hdc, UINT iFirst, UINT iLast, LPINT lpBuffer) {
	return ::GetCharWidthW(hdc, iFirst, iLast, lpBuffer);
}
#endif

#if defined(GetClassInfo)
#	undef GetClassInfo
DEATH_ALWAYS_INLINE BOOL WINAPI GetClassInfo(HINSTANCE hInstance, LPCWSTR lpClassName, LPWNDCLASSW lpWndClass) {
	return ::GetClassInfoW(hInstance, lpClassName, lpWndClass);
}
#endif

#if defined(GetClassName)
#	undef GetClassName
DEATH_ALWAYS_INLINE int WINAPI GetClassName(HWND hWnd, LPWSTR lpClassName, int nMaxCount) {
	return ::GetClassNameW(hWnd, lpClassName, nMaxCount);
}
#endif

#if defined(GetEnvironmentVariable)
#	undef GetEnvironmentVariable
DEATH_ALWAYS_INLINE DWORD WINAPI GetEnvironmentVariable(LPCWSTR lpName, LPWSTR lpBuffer, DWORD nSize) {
	return ::GetEnvironmentVariableW(lpName, lpBuffer, nSize);
}
#endif

#if defined(GetMessage)
#	undef GetMessage
DEATH_ALWAYS_INLINE int WINAPI GetMessage(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax) {
	return ::GetMessageW(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax);
}
#endif

#if defined(GetObject)
#	undef GetObject
DEATH_ALWAYS_INLINE int WINAPI GetObject(HGDIOBJ h, int c, LPVOID pv) {
	return ::GetObjectW(h, c, pv);
}
#endif

#if defined(GetUserName)
#	undef GetUserName
DEATH_ALWAYS_INLINE BOOL WINAPI GetUserName(LPWSTR lpBuffer, LPDWORD pcbBuffer) {
	return ::GetUserNameW(lpBuffer, pcbBuffer);
}
#endif

#if defined(GetWindowsDirectory)
#	undef GetWindowsDirectory
DEATH_ALWAYS_INLINE UINT WINAPI GetWindowsDirectory(LPWSTR lpBuffer, UINT uSize) {
	return ::GetWindowsDirectoryW(lpBuffer, uSize);
}
#endif

#if defined(LoadAccelerators)
#	undef LoadAccelerators
DEATH_ALWAYS_INLINE HACCEL WINAPI LoadAccelerators(HINSTANCE hInstance, LPCWSTR lpTableName) {
	return ::LoadAcceleratorsW(hInstance, lpTableName);
}
#endif

#if defined(LoadBitmap)
#	undef LoadBitmap
DEATH_ALWAYS_INLINE HBITMAP WINAPI LoadBitmap(HINSTANCE hInstance, LPCTSTR lpBitmapName) {
	return ::LoadBitmapW(hInstance, lpBitmapName);
}
#endif

#if defined(LoadIcon)
#	undef LoadIcon
DEATH_ALWAYS_INLINE HICON WINAPI LoadIcon(HINSTANCE hInstance, LPCTSTR lpIconName) {
	return ::LoadIconW(hInstance, lpIconName);
}
#endif

#if defined(LoadLibrary)
#	undef LoadLibrary
DEATH_ALWAYS_INLINE HMODULE WINAPI LoadLibrary(LPCWSTR lpLibFileName) {
	return ::LoadLibraryW(lpLibFileName);
}
#endif

#if defined(LoadLibraryEx)
#	undef LoadLibraryEx
DEATH_ALWAYS_INLINE HMODULE WINAPI LoadLibraryEx(LPCWSTR lpLibFileName, HANDLE hFile, DWORD dwFlags) {
	return ::LoadLibraryExW(lpLibFileName, hFile, dwFlags);
}
#endif

#if defined(LoadMenu)
#	undef LoadMenu
DEATH_ALWAYS_INLINE HMENU WINAPI LoadMenu(HINSTANCE hInstance, LPCTSTR lpMenuName) {
	return ::LoadMenuW(hInstance, lpMenuName);
}
#endif

#if defined(LoadString)
#	undef LoadString
DEATH_ALWAYS_INLINE int WINAPI LoadString(HINSTANCE hInstance, UINT uID, LPWSTR lpBuffer, int cchBufferMax) {
	return ::LoadStringW(hInstance, uID, lpBuffer, cchBufferMax);
}
#endif

#if defined(MoveFile)
#	undef MoveFile
DEATH_ALWAYS_INLINE BOOL WINAPI MoveFile(LPCWSTR lpExistingFileName, LPCWSTR lpNewFileName) {
	return ::MoveFileW(lpExistingFileName, lpNewFileName);
}
#endif

#if defined(PlaySound)
#	undef PlaySound
DEATH_ALWAYS_INLINE BOOL WINAPI PlaySound(LPCWSTR pszSound, HMODULE hMod, DWORD fdwSound) {
	return ::PlaySoundW(pszSound, hMod, fdwSound);
}
#endif

#if defined(RegisterClass)
#	undef RegisterClass
DEATH_ALWAYS_INLINE ATOM WINAPI RegisterClass(CONST WNDCLASSW* lpWndClass) {
	return ::RegisterClassW(lpWndClass);
}
#endif

#if defined(RegisterClassEx)
#	undef RegisterClassEx
DEATH_ALWAYS_INLINE ATOM WINAPI RegisterClassEx(CONST WNDCLASSEXW* lpWndClass) {
	return ::RegisterClassExW(lpWndClass);
}
#endif

#if defined(RegisterWindowMessage)
#	undef RegisterWindowMessage
DEATH_ALWAYS_INLINE UINT WINAPI RegisterWindowMessage(LPCWSTR lpString) {
	return ::RegisterWindowMessageW(lpString);
}
#endif

#if defined(RemoveDirectory)
#	undef RemoveDirectory
DEATH_ALWAYS_INLINE BOOL WINAPI RemoveDirectory(LPCWSTR lpPathName) {
	return ::RemoveDirectoryW(lpPathName);
}
#endif

#if defined(SendMessage)
#	undef SendMessage
DEATH_ALWAYS_INLINE LRESULT WINAPI SendMessage(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam) {
	return ::SendMessageW(hWnd, Msg, wParam, lParam);
}
#endif

#if defined(SetConsoleTitle)
#	undef SetConsoleTitle
DEATH_ALWAYS_INLINE BOOL WINAPI SetConsoleTitle(LPCWSTR lpConsoleTitle) {
	return ::SetConsoleTitleW(lpConsoleTitle);
}
#endif

#if defined(StartDoc)
#	undef StartDoc
DEATH_ALWAYS_INLINE int WINAPI StartDoc(HDC hdc, CONST DOCINFOW* lpdi) {
	return ::StartDocW(hdc, lpdi);
}
#endif

#if defined(UnregisterClass)
#	undef UnregisterClass
DEATH_ALWAYS_INLINE BOOL WINAPI UnregisterClass(LPCWSTR lpClassName, HINSTANCE hInstance) {
	return ::UnregisterClassW(lpClassName, hInstance);
}
#endif

#if defined(WriteConsole)
#	undef WriteConsole
DEATH_ALWAYS_INLINE BOOL WINAPI WriteConsole(HANDLE hConsoleOutput, CONST VOID* lpBuffer, DWORD nNumberOfCharsToWrite, LPDWORD lpNumberOfCharsWritten, LPVOID lpReserved) {
	return ::WriteConsoleW(hConsoleOutput, lpBuffer, nNumberOfCharsToWrite, lpNumberOfCharsWritten, lpReserved);
}
#endif

#if defined(WriteConsoleOutput)
#	undef WriteConsoleOutput
DEATH_ALWAYS_INLINE BOOL WINAPI WriteConsoleOutput(HANDLE hConsoleOutput, CONST CHAR_INFO* lpBuffer, COORD dwBufferSize, COORD dwBufferCoord, PSMALL_RECT lpWriteRegion) {
	return ::WriteConsoleOutputW(hConsoleOutput, lpBuffer, dwBufferSize, dwBufferCoord, lpWriteRegion);
}
#endif

#if defined(Yield)
#	undef Yield
#endif

#endif