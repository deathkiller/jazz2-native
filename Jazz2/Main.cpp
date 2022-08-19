#include "Common.h"

#if defined(DEATH_TARGET_WINDOWS) && !defined(CMAKE_BUILD)
#   if defined(_M_X64)
#       pragma comment(lib, "../Libs/x64/libdeflate.lib")
#   elif defined(_M_IX86)
#       pragma comment(lib, "../Libs/x86/libdeflate.lib")
#   else
#       error Unsupported architecture
#   endif
#endif

#include "nCine/PCApplication.h"
#include "nCine/IAppEventHandler.h"
#include "nCine/Input/IInputEventHandler.h"
#include "nCine/IO/FileSystem.h"
#include "nCine/tracy.h"

#include "Jazz2/IRootController.h"
#include "Jazz2/ContentResolver.h"
#include "Jazz2/LevelHandler.h"
#include "Jazz2/UI/Menu/MainMenu.h"

#include "Jazz2/Compatibility/JJ2Anims.h"
#include "Jazz2/Compatibility/JJ2Level.h"
#include "Jazz2/Compatibility/JJ2Tileset.h"
#include "Jazz2/Compatibility/EventConverter.h"

#if defined(DEATH_TARGET_WINDOWS) && !defined(WITH_QT5)
#	include <cstdlib> // for `__argc` and `__argv`
extern int __argc;
extern char** __argv;
#endif

using namespace nCine;
using namespace Jazz2::Compatibility;

#if defined(ENABLE_LOG)

#if defined(DEATH_TARGET_WINDOWS)
#	include <Utf8.h>
#elif defined(DEATH_TARGET_ANDROID)
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

#if defined(DEATH_TARGET_WINDOWS)
	//if (IsDebuggerPresent()) {
		OutputDebugString(Death::Utf8::ToUtf16(logEntry));
	//}
#elif defined(DEATH_TARGET_ANDROID)
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

	__android_log_write(priority, "Jazz2", logEntry);
#else
	if (level == LogLevel::Error || level == LogLevel::Fatal) {
		fputs(logEntry, stderr);
	} else {
		fputs(logEntry, stdout);
	}
#endif

#ifdef WITH_TRACY
	if (levelInt >= consoleLevelInt || levelInt >= fileLevelInt) {
		uint32_t color = 0x999999;
		// clang-format off
		switch (level) {
			case LogLevel::FATAL:		color = 0xec3e40; break;
			case LogLevel::ERROR:		color = 0xff9b2b; break;
			case LogLevel::WARN:		color = 0xf5d800; break;
			case LogLevel::INFO:		color = 0x01a46d; break;
			case LogLevel::DEBUG:		color = 0x377fc7; break;
			case LogLevel::VERBOSE:		color = 0x73a5d7; break;
			case LogLevel::UNKNOWN:		color = 0x999999; break;
			default:					color = 0x999999; break;
		}
		// clang-format on

		TracyMessageC(logEntry, length, color);
	}
#endif
}

#endif

class GameEventHandler : public IAppEventHandler, public IInputEventHandler, public Jazz2::IRootController
{
public:
	static constexpr int DefaultWidth = 720;
	static constexpr int DefaultHeight = 405;

	void onPreInit(AppConfiguration& config) override;
	void onInit() override;
	void onFrameStart() override;
	void onPostUpdate() override;
	void onShutdown() override;
	void onResizeWindow(int width, int height) override;
	void onTouchEvent(const TouchEvent& event) override;

	void ChangeLevel(Jazz2::LevelInitialization&& levelInit) override;

private:
	std::unique_ptr<Jazz2::IStateHandler> _currentHandler;
	std::unique_ptr<Jazz2::LevelInitialization> _pendingLevelChange;

	void RefreshCache();
};

void GameEventHandler::onPreInit(AppConfiguration& config)
{
	//config.withVSync = false;
	config.windowTitle = "Jazz² Resurrection"_s;
	config.resolution.Set(Jazz2::LevelHandler::DefaultWidth, Jazz2::LevelHandler::DefaultHeight);
}

void GameEventHandler::onInit()
{
#if !defined(DEATH_TARGET_ANDROID) && !defined(DEATH_TARGET_EMSCRIPTEN)
	theApplication().setAutoSuspension(false);
	//theApplication().inputManager().setCursor(IInputManager::Cursor::Hidden);
#endif

#if !defined(DEATH_TARGET_EMSCRIPTEN)
	//theApplication().inputManager().addJoyMappingsFromFile(fs::joinPath({ "Content"_s, "gamecontrollerdb.txt"_s }));
#endif

	RefreshCache();

	_currentHandler = std::make_unique<Jazz2::UI::Menu::MainMenu>(this);
	Viewport::chain().clear();
	Vector2i res = theApplication().resolutionInt();
	_currentHandler->OnInitializeViewport(res.X, res.Y);
}

