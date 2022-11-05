#define _WINSOCKAPI_	// To prevent include "winsock.h"
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

#if defined(DEATH_TARGET_ANDROID)
#	include "nCine/Android/AndroidApplication.h"
#elif defined(DEATH_TARGET_WINDOWS_RT)
#	include "nCine/Uwp/UwpApplication.h"
#else
#	include "nCine/PCApplication.h"
#	if defined(DEATH_TARGET_UNIX)
#		include <unistd.h>
#	endif
#endif

#include "nCine/IAppEventHandler.h"
#include "nCine/Input/IInputEventHandler.h"
#include "nCine/IO/FileSystem.h"
#include "nCine/Threading/Thread.h"
#include "nCine/tracy.h"

#include "Jazz2/IRootController.h"
#include "Jazz2/ContentResolver.h"
#include "Jazz2/LevelHandler.h"
#include "Jazz2/PreferencesCache.h"
#include "Jazz2/UI/Cinematics.h"
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

#include <Environment.h>
#include <HttpRequest.h>

using namespace nCine;
using namespace Jazz2;
using namespace Jazz2::UI;

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

	va_list args;
	va_start(args, fmt);
	unsigned int length = vsnprintf(logEntry, MaxEntryLength, fmt, args);
	va_end(args);

	__android_log_write(priority, "jazz2", logEntry);
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
	while (logEntry[logMsgFuncLength] != '\0' && (logMsgFuncLength == 0 || !(logEntry[logMsgFuncLength - 1] == '-' && logEntry[logMsgFuncLength] == '>'))) {
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
	MainMenuAfterIntro,
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

	void onKeyPressed(const KeyboardEvent& event) override;
	void onKeyReleased(const KeyboardEvent& event) override;
	void onTouchEvent(const TouchEvent& event) override;

	void GoToMainMenu(bool afterIntro) override;
	void ChangeLevel(LevelInitialization&& levelInit) override;

	bool IsVerified() const override {
		return ((_flags & Flags::IsVerified) == Flags::IsVerified);
	}

	bool IsPlayable() const override {
		return ((_flags & Flags::IsPlayable) == Flags::IsPlayable);
	}

	const char* GetNewestVersion() const override {
		return (_newestVersion[0] != '\0' ? _newestVersion : nullptr);
	}

#if !defined(DEATH_TARGET_EMSCRIPTEN)
	void RefreshCacheLevels() override;
#else
	void RefreshCacheLevels() override { }
#endif

private:
	enum class Flags {
		None = 0x00,

		IsVerified = 0x01,
		IsPlayable = 0x02
	};

	DEFINE_PRIVATE_ENUM_OPERATORS(Flags);

	Flags _flags;
	std::unique_ptr<Jazz2::IStateHandler> _currentHandler;
	PendingState _pendingState;
	std::unique_ptr<LevelInitialization> _pendingLevelChange;
	char _newestVersion[8];

#if !defined(DEATH_TARGET_EMSCRIPTEN)
	void RefreshCache();
	void CheckUpdates();
#endif
	static void SaveEpisodeEnd(const std::unique_ptr<LevelInitialization>& pendingLevelChange);
	static void SaveEpisodeContinue(const std::unique_ptr<LevelInitialization>& pendingLevelChange);
};

void GameEventHandler::onPreInit(AppConfiguration& config)
{
	PreferencesCache::Initialize(config);

	config.windowTitle = "Jazz² Resurrection"_s;
	config.withVSync = PreferencesCache::EnableVsync;
	config.resolution.Set(LevelHandler::DefaultWidth, LevelHandler::DefaultHeight);
}

