#include "Application.h"

#if defined(DEATH_TARGET_WINDOWS)
extern "C"
{
	_declspec(dllexport) unsigned long int NvOptimusEnablement = 0x00000001;
	_declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 0x00000001;
};
#endif

#if defined(DEATH_TARGET_WINDOWS) && !defined(CMAKE_BUILD)
#	pragma comment(lib, "opengl32.lib")
#	pragma comment(lib, "winmm.lib")
#	if defined(_M_X64)
#		if defined(WITH_GLEW)
#			pragma comment(lib, "../Libs/Windows/x64/glew32.lib")
#		endif
#		if defined(WITH_GLFW)
#			pragma comment(lib, "../Libs/Windows/x64/glfw3dll.lib")
#		endif
#		if defined(WITH_SDL)
#			pragma comment(lib, "../Libs/Windows/x64/SDL2.lib")
#		endif
#		if defined(WITH_AUDIO)
#			pragma comment(lib, "../Libs/Windows/x64/OpenAL32.lib")
#		endif
#	elif defined(_M_IX86)
#		if defined(WITH_GLEW)
#			pragma comment(lib, "../Libs/Windows/x86/glew32.lib")
#		endif
#		if defined(WITH_GLFW)
#			pragma comment(lib, "../Libs/Windows/x86/glfw3dll.lib")
#		endif
#		if defined(WITH_SDL)
#			pragma comment(lib, "../Libs/Windows/x86/SDL2.lib")
#		endif
#		if defined(WITH_AUDIO)
#			pragma comment(lib, "../Libs/Windows/x86/OpenAL32.lib")
#		endif
#	else
#		error Unsupported architecture
#	endif
#endif

#include "Base/Algorithms.h"
#include "Base/Random.h"
#include "IAppEventHandler.h"
#include "Graphics/GfxCapabilities.h"
#include "Graphics/RenderResources.h"
#include "Graphics/RenderQueue.h"
#include "Graphics/ScreenViewport.h"
#include "Graphics/GL/GLDebug.h"
#include "Base/FrameTimer.h"
#include "Graphics/SceneNode.h"
#include "Input/IInputManager.h"
#include "Input/JoyMapping.h"
#include "Threading/Thread.h"
#include "ServiceLocator.h"
#include "tracy.h"
#include "tracy_opengl.h"

#include <Containers/DateTime.h>
#include <Containers/StringConcatenable.h>
#include <Containers/StringView.h>
#include <IO/FileSystem.h>

#if defined(WITH_AUDIO)
#	include "Audio/ALAudioDevice.h"
#endif

#if defined(WITH_THREADS)
#	include "Threading/ThreadPool.h"
#endif

#if defined(WITH_LUA)
#	include "LuaStatistics.h"
#endif

#if defined(WITH_IMGUI)
#	include "Graphics/ImGuiDrawing.h"
#	include "Graphics/ImGuiDebugOverlay.h"
#endif

#if defined(WITH_RENDERDOC)
#	include "Graphics/RenderDocCapture.h"
#endif

#if defined(WITH_BACKWARD)
#	include <Core/Backward.h>
Backward::ExceptionHandling __eh(Backward::Flags::UseStdError | Backward::Flags::IncludeSnippet | Backward::Flags::CreateMemoryDump);
#endif

using namespace Death::Containers::Literals;
using namespace Death::IO;

#if defined(DEATH_TRACE)

#if defined(DEATH_TARGET_WINDOWS)
#	include <Environment.h>
#	include <Utf8.h>
#elif defined(DEATH_TARGET_ANDROID)
#	include <stdarg.h>
#	include <time.h>
#	include <unistd.h>
#	include <android/log.h>
#else
#	include <cstdarg>
#	include <unistd.h>
#	if defined(DEATH_TARGET_SWITCH)
#		include <time.h>
#		include <switch.h>
#	endif
#endif

#if defined(DEATH_TRACE_ASYNC) && (!defined(WITH_THREADS) || defined(DEATH_TARGET_EMSCRIPTEN))
#	pragma message("DEATH_TRACE_ASYNC is not supported on this platform")
#	undef DEATH_TRACE_ASYNC
#endif

static constexpr std::int32_t MaxLogEntryLength = 4096;

static const char ColorReset[] = "\x1B[0m";
static const char ColorBold[] = "\x1B[1m";
static const char ColorFaint[] = "\x1B[2m";
static const char ColorDarkGray[] = "\x1B[90m";
static const char ColorBrightRed[] = "\x1B[91m";
static const char ColorBrightYellow[] = "\x1B[93m";
static const char ColorBrightMagenta[] = "\x1B[95m";
static const char ColorDarkString[] = "\x1B[0;38;2;211;161;129m";
static const char ColorLightString[] = "\x1B[0;38;2;145;109;94m";
static const char ColorDimString[] = "\x1B[0;38;2;177;150;132m";

#if defined(DEATH_TARGET_EMSCRIPTEN)
#	include <emscripten/emscripten.h>
#else
#	include <IO/FileStream.h>
static std::unique_ptr<Death::IO::Stream> __logFile;
#endif

enum class ConsoleType {
	None,
	Redirect,
	WinApi,
	EscapeCodes,
	EscapeCodes8bit,
	EscapeCodes24bit
};

static ConsoleType __consoleType = ConsoleType::None;
#if !defined(DEATH_TARGET_ANDROID) && !defined(DEATH_TARGET_SWITCH) && !defined(DEATH_TARGET_WINDOWS_RT)
static bool __consoleDarkMode = true;
static bool __consoleSixelSupported = false;
#endif

#if defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)
#	if !defined(ENABLE_VIRTUAL_TERMINAL_INPUT)
#		define ENABLE_VIRTUAL_TERMINAL_INPUT 0x0200
#	endif
#	if !defined(ENABLE_VIRTUAL_TERMINAL_PROCESSING)
#		define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#	endif

extern "C" IMAGE_DOS_HEADER __ImageBase;

static HANDLE __consoleHandleOut;
static SHORT __consoleCursorY;
static Array<wchar_t> __consolePrompt;

static bool EnableVirtualTerminalProcessing(HANDLE consoleHandleOut)
{
	if (consoleHandleOut == INVALID_HANDLE_VALUE || !Environment::IsWindows10()) {
		return false;
	}

	DWORD dwMode = 0;
	return (::GetConsoleMode(consoleHandleOut, &dwMode) &&
			::SetConsoleMode(consoleHandleOut, dwMode | ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING));
}

template<typename TCallback>
static bool FillBufferWithTimeout(HANDLE handle, OVERLAPPED& ov, char* buffer, std::uint32_t bufferSize, TCallback callback)
{
	constexpr std::uint64_t TimeoutMs = 100;

	std::uint32_t bytesRead = 0;
	std::uint64_t startTime = ::GetTickCount64();
	std::uint64_t now = startTime;

	while (true) {
		if (!::ReadFile(handle, buffer + bytesRead, bufferSize - bytesRead, NULL, &ov)) {
			DWORD err = GetLastError();
			if (err == ERROR_IO_PENDING) {
				DWORD waitResult = ::WaitForSingleObject(ov.hEvent, (DWORD)(TimeoutMs - (now - startTime)));
				if (waitResult == WAIT_TIMEOUT) {
					::CancelIo(handle);
					break;
				} else if (waitResult != WAIT_OBJECT_0) {
					break;
				}
			} else {
				break;
			}
		}

		DWORD partialBytesRead = 0;
		if (!::GetOverlappedResult(handle, &ov, &partialBytesRead, FALSE)) {
			break;
		}

		bytesRead += partialBytesRead;
		if (callback(StringView(buffer, bytesRead))) {
			return true;
		}

		now = ::GetTickCount64();
		if (now - startTime >= TimeoutMs || bytesRead >= bufferSize) {
			break;
		}
	}

	return false;
}

