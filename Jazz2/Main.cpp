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
#include "Jazz2/PreferencesCache.h"
#include "Jazz2/UI/ControlScheme.h"
#include "Jazz2/UI/Menu/MainMenu.h"

#include "Jazz2/Compatibility/JJ2Anims.h"
#include "Jazz2/Compatibility/JJ2Episode.h"
#include "Jazz2/Compatibility/JJ2Level.h"
#include "Jazz2/Compatibility/JJ2Tileset.h"
#include "Jazz2/Compatibility/EventConverter.h"

#if defined(DEATH_TARGET_WINDOWS) && !defined(WITH_QT5)
#	include <cstdlib> // for `__argc` and `__argv`
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

	constexpr int MaxEntryLength = 4 * 1024;
	char logEntry[MaxEntryLength];

#if defined(DEATH_TARGET_WINDOWS)
	logEntry[0] = '[';
	switch (level) {
		case LogLevel::Fatal:		logEntry[1] = 'F'; break;
		case LogLevel::Error:		logEntry[1] = 'E'; break;
		case LogLevel::Warn:		logEntry[1] = 'W'; break;
		case LogLevel::Info:		logEntry[1] = 'I'; break;
		default:					logEntry[1] = 'D'; break;
	}
	logEntry[2] = ']';
	logEntry[3] = ' ';

	va_list args;
	va_start(args, fmt);
	unsigned int length = vsnprintf(logEntry + 4, MaxEntryLength - 4, fmt, args) + 4;
	va_end(args);

	if (length >= MaxEntryLength - 2) {
		length = MaxEntryLength - 2;
	}

	logEntry[length++] = '\n';
	logEntry[length] = '\0';

	::OutputDebugString(Death::Utf8::ToUtf16(logEntry));
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
	constexpr char Reset[] = "\033[0m";
	constexpr char Bold[] = "\033[1m";
	constexpr char Faint[] = "\033[2m";
	constexpr char Black[] = "\033[30m";
	constexpr char BrightRed[] = "\033[91m";
	constexpr char BrightGreen[] = "\033[92m";
	constexpr char BrightYellow[] = "\033[93m";
	constexpr char BrightRedBg[] = "\033[101m";

	char logEntryWithColors[MaxEntryLength];
	logEntryWithColors[0] = '\0';
	logEntryWithColors[MaxEntryLength - 1] = '\0';

	va_list args;
	va_start(args, fmt);
	unsigned int length = vsnprintf(logEntry, MaxEntryLength, fmt, args);
	va_end(args);

	// Colorize the output
	unsigned int length2 = snprintf(logEntryWithColors, MaxEntryLength - 1, "%s", Faint);

	unsigned int logMsgFuncLength = 0;
	while (logEntry[logMsgFuncLength] != '>' && logEntry[logMsgFuncLength] != '\0') {
		logMsgFuncLength++;
	}
	logMsgFuncLength++; // Skip '>' character

	strncpy(logEntryWithColors + length2, logEntry, std::min(logMsgFuncLength, MaxEntryLength - length2 - 1));
	length2 += logMsgFuncLength;

	length2 += snprintf(logEntryWithColors + length2, MaxEntryLength - length2 - 1, "%s", Reset);

	if (level == LogLevel::Warn || level == LogLevel::Error || level == LogLevel::Fatal) {
		length2 += snprintf(logEntryWithColors + length2, MaxEntryLength - length2 - 1, "%s", Bold);
	}

	strncpy(logEntryWithColors + length2, logEntry + logMsgFuncLength, std::min(length - logMsgFuncLength, MaxEntryLength - length2 - 1));
	length2 += length - logMsgFuncLength;

	if (level == LogLevel::Warn || level == LogLevel::Error || level == LogLevel::Fatal) {
		length2 += snprintf(logEntryWithColors + length2, MaxEntryLength - length2 - 1, "%s", Reset);
	}

	if (length2 >= MaxEntryLength - 2) {
		length2 = MaxEntryLength - 2;
	}

	logEntryWithColors[length2++] = '\n';
	logEntryWithColors[length2] = '\0';

	if (level == LogLevel::Error || level == LogLevel::Fatal) {
		fputs(logEntryWithColors, stderr);
	} else {
		fputs(logEntryWithColors, stdout);
	}
#endif

#if defined(WITH_TRACY)
	uint32_t color = 0x999999;
	// clang-format off
	switch (level) {
		case LogLevel::Fatal:		color = 0xec3e40; break;
		case LogLevel::Error:		color = 0xff9b2b; break;
		case LogLevel::Warn:		color = 0xf5d800; break;
		case LogLevel::Info:		color = 0x01a46d; break;
		case LogLevel::Debug:		color = 0x377fc7; break;
		case LogLevel::Verbose:		color = 0x73a5d7; break;
		case LogLevel::Unknown:		color = 0x999999; break;
		default:					color = 0x999999; break;
	}
	// clang-format on

	TracyMessageC(logEntry, length, color);
