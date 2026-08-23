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
#		if defined(WITH_SDL3)
#			pragma comment(lib, "../Libs/Windows/x64/SDL3.lib")
#		elif defined(WITH_SDL2)
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
#		if defined(WITH_SDL3)
#			pragma comment(lib, "../Libs/Windows/x86/SDL3.lib")
#		elif defined(WITH_SDL2)
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
#include "Graphics/RenderResources.h"
#include "Graphics/RenderQueue.h"
#include "Graphics/ScreenViewport.h"
#include "Graphics/RHI/Rhi.h"
#include "Base/FrameTimer.h"
#include "Graphics/SceneNode.h"
#include "Input/IInputManager.h"
#include "Input/JoyMapping.h"
#include "Threading/Thread.h"
#include "ServiceLocator.h"
#include "tracy.h"
#include "tracy_opengl.h"

#include <atomic>

#include <Environment.h>
#include <Containers/DateTime.h>
#include <Containers/StringConcatenable.h>
#include <Containers/StringView.h>
#include <IO/FileSystem.h>

#if defined(WITH_AUDIO)
#	if defined(WITH_OPENAL)
#		include "Audio/Backends/AL/ALAudioDevice.h"
#	elif defined(WITH_ASND)
#		include "Audio/Backends/ASND/AsndAudioDevice.h"
#	elif defined(WITH_AICA)
#		include "Audio/Backends/AICA/AicaAudioDevice.h"
#	elif defined(WITH_PS3AUDIO)
#		include "Audio/Backends/PS3/Ps3AudioDevice.h"
#	endif
#endif

#if defined(WITH_THREADS)
#	include "Threading/Thread.h"
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

#include <Containers/StringStlView.h>
#include <Core/Logger.h>
#include <IO/MemoryStream.h>
#if defined(DEATH_TARGET_WINDOWS)
#	include <Environment.h>
#	include <Utf8.h>
#elif defined(DEATH_TARGET_ANDROID)
#	include <stdarg.h>
#	include <time.h>
#	include <unistd.h>
#	include <android/log.h>
#	include "Backends/Android/AndroidJniHelper.h"
#else
#	include <cstdarg>
#	include <unistd.h>
#	if defined(DEATH_TARGET_SWITCH)
#		include <time.h>
#		include <switch.h>
#	elif defined(DEATH_TARGET_VITA)
#		include <psp2/kernel/clib.h>
#	elif defined(DEATH_TARGET_WII) || defined(DEATH_TARGET_GAMECUBE)
#		include <ogc/exi.h>
#		include <ogc/usbgecko.h>
#	elif defined(DEATH_TARGET_DREAMCAST)
#		include <kos/dbgio.h>
#	elif defined(DEATH_TARGET_PSP)
#		include <pspdebug.h>
#	elif defined(DEATH_TARGET_PS3)
#		include <sys/tty.h>
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
#if !defined(DEATH_TARGET_ANDROID) && !defined(DEATH_TARGET_SWITCH) && !defined(DEATH_TARGET_VITA) && !defined(DEATH_TARGET_WINDOWS_RT)
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

#elif defined(DEATH_TARGET_APPLE) || defined(DEATH_TARGET_UNIX)
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

static void AppendDateTime(char* dest, std::int32_t& length, std::uint64_t timestamp, std::atomic<std::int32_t>* lastDay = nullptr)
{
	// Convert nanoseconds to milliseconds
	auto dt = DateTime::FromUnixMilliseconds(timestamp / 1000000ULL);
	auto p = dt.Partitioned();

	if (lastDay != nullptr) {
		// Prepend the full date to the first entry of each day, so the target keeps absolute time across midnight
		// and multi-day gaps. exchange() lets exactly one thread write the date on a day change.
		std::int32_t day = (p.Year * 12 + p.Month) * 31 + p.Day;
		if (lastDay->exchange(day, std::memory_order_relaxed) != day) {
			length += (std::int32_t)formatInto({ dest + length, (std::size_t)(MaxLogEntryLength - length - 1) },
				"{}/{:.2}/{:.2} ", p.Year, p.Month + 1, p.Day);
		}
	}

	length += (std::int32_t)formatInto({ dest + length, (std::size_t)(MaxLogEntryLength - length - 1) },
		"{:.2}:{:.2}:{:.2}.{:.3}", p.Hour, p.Minute, p.Second, p.Millisecond);
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

	std::int32_t partLength = (std::int32_t)formatInto({ dest + length, (std::size_t)(MaxLogEntryLength - length - 1) },
		!threadId.empty() ? "[{:c}]{}}}" : "[{:c}]", levelIdentifier, threadId);
	length += partLength;

	while (partLength < 10) {
		dest[length++] = ' ';
		partLength++;
	}
	dest[length++] = ' ';
}

#if defined(DEATH_TARGET_GCC) || defined(DEATH_TARGET_CLANG) || defined(DEATH_TARGET_MSVC)
// Appends a part of a function name, rewriting the compiler-specific spelling of an anonymous namespace to the
// "{anonymous}" notation of GCC, so a log entry reads the same on every platform. Every compiler emits only its own
// spelling, so only that one is ever searched for - and GCC, which already uses the target notation, does no work at
// all. The order of the checks follows __DEATH_CURRENT_FUNCTION in Asserts.h: Clang-CL defines both compiler macros
// and reports __PRETTY_FUNCTION__, so it belongs to the Clang case.
static void AppendFunctionNamePart(char* dest, std::int32_t& length, const char* functionName, std::int32_t functionNameLength)
{
#	if defined(DEATH_TARGET_CLANG) || defined(DEATH_TARGET_MSVC)
	static constexpr StringView AnonymousNamespace = "{anonymous}"_s;
#		if defined(DEATH_TARGET_CLANG)
	// Appears in the middle of __PRETTY_FUNCTION__, e.g. "void nCine::(anonymous namespace)::Function()"
	static constexpr StringView AnonymousNamespaceForeign = "(anonymous namespace)"_s;
#		elif defined(DEATH_TARGET_MSVC)
	// Appears in the middle of __FUNCTION__ and is spelled with a hyphen, e.g. "nCine::`anonymous-namespace'::Function"
	static constexpr StringView AnonymousNamespaceForeign = "`anonymous-namespace'"_s;
#		endif

	// A name can carry more than one such segment, because on some compilers it includes the return type as well
	StringView remaining = StringView(functionName, functionNameLength);
	while (auto found = remaining.find(AnonymousNamespaceForeign)) {
		AppendPart(dest, length, remaining.data(), (std::int32_t)(found.begin() - remaining.data()));
		AppendPart(dest, length, AnonymousNamespace.data(), (std::int32_t)AnonymousNamespace.size());
		remaining = remaining.suffix(found.end());
	}

	AppendPart(dest, length, remaining.data(), (std::int32_t)remaining.size());
#	else
	// GCC already uses the target notation, and the remaining compilers report only the bare function name
	AppendPart(dest, length, functionName, functionNameLength);
#	endif
}