static void CheckConsoleCapabilities()
{
	HANDLE hStdIn = ::CreateFile(L"CONIN$", GENERIC_READ | GENERIC_WRITE,
		FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, nullptr);
	if (hStdIn == INVALID_HANDLE_VALUE) {
		LOGD("Failed to open async \"CONIN$\" handle with error 0x{:.8x}", ::GetLastError());
		return;
	}

	OVERLAPPED ov{};
	ov.hEvent = ::CreateEvent(NULL, TRUE, FALSE, NULL);
	if (ov.hEvent == NULL) {
		::CloseHandle(hStdIn);
		return;
	}

	HANDLE hStdOut = ::GetStdHandle(STD_OUTPUT_HANDLE);

	DWORD stdInMode = 0;
	::GetConsoleMode(hStdIn, &stdInMode);
	::SetConsoleMode(hStdIn, (stdInMode & ~ENABLE_LINE_INPUT) | ENABLE_VIRTUAL_TERMINAL_INPUT);

	CHAR buffer[128];
	DWORD bytesWritten;

	static const char daRequest[] = "\x1B[c";
	::WriteConsoleA(hStdOut, daRequest, sizeof(daRequest) - 1, &bytesWritten, nullptr);
	::FlushConsoleInputBuffer(hStdIn);

	FillBufferWithTimeout(hStdIn, ov, buffer, sizeof(buffer), [](StringView bufferView) {
		if (StringView end = bufferView.find('c')) {
			if (StringView begin = bufferView.find("\x1B[?"_s)) {
				if (begin.end() < end.begin()) {
					String response = ';' + bufferView.slice(begin.end(), end.begin()) + ';';
					if (response.contains(";4;"_s)) {
						__consoleSixelSupported = true;
					}
				}
			}
			return true;
		}
		return false;
	});

	static const char bgColorRequest[] = "\x1B]11;?\x07";
	::WriteConsoleA(hStdOut, bgColorRequest, sizeof(bgColorRequest) - 1, &bytesWritten, nullptr);
	::FlushConsoleInputBuffer(hStdIn);
	
	FillBufferWithTimeout(hStdIn, ov, buffer, sizeof(buffer), [](StringView bufferView) {
		if (StringView end = bufferView.find("\x1B\\"_s)) {
			if (StringView begin = bufferView.find("\x1B]11;rgb:"_s)) {
				if (begin.end() < end.begin()) {
					auto rrggbb = bufferView.slice(begin.end(), end.begin()).split('/');
					if (rrggbb.size() == 3) {
						String part = rrggbb[0];
						std::uint32_t r = (strtoul(part.data(), nullptr, 16) >> 8) & 0xFF;
						part = rrggbb[1];
						std::uint32_t g = (strtoul(part.data(), nullptr, 16) >> 8) & 0xFF;
						part = rrggbb[2];
						std::uint32_t b = (strtoul(part.data(), nullptr, 16) >> 8) & 0xFF;
						std::uint32_t luminance = ((13933 * r) + (46871 * g) + (4732 * b)) >> 16;
						__consoleDarkMode = (luminance < 128);
					}
				}
			}
			return true;
		}
		return false;
	});


	::SetConsoleMode(hStdIn, stdInMode);
}

static BOOL WINAPI OnHandleConsoleEvent(DWORD signal)
{
	if (signal == CTRL_C_EVENT || signal == CTRL_BREAK_EVENT ||
		signal == CTRL_CLOSE_EVENT || signal == CTRL_LOGOFF_EVENT ||
		signal == CTRL_SHUTDOWN_EVENT) {
		auto& app = nCine::theApplication();
		if (!app.ShouldQuit()) {
			LOGW("Received console close event to shut down the application");
			app.Quit();
			return TRUE;
		}
	}

	return FALSE;
}
#elif defined(DEATH_TARGET_APPLE) || defined(DEATH_TARGET_UNIX)
#	include <signal.h>
#	include <termios.h>
#	include <sys/select.h>

static void CheckConsoleCapabilities()
{
	// Save the terminal settings
	termios oldt;
	::tcgetattr(STDIN_FILENO, &oldt);

	// Set the terminal to raw mode (no buffering or echoing)
	termios newt = oldt;
	newt.c_lflag &= ~(ICANON | ECHO);
	::tcsetattr(STDIN_FILENO, TCSANOW, &newt);

	char buffer[128];

	// Send the escape sequence
	::fputs("\x1b]11;?\x07", stdout);
	::fflush(stdout);

	// Wait for input with a timeout
	fd_set readfds;
	FD_ZERO(&readfds);
	FD_SET(STDIN_FILENO, &readfds);

	struct timeval timeout;
	timeout.tv_sec = 0;
	timeout.tv_usec = 400000; // 400 ms
	std::int32_t result = ::select(STDIN_FILENO + 1, &readfds, nullptr, nullptr, &timeout);

	if (result > 0) {
		ssize_t totalRead = ::read(STDIN_FILENO, buffer, sizeof(buffer));
		StringView bufferView = StringView(buffer, totalRead);
		if (StringView end = bufferView.find('c')) {
			if (StringView begin = bufferView.find("\x1B[?"_s)) {
				if (begin.end() < end.begin()) {
					String response = ';' + bufferView.slice(begin.end(), end.begin()) + ';';
					if (response.contains(";4;"_s)) {
						__consoleSixelSupported = true;
					}
				}
			}
		}
	}

	FD_ZERO(&readfds);
	FD_SET(STDIN_FILENO, &readfds);

	timeout.tv_sec = 0;
	timeout.tv_usec = 400000; // 400 ms
	result = ::select(STDIN_FILENO + 1, &readfds, nullptr, nullptr, &timeout);

	if (result > 0) {
		ssize_t totalRead = ::read(STDIN_FILENO, buffer, sizeof(buffer));
		StringView bufferView = StringView(buffer, totalRead);
		if (StringView end = bufferView.find("\x1B\\"_s)) {
			if (StringView begin = bufferView.find("\x1B]11;rgb:"_s)) {
				if (begin.end() < end.begin()) {
					auto rrggbb = bufferView.slice(begin.end(), end.begin()).split('/');
					if (rrggbb.size() == 3) {
						String part = rrggbb[0];
						std::uint32_t r = (strtoul(part.data(), nullptr, 16) >> 8) & 0xFF;
						part = rrggbb[1];
						std::uint32_t g = (strtoul(part.data(), nullptr, 16) >> 8) & 0xFF;
						part = rrggbb[2];
						std::uint32_t b = (strtoul(part.data(), nullptr, 16) >> 8) & 0xFF;
						std::uint32_t luminance = ((13933 * r) + (46871 * g) + (4732 * b)) >> 16;
						__consoleDarkMode = (luminance < 128);
					}
				}
			}
		}
	}

	// Restore the terminal settings
	::tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
}

static void OnHandleInterruptSignal(int sig)
{
	if (sig == SIGINT) {
		auto& app = nCine::theApplication();
		if (!app.ShouldQuit()) {
			LOGW("Received interrupt signal to shut down the application");
			app.Quit();
			return;
		}

		::signal(sig, SIG_DFL);
		::raise(sig);
	}
}
#endif

template<std::int32_t N>
static DEATH_ALWAYS_INLINE void AppendPart(char* dest, std::int32_t& length, const char(&newPart)[N])
{
	length += nCine::copyStringFirst(dest + length, MaxLogEntryLength - length - 1, newPart, N - 1);
}

static DEATH_ALWAYS_INLINE void AppendPart(char* dest, std::int32_t& length, const char* newPart, std::int32_t newPartLength)
{
	length += nCine::copyStringFirst(dest + length, MaxLogEntryLength - length - 1, newPart, newPartLength);
}

static void AppendDateTime(char* dest, std::int32_t& length, std::uint64_t timestamp)
{
	// Convert nanoseconds to milliseconds
	auto dt = DateTime::FromUnixMilliseconds(timestamp / 1000000ULL);
	auto p = dt.Partitioned();

	length += snprintf(dest + length, MaxLogEntryLength - length - 1,
		"%02u:%02u:%02u.%03u", p.Hour, p.Minute, p.Second, p.Millisecond);
}