void GameEventHandler::onFrameStart()
{
	if (_pendingLevelChange != nullptr) {
		_currentHandler = std::make_unique<Jazz2::LevelHandler>(this, *_pendingLevelChange.get());

		Viewport::chain().clear();
		Vector2i res = theApplication().resolutionInt();
		_currentHandler->OnInitializeViewport(res.X, res.Y);

		_pendingLevelChange = nullptr;
	}

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

void GameEventHandler::onResizeWindow(int width, int height)
{
	// Resolution was changed, all viewports have to be recreated
	Viewport::chain().clear();

	if (_currentHandler != nullptr) {
		_currentHandler->OnInitializeViewport(width, height);
	}
}

void GameEventHandler::onTouchEvent(const TouchEvent& event)
{
	if (_currentHandler != nullptr) {
		_currentHandler->OnTouchEvent(event);
	}
}

void GameEventHandler::ChangeLevel(Jazz2::LevelInitialization&& levelInit)
{
	// Level will be changed in the next frame
	_pendingLevelChange = std::make_unique<Jazz2::LevelInitialization>(std::move(levelInit));
}

void GameEventHandler::RefreshCache()
{
	// Check cache state
	{
		auto s = IFileStream::createFileHandle(fs::joinPath({ "Cache"_s, "State"_s }));
		s->setExitOnFailToOpen(false);
		s->Open(FileAccessMode::Read);

		if (s->GetSize() < 7) {
			goto RecreateCache;
		}

		uint32_t signature = s->ReadValue<uint32_t>();
		uint16_t version = s->ReadValue<uint16_t>();
		if (signature != 0x2063324a || version != JJ2Anims::CacheVersion) {
			goto RecreateCache;
		}

		uint8_t flags = s->ReadValue<uint8_t>();
		if ((flags & 0x01) == 0x01) {
			// Don't overwrite cache
			LOGI("Cache is protected");
			return;
		}

		int64_t animsCached = s->ReadValue<int64_t>();
		int64_t animsModified = fs::lastModificationTime(fs::joinPath({ "Source"_s, "Anims.j2a"_s })).Ticks;
		if (animsModified != 0 && animsCached != animsModified) {
			goto RecreateCache;
		}

		// Cache is up-to-date
		LOGI("Cache is already up-to-date");
		return;
	}

RecreateCache:
	{
		JJ2Anims::Convert(fs::joinPath({ "Source"_s, "Anims.j2a"_s }), fs::joinPath({ "Cache"_s, "Animations"_s }), false);

		EventConverter eventConverter;

		fs::Directory dir("Source"_s);
		while (true) {
			StringView item = dir.readNext();
			if (item == nullptr) {
				break;
			}

			if (fs::hasExtension(item, "j2l")) {
				// TODO: Check only filename
				String levelName = fs::GetFileName(item);
				if (levelName.findOr("-MLLE-Data-", levelName.end()) != levelName.end()) {
					LOGI_X("Level \"%s\" skipped (MLLE extra layers).", item);
				} else {
					fs::createDir(fs::joinPath({ "Cache"_s, "Episodes"_s, "unknown"_s }));

					JJ2Level level;
					level.Open(item, false);
					level.Convert(fs::joinPath({ "Cache"_s, "Episodes"_s, "unknown"_s, levelName }), eventConverter, nullptr);
				}
			} else if (fs::hasExtension(item, "j2t")) {
				fs::createDir(fs::joinPath({ "Cache"_s, "Tilesets"_s }));

				String tilesetName = fs::GetFileName(item);

				JJ2Tileset tileset;
				tileset.Open(item, false);
				tileset.Convert(fs::joinPath({ "Cache"_s, "Tilesets"_s, tilesetName }));
			}
		}


		auto s = IFileStream::createFileHandle(fs::joinPath({ "Cache"_s, "State"_s }));
		s->setExitOnFailToOpen(false);
		s->Open(FileAccessMode::Write);

		s->WriteValue<uint32_t>(0x2063324a);	// Signature
		s->WriteValue<uint16_t>(JJ2Anims::CacheVersion);
		s->WriteValue<uint8_t>(0x00);			// Flags
		int64_t animsModified = fs::lastModificationTime(fs::joinPath({ "Source"_s, "Anims.j2a"_s })).Ticks;
		s->WriteValue<int64_t>(animsModified);

		LOGI("Cache was recreated");
	}
}

#if defined(DEATH_TARGET_WINDOWS) && !defined(WITH_QT5)
int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR pCmdLine, int nCmdShow)
#else
int main(int argc, char** argv)
#endif
{
#if !defined(DEATH_TARGET_WINDOWS)
#	define __argc argc
#	define __argv argv
#endif

	return PCApplication::start([]() -> std::unique_ptr<IAppEventHandler> {
		return std::make_unique<GameEventHandler>();
	}, __argc, __argv);
}