static void AppendShortenedFunctionName(char* dest, std::int32_t& length, const char* functionName, std::int32_t functionNameLength)
{
#	if defined(DEATH_TARGET_GCC) || defined(DEATH_TARGET_CLANG)
	// Strip function specifiers, return type and arguments from function name, because GCC/Clang includes full function signature
	static constexpr StringView LambdaSuffix = "::<lambda()>"_s;
	static constexpr StringView OperatorPrefix = "operator"_s;

	// How the enclosing function is separated from the lambda's own call operator. Each compiler emits only
	// its own spelling, so only that one is compiled in - the same reasoning as in AppendFunctionNamePart()
	// above, including the order of the checks: Clang defines __GNUC__ as well and therefore also
	// DEATH_TARGET_GCC, so it has to be tested first.
	//  - GCC writes the closure inline, e.g. "Class::Method(bool)::<lambda(int)>"
	//  - Clang appends the call operator of the closure type, e.g.
	//    "auto Class::Method(bool)::(lambda)::operator()() const", and renamed that type from
	//    "(anonymous class)" to "(lambda)" in LLVM 22 - the spelling every release up to 21 emits
	// The marker deliberately stops at the opening parenthesis of the call operator and never covers the
	// argument list behind it: a lambda that takes parameters ends with "operator()(int)" rather than
	// "operator()()", and matching the arguments too would silently skip every such lambda. The same holds
	// for GCC's "<lambda(int)>". The parenthesis scan below walks from the marker back to the enclosing
	// function either way, so the arguments need no handling here.
#		if defined(DEATH_TARGET_CLANG)
#			if __clang_major__ >= 22
	static constexpr StringView LambdaMarker = "::(lambda)::operator("_s;
#			else
	static constexpr StringView LambdaMarker = "::(anonymous class)::operator("_s;
#			endif
#		else
	static constexpr StringView LambdaMarker = "::<lambda("_s;
#		endif

	StringView functionNameView = StringView(functionName, functionNameLength);
	std::int32_t i = functionNameLength - 1; bool isLambda = false;
	// The first occurrence is the outermost closure, which is the enclosing function a nested lambda should
	// be reported against as well
	if (auto lambdaMarker = functionNameView.find(LambdaMarker)) {
		i = std::int32_t(lambdaMarker.begin() - functionName);
		isLambda = true;
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

		// Everything from the "operator" keyword on belongs to the name, so the scan has to start in front of the
		// keyword instead of stopping at the first space behind it: the brackets of "operator<<" and "operator>>" would
		// otherwise be counted as template parameters and leave the return type in the name, and "operator new" or
		// "operator delete" carry a space in the middle of the name
		if (auto operatorKeyword = functionNameView.prefix(end).find(OperatorPrefix)) {
			i = std::int32_t(operatorKeyword.begin() - functionName);
		}

		// Go backwards until we find the first space, which is what separates the name from the return type
		for (i--; i >= 0; i--) {
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
		i++;

		// If the return type is a pointer or a reference, the asterisk or ampersand is right before the function name
		while (i < end && (functionName[i] == '*' || functionName[i] == '&')) {
			i++;
		}

		AppendFunctionNamePart(dest, length, &functionName[i], end - i);
		AppendPart(dest, length, "()");
		if (isLambda) {
			AppendPart(dest, length, LambdaSuffix.data(), (std::int32_t)LambdaSuffix.size());
		}
	} else {
		AppendFunctionNamePart(dest, length, functionName, functionNameLength);
	}
#	else
	// MSVC reports a bare qualified name with no return type and no arguments, and the trailing "()" is already part
	// of __DEATH_CURRENT_FUNCTION, so the only thing left to shorten is a lambda. Its closure type is spelled
	// "<lambda_1>" (a hash in older releases) and everything from there on is noise, e.g.
	// "Class::Method::<lambda_1>::operator ()()" or "Class::Method::<lambda_5>::()::<lambda_1>::operator ()()" for a
	// nested one, so cutting at the first occurrence always yields the enclosing function. It also always cuts,
	// unlike a check anchored at the end of the name, which would leave the raw closure type in the log for a lambda
	// that hosts a local class ("...::<lambda_1>::operator ()::S::F"). That's what makes "<lambda_" impossible in a
	// written entry, which the reader relies on - the "()::<lambda()>" spelling is the same one GCC/Clang produce
	// above and the only one anything downstream has to know.
	static constexpr StringView LambdaPrefixMsvc = "<lambda_"_s;
	static constexpr StringView ScopeSeparator = "::"_s;

	StringView functionNameView = StringView(functionName, functionNameLength);
	if (auto lambdaPrefix = functionNameView.find(LambdaPrefixMsvc)) {
		StringView enclosingFunction = functionNameView.prefix(lambdaPrefix.begin());
		if (enclosingFunction.hasSuffix(ScopeSeparator)) {
			enclosingFunction = enclosingFunction.exceptSuffix(ScopeSeparator);
		}

		// A lambda in a namespace-scope initializer has no enclosing function to report it against
		if (!enclosingFunction.empty()) {
			AppendFunctionNamePart(dest, length, enclosingFunction.data(), (std::int32_t)enclosingFunction.size());
			AppendPart(dest, length, "()::<lambda()>");
		} else {
			AppendPart(dest, length, "<lambda()>");
		}
		return;
	}

	AppendFunctionNamePart(dest, length, functionName, functionNameLength);
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

// Turning a shutdown request into a clean exit has nothing to do with tracing, so unlike the console
// detection above these handlers are compiled and installed even when `DEATH_TRACE` is disabled ---
// a dedicated server has to disconnect its peers and delist itself from the online server list on
// the way out no matter how the build was configured
#if defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)

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

static void InstallShutdownHandlers()
{
	// The handler is called only for events of a console the process is attached to, so registering
	// it is harmless even when there is none
	::SetConsoleCtrlHandler(OnHandleConsoleEvent, TRUE);
}

#elif defined(DEATH_TARGET_APPLE) || defined(DEATH_TARGET_UNIX)
#	include <signal.h>

static void OnHandleTerminationSignal(int sig)
{
	if (sig != SIGINT && sig != SIGTERM) {
		return;
	}

	auto& app = nCine::theApplication();
	if (!app.ShouldQuit()) {
		if (sig == SIGTERM) {
			LOGW("Received termination signal to shut down the application");
		} else {
			LOGW("Received interrupt signal to shut down the application");
		}
		app.Quit();
		return;
	}

	// The shutdown is already in progress, so a second signal restores the default action
	// (which terminates the process) and delivers it again --- otherwise a hung shutdown
	// couldn't be aborted anymore
	::signal(sig, SIG_DFL);
	::raise(sig);
}

static void InstallShutdownHandlers()
{
	// Both signals that conventionally ask a process to stop are turned into a clean shutdown:
	// SIGINT for Ctrl+C in a terminal, SIGTERM for `kill`, service managers and container
	// runtimes --- a dedicated server usually receives the latter one
	::signal(SIGINT, OnHandleTerminationSignal);
	::signal(SIGTERM, OnHandleTerminationSignal);
}

#else

static void InstallShutdownHandlers()
{
	// The remaining platforms deliver a shutdown request through their own callbacks instead
	// (or don't have the concept at all), see the platform backends
}

#endif

namespace nCine
{
	Application::Application()
		: _isSuspended(false), _autoSuspension(false), _hasFocus(true), _shouldQuit(false)
#if defined(DEATH_TRACE)
			, _mainThreadId(Death::Trace::Implementation::GetNativeThreadId())
#endif
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
		return *_screenViewport;
	}

	std::uint32_t Application::GetFrameCount() const
	{
		return _frameTimer->GetTotalNumberFrames();
	}

	float Application::GetTimeMult() const
	{
		return _frameTimer->GetTimeMult();
	}

	const FrameTimer& Application::GetFrameTimer() const
	{
		return *_frameTimer;
	}

	void Application::ResizeScreenViewport(std::int32_t width, std::int32_t height)
	{
		if (_screenViewport != nullptr) {
			bool sizeChanged = (width != _screenViewport->_width || height != _screenViewport->_height);
			if (sizeChanged && width > 0 && height > 0) {
				_screenViewport->Resize(width, height);
				_appEventHandler->OnResizeWindow(width, height);
			}
		}
	}

	bool Application::ShouldSuspend()
	{
		return ((!_hasFocus && _autoSuspension) || _isSuspended);
	}

	void Application::Quit()
	{
		_shouldQuit = true;
	}

	void Application::PreInitCommon(std::unique_ptr<IAppEventHandler> appEventHandler)
	{
		// Installed before anything else, so even a shutdown request that arrives during the
		// initialization is honored
		InstallShutdownHandlers();

#if defined(DEATH_TRACE)
		InitializeTrace();
#endif

		_appEventHandler = std::move(appEventHandler);
		_appEventHandler->OnPreInitialize(_appCfg);
		LOGB("IAppEventHandler::OnPreInitialize() invoked");
	}

	void Application::InitCommon()
	{
		TracyGpuContext;
		ZoneScopedC(0x81A861);
		// This timestamp is needed to initialize random number generator
		_profileStartTime = TimeStamp::now();

#if defined(WITH_TRACY)
		TracyAppInfo(NCINE_APP, sizeof(NCINE_APP) - 1);
		LOGW("Tracy integration is enabled");
#endif

		// Initialization of the static random generator seeds
		Random().Init(TimeStamp::now().ticks(), _profileStartTime.ticks());

		_frameTimer = std::make_unique<FrameTimer>(_appCfg.frameTimerLogInterval, 0.2f);
#if defined(DEATH_TARGET_WINDOWS)
		_waitableTimer = ::CreateWaitableTimerW(NULL, TRUE, NULL);
#endif

#if defined(WITH_AUDIO)
		if (_appCfg.withAudio) {
#	if defined(WITH_OPENAL)
			theServiceLocator().RegisterAudioDevice(std::make_unique<ALAudioDevice>());
#	elif defined(WITH_ASND)
			theServiceLocator().RegisterAudioDevice(std::make_unique<AsndAudioDevice>());
#	elif defined(WITH_AICA)
			theServiceLocator().RegisterAudioDevice(std::make_unique<AicaAudioDevice>());
#	elif defined(WITH_PS3AUDIO)
			theServiceLocator().RegisterAudioDevice(std::make_unique<Ps3AudioDevice>());
#	endif
		}
#endif
#if defined(WITH_THREADS)
		if (_appCfg.withThreads) {
			theServiceLocator().RegisterThreadPool(std::make_unique<ThreadPool>());
		}
#endif

		if (_appCfg.withGraphics) {
			theServiceLocator().RegisterRhiCapabilities(std::make_unique<RHI::Capabilities>());
			const auto& rhiCapabilities = theServiceLocator().GetRhiCapabilities();
			RHI::Debug::Init(rhiCapabilities);

#if !defined(WITH_ANGLE) && !defined(DEATH_TARGET_EMSCRIPTEN) && !defined(DEATH_TARGET_WINDOWS_RT)
			if (_appCfg.fixedBatchSize > 0) {
				LOGI("Using fixed batch size: {}", _appCfg.fixedBatchSize);
			} else {
				const auto& info = rhiCapabilities.GetInfoStrings();
				const StringView vendor = info.vendor;
				const StringView renderer = info.renderer;
				// Some GPUs don't work with dynamic batch size, so it refuses to render VBOs (shows a black screen), disable it for them
				if ((vendor == "Imagination Technologies"_s && (renderer == "PowerVR Rogue GE8300"_s || renderer == "PowerVR Rogue GE8320"_s || renderer == "PowerVR Rogue GE9215"_s)) ||
					(vendor == "ARM"_s && renderer == "Mali-T830"_s)) {
					const StringView vendorPrefix = vendor.findOr(' ', vendor.end());
					if DEATH_UNLIKELY(renderer.hasPrefix(vendor.prefix(vendorPrefix.begin()))) {
						LOGW("Detected {}: Using fixed batch size", renderer);
					} else {
						LOGW("Detected {} {}: Using fixed batch size", vendor, renderer);
					}
					_appCfg.fixedBatchSize = 10;
				}
			}
#endif

#if defined(WITH_RENDERDOC)
			RenderDocCapture::init();
#endif

#if defined(WITH_THREADS) && defined(DEATH_TRACE)
			std::size_t stackSize, stackRemaining;
			if (Thread::GetCurrentStackInfo(stackSize, stackRemaining)) {
				LOGI("Current thread stack size: {}/{} bytes", stackSize - stackRemaining, stackSize);
			}
#endif

			LOGI("Creating rendering resources...");

			// Create a minimal set of render resources before compiling the first shader
			RenderResources::CreateMinimal(); // they are required for rendering even without a scenegraph

			if (_appCfg.withScenegraph) {
				_gfxDevice->setupDevice();
				RenderResources::Create();
				_rootNode = std::make_unique<SceneNode>();
				_screenViewport = std::make_unique<ScreenViewport>();
				_screenViewport->SetRootNode(_rootNode.get());
			}

#if defined(WITH_IMGUI)
			_imguiDrawing = std::make_unique<ImGuiDrawing>(_appCfg.withScenegraph);

			// Debug overlay is available even when scenegraph is not
			if (_appCfg.withDebugOverlay) {
				_debugOverlay = std::make_unique<ImGuiDebugOverlay>(0.5f);	// 2 updates per second
			}
#endif
		} else {
			// Create scenegraph even without graphics to update nodes properly
			if (_appCfg.withScenegraph) {
				_rootNode = std::make_unique<SceneNode>();
				_screenViewport = std::make_unique<ScreenViewport>();
				_screenViewport->SetRootNode(_rootNode.get());
			}
		}

		LOGI("Core components initialized");
#if defined(NCINE_PROFILING)
		_timings[(std::int32_t)Timings::InitCommon] = _profileStartTime.secondsSince();
#endif
		{
			ZoneScopedNC("OnInitialize", 0x81A861);
#if defined(NCINE_PROFILING)
			_profileStartTime = TimeStamp::now();
#endif
			_appEventHandler->OnInitialize();
#if defined(NCINE_PROFILING)
			_timings[(std::int32_t)Timings::AppInit] = _profileStartTime.secondsSince();
#endif
			LOGB("IAppEventHandler::OnInitialize() invoked");
		}

		if (_appCfg.withGraphics) {
			// Swapping frame now for a cleaner API trace capture when debugging
			_gfxDevice->update();
			FrameMark;
			TracyGpuCollect;
		}
	}

	void Application::Step()
	{
		_frameTimer->AddFrame();

#if defined(WITH_IMGUI)
		if (_appCfg.withGraphics) {
			ZoneScopedN("ImGui newFrame");
#	if defined(NCINE_PROFILING)
			_profileStartTime = TimeStamp::now();
#	endif
			_imguiDrawing->NewFrame();
#	if defined(NCINE_PROFILING)
			_timings[(std::int32_t)Timings::ImGui] = _profileStartTime.secondsSince();
#	endif
		}
#endif
#if defined(WITH_LUA)
		LuaStatistics::update();
#endif

		{
			ZoneScopedNC("OnBeginFrame", 0x81A861);
#if defined(NCINE_PROFILING)
			_profileStartTime = TimeStamp::now();
#endif
			_appEventHandler->OnBeginFrame();
#if defined(NCINE_PROFILING)
			_timings[(std::int32_t)Timings::BeginFrame] = _profileStartTime.secondsSince();
#endif
		}

#if defined(WITH_IMGUI)
		if (_debugOverlay != nullptr) {
			_debugOverlay->Update();
		}
#endif

		if (_appCfg.withScenegraph) {
			ZoneScopedNC("SceneGraph", 0x81A861);
			{
				ZoneScopedNC("Update", 0x81A861);
#if defined(NCINE_PROFILING)
				_profileStartTime = TimeStamp::now();
#endif
				_screenViewport->Update();
#if defined(NCINE_PROFILING)
				_timings[(std::int32_t)Timings::Update] = _profileStartTime.secondsSince();
#endif
			}

			{
				ZoneScopedNC("OnPostUpdate", 0x81A861);
#if defined(NCINE_PROFILING)
				_profileStartTime = TimeStamp::now();
#endif
				_appEventHandler->OnPostUpdate();
#if defined(NCINE_PROFILING)
				_timings[(std::int32_t)Timings::PostUpdate] = _profileStartTime.secondsSince();
#endif
			}

			if (_appCfg.withGraphics) {
				{
					ZoneScopedNC("Visit", 0x81A861);
#if defined(NCINE_PROFILING)
					_profileStartTime = TimeStamp::now();
#endif
					_screenViewport->Visit();
#if defined(NCINE_PROFILING)
					_timings[(std::int32_t)Timings::Visit] = _profileStartTime.secondsSince();
#endif
				}

#if defined(WITH_IMGUI)
				{
					ZoneScopedN("ImGui endFrame");
#	if defined(NCINE_PROFILING)
					_profileStartTime = TimeStamp::now();
#	endif
					RenderQueue& imguiRenderQueue = (_guiSettings.imguiViewport ? _guiSettings.imguiViewport->_renderQueue : _screenViewport->_renderQueue);
					_imguiDrawing->EndFrame(imguiRenderQueue);
#	if defined(NCINE_PROFILING)
					_timings[(std::int32_t)Timings::ImGui] += _profileStartTime.secondsSince();
#	endif
				}
#endif

				{
					ZoneScopedNC("Draw", 0x81A861);
#if defined(NCINE_PROFILING)
					_profileStartTime = TimeStamp::now();
#endif
					_screenViewport->SortAndCommitQueue();
					_screenViewport->Draw();
#if defined(NCINE_PROFILING)
					_timings[(std::int32_t)Timings::Draw] = _profileStartTime.secondsSince();
#endif
				}
			}
		} else {
#if defined(WITH_IMGUI)
			if (_appCfg.withGraphics) {
				ZoneScopedN("ImGui endFrame");
#	if defined(NCINE_PROFILING)
				_profileStartTime = TimeStamp::now();
#	endif
				_imguiDrawing->EndFrame();
#	if defined(NCINE_PROFILING)
				_timings[(std::int32_t)Timings::ImGui] += _profileStartTime.secondsSince();
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
			_profileStartTime = TimeStamp::now();
#endif
			_appEventHandler->OnEndFrame();
#if defined(NCINE_PROFILING)
			_timings[(std::int32_t)Timings::EndFrame] = _profileStartTime.secondsSince();
#endif
		}

#if defined(WITH_IMGUI)
		if (_debugOverlay != nullptr) {
			_debugOverlay->UpdateFrameTimings();
		}
#endif

		if (_appCfg.withGraphics) {
			_gfxDevice->update();
			FrameMark;
			TracyGpuCollect;
		}

		if (_appCfg.frameLimit > 0) {
			FrameMarkStart("Frame limiting");
			const std::int64_t frameTimeDuration = clock().frequency() / _appCfg.frameLimit;

#if defined(DEATH_TARGET_WINDOWS)
			// It can wait longer than necessary, so subtract 1 ms to compensate
			const std::int64_t remainingTime100ns = ((((std::int64_t)frameTimeDuration - (std::int64_t)_frameTimer->GetFrameDurationAsTicks())
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
			const std::int64_t remainingTimeNs = (1'000'000'000LL / (std::int64_t)_appCfg.frameLimit) -
				((std::int64_t)_frameTimer->GetFrameDurationAsTicks() * 1'000'000'000LL / (std::int64_t)clock().frequency()) - 500'000LL;
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
			const std::int64_t remainingTimeNs = (1'000'000'000LL / (std::int64_t)_appCfg.frameLimit) -
				((std::int64_t)_frameTimer->GetFrameDurationAsTicks() * 1'000'000'000LL / (std::int64_t)clock().frequency()) - 500'000LL;
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
#else
			// Generic fallback for platforms without a dedicated high-precision arm (console ports and other
			// new targets): a coarse millisecond sleep covers the bulk of the wait so the spin below only tops
			// up the last couple of milliseconds instead of burning a full core for the whole frame remainder
			const std::int64_t remainingTimeMs = (((std::int64_t)frameTimeDuration - (std::int64_t)_frameTimer->GetFrameDurationAsTicks())
				* 1'000LL) / (std::int64_t)clock().frequency() - 2; // 2 ms slack for coarse sleep granularity
			if (remainingTimeMs > 0) {
				Thread::Sleep((std::uint32_t)remainingTimeMs);
			}
#endif

			while ((std::int64_t)_frameTimer->GetFrameDurationAsTicks() < frameTimeDuration) {
				Thread::Sleep(0);
			}
			FrameMarkEnd("Frame limiting");
		}
	}

	void Application::ShutdownCommon()
	{
		ZoneScopedC(0x81A861);
		_appEventHandler->OnShutdown();
		LOGI("IAppEventHandler::OnShutdown() invoked");
		_appEventHandler = nullptr;

		_rootNode = nullptr;

		if (_appCfg.withGraphics) {
#if defined(WITH_IMGUI)
			_imguiDrawing = nullptr;
			_debugOverlay = nullptr;
#endif
#if defined(WITH_RENDERDOC)
			RenderDocCapture::removeHooks();
#endif
			RenderResources::Dispose();
			_gfxDevice = nullptr;
		}

		_frameTimer = nullptr;
		_inputManager = nullptr;

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

		_hasFocus = hasFocus;
	}

#if defined(DEATH_TRACE)
	void Application::OnTraceReceived(TraceLevel level, std::uint64_t timestamp, StringView threadId, StringView functionName, StringView content)
	{
		char logEntryWithColors[MaxLogEntryLength + 24];

#if defined(DEATH_TARGET_ANDROID)
		std::int32_t length2 = 0;
		// Provide actual timestamps even though Android includes its own
		AppendDateTime(logEntryWithColors, length2, timestamp);
		logEntryWithColors[length2++] = ' ';
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
#elif defined(DEATH_TARGET_PS2)
		// A bare ELF has no connected stdout - its file descriptor is only wired up when the program was
		// launched through ps2client - so the message goes to the EE's SIO transmit register instead.
		// PCSX2 captures that as EE console output (with "EnableEEConsole" turned on) and it reaches a
		// serial cable on real hardware, which is the same trade the Dreamcast makes with dbgio above.
		std::int32_t length2 = 0;
		AppendLevel(logEntryWithColors, length2, level, threadId);
		AppendFunctionName(logEntryWithColors, length2, functionName);
		AppendPart(logEntryWithColors, length2, content.data(), (std::int32_t)content.size());
		if (length2 >= MaxLogEntryLength - 2) {
			length2 = MaxLogEntryLength - 2;
		}
		logEntryWithColors[length2++] = '\n';
		{
			volatile std::uint8_t* const sioTx = reinterpret_cast<volatile std::uint8_t*>(0x1000F180);
			for (std::int32_t i = 0; i < length2; i++) {
				*sioTx = std::uint8_t(logEntryWithColors[i]);
			}
		}
#elif defined(DEATH_TARGET_PSP)
		// Write the message to stdout, which PSPSDK routes to the host through sceIoWrite on fd 1: the
		// PPSSPP emulator prints it into its log and psplink shows it in its console. On real hardware
		// launched from the firmware there is nobody listening, but pspDebugScreenPrintf below still puts
		// it on the screen for as long as the GU session has not taken the framebuffer over - which covers
		// exactly the startup window where a failure would otherwise be invisible.
		std::int32_t length2 = 0;
		AppendLevel(logEntryWithColors, length2, level, threadId);
		AppendFunctionName(logEntryWithColors, length2, functionName);
		AppendPart(logEntryWithColors, length2, content.data(), (std::int32_t)content.size());
		if (length2 >= MaxLogEntryLength - 2) {
			length2 = MaxLogEntryLength - 2;
		}
		logEntryWithColors[length2++] = '\n';
		logEntryWithColors[length2] = '\0';
		::fwrite(logEntryWithColors, 1, length2, stdout);
		::fflush(stdout);
		pspDebugScreenPrintf("%s", logEntryWithColors);
#elif defined(DEATH_TARGET_VITA)
		std::int32_t length2 = 0;
		AppendLevel(logEntryWithColors, length2, level, threadId);
		AppendFunctionName(logEntryWithColors, length2, functionName);
		AppendPart(logEntryWithColors, length2, content.data(), (std::int32_t)content.size());
		logEntryWithColors[length2] = '\0';
		sceClibPrintf("%s", logEntryWithColors);
#elif defined(DEATH_TARGET_PS3)
		// PSL1GHT's newlib routes fd 1 and 2 straight to the lv2 `sysTtyWrite` syscall rather than to a
		// file, so an ordinary write reaches the console TTY on hardware and RPCS3's log in the emulator -
		// no serial cable or on-screen fallback is needed the way the PS2 and PSP arms above need one.
		// The write is issued directly instead of through stdio so a trace still gets out when the message
		// comes from a thread that already holds stdio's lock (a fatal inside a printf, for instance).
		std::int32_t length2 = 0;
		AppendLevel(logEntryWithColors, length2, level, threadId);
		AppendFunctionName(logEntryWithColors, length2, functionName);
		AppendPart(logEntryWithColors, length2, content.data(), (std::int32_t)content.size());
		if (length2 >= MaxLogEntryLength - 2) {
			length2 = MaxLogEntryLength - 2;
		}
		logEntryWithColors[length2++] = '\n';
		{
			std::uint32_t written = 0;
			sysTtyWrite(STDOUT_FILENO, logEntryWithColors, std::uint32_t(length2), &written);
		}
#elif defined(DEATH_TARGET_WII) || defined(DEATH_TARGET_GAMECUBE)
		std::int32_t length2 = 0;
		AppendLevel(logEntryWithColors, length2, level, threadId);
		AppendFunctionName(logEntryWithColors, length2, functionName);
		AppendPart(logEntryWithColors, length2, content.data(), (std::int32_t)content.size());
		if (length2 >= MaxLogEntryLength - 2) {
			length2 = MaxLogEntryLength - 2;
		}
		logEntryWithColors[length2++] = '\n';

		// Show the message on the early boot console (a no-op once OgcGfxDevice owns the video output)
		::fwrite(logEntryWithColors, 1, length2, stdout);

		// Also send it to a USB Gecko in memory card slot B - the Dolphin emulator exposes it as a raw
		// TCP socket on port 55020 (0xd6ec, with "SlotB = 7" in Dolphin.ini) and it also works with
		// the real adapter
		static const bool __geckoAlive = (usb_isgeckoalive(EXI_CHANNEL_1) != 0);
		if (__geckoAlive) {
			usb_sendbuffer_safe(EXI_CHANNEL_1, logEntryWithColors, length2);
		}
#elif defined(DEATH_TARGET_DREAMCAST)
		// Write the message to dbgio (SCIF serial by default) - the Flycast/lxdream emulators show it
		// in their logs and dc-tool/dcload shows it in the console
		std::int32_t length2 = 0;
		AppendLevel(logEntryWithColors, length2, level, threadId);
		AppendFunctionName(logEntryWithColors, length2, functionName);
		AppendPart(logEntryWithColors, length2, content.data(), (std::int32_t)content.size());
		if (length2 >= MaxLogEntryLength - 2) {
			length2 = MaxLogEntryLength - 2;
		}
		logEntryWithColors[length2++] = '\n';
		dbgio_write_buffer_xlat(reinterpret_cast<const std::uint8_t*>(logEntryWithColors), length2);
#elif defined(DEATH_TARGET_WINDOWS_RT)
		// Use OutputDebugStringA() to avoid conversion UTF-8 => UTF-16 => current code page
		std::int32_t length2 = 0;
		AppendDateTime(logEntryWithColors, length2, timestamp);
		logEntryWithColors[length2++] = ' ';
		AppendLevel(logEntryWithColors, length2, level, threadId);
		AppendFunctionName(logEntryWithColors, length2, functionName);
		AppendPart(logEntryWithColors, length2, content.data(), (std::int32_t)content.size());
		if (length2 >= MaxLogEntryLength - 2) {
			length2 = MaxLogEntryLength - 2;
		}
		logEntryWithColors[length2++] = '\n';
		logEntryWithColors[length2] = '\0';
		::OutputDebugStringA(logEntryWithColors);
#elif !defined(DEATH_TRACE_LOG_PATH)	// Log to stdout only if file logging is not force enabled
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
			AppendDateTime(logEntryWithColors, length2, timestamp);
			logEntryWithColors[length2++] = ' ';
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
			static std::atomic<std::int32_t> logFileLastDay{-1};
			AppendDateTime(logEntryWithColors, length3, timestamp, &logFileLastDay);
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
#	if defined(DEATH_TARGET_WII) || defined(DEATH_TARGET_GAMECUBE)
			// Flush every entry, so the log is complete even if the game hangs or crashes
			__logFile->Flush();
#	endif
		}
#endif

#if defined(WITH_IMGUI)
		auto* debugOverlay = theApplication()._debugOverlay.get();
		if (debugOverlay != nullptr) {
			std::int32_t length3 = 0;
			AppendDateTime(logEntryWithColors, length3, timestamp);
			debugOverlay->Log(level, { logEntryWithColors, (std::size_t)length3 }, threadId, functionName, content);
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
		_frameTimer->Suspend();
		if (_appEventHandler != nullptr) {
			_appEventHandler->OnSuspend();
		}
#if defined(WITH_AUDIO)
		if (_appCfg.withAudio) {
			IAudioDevice& audioDevice = theServiceLocator().GetAudioDevice();
			audioDevice.suspendDevice();
		}
#endif

		LOGI("IAppEventHandler::OnSuspend() invoked");
	}

	void Application::Resume()
	{
		if (_appEventHandler != nullptr) {
			_appEventHandler->OnResume();
		}
#if defined(WITH_AUDIO)
		if (_appCfg.withAudio) {
			IAudioDevice& audioDevice = theServiceLocator().GetAudioDevice();
			audioDevice.resumeDevice();
		}
#endif

		DEATH_UNUSED TimeStamp suspensionDuration = _frameTimer->Resume();
		LOGD("Suspended for {:.3} seconds", suspensionDuration.seconds());
#if defined(NCINE_PROFILING)
		_profileStartTime += suspensionDuration;
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

	void Application::Vibrate(std::int32_t milliseconds)
	{
		// Not implemented in base class
	}

	void Application::ShowStatusBar()
	{
		// Not implemented in base class
	}

	void Application::HideStatusBar()
	{
		// Not implemented in base class
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
		if (__logFile->IsValid()) {
			AppendLogFileHeader(*__logFile);
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

	void Application::SetCrashDumpDirectory(Containers::StringView path)
	{
#if defined(WITH_BACKWARD)
		__eh.DumpDirectory = path;
#endif
	}

#if defined(DEATH_TRACE)
	void Application::InitializeTrace()
	{
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

		StringView NO_COLOR = ::getenv("NO_COLOR");
		StringView FORCE_COLOR = ::getenv("FORCE_COLOR");
		if (NO_COLOR) {
			// Don't use colors if NO_COLOR is set, even if the console supports it
		} else if (::isatty(1)) {
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
		} else if (FORCE_COLOR) {
			// Use colors if FORCE_COLOR is set, even if the console doesn't report support for it (e.g.,
			// when piping to a file), this is useful when the output is later viewed in a compatible viewer
			__consoleType = ConsoleType::EscapeCodes24bit;
		}

#	elif defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)
		DWORD NO_COLOR = ::GetEnvironmentVariable(L"NO_COLOR", nullptr, 0);
		if (NO_COLOR) {
			// Don't use colors if NO_COLOR is set, even if the console supports it
		} else {
			HANDLE hStdOut = ::GetStdHandle(STD_OUTPUT_HANDLE);
			switch (::GetFileType(hStdOut)) {
				case FILE_TYPE_CHAR: {
					bool hasVirtualTerminal = EnableVirtualTerminalProcessing(hStdOut);
					__consoleType = (hasVirtualTerminal ? ConsoleType::EscapeCodes24bit : ConsoleType::WinApi);
					if (hasVirtualTerminal) {
						CheckConsoleCapabilities();
					}
					break;
				}
				case FILE_TYPE_UNKNOWN:
					// Nothing is attached to stdout
					break;
				default:
					__consoleType = ConsoleType::Redirect;

					DWORD FORCE_COLOR = ::GetEnvironmentVariable(L"FORCE_COLOR", nullptr, 0);
					if (FORCE_COLOR) {
						// Use colors if FORCE_COLOR is set, even if the console doesn't report support for it (e.g.,
						// when piping to a file), this is useful when the output is later viewed in a compatible viewer
						__consoleType = ConsoleType::EscapeCodes24bit;
					}
					break;
			}
		}
#	endif

#	if defined(WITH_BACKWARD)
		if (__consoleType >= ConsoleType::EscapeCodes) {
			__eh.FeatureFlags |= Backward::Flags::Colorized;
		}
#	endif

		Trace::AttachSink(this);
#	if !defined(DEATH_DEBUG)
		Trace::InitializeBacktrace(8, TraceLevel::Warning);
#	endif
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

#	if !defined(DEATH_TARGET_EMSCRIPTEN)
	// Operating system reported in the TraceDigger metadata header, the values must match `MetadataPlatform`
	// on the reader side - they are part of the wire format, so they must never be reassigned
	enum class MetadataPlatform : std::uint32_t {
		Unknown = 0,
		Other = 1,

		Windows = 2,
		WindowsRT = 3,
		Unix = 4,
		Linux = 5,
		Bsd = 6,
		Apple = 7,
		iOS = 8,
		Android = 12,
		Web = 14,

		GameCube = 67,
		Wii = 68,
		Switch = 70,
		PlayStationPortable = 101,
		PlayStationVita = 102,
		SegaDreamcast = 117
	};

	// How the application version is stored in the TraceDigger metadata header, the values are part
	// of the wire format, so they must never be reassigned
	enum class MetadataVersionForm : std::uint32_t {
		// The version doesn't match any of the known patterns, it's stored as the last string instead
		Raw = 0,
		// `<major>.<minor>.<patch>` --- stored as 3 variable-length integers
		Numeric = 1,
		// `<major>.<minor>.r<revision>-<hash>` --- stored as 3 variable-length integers, followed
		// by the number of hexadecimal digits of the commit hash and the digits packed 2 per byte
		GitRevision = 2
	};

	// How a single string is stored in the TraceDigger metadata header, the values are part of the wire
	// format, so they must never be reassigned
	enum class MetadataStringEncoding : std::uint32_t {
		// UTF-8 bytes as they are, used if the string contains any multi-byte sequence
		Raw = 0,
		// 7 bits per character, used if the string fits neither of the two alphabets below
		Ascii = 1,
		// 6 bits per character according to `MetadataStringAlphabet`
		LowercaseSet = 2,
		// 6 bits per character according to `MetadataStringMixedCaseAlphabet`
		MixedCaseSet = 3
	};

	// Characters that fit into 6 bits - lowercase text with any of the usual symbols, which covers versions,
	// paths and command-line arguments
	static constexpr char MetadataStringAlphabet[] = "abcdefghijklmnopqrstuvwxyz0123456789 .,-_:;/\\()[]{}=+*?!@#%&'\"<>";
	static_assert(sizeof(MetadataStringAlphabet) == 64 + 1, "MetadataStringAlphabet must contain exactly 64 characters");

	// Characters that fit into 6 bits when both letter cases are needed - all 52 letters leave room for the digits
	// and 2 symbols only, which is still enough for file names and host names
	static constexpr char MetadataStringMixedCaseAlphabet[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-";
	static_assert(sizeof(MetadataStringMixedCaseAlphabet) == 64 + 1, "MetadataStringMixedCaseAlphabet must contain exactly 64 characters");

	void Application::AppendLogFileHeader(Stream& s)
	{
		// Write TraceDigger metadata header, the payload is encoded with URL-safe Base64 (without padding):
		//
		//   u8       Control --- bits 0-4: format version (1), bits 5-6: `MetadataVersionForm`,
		//                        bit 7: reserved
		//   varint   Flags
		//   u8       Platform --- `MetadataPlatform`
		//   varint   Timestamp --- Unix time in milliseconds
		//   varint   Process ID
		//   varint   Main thread ID as zig-zag delta from the process ID
		//   ...      Application version according to `MetadataVersionForm`
		//   ...      Strings in this order: executable name, host name, arguments, compatibility layer
		//            (only if `HasAppCompatLayer` is set), application version (only if the form
		//            is `MetadataVersionForm::Raw`); trailing empty strings are omitted, so any string
		//            that is not there anymore is empty
		//
		// Each string begins with a variable-length integer --- the number of characters shifted left by 2 bits
		// with `MetadataStringEncoding` in the lower 2 bits --- followed by the characters packed according
		// to the encoding, the leftover bits of the last byte are always zero
		//
		// Everything that can be derived on the reader side is left out --- the main thread ID usually differs
		// from the process ID only slightly and the version is mostly digits, so both of them compress well
		// into the layout above

		std::int64_t timestampMs = DateTime::UtcNow().ToUnixMilliseconds() - 300;
		if (timestampMs < 0) {
			timestampMs = 0;
		}

		std::uint32_t flags = 0;
		if (Environment::GetCurrentElevation() == Environment::ElevationState::Full) {
			flags |= 0x01;	// Elevated
		}
#		if defined(DEATH_TARGET_32BIT)
		flags |= 0x40;	// Is32Bit
#		endif
#		if defined(DEATH_TARGET_BIG_ENDIAN)
		flags |= 0x80;	// IsBigEndian
#		endif

#		if defined(DEATH_TARGET_WINDOWS_RT)
		constexpr MetadataPlatform platform = MetadataPlatform::WindowsRT;
#		elif defined(DEATH_TARGET_WINDOWS)
		constexpr MetadataPlatform platform = MetadataPlatform::Windows;
#		elif defined(DEATH_TARGET_ANDROID)
		constexpr MetadataPlatform platform = MetadataPlatform::Android;
#		elif defined(DEATH_TARGET_SWITCH)
		constexpr MetadataPlatform platform = MetadataPlatform::Switch;
#		elif defined(DEATH_TARGET_PSP)
		constexpr MetadataPlatform platform = MetadataPlatform::PlayStationPortable;
#		elif defined(DEATH_TARGET_VITA)
		constexpr MetadataPlatform platform = MetadataPlatform::PlayStationVita;
#		elif defined(DEATH_TARGET_WII)
		constexpr MetadataPlatform platform = MetadataPlatform::Wii;
#		elif defined(DEATH_TARGET_GAMECUBE)
		constexpr MetadataPlatform platform = MetadataPlatform::GameCube;
#		elif defined(DEATH_TARGET_DREAMCAST)
		constexpr MetadataPlatform platform = MetadataPlatform::SegaDreamcast;
#		elif defined(DEATH_TARGET_IOS)
		constexpr MetadataPlatform platform = MetadataPlatform::iOS;
#		elif defined(DEATH_TARGET_APPLE)
		constexpr MetadataPlatform platform = MetadataPlatform::Apple;
#		elif defined(DEATH_TARGET_EMSCRIPTEN)
		constexpr MetadataPlatform platform = MetadataPlatform::Web;
#		elif defined(__linux__)
		constexpr MetadataPlatform platform = MetadataPlatform::Linux;
#		elif defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) || defined(__DragonFly__)
		constexpr MetadataPlatform platform = MetadataPlatform::Bsd;
#		elif defined(DEATH_TARGET_UNIX)
		constexpr MetadataPlatform platform = MetadataPlatform::Unix;
#		else
		constexpr MetadataPlatform platform = MetadataPlatform::Unknown;
#		endif

#		if defined(DEATH_TARGET_WINDOWS)
		std::uint32_t processId = (std::uint32_t)::GetCurrentProcessId();
		wchar_t bufferW[128]; DWORD hostNameWLength = (DWORD)arraySize(bufferW);
		if (!::GetComputerNameW(bufferW, &hostNameWLength)) {
			hostNameWLength = 0;
		}
		char hostName[128];
		std::int32_t hostNameLength = Utf8::FromUtf16(hostName, bufferW, hostNameWLength);

		DWORD compatLayerLength = ::GetEnvironmentVariable(L"__COMPAT_LAYER", bufferW, (DWORD)arraySize(bufferW));
		if (compatLayerLength > 0) {
			flags |= 0x1000;	// HasAppCompatLayer
		}
		if (Environment::IsWine()) {
			flags |= 0x2000;	// IsWine
		}
#		elif defined(DEATH_TARGET_ANDROID)
		flags |= 0x04 | 0x20;	// ProcessIdEqualsToMainThreadId | RemoteDevice
		std::uint32_t processId = (std::uint32_t)::getpid();
		auto androidId = nCine::Backends::AndroidJniWrap_Secure::getAndroidId();
		const char* hostName = androidId.data();
		std::int32_t hostNameLength = (std::int32_t)androidId.size();
#		elif defined(DEATH_TARGET_PSP) || defined(DEATH_TARGET_VITA) || defined(DEATH_TARGET_WII) || \
				defined(DEATH_TARGET_GAMECUBE) || defined(DEATH_TARGET_DREAMCAST) || defined(DEATH_TARGET_PS2) || \
				defined(DEATH_TARGET_PS3)
		flags |= 0x20;	// RemoteDevice
		std::uint32_t processId = (std::uint32_t)::getpid();
		// TODO: Hostname is not implemented on Vita, libogc, KOS, PSPSDK, PS2SDK and PSL1GHT
		char hostName[32] {}; std::int32_t hostNameLength = 0;
#		else
#			if !defined(DEATH_TARGET_APPLE) && !defined(DEATH_TARGET_EMSCRIPTEN)
			flags |= 0x04;	// ProcessIdEqualsToMainThreadId
#			endif
#			if defined(DEATH_TARGET_SWITCH)
			flags |= 0x20;	// RemoteDevice
#			endif
		std::uint32_t processId = (std::uint32_t)::getpid();
		char hostName[128] {}; std::int32_t hostNameLength = 0;
		if (::gethostname(hostName, arraySize(hostName)) == 0) {
			hostName[arraySize(hostName) - 1] = '\0';
			hostNameLength = std::strlen(hostName);
		}
#		endif

		// Try to store the application version numerically, the string form is used only as a fallback
		auto parseDecimal = [](StringView value, std::uint32_t& result) {
			if (value.empty() || value.size() > 9) {
				return false;
			}
			std::uint32_t n = 0;
			for (char c : value) {
				if (c < '0' || c > '9') {
					return false;
				}
				n = n * 10 + (std::uint32_t)(c - '0');
			}
			result = n;
			return true;
		};

		MetadataVersionForm versionForm = MetadataVersionForm::Raw;
		std::uint32_t versionMajor = 0, versionMinor = 0, versionPatch = 0;
		StringView versionHash;
		StringView version = NCINE_VERSION;
		if (StringView firstDot = version.find('.')) {
			StringView rest = version.suffix(firstDot.end());
			if (StringView secondDot = rest.find('.')) {
				StringView patchPart = rest.suffix(secondDot.end());
				if (parseDecimal(version.prefix(firstDot.begin()), versionMajor) &&
					parseDecimal(rest.prefix(secondDot.begin()), versionMinor)) {
					if (parseDecimal(patchPart, versionPatch)) {
						versionForm = MetadataVersionForm::Numeric;
					} else if (patchPart.hasPrefix('r')) {
						StringView revisionPart = patchPart.exceptPrefix(1);
						if (StringView dash = revisionPart.find('-')) {
							StringView hash = revisionPart.suffix(dash.end());
							bool isHex = (!hash.empty() && hash.size() <= 40);
							for (char c : hash) {
								if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
									isHex = false;
									break;
								}
							}
							if (isHex && parseDecimal(revisionPart.prefix(dash.begin()), versionPatch)) {
								versionHash = hash;
								versionForm = MetadataVersionForm::GitRevision;
							}
						}
					}
				}
			}
		}

#		if defined(DEATH_TARGET_ANDROID)
		auto executableFileName = nCine::Backends::AndroidJniWrap_Activity::getPackageName();
#		else
		auto executablePath = fs::GetExecutablePath();
		auto executableFileName = fs::GetFileName(executablePath);
		if (executableFileName.empty()) {
			executableFileName = NCINE_APP;
		}
#		endif

		// The executable itself is not among the arguments, it's stripped when they are parsed
		std::string arguments;
		for (std::size_t i = 0; i < _appCfg.argc(); i++) {
			if (i > 0) {
				arguments += ' ';
			}
			arguments += _appCfg.argv(i);
		}

		char buffer[256];
		MemoryStream ms(buffer, sizeof(buffer));
		ms.WriteValue<std::uint8_t>((std::uint8_t)(1 |					// Format version
			((std::uint32_t)versionForm << 5)));
		ms.WriteVariableUint32(flags);
		ms.WriteValue<std::uint8_t>((std::uint8_t)platform);
		ms.WriteVariableUint64((std::uint64_t)timestampMs);
		ms.WriteVariableUint32(processId);
		// Thread IDs are usually assigned close to the process ID, so the delta is much shorter than the ID itself
		ms.WriteVariableInt64((std::int64_t)_mainThreadId - (std::int64_t)processId);

		if (versionForm != MetadataVersionForm::Raw) {
			ms.WriteVariableUint32(versionMajor);
			ms.WriteVariableUint32(versionMinor);
			ms.WriteVariableUint32(versionPatch);

			if (versionForm == MetadataVersionForm::GitRevision) {
				auto toNibble = [](char c) -> std::uint8_t {
					return (std::uint8_t)(c <= '9' ? c - '0' : c - 'a' + 10);
				};
				ms.WriteValue<std::uint8_t>((std::uint8_t)versionHash.size());
				for (std::size_t i = 0; i < versionHash.size(); i += 2) {
					std::uint8_t packedNibbles = (std::uint8_t)(toNibble(versionHash[i]) << 4);
					if (i + 1 < versionHash.size()) {
						packedNibbles |= toNibble(versionHash[i + 1]);
					}
					ms.WriteValue<std::uint8_t>(packedNibbles);
				}
			}
		}

		// Each string is stored with the tightest encoding its characters allow - the reader drops any string
		// that doesn't fit in the header entirely, so clamp each one to the space that is left in the buffer
		// instead of letting it be cut in half by the stream, `reserve` keeps room for the strings after it
		auto writeString = [&ms](StringView value, std::int64_t reserve) {
			auto indexInAlphabet = [](const char* alphabet, char c) -> std::int32_t {
				for (std::int32_t i = 0; i < 64; i++) {
					if (alphabet[i] == c) {
						return i;
					}
				}
				return -1;
			};

			// Up to 4 bytes are needed for the header itself
			std::int64_t available = ms.GetSize() - ms.GetPosition() - reserve - 4;
			std::int64_t length = (std::int64_t)value.size();
			if (length > available) {
				length = (available > 0 ? available : 0);
				// Don't cut in the middle of a multi-byte UTF-8 sequence
				while (length > 0 && (value[length] & 0xC0) == 0x80) {
					length--;
				}
			}

			MetadataStringEncoding encoding = MetadataStringEncoding::Raw;
			if (length > 0) {
				bool fitsLowercase = true, fitsMixedCase = true, isAscii = true;
				for (std::int64_t i = 0; i < length; i++) {
					char c = value[i];
					if ((std::uint8_t)c >= 0x80) {
						isAscii = fitsLowercase = fitsMixedCase = false;
						break;
					}
					if (fitsLowercase && indexInAlphabet(MetadataStringAlphabet, c) < 0) {
						fitsLowercase = false;
					}
					if (fitsMixedCase && indexInAlphabet(MetadataStringMixedCaseAlphabet, c) < 0) {
						fitsMixedCase = false;
					}
				}
				encoding = (fitsLowercase ? MetadataStringEncoding::LowercaseSet
					: (fitsMixedCase ? MetadataStringEncoding::MixedCaseSet
						: (isAscii ? MetadataStringEncoding::Ascii : MetadataStringEncoding::Raw)));
			}

			ms.WriteVariableUint32((std::uint32_t)((length << 2) | (std::int64_t)encoding));

			if (encoding == MetadataStringEncoding::Raw) {
				if (length > 0) {
					ms.Write(value.data(), length);
				}
			} else {
				const char* alphabet = (encoding == MetadataStringEncoding::MixedCaseSet
					? MetadataStringMixedCaseAlphabet : MetadataStringAlphabet);
				std::int32_t bitsPerCharacter = (encoding == MetadataStringEncoding::Ascii ? 7 : 6);
				std::uint32_t accumulator = 0; std::int32_t accumulatorBits = 0;
				for (std::int64_t i = 0; i < length; i++) {
					std::uint32_t code = (bitsPerCharacter == 7
						? (std::uint32_t)(value[i] & 0x7F)
						: (std::uint32_t)indexInAlphabet(alphabet, value[i]));
					accumulator |= (code << accumulatorBits);
					accumulatorBits += bitsPerCharacter;
					while (accumulatorBits >= 8) {
						ms.WriteValue<std::uint8_t>((std::uint8_t)(accumulator & 0xFF));
						accumulator >>= 8;
						accumulatorBits -= 8;
					}
				}
				if (accumulatorBits > 0) {
					ms.WriteValue<std::uint8_t>((std::uint8_t)(accumulator & 0xFF));
				}
			}
		};

		String compatLayer;
#		if defined(DEATH_TARGET_WINDOWS)
		if (compatLayerLength > 0) {
			if (compatLayerLength > (std::uint32_t)arraySize(bufferW)) {
				compatLayerLength = (std::uint32_t)arraySize(bufferW);
			}
			compatLayer = Utf8::FromUtf16(bufferW, compatLayerLength);
		}
#		endif

		StringView strings[5];
		std::int32_t stringCount = 0;
		strings[stringCount++] = executableFileName;
		strings[stringCount++] = { hostName, (std::size_t)hostNameLength };
		strings[stringCount++] = { arguments.data(), arguments.size() };
		if ((flags & 0x1000) != 0) {	// HasAppCompatLayer
			strings[stringCount++] = compatLayer;
		}
		if (versionForm == MetadataVersionForm::Raw) {
			strings[stringCount++] = version;
		}

		// Trailing empty strings don't need to be stored at all
		while (stringCount > 0 && strings[stringCount - 1].empty()) {
			stringCount--;
		}

		// Room each of the strings that are still to be written needs at the end of the header
		constexpr std::int64_t ReservedPerString = 24;

		for (std::int32_t i = 0; i < stringCount; i++) {
			writeString(strings[i], (std::int64_t)(stringCount - i - 1) * ReservedPerString);
		}

		auto metadataBase64 = nCine::toBase64Url(buffer, buffer + (std::size_t)ms.GetPosition());

		constexpr StringView FileHeader = "#! /usr/bin/tracedigger :"_s;
		s.Write(FileHeader.data(), (std::int64_t)FileHeader.size());
		s.Write(metadataBase64.data(), (std::int64_t)metadataBase64.size());
		s.Write("\n", 1);
	}
#	endif

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

			::SetConsoleOutputCP(CP_UTF8);
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

			::SetConsoleOutputCP(CP_UTF8);
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