static void AppendLevel(char* dest, std::int32_t& length, TraceLevel level, StringView threadId)
{
	if (length >= MaxLogEntryLength) {
		return;
	}

	char levelIdentifier;
	switch (level) {
		case TraceLevel::Fatal:		levelIdentifier = 'F'; break;
		case TraceLevel::Assert:	levelIdentifier = 'A'; break;
		case TraceLevel::Error:		levelIdentifier = 'E'; break;
		case TraceLevel::Warning:	levelIdentifier = 'W'; break;
		case TraceLevel::Info:		levelIdentifier = 'I'; break;
		default:					levelIdentifier = 'D'; break;
	}

	std::int32_t partLength = snprintf(dest + length, MaxLogEntryLength - length - 1, !threadId.empty() ? "[%c]%s}" : "[%c]", levelIdentifier, threadId.data());
	length += partLength;

	while (partLength < 10) {
		dest[length++] = ' ';
		partLength++;
	}
	dest[length++] = ' ';
}

#if defined(DEATH_TARGET_GCC) || defined(DEATH_TARGET_CLANG) || defined(DEATH_TARGET_MSVC)
static void AppendShortenedFunctionName(char* dest, std::int32_t& length, const char* functionName, std::int32_t functionNameLength)
{
#	if defined(DEATH_TARGET_GCC) || defined(DEATH_TARGET_CLANG)
	// Strip function specifiers, return type and arguments from function name, because GCC/Clang includes full function signature
	static constexpr StringView LambdaSuffix = "::<lambda()>"_s;
	static constexpr StringView LambdaClangSuffix = "::(anonymous class)::operator()()"_s;
	static constexpr StringView OperatorPrefix = "operator"_s;

	StringView functionNameView = StringView(functionName, functionNameLength);
	std::int32_t i; bool isLambda = false;
	if (auto lambdaClang = functionNameView.find(LambdaClangSuffix)) {
		i = lambdaClang.begin() - functionName;
		isLambda = true;
	} else {
		i = functionNameLength - 1;
		if (functionNameView.hasSuffix(LambdaSuffix)) {
			i -= LambdaSuffix.size();
			isLambda = true;
		}
	}

	// Go backwards until we find the first opening parenthesis (arguments)
	std::int32_t parethesisCount = 0;
	for (; i >= 0; i--) {
		if (functionName[i] == ')') {
			parethesisCount++;
		} else if (functionName[i] == '(') {
			parethesisCount--;
			if (parethesisCount == 0) {
				break;
			}
		} else if (functionName[i] == '>') {
			parethesisCount++;
		} else if (functionName[i] == '<') {
			parethesisCount--;
		} else if (functionName[i] == ']') {
			parethesisCount++;
		} else if (functionName[i] == '[') {
			parethesisCount--;
		}
	}

	if (i > 0) {
		std::int32_t end = i;
		i--;
	FindFunctionName:
		// Go backwards until we find the first space
		for (; i >= 0; i--) {
			if (functionName[i] == ')') {
				parethesisCount++;
			} else if (functionName[i] == '(') {
				parethesisCount--;
			} else if (functionName[i] == '>') {
				parethesisCount++;
			} else if (functionName[i] == '<') {
				parethesisCount--;
			} else if (functionName[i] == ' ' && parethesisCount == 0) {
				break;
			}
		}
		std::int32_t j = std::max(i, 0);
		while (j > 0 && functionName[j - 1] == ' ') {
			j--;
		}
		// Hopefully only operators can contain spaces in their name
		if (j > OperatorPrefix.size() && functionNameView.slice(j - OperatorPrefix.size(), j) == OperatorPrefix) {
			i = j - std::int32_t(OperatorPrefix.size());
			goto FindFunctionName;
		}
		i++;
		// If the return type is a pointer, the asterisk can be right before the function name, so skip it
		if (i > 0 && functionName[i] == '*') {
			i++;
		}
		AppendPart(dest, length, &functionName[i], end - i);
		AppendPart(dest, length, "()");
		if (isLambda) {
			AppendPart(dest, length, LambdaSuffix.data(), (std::int32_t)LambdaSuffix.size());
		}
	} else {
		AppendPart(dest, length, functionName, functionNameLength);
	}
#	else
	// Try to shorten lambda function names, for example "FunctionName::<lambda_fdcd1e49ba268f6f79b01fbdc7d2a72f>::operator ()()"
	static constexpr StringView LambdaSuffix = "()::<lambda()>"_s;
	static constexpr StringView LambdaPrefixMsvc = "::<lambda_"_s;
	static constexpr StringView LambdaSuffixMsvc = ">::operator ()()"_s;

	StringView functionNameView = StringView(functionName, functionNameLength);
	if (auto lambdaPrefix = functionNameView.find(LambdaPrefixMsvc)) {
		if (functionNameView.suffix(lambdaPrefix.end()).hasSuffix(LambdaSuffixMsvc)) {
			auto functionNamePrefix = functionNameView.prefix(lambdaPrefix.begin());
			AppendPart(dest, length, functionNamePrefix.data(), (std::int32_t)functionNamePrefix.size());
			AppendPart(dest, length, LambdaSuffix.data(), (std::int32_t)LambdaSuffix.size());
			return;
		}
	}

	AppendPart(dest, length, functionName, functionNameLength);
#	endif
}
#endif

static void AppendFunctionName(char* dest, std::int32_t& length, StringView functionName)
{
	if (!functionName.empty()) {
#if defined(DEATH_TARGET_GCC) || defined(DEATH_TARGET_CLANG) || defined(DEATH_TARGET_MSVC)
		AppendShortenedFunctionName(dest, length, functionName.data(), (std::int32_t)functionName.size());
#else
		AppendPart(dest, length, functionName.data(), (std::int32_t)functionName.size());
#endif
		AppendPart(dest, length, " ‡ ");
	}
}

static void AppendMessagePrefixIfAny(char* dest, std::int32_t& length, TraceLevel level, StringView functionName)
{
	if (!functionName.empty()) {
		if (__consoleType >= ConsoleType::EscapeCodes) {
			AppendPart(dest, length, ColorFaint);

			switch (level) {
				case TraceLevel::Error:
				case TraceLevel::Fatal:
					AppendPart(dest, length, ColorBrightRed);
					break;
				case TraceLevel::Assert:
					AppendPart(dest, length, ColorBrightMagenta);
					break;
				case TraceLevel::Warning:
					AppendPart(dest, length, ColorBrightYellow);
					break;
#if defined(DEATH_TARGET_EMSCRIPTEN)
				case TraceLevel::Debug:
				case TraceLevel::Deferred:
					AppendPart(dest, length, ColorDarkGray);
					break;
#endif
			}
		}

#if defined(DEATH_TARGET_GCC) || defined(DEATH_TARGET_CLANG) || defined(DEATH_TARGET_MSVC)
		AppendShortenedFunctionName(dest, length, functionName.data(), (std::int32_t)functionName.size());
#else
		AppendPart(dest, length, functionName.data(), (std::int32_t)functionName.size());
#endif
		AppendPart(dest, length, " ‡ ");
	}
}

static void AppendMessageColor(char* dest, std::int32_t& length, TraceLevel level, bool resetBefore)
{
	if (resetBefore) {
		AppendPart(dest, length, ColorReset);
	}

	switch (level) {
		case TraceLevel::Error:
		case TraceLevel::Fatal:
			AppendPart(dest, length, ColorBrightRed);
			if (level == TraceLevel::Fatal) {
				AppendPart(dest, length, ColorBold);
			}
			break;
		case TraceLevel::Assert:
			AppendPart(dest, length, ColorBrightMagenta);
			break;
#if defined(DEATH_TARGET_EMSCRIPTEN)
		case TraceLevel::Info:
		case TraceLevel::Warning:
			AppendPart(dest, length, ColorBold);
			break;
#else
		case TraceLevel::Warning:
			AppendPart(dest, length, ColorBrightYellow);
			break;
		case TraceLevel::Debug:
		case TraceLevel::Deferred:
			AppendPart(dest, length, ColorDarkGray);
			break;
#endif
	}
}
#endif

namespace nCine
{
	Application::Application()
		: isSuspended_(false), autoSuspension_(false), hasFocus_(true), shouldQuit_(false)
	{
	}