#endif
}

#endif

enum class PendingState {
	None,
	MainMenu,
	LevelChange
};

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

	void GoToMainMenu() override;
	void ChangeLevel(Jazz2::LevelInitialization&& levelInit) override;

	bool IsVerified() override {
		return _isVerified;
	}

private:
	bool _isVerified;
	std::unique_ptr<Jazz2::IStateHandler> _currentHandler;
	PendingState _pendingState;
	std::unique_ptr<Jazz2::LevelInitialization> _pendingLevelChange;

	bool RefreshCache();
};

void GameEventHandler::onPreInit(AppConfiguration& config)
{
	Jazz2::PreferencesCache::Initialize(config);

	config.inFullscreen = Jazz2::PreferencesCache::EnableFullscreen;
	config.withVSync = Jazz2::PreferencesCache::EnableVsync;
	config.resolution.Set(Jazz2::LevelHandler::DefaultWidth, Jazz2::LevelHandler::DefaultHeight);
	config.windowTitle = "Jazz² Resurrection"_s;
}

void GameEventHandler::onInit()
{
#if !defined(DEATH_TARGET_ANDROID) && !defined(DEATH_TARGET_EMSCRIPTEN)
	theApplication().setAutoSuspension(false);
	//theApplication().inputManager().setCursor(IInputManager::Cursor::Hidden);

	String mappingsPath = fs::JoinPath("Content"_s, "gamecontrollerdb.txt"_s);
	if (fs::IsReadableFile(mappingsPath)) {
		theApplication().inputManager().addJoyMappingsFromFile(mappingsPath);
	}
#endif

	Jazz2::UI::ControlScheme::Initialize();

	_isVerified = RefreshCache();

	_pendingState = PendingState::None;
	_currentHandler = std::make_unique<Jazz2::UI::Menu::MainMenu>(this);
	Viewport::chain().clear();
	Vector2i res = theApplication().resolutionInt();
	_currentHandler->OnInitializeViewport(res.X, res.Y);
}