void GameEventHandler::onInit()
{
	_flags = Flags::None;
	_pendingState = PendingState::None;

	std::memset(_newestVersion, 0, sizeof(_newestVersion));

	auto& resolver = ContentResolver::Current();
	
#if defined(DEATH_TARGET_ANDROID)
	// Set working directory to external storage on Android
	StringView externalPath = static_cast<AndroidApplication&>(theApplication()).externalDataPath();
	if (!externalPath.empty()) {
		if (fs::SetWorkingDirectory(externalPath)) {
			LOGI_X("External storage path: %s", externalPath.data());
		} else {
			LOGE_X("Cannot set external storage path \"%s\"", externalPath.data());
		}
	} else {
		LOGE("Cannot get external storage path");
	}
#elif !defined(DEATH_TARGET_EMSCRIPTEN) && !defined(DEATH_TARGET_IOS)
	theApplication().setAutoSuspension(false);

	if (PreferencesCache::EnableFullscreen) {
		theApplication().gfxDevice().setResolution(true);
		theApplication().inputManager().setCursor(IInputManager::Cursor::Hidden);
	}

	String mappingsPath = fs::JoinPath(resolver.GetContentPath(), "gamecontrollerdb.txt"_s);
	if (fs::IsReadableFile(mappingsPath)) {
		theApplication().inputManager().addJoyMappingsFromFile(mappingsPath);
	}
#endif

	resolver.CompileShaders();

#if defined(WITH_THREADS) && !defined(DEATH_TARGET_EMSCRIPTEN)
	// If threading support is enabled, refresh cache during intro cinematics and don't allow skip until it's completed
	Thread thread([](void* arg) {
		auto handler = reinterpret_cast<GameEventHandler*>(arg);
		handler->RefreshCache();
		handler->CheckUpdates();
	}, this);

	_currentHandler = std::make_unique<Cinematics>(this, "intro"_s, [thread](IRootController* root, bool endOfStream) mutable {
		if (!root->IsVerified()) {
			return false;
		}
		
		thread.Join();
		root->GoToMainMenu(endOfStream);
		return true;
	});
#else
	// Building without threading support is not recommended, so it can look ugly
#	if defined(DEATH_TARGET_EMSCRIPTEN)
	// All required files are already included in Emscripten version, so nothing is verified
	_flags = Flags::IsVerified | Flags::IsPlayable;
#	else
	RefreshCache();
	CheckUpdates();
#	endif

	_currentHandler = std::make_unique<Cinematics>(this, "intro"_s, [](IRootController* root, bool endOfStream) {
		root->GoToMainMenu(endOfStream);
		return true;
	});
#endif

	Viewport::chain().clear();
	Vector2i res = theApplication().resolutionInt();
	_currentHandler->OnInitializeViewport(res.X, res.Y);
}

