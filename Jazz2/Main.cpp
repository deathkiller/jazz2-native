#if defined(_WIN32) && !defined(CMAKE_BUILD)
#   if defined(_M_X64)
#       pragma comment(lib, "../Libs/x64/libdeflate.lib")
#   elif defined(_M_IX86)
#       pragma comment(lib, "../Libs/x86/libdeflate.lib")
#   else
#       error Unsupported architecture
#   endif
#endif

#include "Common.h"

#include "nCine/PCApplication.h"
#include "nCine/IAppEventHandler.h"
#include "nCine/Input/IInputEventHandler.h"
#include "nCine/IO/FileSystem.h"

#include "Jazz2/ContentResolver.h"
#include "Jazz2/LevelHandler.h"

#if defined(_WIN32) && !defined(WITH_QT5)
#	include <cstdlib> // for `__argc` and `__argv`
extern int __argc;
extern char** __argv;
#endif

using namespace nCine;

#if defined(ENABLE_LOG)

#if defined(_WIN32)
#	include <Utf8.h>
#elif defined(__ANDROID__)
#	include <stdarg.h>
#	include <android/log.h>
#else
#	include <cstdarg>
#endif

void __WriteLog(LogLevel level, const char* fmt, ...)
{
	/*if (level <= LogLevel::Verbose) {
		return;
	}*/

	constexpr int MaxEntryLength = 1024;
	char logEntry[MaxEntryLength];

	va_list args;
	va_start(args, fmt);
	unsigned int length = vsnprintf(logEntry, MaxEntryLength, fmt, args);
	va_end(args);

	if (length < MaxEntryLength - 2) {
		logEntry[length++] = '\n';
		logEntry[length] = '\0';
	}

#if defined(_WIN32)
	//if (IsDebuggerPresent()) {
		OutputDebugString(Utf8::ToUtf16(logEntry));
	//}
#elif defined(__ANDROID__)
	android_LogPriority priority;

	// clang-format off
	switch (level) {
		case LogLevel::Fatal:		priority = ANDROID_LOG_FATAL; break;
		case LogLevel::Error:		priority = ANDROID_LOG_ERROR; break;
		case LogLevel::Warn:		priority = ANDROID_LOG_WARN; break;
		case LogLevel::Info:		priority = ANDROID_LOG_INFO; break;
		case LogLevel::Debug:		priority = ANDROID_LOG_DEBUG; break;
		case LogLevel::Verbose:		priority = ANDROID_LOG_VERBOSE; break;
		case LogLevel::Unknown:		priority = ANDROID_LOG_UNKNOWN; break;
		default:					priority = ANDROID_LOG_UNKNOWN; break;
	}
	// clang-format on

	__android_log_write(priority, "Jazz2", logEntry_);
#else
	if (level == LogLevel::Error || level == LogLevel::Fatal) {
		fputs(logEntry, stderr);
	} else {
		fputs(logEntry, stdout);
	}
#endif
}

#endif

class GameEventHandler : public IAppEventHandler, public IInputEventHandler
{
public:
	static constexpr int DefaultWidth = 720;
	static constexpr int DefaultHeight = 405;

	void onPreInit(AppConfiguration& config) override;
	void onInit() override;
	void onFrameStart() override;
	void onPostUpdate() override;
	void onShutdown() override;
	void onRootViewportResized(int width, int height) override;

#if defined(__ANDROID__)
	void onTouchDown(const TouchEvent& event) override;
	void onTouchUp(const TouchEvent& event) override;
#endif
	void onKeyPressed(const KeyboardEvent& event) override;
	void onKeyReleased(const KeyboardEvent& event) override;
	void onMouseButtonPressed(const MouseEvent& event) override;
	void onMouseButtonReleased(const MouseEvent& event) override;
	void onJoyMappedButtonPressed(const JoyMappedButtonEvent& event) override;
	void onJoyMappedButtonReleased(const JoyMappedButtonEvent& event) override;

private:
	std::unique_ptr<Jazz2::ILevelHandler> _currentHandler;
};

void GameEventHandler::onPreInit(AppConfiguration& config)
{
#if defined(__ANDROID__)
	config.dataPath() = "asset::";
#elif defined(__EMSCRIPTEN__)
	config.dataPath() = "/";
#elif defined(NCPROJECT_DEFAULT_DATA_DIR)
	config.dataPath() = NCPROJECT_DEFAULT_DATA_DIR;
#else
	config.dataPath() = "Content/";
#endif

	config.windowTitle = "Jazz² Resurrection";
	config.resolution.Set(Jazz2::LevelHandler::DefaultWidth, Jazz2::LevelHandler::DefaultHeight);
}

void GameEventHandler::onInit()
{
#if !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
	theApplication().setAutoSuspension(false);
	theApplication().inputManager().setCursor(IInputManager::Cursor::Hidden);
#endif

#if defined(__EMSCRIPTEN__)
	// TODO: Baching in Emscripten
	theApplication().renderingSettings().batchingEnabled = false;
#endif

	// TODO
	Jazz2::PlayerType players[] = { Jazz2::PlayerType::Jazz };
	Jazz2::LevelInitialization data("unknown", "unknown", Jazz2::GameDifficulty::Normal, false, false, players, _countof(players));


	_currentHandler = std::make_unique<Jazz2::LevelHandler>(data);

}

void GameEventHandler::onFrameStart()
{
	if (_currentHandler != nullptr) {
		_currentHandler->OnBeginFrame();
	}
}

void GameEventHandler::onPostUpdate()
{
	if (_currentHandler != nullptr) {
		_currentHandler->OnEndFrame();
	}
}

void GameEventHandler::onShutdown()
{
	_currentHandler = nullptr;

	Jazz2::ContentResolver::Current().Release();
}

void GameEventHandler::onRootViewportResized(int width, int height)
{
	if (_currentHandler != nullptr) {
		_currentHandler->OnRootViewportResized(width, height);
	}
}

#if defined(__ANDROID__)
void GameEventHandler::onTouchDown(const TouchEvent& event)
{
}

void GameEventHandler::onTouchUp(const TouchEvent& event)
{
}
#endif

void GameEventHandler::onKeyPressed(const KeyboardEvent& event)
{
	if (_currentHandler != nullptr) {
		_currentHandler->OnKeyPressed(event);
	}
}

void GameEventHandler::onKeyReleased(const KeyboardEvent& event)
{
	if (_currentHandler != nullptr) {
		_currentHandler->OnKeyReleased(event);
	}
}

void GameEventHandler::onMouseButtonPressed(const MouseEvent& event)
{
}

void GameEventHandler::onMouseButtonReleased(const MouseEvent& event)
{
}

void GameEventHandler::onJoyMappedButtonPressed(const JoyMappedButtonEvent& event)
{
}

void GameEventHandler::onJoyMappedButtonReleased(const JoyMappedButtonEvent& event)
{
}

#if defined(_WIN32) && !defined(WITH_QT5)
int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR pCmdLine, int nCmdShow)
#else
int main(int argc, char** argv)
#endif
{
#if !defined(_WIN32)
#define __argc argc
#define __argv argv
#endif

	return PCApplication::start([]() -> std::unique_ptr<IAppEventHandler> {
		return std::make_unique<GameEventHandler>();
	}, __argc, __argv);
}