void GameEventHandler::onFrameStart()
{
	if (_pendingState != PendingState::None) {
		switch (_pendingState) {
			case PendingState::MainMenu:
				_currentHandler = std::make_unique<Jazz2::UI::Menu::MainMenu>(this);
				break;
			case PendingState::LevelChange:
				if (_pendingLevelChange->LevelName.empty()) {
					// Next level not specified, so show main menu
					_currentHandler = std::make_unique<Jazz2::UI::Menu::MainMenu>(this);
				} else if (_pendingLevelChange->LevelName == ":end"_s) {
					// End of episode
					// TODO: Save state and go to next episode
					_currentHandler = std::make_unique<Jazz2::UI::Menu::MainMenu>(this);
				} else if (_pendingLevelChange->LevelName == ":credits"_s) {
					// End of game
					// TODO: Save state and play ending cinematics
					_currentHandler = std::make_unique<Jazz2::UI::Menu::MainMenu>(this);
				} else {
					_currentHandler = std::make_unique<Jazz2::LevelHandler>(this, *_pendingLevelChange.get());
				}
				_pendingLevelChange = nullptr;
				break;
		}
		_pendingState = PendingState::None;

		Viewport::chain().clear();
		Vector2i res = theApplication().resolutionInt();
		_currentHandler->OnInitializeViewport(res.X, res.Y);
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

void GameEventHandler::GoToMainMenu()
{
	_pendingState = PendingState::MainMenu;
}

void GameEventHandler::ChangeLevel(Jazz2::LevelInitialization&& levelInit)
{
	// Level will be changed in the next frame
	_pendingLevelChange = std::make_unique<Jazz2::LevelInitialization>(std::move(levelInit));
	_pendingState = PendingState::LevelChange;
}

bool GameEventHandler::RefreshCache()
{
#if defined(DEATH_TARGET_EMSCRIPTEN)
	// Emscripten version doesn't support external source (yet)
	return true;
#else
	// Check cache state
	{
		auto s = fs::Open(fs::JoinPath("Cache"_s, "State"_s), FileAccessMode::Read);
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
			return true;
		}

		int64_t animsCached = s->ReadValue<int64_t>();
		int64_t animsModified = fs::LastModificationTime(fs::JoinPath("Source"_s, "Anims.j2a"_s)).Ticks;
		if (animsModified != 0 && animsCached != animsModified) {
			goto RecreateCache;
		}

		// If some events were added, recreate cache
		uint16_t eventTypeCount = s->ReadValue<uint16_t>();
		if (eventTypeCount != (uint16_t)Jazz2::EventType::Count) {
			goto RecreateCache;
		}

		// Cache is up-to-date
		LOGI("Cache is already up-to-date");
		return true;
	}

RecreateCache:
	// "Source" directory must be case in-sensitive
	String animsPath = fs::FindPathCaseInsensitive(fs::JoinPath("Source"_s, "Anims.j2a"_s));
	if (!fs::IsReadableFile(animsPath)) {
		LOGE("Cannot open \"./Source/Anims.j2a\" file! Ensure that Jazz Jackrabbit 2 files are present in \"Source\" directory.");
		return false;
	}

	JJ2Anims::Convert(animsPath, fs::JoinPath("Cache"_s, "Animations"_s), false);

	EventConverter eventConverter;

	String xmasEpisodeToken = (!fs::FindPathCaseInsensitive(fs::JoinPath("Cache"_s, "xmas99.j2e")).empty() ? "xmas99" : "xmas98");
	HashMap<String, Pair<String, String>> knownLevels = {
		{ "castle1"_s, { "prince"_s, "01"_s } },
		{ "castle1n"_s, { "prince"_s, "02"_s } },
		{ "carrot1"_s, { "prince"_s, "03"_s } },
		{ "carrot1n"_s, { "prince"_s, "04"_s } },
		{ "labrat1"_s, { "prince"_s, "05"_s } },
		{ "labrat2"_s, { "prince"_s, "06"_s } },
		{ "labrat3"_s, { "prince"_s, "bonus"_s } },

		{ "colon1"_s, { "rescue"_s, "01"_s } },
		{ "colon2"_s, { "rescue"_s, "02"_s } },
		{ "psych1"_s, { "rescue"_s, "03"_s } },
		{ "psych2"_s, { "rescue"_s, "04"_s } },
		{ "beach"_s, { "rescue"_s, "05"_s } },
		{ "beach2"_s, { "rescue"_s, "06"_s } },
		{ "psych3"_s, { "rescue"_s, "bonus"_s } },

		{ "diam1"_s, { "flash"_s, "01"_s } },
		{ "diam3"_s, { "flash"_s, "02"_s } },
		{ "tube1"_s, { "flash"_s, "03"_s } },
		{ "tube2"_s, { "flash"_s, "04"_s } },
		{ "medivo1"_s, { "flash"_s, "05"_s } },
		{ "medivo2"_s, { "flash"_s, "06"_s } },
		{ "garglair"_s, { "flash"_s, "bonus"_s } },
		{ "tube3"_s, { "flash"_s, "bonus"_s } },

		{ "jung1"_s, { "monk"_s, "01"_s } },
		{ "jung2"_s, { "monk"_s, "02"_s } },
		{ "hell"_s, { "monk"_s, "03"_s } },
		{ "hell2"_s, { "monk"_s, "04"_s } },
		{ "damn"_s, { "monk"_s, "05"_s } },
		{ "damn2"_s, { "monk"_s, "06"_s } },

		{ "share1"_s, { "share"_s, "01"_s } },
		{ "share2"_s, { "share"_s, "02"_s } },
		{ "share3"_s, { "share"_s, "03"_s } },

		{ "xmas1"_s, { xmasEpisodeToken, "01"_s } },
		{ "xmas2"_s, { xmasEpisodeToken, "02"_s } },
		{ "xmas3"_s, { xmasEpisodeToken, "03"_s } },

		{ "easter1"_s, { "secretf"_s, "01"_s } },
		{ "easter2"_s, { "secretf"_s, "02"_s } },
		{ "easter3"_s, { "secretf"_s, "03"_s } },
		{ "haunted1"_s, { "secretf"_s, "04"_s } },
		{ "haunted2"_s, { "secretf"_s, "05"_s } },
		{ "haunted3"_s, { "secretf"_s, "06"_s } },
		{ "town1"_s, { "secretf"_s, "07"_s } },
		{ "town2"_s, { "secretf"_s, "08"_s } },
		{ "town3"_s, { "secretf"_s, "09"_s } },

		// Special names
		{ "endepis"_s, { { }, ":end"_s } },
		{ "ending"_s, { { }, ":credits"_s } }
	};

	auto LevelTokenConversion = [&knownLevels](String levelToken) -> JJ2Level::LevelToken {
		lowercaseInPlace(levelToken);

		auto it = knownLevels.find(levelToken);
		if (it != knownLevels.end()) {
			if (it->second.second().empty()) {
				return { it->second.first(), levelToken };
			}
			return { it->second.first(), (it->second.second()[0] == ':' ? it->second.second() : (it->second.second() + "_"_s + levelToken)) };
		}
		return { { }, levelToken };
	};

	auto EpisodeNameConversion = [](JJ2Episode* episode) -> String {
		if (episode->Name == "share"_s && episode->DisplayName == "#Shareware@Levels"_s) {
			return "Shareware Demo"_s;
		} else if (episode->Name == "xmas98"_s && episode->DisplayName == "#Xmas 98@Levels"_s) {
			return "Holiday Hare '98"_s;
		} else if (episode->Name == "xmas99"_s && episode->DisplayName == "#Xmas 99@Levels"_s) {
			return "The Christmas Chronicles"_s;
		} else if (episode->Name == "secretf"_s && episode->DisplayName == "#Secret@Files"_s) {
			return "The Secret Files"_s;
		} else if (episode->Name == "hh17"_s && episode->DisplayName == "Holiday Hare 17"_s) {
			return "Holiday Hare '17"_s;
		} else if (episode->Name == "hh18"_s && episode->DisplayName == "Holiday Hare 18"_s) {
			return "Holiday Hare '18"_s;
		} else {
			// TODO: Strip formatting - @ is new line, # is random color
			return episode->DisplayName;
		}
	};
	
	auto EpisodePrevNext = [](JJ2Episode* episode) -> Pair<String, String> {
		if (episode->Name == "prince"_s) {
			return { { }, "rescue"_s };
		} else if (episode->Name == "rescue"_s) {
			return { "prince"_s, "flash"_s };
		} else if (episode->Name == "flash"_s) {
			return { "rescue"_s, "monk"_s };
		} else if (episode->Name == "monk"_s) {
			return { "flash"_s, { } };
		} else {
			return { { }, { } };
		}
	};

	String episodesPath = fs::JoinPath("Cache"_s, "Episodes"_s);

	fs::Directory dir("Source"_s, fs::EnumerationOptions::SkipDirectories);
	while (true) {
		StringView item = dir.GetNext();
		if (item == nullptr) {
			break;
		}

		if (fs::HasExtension(item, "j2e"_s)) {
			JJ2Episode episode;
			episode.Open(item);
			if (episode.Name == "home"_s) {
				continue;
			}

			String fullPath = fs::JoinPath(episodesPath, episode.Name + ".j2e"_s);
			episode.Convert(fullPath, LevelTokenConversion, EpisodeNameConversion, EpisodePrevNext);
		} else if (fs::HasExtension(item, "j2l"_s)) {
			String levelName = fs::GetFileName(item);
			if (levelName.findOr("-MLLE-Data-"_s, levelName.end()) != levelName.end()) {
				LOGI_X("Level \"%s\" skipped (MLLE extra layers).", item);
			} else {
				JJ2Level level;
				level.Open(item, false);

				String fullPath;
				auto it = knownLevels.find(level.LevelName);
				if (it != knownLevels.end()) {
					if (it->second.second().empty()) {
						fullPath = fs::JoinPath({ episodesPath, it->second.first(), level.LevelName + ".j2l"_s });
					} else {
						fullPath = fs::JoinPath({ episodesPath, it->second.first(), it->second.second() + "_"_s + level.LevelName + ".j2l"_s });
					}
				} else {
					fullPath = fs::JoinPath({ episodesPath, "unknown"_s, level.LevelName + ".j2l"_s });
				}

				fs::CreateDirectories(fs::GetDirectoryName(fullPath));
				level.Convert(fullPath, eventConverter, LevelTokenConversion);
			}
		} else if (fs::HasExtension(item, "j2t"_s)) {
			fs::CreateDirectories(fs::JoinPath("Cache"_s, "Tilesets"_s));

			String tilesetName = fs::GetFileName(item);
			lowercaseInPlace(tilesetName);

			JJ2Tileset tileset;
			tileset.Open(item, false);
			tileset.Convert(fs::JoinPath({ "Cache"_s, "Tilesets"_s, tilesetName }));
		}
	}

	auto s = fs::Open(fs::JoinPath("Cache"_s, "State"_s), FileAccessMode::Write);

	s->WriteValue<uint32_t>(0x2063324a);	// Signature
	s->WriteValue<uint16_t>(JJ2Anims::CacheVersion);
	s->WriteValue<uint8_t>(0x00);			// Flags
	int64_t animsModified = fs::LastModificationTime(fs::JoinPath("Source"_s, "Anims.j2a"_s)).Ticks;
	s->WriteValue<int64_t>(animsModified);
	s->WriteValue<uint16_t>((uint16_t)Jazz2::EventType::Count);

	LOGI("Cache was recreated");
	return true;
#endif
}

#if defined(DEATH_TARGET_WINDOWS) && !defined(WITH_QT5)
int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR pCmdLine, int nCmdShow)
{
	return PCApplication::start([]() -> std::unique_ptr<IAppEventHandler> {
		return std::make_unique<GameEventHandler>();
	}, __argc, __wargv);
}
#else
int main(int argc, char** argv)
{
	return PCApplication::start([]() -> std::unique_ptr<IAppEventHandler> {
		return std::make_unique<GameEventHandler>();
	}, argc, argv);
}
#endif