void GameEventHandler::onFrameStart()
{
	if (_pendingState != PendingState::None) {
		switch (_pendingState) {
			case PendingState::MainMenu:
				_currentHandler = std::make_unique<Menu::MainMenu>(this, false);
				break;
			case PendingState::MainMenuAfterIntro:
				_currentHandler = std::make_unique<Menu::MainMenu>(this, true);
				break;
			case PendingState::LevelChange:
				if (_pendingLevelChange->LevelName.empty()) {
					// Next level not specified, so show main menu
					_currentHandler = std::make_unique<Menu::MainMenu>(this, false);
				} else if (_pendingLevelChange->LevelName == ":end"_s) {
					// End of episode
					SaveEpisodeEnd(_pendingLevelChange);

					PreferencesCache::RemoveEpisodeContinue(_pendingLevelChange->LastEpisodeName);

					std::optional<Episode> lastEpisode = ContentResolver::Current().GetEpisode(_pendingLevelChange->LastEpisodeName);
					if (lastEpisode.has_value()) {
						// Redirect to next episode
						std::optional<Episode> nextEpisode = ContentResolver::Current().GetEpisode(lastEpisode->NextEpisode);
						if (nextEpisode.has_value()) {
							_pendingLevelChange->EpisodeName = lastEpisode->NextEpisode;
							_pendingLevelChange->LevelName = nextEpisode->FirstLevel;
						}
					}

					if (_pendingLevelChange->LevelName != ":end"_s) {
						_currentHandler = std::make_unique<LevelHandler>(this, *_pendingLevelChange.get());
					} else {
						_currentHandler = std::make_unique<Menu::MainMenu>(this, false);
					}
				} else if (_pendingLevelChange->LevelName == ":credits"_s) {
					// End of game
					SaveEpisodeEnd(_pendingLevelChange);

					PreferencesCache::RemoveEpisodeContinue(_pendingLevelChange->LastEpisodeName);

					_currentHandler = std::make_unique<Cinematics>(this, "ending"_s, [](IRootController* root, bool endOfStream) {
						root->GoToMainMenu(false);
						return true;
					});
				} else {
					SaveEpisodeContinue(_pendingLevelChange);

#if defined(SHAREWARE_DEMO_ONLY)
					// Check if specified episode is unlocked, used only if compiled with SHAREWARE_DEMO_ONLY
					bool hasEpisode = true;
					if (_pendingLevelChange->EpisodeName == "prince"_s && (PreferencesCache::UnlockedEpisodes & UnlockableEpisodes::FormerlyAPrince) == UnlockableEpisodes::None) hasEpisode = false;
					if (_pendingLevelChange->EpisodeName == "rescue"_s && (PreferencesCache::UnlockedEpisodes & UnlockableEpisodes::JazzInTime) == UnlockableEpisodes::None) hasEpisode = false;
					if (_pendingLevelChange->EpisodeName == "flash"_s && (PreferencesCache::UnlockedEpisodes & UnlockableEpisodes::Flashback) == UnlockableEpisodes::None) hasEpisode = false;
					if (_pendingLevelChange->EpisodeName == "monk"_s && (PreferencesCache::UnlockedEpisodes & UnlockableEpisodes::FunkyMonkeys) == UnlockableEpisodes::None) hasEpisode = false;
					if ((_pendingLevelChange->EpisodeName == "xmas98"_s || _pendingLevelChange->EpisodeName == "xmas99"_s) && (PreferencesCache::UnlockedEpisodes & UnlockableEpisodes::HolidayHare98) == UnlockableEpisodes::None) hasEpisode = false;
					if (_pendingLevelChange->EpisodeName == "secretf"_s && (PreferencesCache::UnlockedEpisodes & UnlockableEpisodes::TheSecretFiles) == UnlockableEpisodes::None) hasEpisode = false;

					if (!hasEpisode) {
						_currentHandler = std::make_unique<Menu::MainMenu>(this, false);
					}
#endif

					_currentHandler = std::make_unique<LevelHandler>(this, *_pendingLevelChange.get());
				}
				_pendingLevelChange = nullptr;
				break;
		}
		_pendingState = PendingState::None;

		Viewport::chain().clear();
		Vector2i res = theApplication().resolutionInt();
		_currentHandler->OnInitializeViewport(res.X, res.Y);
	}

	_currentHandler->OnBeginFrame();
}

void GameEventHandler::onPostUpdate()
{
	_currentHandler->OnEndFrame();
}

void GameEventHandler::onShutdown()
{
	_currentHandler = nullptr;

	ContentResolver::Current().Release();
}

void GameEventHandler::onResizeWindow(int width, int height)
{
	// Resolution was changed, all viewports have to be recreated
	Viewport::chain().clear();

	if (_currentHandler != nullptr) {
		_currentHandler->OnInitializeViewport(width, height);
	}

	PreferencesCache::EnableFullscreen = theApplication().gfxDevice().isFullscreen();
}

void GameEventHandler::onKeyPressed(const KeyboardEvent& event)
{
#if !defined(DEATH_TARGET_ANDROID) && !defined(DEATH_TARGET_EMSCRIPTEN) && !defined(DEATH_TARGET_IOS)
	// Allow Alt+Enter to switch fullscreen
	if (event.sym == KeySym::RETURN && (event.mod & KeyMod::MASK) == KeyMod::LALT) {
		PreferencesCache::EnableFullscreen = !PreferencesCache::EnableFullscreen;
		if (PreferencesCache::EnableFullscreen) {
			theApplication().gfxDevice().setResolution(true);
			theApplication().inputManager().setCursor(IInputManager::Cursor::Hidden);
		} else {
			theApplication().gfxDevice().setResolution(false);
			theApplication().inputManager().setCursor(IInputManager::Cursor::Arrow);
		}
		return;
	}
#endif

	_currentHandler->OnKeyPressed(event);
}

void GameEventHandler::onKeyReleased(const KeyboardEvent& event)
{
	_currentHandler->OnKeyReleased(event);
}

void GameEventHandler::onTouchEvent(const TouchEvent& event)
{
	_currentHandler->OnTouchEvent(event);
}

void GameEventHandler::GoToMainMenu(bool afterIntro)
{
	_pendingState = (afterIntro ? PendingState::MainMenuAfterIntro : PendingState::MainMenu);
}