	Application::~Application() = default;

#if defined(WITH_IMGUI)
	Application::GuiSettings::GuiSettings()
		: imguiLayer(0xffff - 1024), imguiViewport(nullptr)
	{
	}
#endif

	Viewport& Application::GetScreenViewport()
	{
		return *screenViewport_;
	}

	std::uint32_t Application::GetFrameCount() const
	{
		return frameTimer_->GetTotalNumberFrames();
	}

	float Application::GetTimeMult() const
	{
		return frameTimer_->GetTimeMult();
	}

	const FrameTimer& Application::GetFrameTimer() const
	{
		return *frameTimer_;
	}

	void Application::ResizeScreenViewport(std::int32_t width, std::int32_t height)
	{
		if (screenViewport_ != nullptr) {
			bool sizeChanged = (width != screenViewport_->width_ || height != screenViewport_->height_);
			screenViewport_->Resize(width, height);
			if (sizeChanged && width > 0 && height > 0) {
				appEventHandler_->OnResizeWindow(width, height);
			}
		}
	}

	bool Application::ShouldSuspend()
	{
		return ((!hasFocus_ && autoSuspension_) || isSuspended_);
	}

	void Application::Quit()
	{
		shouldQuit_ = true;
	}

	void Application::PreInitCommon(std::unique_ptr<IAppEventHandler> appEventHandler)
	{
		LOGI("PreInitCommon() started");

#if defined(DEATH_TRACE)
		InitializeTrace();
#endif

		LOGI("Trace initialized");

		appEventHandler_ = std::move(appEventHandler);
		appEventHandler_->OnPreInitialize(appCfg_);
		LOGI("IAppEventHandler::OnPreInitialize() invoked");
	}

	void Application::InitCommon()
	{
		TracyGpuContext;
		ZoneScopedC(0x81A861);
		// This timestamp is needed to initialize random number generator
		profileStartTime_ = TimeStamp::now();

		if (appCfg_.withGraphics) {
#if defined(DEATH_TARGET_WINDOWS_RT)
			LOGI(NCINE_APP_NAME " v" NCINE_VERSION " (UWP) initializing...");
#elif defined(WITH_GLFW)
			LOGI(NCINE_APP_NAME " v" NCINE_VERSION " (GLFW) initializing...");
#elif defined(WITH_SDL)
			LOGI(NCINE_APP_NAME " v" NCINE_VERSION " (SDL2) initializing...");
#else
			LOGI(NCINE_APP_NAME " v" NCINE_VERSION " initializing...");
#endif
		} else {
			LOGI(NCINE_APP_NAME " v" NCINE_VERSION " initializing...");
		}

#if defined(WITH_TRACY)
		TracyAppInfo(NCINE_APP, sizeof(NCINE_APP) - 1);
		LOGW("Tracy integration is enabled");
#endif

		// Initialization of the static random generator seeds
		Random().Init(TimeStamp::now().ticks(), profileStartTime_.ticks());

		frameTimer_ = std::make_unique<FrameTimer>(appCfg_.frameTimerLogInterval, 0.2f);
#if defined(DEATH_TARGET_WINDOWS)
		_waitableTimer = ::CreateWaitableTimerW(NULL, TRUE, NULL);
#endif

#if defined(WITH_AUDIO)
		if (appCfg_.withAudio) {
			theServiceLocator().RegisterAudioDevice(std::make_unique<ALAudioDevice>());
		}
#endif
#if defined(WITH_THREADS)
		if (appCfg_.withThreads) {
			theServiceLocator().RegisterThreadPool(std::make_unique<ThreadPool>());
		}
#endif

		if (appCfg_.withGraphics) {
			theServiceLocator().RegisterGfxCapabilities(std::make_unique<GfxCapabilities>());
			const auto& gfxCapabilities = theServiceLocator().GetGfxCapabilities();
			GLDebug::Init(gfxCapabilities);

#if !defined(WITH_ANGLE) && !defined(DEATH_TARGET_EMSCRIPTEN) && !defined(DEATH_TARGET_WINDOWS_RT)
			if (appCfg_.fixedBatchSize > 0) {
				LOGI("Using fixed batch size: {}", appCfg_.fixedBatchSize);
			} else {
#	if defined(DEATH_TARGET_ANDROID)
				const StringView vendor = gfxCapabilities.GetGLInfoStrings().vendor;
				const StringView renderer = gfxCapabilities.GetGLInfoStrings().renderer;
				// Some GPUs don't work with dynamic batch size, so it refuses to render VBOs (shows a black screen), disable it for them
				if ((vendor == "Imagination Technologies"_s && (renderer == "PowerVR Rogue GE8300"_s || renderer == "PowerVR Rogue GE8320"_s)) ||
					(vendor == "ARM"_s && renderer == "Mali-T830"_s)) {
					const StringView vendorPrefix = vendor.findOr(' ', vendor.end());
					if (renderer.hasPrefix(vendor.prefix(vendorPrefix.begin()))) {
						LOGW("Detected {}: Using fixed batch size", renderer);
					} else {
						LOGW("Detected {} {}: Using fixed batch size", vendor, renderer);
					}
					appCfg_.fixedBatchSize = 10;
				}
#	endif
			}
#endif

#if defined(WITH_RENDERDOC)
			RenderDocCapture::init();
#endif

			LOGI("Creating rendering resources...");

			// Create a minimal set of render resources before compiling the first shader
			RenderResources::createMinimal(); // they are required for rendering even without a scenegraph

			if (appCfg_.withScenegraph) {
				gfxDevice_->setupGL();
				RenderResources::create();
				rootNode_ = std::make_unique<SceneNode>();
				screenViewport_ = std::make_unique<ScreenViewport>();
				screenViewport_->SetRootNode(rootNode_.get());
			}

#if defined(WITH_IMGUI)
			imguiDrawing_ = std::make_unique<ImGuiDrawing>(appCfg_.withScenegraph);

			// Debug overlay is available even when scenegraph is not
			if (appCfg_.withDebugOverlay) {
				debugOverlay_ = std::make_unique<ImGuiDebugOverlay>(0.5f);	// 2 updates per second
			}
#endif
		} else {
			// Create scenegraph even without graphics to update nodes properly
			if (appCfg_.withScenegraph) {
				rootNode_ = std::make_unique<SceneNode>();
				screenViewport_ = std::make_unique<ScreenViewport>();
				screenViewport_->SetRootNode(rootNode_.get());
			}
		}

		LOGI("Application initialized");
#if defined(NCINE_PROFILING)
		timings_[(std::int32_t)Timings::InitCommon] = profileStartTime_.secondsSince();
#endif
		{
			ZoneScopedNC("OnInitialize", 0x81A861);
#if defined(NCINE_PROFILING)
			profileStartTime_ = TimeStamp::now();
#endif
			appEventHandler_->OnInitialize();
#if defined(NCINE_PROFILING)
			timings_[(std::int32_t)Timings::AppInit] = profileStartTime_.secondsSince();
#endif
			LOGI("IAppEventHandler::OnInitialize() invoked");
		}

		if (appCfg_.withGraphics) {
#if defined(WITH_IMGUI)
			imguiDrawing_->buildFonts();
#endif
			// Swapping frame now for a cleaner API trace capture when debugging
			gfxDevice_->update();
			FrameMark;
			TracyGpuCollect;
		}
	}