void GameEventHandler::ChangeLevel(LevelInitialization&& levelInit)
{
	// Level will be changed in the next frame
	_pendingLevelChange = std::make_unique<LevelInitialization>(std::move(levelInit));
	_pendingState = PendingState::LevelChange;
}

#if !defined(DEATH_TARGET_EMSCRIPTEN)
void GameEventHandler::RefreshCache()
{
	if (PreferencesCache::BypassCache) {
		LOGI("Cache is bypassed by command-line parameter");
		_flags = Flags::IsVerified | Flags::IsPlayable;
		return;
	}

	auto& resolver = ContentResolver::Current();

	// Check cache state
	{
		auto s = fs::Open(fs::JoinPath({ resolver.GetCachePath(), "Animations"_s, "cache.index"_s }), FileAccessMode::Read);
		if (s->GetSize() < 16) {
			goto RecreateCache;
		}

		uint64_t signature = s->ReadValue<uint64_t>();
		uint8_t fileType = s->ReadValue<uint8_t>();
		uint16_t version = s->ReadValue<uint16_t>();
		if (signature != 0x2095A59FF0BFBBEF || fileType != ContentResolver::CacheIndexFile || version != Compatibility::JJ2Anims::CacheVersion) {
			goto RecreateCache;
		}

		uint8_t flags = s->ReadValue<uint8_t>();
		if ((flags & 0x01) == 0x01) {
			// Don't overwrite cache
			LOGI("Cache is protected");
			_flags = Flags::IsVerified | Flags::IsPlayable;
			return;
		}

		String animsPath = fs::FindPathCaseInsensitive(fs::JoinPath(resolver.GetSourcePath(), "Anims.j2a"_s));
		if (!fs::IsReadableFile(animsPath)) {
			animsPath = fs::FindPathCaseInsensitive(fs::JoinPath(resolver.GetSourcePath(), "AnimsSw.j2a"_s));
		}
		int64_t animsCached = s->ReadValue<int64_t>();
		int64_t animsModified = fs::LastModificationTime(animsPath).Ticks;
		if (animsModified != 0 && animsCached != animsModified) {
			goto RecreateCache;
		}

		// If some events were added, recreate cache
		uint16_t eventTypeCount = s->ReadValue<uint16_t>();
		if (eventTypeCount != (uint16_t)EventType::Count) {
			goto RecreateCache;
		}

		// Cache is up-to-date
		LOGI("Cache is already up-to-date");
		_flags = Flags::IsVerified | Flags::IsPlayable;
		return;
	}

RecreateCache:
	// "Source" directory must be case in-sensitive
	String animsPath = fs::FindPathCaseInsensitive(fs::JoinPath(resolver.GetSourcePath(), "Anims.j2a"_s));
	if (!fs::IsReadableFile(animsPath)) {
		animsPath = fs::FindPathCaseInsensitive(fs::JoinPath(resolver.GetSourcePath(), "AnimsSw.j2a"_s));
		if (!fs::IsReadableFile(animsPath)) {
			LOGE_X("Cannot open \".%sSource%sAnims.j2a\" file! Ensure that Jazz Jackrabbit 2 files are present in \"%s\" directory.", fs::PathSeparator, fs::PathSeparator, resolver.GetSourcePath().data());
			_flags = Flags::IsVerified;
			return;
		}
	}

	String animationsPath = fs::JoinPath(resolver.GetCachePath(), "Animations"_s);
	fs::RemoveDirectoryRecursive(animationsPath);
	Compatibility::JJ2Anims::Convert(animsPath, animationsPath, false);

	RefreshCacheLevels();

	// Create cache index
	auto so = fs::Open(fs::JoinPath({ resolver.GetCachePath(), "Animations"_s, "cache.index"_s }), FileAccessMode::Write);

	so->WriteValue<uint64_t>(0x2095A59FF0BFBBEF);	// Signature
	so->WriteValue<uint8_t>(ContentResolver::CacheIndexFile);
	so->WriteValue<uint16_t>(Compatibility::JJ2Anims::CacheVersion);
	so->WriteValue<uint8_t>(0x00);					// Flags
	int64_t animsModified = fs::LastModificationTime(animsPath).Ticks;
	so->WriteValue<int64_t>(animsModified);
	so->WriteValue<uint16_t>((uint16_t)EventType::Count);

	LOGI("Cache was recreated");
	_flags = Flags::IsVerified | Flags::IsPlayable;
}

void GameEventHandler::RefreshCacheLevels()
{
	auto& resolver = ContentResolver::Current();

	Compatibility::EventConverter eventConverter;

	bool hasChristmasChronicles = fs::IsReadableFile(fs::FindPathCaseInsensitive(fs::JoinPath(resolver.GetSourcePath(), "xmas99.j2e"_s)));
	String xmasEpisodeToken = (hasChristmasChronicles ? "xmas99"_s : "xmas98"_s);
	const HashMap<String, Pair<String, String>> knownLevels = {
		{ "trainer"_s, { "prince"_s, { } } },
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

	auto LevelTokenConversion = [&knownLevels](MutableStringView& levelToken) -> Compatibility::JJ2Level::LevelToken {
		auto it = knownLevels.find(levelToken);
		if (it != knownLevels.end()) {
			if (it->second.second().empty()) {
				return { it->second.first(), levelToken };
			}
			return { it->second.first(), (it->second.second()[0] == ':' ? it->second.second() : (it->second.second() + "_"_s + levelToken)) };
		}
		return { { }, levelToken };
	};

	auto EpisodeNameConversion = [](Compatibility::JJ2Episode* episode) -> String {
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
			// Strip formatting - @ is new line, # is random color
			int nameLength = 0;
			for (auto& c : episode->DisplayName) {
				if (c != '#') {
					nameLength++;
				}
			}

			String name(NoInit, nameLength);
			int i = 0;
			for (auto& c : episode->DisplayName) {
				if (c == '@') {
					name[i++] = ' ';
				} else if (c != '#') {
					name[i++] = c;
				}
			}

			return name;
		}
	};

	auto EpisodePrevNext = [](Compatibility::JJ2Episode* episode) -> Pair<String, String> {
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

	String episodesPath = fs::JoinPath(resolver.GetCachePath(), "Episodes"_s);
	fs::RemoveDirectoryRecursive(episodesPath);
	fs::CreateDirectories(episodesPath);

	HashMap<String, bool> usedTilesets;

	fs::Directory dir(fs::FindPathCaseInsensitive(resolver.GetSourcePath()), fs::EnumerationOptions::SkipDirectories);
	while (true) {
		StringView item = dir.GetNext();
		if (item == nullptr) {
			break;
		}

		auto extension = fs::GetExtension(item);
		if (extension == "j2e"_s || extension == "j2pe"_s) {
			// Episode
			Compatibility::JJ2Episode episode;
			episode.Open(item);
			if (episode.Name == "home"_s || (hasChristmasChronicles && episode.Name == "xmas98"_s)) {
				continue;
			}

			String fullPath = fs::JoinPath(episodesPath, episode.Name + ".j2e"_s);
			episode.Convert(fullPath, LevelTokenConversion, EpisodeNameConversion, EpisodePrevNext);
		} else if (extension == "j2l"_s) {
			// Level
			String levelName = fs::GetFileName(item);
			if (levelName.findOr("-MLLE-Data-"_s, levelName.end()) != levelName.end()) {
				LOGI_X("Level \"%s\" skipped (MLLE extra layers).", item);
			} else {
				Compatibility::JJ2Level level;
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

				usedTilesets.emplace(level.Tileset, true);

				// Also copy level script file if exists
				StringView foundDot = item.findLastOr('.', item.end());
				String scriptPath = item.prefix(foundDot.begin()) + ".j2as"_s;
				auto adjustedPath = fs::FindPathCaseInsensitive(scriptPath);
				if (fs::IsReadableFile(adjustedPath)) {
					foundDot = fullPath.findLastOr('.', fullPath.end());
					fs::Copy(adjustedPath, fullPath.prefix(foundDot.begin()) + ".j2as"_s);
				}
			}
		}
	}

	// Convert only used tilesets
	String tilesetsPath = fs::JoinPath(resolver.GetCachePath(), "Tilesets"_s);
	fs::RemoveDirectoryRecursive(tilesetsPath);
	fs::CreateDirectories(tilesetsPath);

	for (auto& pair : usedTilesets) {
		String tilesetPath = fs::JoinPath(resolver.GetSourcePath(), pair.first + ".j2t"_s);
		auto adjustedPath = fs::FindPathCaseInsensitive(tilesetPath);
		if (fs::IsReadableFile(adjustedPath)) {
			Compatibility::JJ2Tileset tileset;
			tileset.Open(adjustedPath, false);
			tileset.Convert(fs::JoinPath({ tilesetsPath, pair.first + ".j2t"_s }));
		}
	}
}

void GameEventHandler::CheckUpdates()
{
#if !NCINE_DEBUG
#if defined(DEATH_TARGET_ANDROID)
	constexpr char DeviceDesc[] = "|Android|"; int DeviceDescLength = sizeof(DeviceDesc) - 1;
#elif defined(DEATH_TARGET_APPLE)
	char DeviceDesc[64]; int DeviceDescLength;
	if (::gethostname(DeviceDesc, _countof(DeviceDesc) - (sizeof("|macOS|") - 1)) == 0) {
		DeviceDescLength = strlen(DeviceDesc);
	} else {
		DeviceDescLength = 0;
	}
	std::memcpy(DeviceDesc + DeviceDescLength, "|macOS|", sizeof("|macOS|") - 1);
	DeviceDescLength += sizeof("|macOS|") - 1;
#elif defined(DEATH_TARGET_UNIX)
	char DeviceDesc[64]; int DeviceDescLength;
	if (::gethostname(DeviceDesc, _countof(DeviceDesc) - (sizeof("|Unix|") - 1)) == 0) {
		DeviceDescLength = strlen(DeviceDesc);
	} else {
		DeviceDescLength = 0;
	}
	std::memcpy(DeviceDesc + DeviceDescLength, "|Unix|", sizeof("|Unix|") - 1);
	DeviceDescLength += sizeof("|Unix|") - 1;
#elif defined(DEATH_TARGET_WINDOWS_RT)
	char DeviceDesc[64]; DWORD DeviceDescLength = _countof(DeviceDesc);
	if (!::GetComputerNameA(DeviceDesc, &DeviceDescLength)) {
		DeviceDescLength = 0;
	}
	std::memcpy(DeviceDesc + DeviceDescLength, "|Windows RT|", sizeof("|Windows RT|") - 1);
	DeviceDescLength += sizeof("|Windows RT|") - 1;
#elif defined(DEATH_TARGET_WINDOWS)
	auto osVersion = Death::WindowsVersion;
	char DeviceDesc[64]; DWORD DeviceDescLength = _countof(DeviceDesc);
	if (!::GetComputerNameA(DeviceDesc, &DeviceDescLength)) {
		DeviceDescLength = 0;
	}
	DeviceDescLength += formatString(DeviceDesc + DeviceDescLength, _countof(DeviceDesc) - DeviceDescLength, "|Windows %i.%i.%i|", (int)((osVersion >> 48) & 0xffffu), (int)((osVersion >> 32) & 0xffffu), (int)(osVersion & 0xffffffffu));
#else
	constexpr char DeviceDesc[] = "||"; int DeviceDescLength = sizeof(DeviceDesc) - 1;
#endif

#if defined(DEATH_TARGET_ANDROID)
	String url = "http://deat.tk/downloads/android/jazz2/updates?v=" NCINE_VERSION "&d=" + Http::EncodeBase64(DeviceDesc, DeviceDesc + DeviceDescLength);
#else
	String url = "http://deat.tk/downloads/games/jazz2/updates?v=" NCINE_VERSION "&d=" + Http::EncodeBase64(DeviceDesc, DeviceDesc + DeviceDescLength);
#endif

	Http::Request req(url, Http::InternetProtocol::V4);
	Http::Response resp = req.Send("GET"_s, std::chrono::seconds(10));
	if (resp.status.code == Http::Status::Ok && !resp.body.empty() && resp.body.size() < sizeof(_newestVersion) - 1) {
		std::memcpy(_newestVersion, resp.body.data(), resp.body.size());
		_newestVersion[resp.body.size()] = '\0';
	}
#endif
}
#endif

void GameEventHandler::SaveEpisodeEnd(const std::unique_ptr<LevelInitialization>& pendingLevelChange)
{
	if (pendingLevelChange->LastEpisodeName.empty()) {
		return;
	}

	int playerCount = 0;
	PlayerCarryOver* firstPlayer = nullptr;
	for (int i = 0; i < _countof(pendingLevelChange->PlayerCarryOvers); i++) {
		if (pendingLevelChange->PlayerCarryOvers[i].Type != PlayerType::None) {
			firstPlayer = &pendingLevelChange->PlayerCarryOvers[i];
			playerCount++;
		}
	}

	if (playerCount == 1) {
		auto episodeEnd = PreferencesCache::GetEpisodeEnd(pendingLevelChange->LastEpisodeName, true);
		episodeEnd->Flags = EpisodeContinuationFlags::IsCompleted;
		if (pendingLevelChange->CheatsUsed) {
			episodeEnd->Flags |= EpisodeContinuationFlags::CheatsUsed;
		}

		episodeEnd->Lives = firstPlayer->Lives;
		episodeEnd->Score = firstPlayer->Score;
		memcpy(episodeEnd->Ammo, firstPlayer->Ammo, sizeof(firstPlayer->Ammo));
		memcpy(episodeEnd->WeaponUpgrades, firstPlayer->WeaponUpgrades, sizeof(firstPlayer->WeaponUpgrades));

		PreferencesCache::Save();
	}
}

void GameEventHandler::SaveEpisodeContinue(const std::unique_ptr<LevelInitialization>& pendingLevelChange)
{
	if (pendingLevelChange->EpisodeName.empty() || pendingLevelChange->LevelName.empty() ||
		pendingLevelChange->EpisodeName == "unknown"_s ||
		(pendingLevelChange->EpisodeName == "prince"_s && pendingLevelChange->LevelName == "trainer"_s)) {
		return;
	}

	std::optional<Episode> currentEpisode = ContentResolver::Current().GetEpisode(pendingLevelChange->EpisodeName);
	if (!currentEpisode.has_value() || currentEpisode->FirstLevel == pendingLevelChange->LevelName) {
		return;
	}

	int playerCount = 0;
	PlayerCarryOver* firstPlayer = nullptr;
	for (int i = 0; i < _countof(pendingLevelChange->PlayerCarryOvers); i++) {
		if (pendingLevelChange->PlayerCarryOvers[i].Type != PlayerType::None) {
			firstPlayer = &pendingLevelChange->PlayerCarryOvers[i];
			playerCount++;
		}
	}

	if (playerCount == 1) {
		auto episodeContinue = PreferencesCache::GetEpisodeContinue(pendingLevelChange->EpisodeName, true);
		episodeContinue->LevelName = pendingLevelChange->LevelName;
		episodeContinue->State.Flags = EpisodeContinuationFlags::None;
		if (pendingLevelChange->CheatsUsed) {
			episodeContinue->State.Flags |= EpisodeContinuationFlags::CheatsUsed;
		}

		episodeContinue->State.DifficultyAndPlayerType = ((int)pendingLevelChange->Difficulty & 0x0f) | (((int)firstPlayer->Type & 0x0f) << 4);
		episodeContinue->State.Lives = firstPlayer->Lives;
		episodeContinue->State.Score = firstPlayer->Score;
		memcpy(episodeContinue->State.Ammo, firstPlayer->Ammo, sizeof(firstPlayer->Ammo));
		memcpy(episodeContinue->State.WeaponUpgrades, firstPlayer->WeaponUpgrades, sizeof(firstPlayer->WeaponUpgrades));

		PreferencesCache::TutorialCompleted = true;
		PreferencesCache::Save();
	} else if (!PreferencesCache::TutorialCompleted) {
		PreferencesCache::TutorialCompleted = true;
		PreferencesCache::Save();
	}
}

#if defined(DEATH_TARGET_ANDROID)
std::unique_ptr<IAppEventHandler> createAppEventHandler()
{
	return std::make_unique<GameEventHandler>();
}
#elif defined(DEATH_TARGET_WINDOWS_RT)
int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR pCmdLine, int nCmdShow)
{
	return UwpApplication::start([]() -> std::unique_ptr<IAppEventHandler> {
		return std::make_unique<GameEventHandler>();
	});
}
#elif defined(DEATH_TARGET_WINDOWS) && !defined(WITH_QT5)
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