	void Application::Step()
	{
		frameTimer_->AddFrame();

#if defined(WITH_IMGUI)
		if (appCfg_.withGraphics) {
			ZoneScopedN("ImGui newFrame");
#	if defined(NCINE_PROFILING)
			profileStartTime_ = TimeStamp::now();
#	endif
			imguiDrawing_->newFrame();
#	if defined(NCINE_PROFILING)
			timings_[(std::int32_t)Timings::ImGui] = profileStartTime_.secondsSince();
#	endif
		}
#endif
#if defined(WITH_LUA)
		LuaStatistics::update();
#endif

		{
			ZoneScopedNC("OnBeginFrame", 0x81A861);
#if defined(NCINE_PROFILING)
			profileStartTime_ = TimeStamp::now();
#endif
			appEventHandler_->OnBeginFrame();
#if defined(NCINE_PROFILING)
			timings_[(std::int32_t)Timings::BeginFrame] = profileStartTime_.secondsSince();
#endif
		}

#if defined(WITH_IMGUI)
		if (debugOverlay_ != nullptr) {
			debugOverlay_->update();
		}
#endif

		if (appCfg_.withScenegraph) {
			ZoneScopedNC("SceneGraph", 0x81A861);
			{
				ZoneScopedNC("Update", 0x81A861);
#if defined(NCINE_PROFILING)
				profileStartTime_ = TimeStamp::now();
#endif
				screenViewport_->Update();
#if defined(NCINE_PROFILING)
				timings_[(std::int32_t)Timings::Update] = profileStartTime_.secondsSince();
#endif
			}

			{
				ZoneScopedNC("OnPostUpdate", 0x81A861);
#if defined(NCINE_PROFILING)
				profileStartTime_ = TimeStamp::now();
#endif
				appEventHandler_->OnPostUpdate();
#if defined(NCINE_PROFILING)
				timings_[(std::int32_t)Timings::PostUpdate] = profileStartTime_.secondsSince();
#endif
			}

			if (appCfg_.withGraphics) {
				{
					ZoneScopedNC("Visit", 0x81A861);
#if defined(NCINE_PROFILING)
					profileStartTime_ = TimeStamp::now();
#endif
					screenViewport_->Visit();
#if defined(NCINE_PROFILING)
					timings_[(std::int32_t)Timings::Visit] = profileStartTime_.secondsSince();
#endif
				}

#if defined(WITH_IMGUI)
				{
					ZoneScopedN("ImGui endFrame");
#	if defined(NCINE_PROFILING)
					profileStartTime_ = TimeStamp::now();
#	endif
					RenderQueue& imguiRenderQueue = (guiSettings_.imguiViewport ? guiSettings_.imguiViewport->renderQueue_ : screenViewport_->renderQueue_);
					imguiDrawing_->endFrame(imguiRenderQueue);
#	if defined(NCINE_PROFILING)
					timings_[(std::int32_t)Timings::ImGui] += profileStartTime_.secondsSince();
#	endif
				}
#endif

				{
					ZoneScopedNC("Draw", 0x81A861);
#if defined(NCINE_PROFILING)
					profileStartTime_ = TimeStamp::now();
#endif
					screenViewport_->SortAndCommitQueue();
					screenViewport_->Draw();
#if defined(NCINE_PROFILING)
					timings_[(std::int32_t)Timings::Draw] = profileStartTime_.secondsSince();
#endif
				}
			}
		} else {
#if defined(WITH_IMGUI)
			if (appCfg_.withGraphics) {
				ZoneScopedN("ImGui endFrame");
#	if defined(NCINE_PROFILING)
				profileStartTime_ = TimeStamp::now();
#	endif
				imguiDrawing_->endFrame();
#	if defined(NCINE_PROFILING)
				timings_[(std::int32_t)Timings::ImGui] += profileStartTime_.secondsSince();
#	endif
			}
#endif
		}

		{
			theServiceLocator().GetAudioDevice().updatePlayers();
		}

		{
			ZoneScopedNC("OnFrameEnd", 0x81A861);
#if defined(NCINE_PROFILING)
			profileStartTime_ = TimeStamp::now();
#endif
			appEventHandler_->OnEndFrame();
#if defined(NCINE_PROFILING)
			timings_[(std::int32_t)Timings::EndFrame] = profileStartTime_.secondsSince();
#endif
		}

#if defined(WITH_IMGUI)
		if (debugOverlay_ != nullptr) {
			debugOverlay_->updateFrameTimings();
		}
#endif

		if (appCfg_.withGraphics) {
			gfxDevice_->update();
			FrameMark;
			TracyGpuCollect;
		}

		if (appCfg_.frameLimit > 0) {
			FrameMarkStart("Frame limiting");
			const std::int64_t frameTimeDuration = clock().frequency() / appCfg_.frameLimit;

#if defined(DEATH_TARGET_WINDOWS)
			// It can wait longer than necessary, so subtract 1 ms to compensate
			const std::int64_t remainingTime100ns = ((((std::int64_t)frameTimeDuration - (std::int64_t)frameTimer_->GetFrameDurationAsTicks())
				* 10'000'000LL) / (std::int64_t)clock().frequency()) - 10'000; // 1 ms
			if (remainingTime100ns > 0) {
				LARGE_INTEGER dueTime;
				dueTime.QuadPart = -remainingTime100ns;

				::SetWaitableTimer(_waitableTimer, &dueTime, 0, NULL, NULL, FALSE);
				::WaitForSingleObject(_waitableTimer, 1000);
				::CancelWaitableTimer(_waitableTimer);
			}
#elif defined(DEATH_TARGET_APPLE)
			// It can wait longer than necessary, so subtract 0.5 ms to compensate
			const std::int64_t remainingTimeNs = (1'000'000'000LL / (std::int64_t)appCfg_.frameLimit) -
				((std::int64_t)frameTimer_->GetFrameDurationAsTicks() * 1'000'000'000LL / (std::int64_t)clock().frequency()) - 500'000LL;
			if (remainingTimeNs > 0) {
				timespec dueTime{};
				dueTime.tv_nsec += remainingTimeNs;
				if (dueTime.tv_nsec >= 1'000'000'000L) {
					dueTime.tv_sec += dueTime.tv_nsec / 1'000'000'000L;
					dueTime.tv_nsec %= 1'000'000'000L;
				}
				nanosleep(&dueTime, &dueTime);
			}
#elif defined(DEATH_TARGET_UNIX)
			// It can wait longer than necessary, so subtract 0.5 ms to compensate
			const std::int64_t remainingTimeNs = (1'000'000'000LL / (std::int64_t)appCfg_.frameLimit) -
				((std::int64_t)frameTimer_->GetFrameDurationAsTicks() * 1'000'000'000LL / (std::int64_t)clock().frequency()) - 500'000LL;
			if (remainingTimeNs > 0) {
				timespec dueTime;
				clock_gettime(CLOCK_MONOTONIC, &dueTime);
				dueTime.tv_nsec += remainingTimeNs;
				if (dueTime.tv_nsec >= 1'000'000'000L) {
					dueTime.tv_sec += dueTime.tv_nsec / 1'000'000'000L;
					dueTime.tv_nsec %= 1'000'000'000L;
				}
				clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &dueTime, nullptr);
			}
#endif

			while ((std::int64_t)frameTimer_->GetFrameDurationAsTicks() < frameTimeDuration) {
				Thread::Sleep(0);
			}
			FrameMarkEnd("Frame limiting");
		}
	}

	void Application::ShutdownCommon()
	{
		ZoneScopedC(0x81A861);
		appEventHandler_->OnShutdown();
		LOGI("IAppEventHandler::OnShutdown() invoked");
		appEventHandler_ = nullptr;

		rootNode_ = nullptr;

		if (appCfg_.withGraphics) {
#if defined(WITH_IMGUI)
			imguiDrawing_ = nullptr;
			debugOverlay_ = nullptr;
#endif
#if defined(WITH_RENDERDOC)
			RenderDocCapture::removeHooks();
#endif
			RenderResources::dispose();
			gfxDevice_ = nullptr;
		}

		frameTimer_ = nullptr;
		inputManager_ = nullptr;

#if defined(DEATH_TARGET_WINDOWS)
		::CloseHandle(_waitableTimer);
#endif

		LOGI("Application is shutting down");

		theServiceLocator().UnregisterAll();

#if defined(DEATH_TRACE)
		ShutdownTrace();
#endif
	}

	void Application::SetFocus(bool hasFocus)
	{
#if defined(WITH_TRACY) && !defined(DEATH_TARGET_ANDROID)
		hasFocus = true;
#endif

		hasFocus_ = hasFocus;
	}

#if defined(DEATH_TRACE)
	void Application::OnTraceReceived(TraceLevel level, std::uint64_t timestamp, StringView threadId, StringView functionName, StringView content)
	{
		char logEntryWithColors[MaxLogEntryLength + 24];

#if defined(DEATH_TARGET_ANDROID)
		std::int32_t length2 = 0;
		AppendLevel(logEntryWithColors, length2, level, threadId);
		AppendFunctionName(logEntryWithColors, length2, functionName);
		AppendPart(logEntryWithColors, length2, content.data(), (std::int32_t)content.size());

		android_LogPriority priority;
		switch (level) {
			case TraceLevel::Fatal:		priority = ANDROID_LOG_FATAL; break;
			case TraceLevel::Assert:	// Android doesn't support this priority, use ANDROID_LOG_ERROR instead
			case TraceLevel::Error:		priority = ANDROID_LOG_ERROR; break;
			case TraceLevel::Warning:	priority = ANDROID_LOG_WARN; break;
			case TraceLevel::Info:		priority = ANDROID_LOG_INFO; break;
			default:					priority = ANDROID_LOG_DEBUG; break;
		}

		std::int32_t result = __android_log_write(priority, NCINE_APP, logEntryWithColors);
		std::int32_t n = 0;
		while (result == -11 /*EAGAIN*/ && n < 2) {
			::usleep(2000); // 2ms in microseconds
			result = __android_log_write(priority, NCINE_APP, logEntryWithColors);
			n++;
		}
#elif defined(DEATH_TARGET_SWITCH)
		std::int32_t length2 = 0;
		AppendLevel(logEntryWithColors, length2, level, threadId);
		AppendFunctionName(logEntryWithColors, length2, functionName);
		AppendPart(logEntryWithColors, length2, content.data(), (std::int32_t)content.size());
		svcOutputDebugString(logEntryWithColors, length2);
#elif defined(DEATH_TARGET_WINDOWS_RT)
		// Use OutputDebugStringA() to avoid conversion UTF-8 => UTF-16 => current code page
		std::int32_t length2 = 0;
		AppendLevel(logEntryWithColors, length2, level, threadId);
		AppendFunctionName(logEntryWithColors, length2, functionName);
		AppendPart(logEntryWithColors, length2, content.data(), (std::int32_t)content.size());
		if (length2 >= MaxLogEntryLength - 2) {
			length2 = MaxLogEntryLength - 2;
		}
		logEntryWithColors[length2++] = '\n';
		logEntryWithColors[length2] = '\0';
		::OutputDebugStringA(logEntryWithColors);
#else
#	if defined(DEATH_TARGET_WINDOWS) && defined(DEATH_DEBUG)
		if (__consoleType >= ConsoleType::Redirect) {
#	endif
			// Colorize the output
			std::int32_t length2 = 0;
			AppendMessagePrefixIfAny(logEntryWithColors, length2, level, functionName);

			if (__consoleType >= ConsoleType::EscapeCodes) {
#	if defined(DEATH_TARGET_EMSCRIPTEN)
				bool shouldResetBefore = (level != TraceLevel::Warning && level != TraceLevel::Debug && level != TraceLevel::Deferred);
#	else
				bool shouldResetBefore = true;
#	endif
				bool shouldResetAfter = (level == TraceLevel::Debug || level == TraceLevel::Deferred || level == TraceLevel::Warning || level == TraceLevel::Error || level == TraceLevel::Assert || level == TraceLevel::Fatal);

				if (level < TraceLevel::Error && __consoleType >= ConsoleType::EscapeCodes24bit) {
					std::int32_t prevState = 0;
					StringView contentPart = content;
					do {
						StringView quotesBegin = contentPart.find('"');
						if (!quotesBegin) {
							break;
						}
						StringView quotesEnd = contentPart.suffix(quotesBegin.end()).find('"');
						if (!quotesEnd) {
							break;
						}

						StringView prefix = contentPart.prefix(quotesBegin.begin());
						if (!prefix.empty()) {
							AppendMessageColor(logEntryWithColors, length2, level, prevState == 2 || shouldResetBefore);
							shouldResetBefore = false;
							prevState = 1;

							AppendPart(logEntryWithColors, length2, prefix.data(), (std::int32_t)prefix.size());
						}

						if (prevState != 2) {
							if (level == TraceLevel::Debug || level == TraceLevel::Deferred) {
								AppendPart(logEntryWithColors, length2, ColorDimString);
							} else if (__consoleDarkMode) {
								AppendPart(logEntryWithColors, length2, ColorDarkString);
							} else {
								AppendPart(logEntryWithColors, length2, ColorLightString);
							}
							prevState = 2;
						}

						StringView inner = contentPart.suffix(quotesBegin.begin()).prefix(quotesEnd.end());
						AppendPart(logEntryWithColors, length2, inner.data(), (std::int32_t)inner.size());

						contentPart = contentPart.suffix(quotesEnd.end());
					} while (!contentPart.empty());

					if (!contentPart.empty()) {
						AppendMessageColor(logEntryWithColors, length2, level, prevState == 2 || shouldResetBefore);
						AppendPart(logEntryWithColors, length2, contentPart.data(), (std::int32_t)contentPart.size());
					} else if (prevState == 2) {
						// Always reset color after quotes
						shouldResetAfter = true;
					}
				} else {
					AppendMessageColor(logEntryWithColors, length2, level, shouldResetBefore);
					AppendPart(logEntryWithColors, length2, content.data(), (std::int32_t)content.size());
				}

				if (shouldResetAfter) {
					AppendPart(logEntryWithColors, length2, ColorReset);
				}
			} else {
				AppendPart(logEntryWithColors, length2, content.data(), (std::int32_t)content.size());
			}

			if (length2 >= MaxLogEntryLength - 2) {
				length2 = MaxLogEntryLength - 2;
			}

#	if defined(DEATH_TARGET_WINDOWS)
			// Try to restore previous cursor position (this doesn't work correctly in Windows Terminal v1.19)
			if (__consoleHandleOut != NULL) {
				CONSOLE_SCREEN_BUFFER_INFO csbi;
				if (::GetConsoleScreenBufferInfo(__consoleHandleOut, &csbi)) {
					if (__consoleCursorY <= csbi.dwCursorPosition.Y) {
						::SetConsoleCursorPosition(__consoleHandleOut, { 0, __consoleCursorY });
					}
				}
			}
			if (__consoleType >= ConsoleType::EscapeCodes && length2 < MaxLogEntryLength) {
				// Console can be shared with the parent process, so clear the rest of the line (using "\x1b[0K" sequence)
				logEntryWithColors[length2++] = '\x1b';
				logEntryWithColors[length2++] = '[';
				logEntryWithColors[length2++] = '0';
				logEntryWithColors[length2++] = 'K';
			}

			logEntryWithColors[length2++] = '\n';
			::fwrite(logEntryWithColors, 1, length2, level == TraceLevel::Error || level == TraceLevel::Fatal ? stderr : stdout);

			// Save the last cursor position for later
			if (__consoleHandleOut != NULL) {
				CONSOLE_SCREEN_BUFFER_INFO csbi;
				if (::GetConsoleScreenBufferInfo(__consoleHandleOut, &csbi)) {
					__consoleCursorY = csbi.dwCursorPosition.Y;
				}
			}
#	else
			logEntryWithColors[length2++] = '\n';
			::fwrite(logEntryWithColors, 1, length2, level == TraceLevel::Error || level == TraceLevel::Fatal ? stderr : stdout);
#	endif

#	if defined(DEATH_TARGET_WINDOWS) && defined(DEATH_DEBUG)
		} else {
			// Use OutputDebugStringA() to avoid conversion UTF-8 => UTF-16 => current code page
			std::int32_t length2 = 0;
			AppendLevel(logEntryWithColors, length2, level, threadId);
			AppendFunctionName(logEntryWithColors, length2, functionName);
			AppendPart(logEntryWithColors, length2, content.data(), (std::int32_t)content.size());
			if (length2 >= MaxLogEntryLength - 2) {
				length2 = MaxLogEntryLength - 2;
			}
			logEntryWithColors[length2++] = '\n';
			logEntryWithColors[length2] = '\0';
			::OutputDebugStringA(logEntryWithColors);
		}
#	endif
#endif

#if !defined(DEATH_TARGET_EMSCRIPTEN)
		// Allow attaching custom target using Application::AttachTraceTarget()
		if (__logFile != nullptr) {
			std::int32_t length3 = 0;
			AppendDateTime(logEntryWithColors, length3, timestamp);
			logEntryWithColors[length3++] = ' ';
			AppendLevel(logEntryWithColors, length3, level, threadId);
			AppendFunctionName(logEntryWithColors, length3, functionName);
			AppendPart(logEntryWithColors, length3, content.data(), (std::int32_t)content.size());
			logEntryWithColors[length3++] = '\n';

#	if !defined(DEATH_TRACE_ASYNC)
			// File needs to be locked, because messages can arrive from different threads
			__logFile->Write(logEntryWithColors, length3);
#	else
			__logFile->Write(logEntryWithColors, length3);
#	endif
		}
#endif

#if defined(WITH_IMGUI)
		auto* debugOverlay = theApplication().debugOverlay_.get();
		if (debugOverlay != nullptr) {
			std::int32_t length4 = 0;
			AppendDateTime(logEntryWithColors, length4, timestamp);

			debugOverlay->log(level, logEntryWithColors, threadId, functionName, content);
		}
#endif

#if defined(WITH_TRACY)
		std::uint32_t colorTracy;
		switch (level) {
			case TraceLevel::Fatal:		colorTracy = 0xEC3E40; break;
			case TraceLevel::Assert:	colorTracy = 0xD651B0; break;
			case TraceLevel::Error:		colorTracy = 0xD85050; break;
			case TraceLevel::Warning:	colorTracy = 0xEBC77A; break;
			case TraceLevel::Info:		colorTracy = 0xD2D2D2; break;
			default:					colorTracy = 0x969696; break;
		}

		TracyMessageC(content.data(), content.size(), colorTracy);
#endif
	}

	void Application::OnTraceFlushed()
	{
#	if !defined(DEATH_TARGET_EMSCRIPTEN)
		if (__logFile != nullptr) {
			__logFile->Flush();
		}
#	endif
	}
#endif

	void Application::Suspend()
	{
		frameTimer_->Suspend();
		if (appEventHandler_ != nullptr) {
			appEventHandler_->OnSuspend();
		}
#if defined(WITH_AUDIO)
		if (appCfg_.withAudio) {
			IAudioDevice& audioDevice = theServiceLocator().GetAudioDevice();
			audioDevice.suspendDevice();
		}
#endif

		LOGI("IAppEventHandler::OnSuspend() invoked");
	}

	void Application::Resume()
	{
		if (appEventHandler_ != nullptr) {
			appEventHandler_->OnResume();
		}
#if defined(WITH_AUDIO)
		if (appCfg_.withAudio) {
			IAudioDevice& audioDevice = theServiceLocator().GetAudioDevice();
			audioDevice.resumeDevice();
		}
#endif

		DEATH_UNUSED TimeStamp suspensionDuration = frameTimer_->Resume();
		LOGD("Suspended for {:.3} seconds", suspensionDuration.seconds());
#if defined(NCINE_PROFILING)
		profileStartTime_ += suspensionDuration;
#endif
		LOGI("IAppEventHandler::OnResume() invoked");
	}

	bool Application::EnablePlayStationExtendedSupport(bool enable)
	{
		// Not implemented in base class
		return false;
	}

	String Application::GetUserName()
	{
		// Not implemented in base class
		return {};
	}

	bool Application::OpenUrl(StringView url)
	{
		// Not implemented in base class
		return false;
	}
	
	bool Application::CanShowScreenKeyboard()
	{
		return false;
	}
	
	bool Application::ToggleScreenKeyboard()
	{
		return false;
	}
	
	bool Application::ShowScreenKeyboard()
	{
		return false;
	}
	
	bool Application::HideScreenKeyboard()
	{
		return false;
	}

	void Application::AttachTraceTarget(Containers::StringView targetPath)
	{
#if defined(DEATH_TRACE) && defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)
		if (targetPath == ConsoleTarget) {
			if (__consoleType == ConsoleType::None) {
				bool hasVirtualTerminal = false;
				if (CreateTraceConsole(NCINE_APP_NAME, hasVirtualTerminal)) {
					__consoleType = (hasVirtualTerminal ? ConsoleType::EscapeCodes24bit : ConsoleType::WinApi);

					CONSOLE_SCREEN_BUFFER_INFO csbi;
					__consoleHandleOut = ::GetStdHandle(STD_OUTPUT_HANDLE);
					if (::GetConsoleScreenBufferInfo(__consoleHandleOut, &csbi)) {
						__consoleCursorY = csbi.dwCursorPosition.Y;
					} else {
						__consoleHandleOut = NULL;
					}

					if (hasVirtualTerminal) {
						CheckConsoleCapabilities();
					}

					::SetConsoleCtrlHandler(OnHandleConsoleEvent, TRUE);
				} else {
					__consoleType = ConsoleType::Redirect;
				}

#	if defined(WITH_BACKWARD)
				if (__consoleType >= ConsoleType::EscapeCodes) {
					__eh.FeatureFlags |= Backward::Flags::Colorized;
				}
#	endif
			}
			return;
		}
#endif

#if defined(DEATH_TRACE) && !defined(DEATH_TARGET_EMSCRIPTEN)
		__logFile = fs::Open(targetPath, FileAccess::Write);
		if (*__logFile) {
#	if defined(WITH_BACKWARD)
			// Try to save crash info to log file
			__eh.Destination = __logFile.get();
#	endif
		} else {
			__logFile = nullptr;
#	if defined(WITH_BACKWARD)
			__eh.Destination = nullptr;
#	endif
		}
#endif
	}

#if defined(DEATH_TRACE)
	void Application::InitializeTrace()
	{
		LOGI("InitializeTrace() started 1");

#	if defined(DEATH_TARGET_EMSCRIPTEN)
		char* userAgent = (char*)EM_ASM_PTR({
			return (typeof navigator !== 'undefined' && navigator !== null &&
					typeof navigator.userAgent !== 'undefined' && navigator.userAgent !== null
						? stringToNewUTF8(navigator.userAgent) : 0);
		});
		if (userAgent != nullptr) {
			// Only Chrome supports ANSI escape sequences for now
			__consoleType = (::strcasestr(userAgent, "chrome") != nullptr ? ConsoleType::EscapeCodes : ConsoleType::Redirect);
			std::free(userAgent);
		} else {
			__consoleType = ConsoleType::Redirect;
		}
#	elif defined(DEATH_TARGET_APPLE) || defined(DEATH_TARGET_UNIX)
#		if defined(DEATH_TARGET_UNIX)
		::setvbuf(stdout, nullptr, _IONBF, 0);
		::setvbuf(stderr, nullptr, _IONBF, 0);
#		endif

		// Xcode's console reports that it is a TTY, but it doesn't support colors, TERM is not defined in this case
		__consoleType = ConsoleType::Redirect;

		if (::isatty(1)) {
			StringView COLORTERM = ::getenv("COLORTERM");
			StringView TERM = ::getenv("TERM");

			if (!COLORTERM.empty()) {
				if (COLORTERM.contains("truecolor"_s) || COLORTERM.contains("24bit"_s)) {
					__consoleType = ConsoleType::EscapeCodes24bit;
					CheckConsoleCapabilities();
				} else if (COLORTERM.contains("256color"_s) || COLORTERM.contains("rxvt-xpm"_s)) {
					__consoleType = ConsoleType::EscapeCodes8bit;
				}
			}

			if (__consoleType < ConsoleType::EscapeCodes8bit && !TERM.empty()) {
				if (TERM.contains("256color"_s) || TERM.contains("rxvt-xpm"_s)) {
					__consoleType = ConsoleType::EscapeCodes8bit;
				} else if (TERM.contains("xterm"_s) || TERM.contains("vt1"_s) || TERM.contains("linux"_s)) {
					__consoleType = ConsoleType::EscapeCodes;
				}
			}
		}

		::signal(SIGINT, OnHandleInterruptSignal);
#	elif defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)
		HANDLE hStdOut = ::GetStdHandle(STD_OUTPUT_HANDLE);
		switch (::GetFileType(hStdOut)) {
			case FILE_TYPE_CHAR: {
				bool hasVirtualTerminal = EnableVirtualTerminalProcessing(hStdOut);
				__consoleType = (hasVirtualTerminal ? ConsoleType::EscapeCodes24bit : ConsoleType::WinApi);
				if (hasVirtualTerminal) {
					CheckConsoleCapabilities();	
				}

				::SetConsoleCtrlHandler(OnHandleConsoleEvent, TRUE);
				break;
			}
			case FILE_TYPE_UNKNOWN:
				// Nothing is attached to stdout
				break;
			default:
				__consoleType = ConsoleType::Redirect;
				break;
		}
#	endif

#	if defined(WITH_BACKWARD)
		if (__consoleType >= ConsoleType::EscapeCodes) {
			__eh.FeatureFlags |= Backward::Flags::Colorized;
		}
#	endif

		LOGI("InitializeTrace() started 2");

		Trace::AttachSink(this);

		LOGI("InitializeTrace() started 3");

#	if !defined(DEATH_DEBUG)
		Trace::InitializeBacktrace(8, TraceLevel::Warning);
#	endif

		LOGI("InitializeTrace() ended");
	}

	void Application::ShutdownTrace()
	{
#	if !defined(DEATH_TARGET_EMSCRIPTEN)
		if (__logFile != nullptr) {
			Trace::Flush();
#		if defined(WITH_BACKWARD)
			__eh.Destination = nullptr;
#		endif
			__logFile = nullptr;
		}
#	endif

		Trace::RemoveSink(this);

#	if defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)
		if (__consoleType >= ConsoleType::WinApi) {
			DestroyTraceConsole();
		}
#	endif
	}

#	if defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)
	bool Application::CreateTraceConsole(StringView title, bool& hasVirtualTerminal)
	{
		FILE* fDummy = nullptr;

		if (::AttachConsole(ATTACH_PARENT_PROCESS)) {
			HANDLE consoleHandleOut = ::GetStdHandle(STD_OUTPUT_HANDLE);
			if (consoleHandleOut != INVALID_HANDLE_VALUE) {
				::freopen_s(&fDummy, "CONOUT$", "w", stdout);
				::setvbuf(stdout, NULL, _IONBF, 0);
			}

			HANDLE consoleHandleError = ::GetStdHandle(STD_ERROR_HANDLE);
			if (consoleHandleError != INVALID_HANDLE_VALUE) {
				::freopen_s(&fDummy, "CONOUT$", "w", stderr);
				::setvbuf(stderr, NULL, _IONBF, 0);
			}

			HANDLE consoleHandleIn = ::GetStdHandle(STD_INPUT_HANDLE);
			if (consoleHandleIn != INVALID_HANDLE_VALUE) {
				::freopen_s(&fDummy, "CONIN$", "r", stdin);
				::setvbuf(stdin, NULL, _IONBF, 0);
			}

			hasVirtualTerminal = EnableVirtualTerminalProcessing(consoleHandleOut);

			// Try to get command prompt to be able to reprint it when the game exits
			CONSOLE_SCREEN_BUFFER_INFO csbi;
			if (::GetConsoleScreenBufferInfo(consoleHandleOut, &csbi)) {
				DWORD dwConsoleColumnWidth = (DWORD)(csbi.srWindow.Right - csbi.srWindow.Left + 1);
				SHORT xEnd = csbi.dwCursorPosition.X;
				SHORT yEnd = csbi.dwCursorPosition.Y;
				if (xEnd != 0 || yEnd != 0) {
					DWORD dwNumberOfChars;
					SHORT yBegin = yEnd;
					if (dwConsoleColumnWidth > 16) {
						Array<wchar_t> tmp(NoInit, dwConsoleColumnWidth);
						while (yBegin > 0) {
							COORD dwReadCoord = { 0, yBegin };
							if (!::ReadConsoleOutputCharacter(consoleHandleOut, tmp.data(), dwConsoleColumnWidth, dwReadCoord, &dwNumberOfChars)) {
								break;
							}

							for (DWORD i = dwNumberOfChars - 8; i < dwNumberOfChars; i++) {
								wchar_t wchar = tmp[i];
								if (wchar != L' ') {
									yBegin--;
									continue;
								}
							}

							if (yBegin < yEnd) {
								yBegin++;
							}
							break;
						}
					}

					DWORD promptLength = (yEnd - yBegin) * dwConsoleColumnWidth + xEnd;
					__consolePrompt = Array<wchar_t>(NoInit, promptLength);
					COORD dwPromptCoord = { 0, yEnd };
					if (::ReadConsoleOutputCharacter(consoleHandleOut, __consolePrompt.data(), promptLength, dwPromptCoord, &dwNumberOfChars)) {
						if (::SetConsoleCursorPosition(consoleHandleOut, dwPromptCoord)) {
							::FillConsoleOutputCharacter(consoleHandleOut, L' ', promptLength, dwPromptCoord, &dwNumberOfChars);
						}
					} else {
						__consolePrompt = {};
					}
				}
			}

			return true;
		} else if (::AllocConsole()) {
			::freopen_s(&fDummy, "CONOUT$", "w", stdout);
			::freopen_s(&fDummy, "CONOUT$", "w", stderr);
			::freopen_s(&fDummy, "CONIN$", "r", stdin);

			HANDLE consoleHandleOut = ::CreateFile(L"CONOUT$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
			HANDLE consoleHandleIn = ::CreateFile(L"CONIN$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
			::SetStdHandle(STD_OUTPUT_HANDLE, consoleHandleOut);
			::SetStdHandle(STD_ERROR_HANDLE, consoleHandleOut);
			::SetStdHandle(STD_INPUT_HANDLE, consoleHandleIn);

			hasVirtualTerminal = EnableVirtualTerminalProcessing(consoleHandleOut);

			::SetConsoleTitle(Death::Utf8::ToUtf16(title));
			HWND hWnd = ::GetConsoleWindow();
			if (hWnd != nullptr) {
				HINSTANCE inst = ((HINSTANCE)&__ImageBase);
				HICON windowIcon = (HICON)::LoadImage(inst, L"WINDOW_ICON", IMAGE_ICON, ::GetSystemMetrics(SM_CXICON), ::GetSystemMetrics(SM_CYICON), LR_DEFAULTSIZE);
				HICON windowIconSmall = (HICON)::LoadImage(inst, L"WINDOW_ICON", IMAGE_ICON, ::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON), LR_DEFAULTSIZE);
				if (windowIconSmall != NULL) ::SendMessage(hWnd, WM_SETICON, ICON_SMALL, (LPARAM)windowIconSmall);
				if (windowIcon != NULL) ::SendMessage(hWnd, WM_SETICON, ICON_BIG, (LPARAM)windowIcon);
			}

			return true;
		} else {
			hasVirtualTerminal = false;
		}

		return false;
	}

	void Application::DestroyTraceConsole()
	{
		if (!__consolePrompt.empty()) {
			HANDLE consoleHandleOut = ::GetStdHandle(STD_OUTPUT_HANDLE);
			if (consoleHandleOut != INVALID_HANDLE_VALUE) {
				CONSOLE_SCREEN_BUFFER_INFO csbi;
				if (::GetConsoleScreenBufferInfo(consoleHandleOut, &csbi)) {
					DWORD xEnd = csbi.dwCursorPosition.X;
					DWORD yEnd = csbi.dwCursorPosition.Y;
					if (xEnd != 0 || yEnd != 0) {
						DWORD dwNumberOfCharsWritten;
						::WriteConsoleW(consoleHandleOut, L"\r\n", (DWORD)arraySize(L"\r\n") - 1, &dwNumberOfCharsWritten, NULL);
						::WriteConsoleW(consoleHandleOut, __consolePrompt.data(), (DWORD)__consolePrompt.size(), &dwNumberOfCharsWritten, NULL);
					}
				}
			}
		}

		::FreeConsole();
	}
#	endif
#endif
}
