#include "Main.h"

#if defined(DEATH_TARGET_ANDROID)
#	include "nCine/Backends/Android/AndroidApplication.h"
#	include "nCine/Backends/Android/AndroidJniHelper.h"
#elif defined(DEATH_TARGET_WINDOWS_RT)
#	include "nCine/Backends/Uwp/UwpApplication.h"
#else
#	include "nCine/MainApplication.h"
#	if defined(DEATH_TARGET_APPLE) || defined(DEATH_TARGET_UNIX)
#		include <unistd.h>
#	endif
#endif

#include "nCine/IAppEventHandler.h"
#include "nCine/tracy.h"
#include "nCine/Base/Random.h"
#include "nCine/Graphics/BinaryShaderCache.h"
#include "nCine/Graphics/RenderResources.h"
#include "nCine/Input/IInputEventHandler.h"
#include "nCine/Threading/Thread.h"

#include "Jazz2/IRootController.h"
#include "Jazz2/ContentResolver.h"
#include "Jazz2/LevelHandler.h"
#include "Jazz2/PreferencesCache.h"
#include "Jazz2/UI/Cinematics.h"
#include "Jazz2/UI/DiscordRpcClient.h"
#include "Jazz2/UI/InGameConsole.h"
#include "Jazz2/UI/LoadingHandler.h"
#include "Jazz2/UI/Menu/MainMenu.h"
#include "Jazz2/UI/Menu/HighscoresSection.h"
#include "Jazz2/UI/Menu/SimpleMessageSection.h"

#include "Jazz2/Compatibility/JJ2Anims.h"
#include "Jazz2/Compatibility/JJ2Data.h"
#include "Jazz2/Compatibility/JJ2Episode.h"
#include "Jazz2/Compatibility/JJ2Level.h"
#include "Jazz2/Compatibility/JJ2Strings.h"
#include "Jazz2/Compatibility/JJ2Tileset.h"
#include "Jazz2/Compatibility/EventConverter.h"

#if defined(WITH_MULTIPLAYER)
#	include "Jazz2/Multiplayer/NetworkManager.h"
#	include "Jazz2/Multiplayer/INetworkHandler.h"
#	include "Jazz2/Multiplayer/MpLevelHandler.h"
#	include "Jazz2/Multiplayer/PacketTypes.h"
using namespace Jazz2::Multiplayer;
#endif

#if defined(DEATH_TRACE) && (defined(DEATH_TARGET_APPLE) || defined(DEATH_TARGET_UNIX))
#	include "TermLogo.h"
#endif

#if defined(DEATH_TARGET_WINDOWS) && !defined(WITH_QT5)
#	include <cstdlib> // for `__argc` and `__argv`
#endif

#include <Containers/DateTime.h>
#include <Containers/StringConcatenable.h>
#include <Containers/StringUtils.h>
#include <Environment.h>
#include <IO/FileSystem.h>
#include <IO/PakFile.h>
#include <IO/Compression/DeflateStream.h>
#include <IO/WebRequest.h>
#include <Utf8.h>

#if defined(WITH_THREADS)
#	include <mutex>
#	include <Threading/Spinlock.h>
#endif

/** @brief @ref Death::Containers::StringView from @ref NCINE_VERSION */
#define NCINE_VERSION_s DEATH_PASTE(NCINE_VERSION, _s)

using namespace Death::IO::Compression;
#if defined(WITH_THREADS)
using namespace Death::Threading;
#endif
using namespace nCine;
using namespace Jazz2;
using namespace Jazz2::UI;

class GameEventHandler : public IAppEventHandler, public IInputEventHandler, public IRootController
#if defined(WITH_MULTIPLAYER)
	, public INetworkHandler
#endif
{
public:
	static constexpr std::uint16_t StateVersion = 3;
	static constexpr StringView StateFileName = "Jazz2.resume"_s;

#if defined(WITH_MULTIPLAYER)
	static constexpr std::uint16_t MultiplayerDefaultPort = 7438;
	static constexpr std::uint32_t MultiplayerProtocolVersion = 1;
#endif

	void OnPreInitialize(AppConfiguration& config) override;
	void OnInitialize() override;
	void OnBeginFrame() override;
	void OnPostUpdate() override;
	void OnResizeWindow(std::int32_t width, std::int32_t height) override;
	void OnShutdown() override;
	void OnSuspend() override;
	void OnResume() override;
	void OnBackInvoked() override;

	void OnKeyPressed(const KeyboardEvent& event) override;
	void OnKeyReleased(const KeyboardEvent& event) override;
	void OnTextInput(const TextInputEvent& event) override;
	void OnTouchEvent(const TouchEvent& event) override;

	void InvokeAsync(Function<void()>&& callback) override;
	void InvokeAsync(std::weak_ptr<void> reference, Function<void()>&& callback) override;
	void GoToMainMenu(bool afterIntro) override;
	void ChangeLevel(LevelInitialization&& levelInit) override;
	bool HasResumableState() const override;
	void ResumeSavedState() override;
	bool SaveCurrentStateIfAny() override;

#if defined(WITH_MULTIPLAYER)
	void ConnectToServer(StringView endpoint, std::uint16_t defaultPort) override;
	bool CreateServer(ServerInitialization&& serverInit) override;

	ConnectionResult OnPeerConnected(const Peer& peer, std::uint32_t clientData) override;
	void OnPeerDisconnected(const Peer& peer, Reason reason) override;
	void OnPacketReceived(const Peer& peer, std::uint8_t channelId, std::uint8_t packetType, ArrayView<const std::uint8_t> data) override;
#endif

	Flags GetFlags() const override {
		return _flags;
	}

	StringView GetNewestVersion() const override {
		return _newestVersion;
	}

#if !defined(DEATH_TARGET_EMSCRIPTEN)
	void RefreshCacheLevels(bool recreateAll) override;
#else
	void RefreshCacheLevels(bool recreateAll) override { }
#endif

private:
	constexpr static std::uint32_t MaxPlayerNameLength = 24;

	Flags _flags = Flags::None;
	std::int32_t _backInvokedTimeLeft = 0;
	std::shared_ptr<IStateHandler> _currentHandler;
	SmallVector<Pair<std::weak_ptr<void>, Function<void()>>> _pendingCallbacks;
	Spinlock _pendingCallbacksLock;
	String _newestVersion;
#if defined(WITH_MULTIPLAYER)
	std::unique_ptr<NetworkManager> _networkManager;
#endif

	void OnBeginInitialize();
	void OnAfterInitialize();
	void SetStateHandler(std::shared_ptr<IStateHandler>&& handler);
	void WaitForVerify();
#if !defined(DEATH_TARGET_EMSCRIPTEN)
	void RefreshCache();
	void CheckUpdates();
#endif
	bool SetLevelHandler(const LevelInitialization& levelInit);
	void HandleEndOfGame(const LevelInitialization& levelInit, bool playerDied);
	void RemoveResumableStateIfAny();
#if defined(DEATH_TARGET_ANDROID)
	void ApplyActivityIcon();
#endif
#if defined(WITH_MULTIPLAYER) && defined(WITH_THREADS)
	void RunDedicatedServer(StringView configPath);
	void StartProcessingStdin();
#endif
	static void WriteCacheDescriptor(StringView path, std::uint64_t currentVersion, std::int64_t animsModified);
	static void SaveEpisodeEnd(const LevelInitialization& levelInit);
	static void SaveEpisodeContinue(const LevelInitialization& levelInit);
	static void ExtractPakFile(StringView pakFile, StringView targetPath);
};

void GameEventHandler::OnPreInitialize(AppConfiguration& config)
{
	ZoneScopedC(0x888888);

#if defined(WITH_MULTIPLAYER) && defined(DEDICATED_SERVER)
	constexpr bool isServer = true;
#elif defined(DEATH_TARGET_APPLE) || defined(DEATH_TARGET_UNIX) || (defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT))
	// Allow `/extract-pak` and `/server` only on PC platforms
	bool isServer = false;
	for (std::int32_t i = 0; i < config.argc(); i++) {
		auto arg = config.argv(i);
		if (arg == "/extract-pak"_s && i + 2 < config.argc()) {
#	if defined(DEATH_TRACE) && defined(DEATH_TARGET_WINDOWS)
			// Always attach to console in this case
			theApplication().AttachTraceTarget(Application::ConsoleTarget);
#	endif
			auto pakFile = config.argv(i + 1);
			if (fs::FileExists(pakFile)) {
				ExtractPakFile(pakFile, config.argv(i + 2));
			} else {
				LOGE("\"%s\" not found", pakFile.data());
			}

			theApplication().Quit();
			return;
		}
#	if defined(WITH_MULTIPLAYER) && !defined(DEATH_TARGET_WINDOWS)
		if (arg == "/server"_s || arg == "--server"_s) {
			isServer = true;
		}
#	endif
	}
#else
	constexpr bool isServer = false;
#endif

	PreferencesCache::Initialize(config);

	config.windowTitle = NCINE_APP_NAME;
	if (isServer) {
		config.withGraphics = false;
		config.withAudio = false;
		config.withVSync = false;
		config.frameLimit = (std::uint32_t)FrameTimer::FramesPerSecond;

		auto& resolver = ContentResolver::Get();
		resolver.SetHeadless(true);
	} else {
		if (PreferencesCache::MaxFps == PreferencesCache::UseVsync) {
			config.withVSync = true;
		} else {
			config.withVSync = false;
			config.frameLimit = PreferencesCache::MaxFps;
		}
#if !defined(DEATH_TARGET_SWITCH)
		config.resolution.Set(LevelHandler::DefaultWidth, LevelHandler::DefaultHeight);
#endif

#if !defined(DEATH_TARGET_EMSCRIPTEN)
		auto& resolver = ContentResolver::Get();
		config.shaderCachePath = fs::CombinePath(resolver.GetCachePath(), "Shaders"_s);
#endif

		if (PreferencesCache::PlayStationExtendedSupport) {
			theApplication().EnablePlayStationExtendedSupport(true);
		}

#if defined(WITH_IMGUI)
		config.withDebugOverlay = true;
#endif
	}
}

void GameEventHandler::OnInitialize()
{
	ZoneScopedC(0x888888);

	OnBeginInitialize();

#if !defined(SHAREWARE_DEMO_ONLY) && !(defined(WITH_MULTIPLAYER) && defined(DEDICATED_SERVER))
	if (PreferencesCache::ResumeOnStart) {
		LOGI("Resuming last state due to suspended termination");
		PreferencesCache::ResumeOnStart = false;
		PreferencesCache::Save();
		if (HasResumableState()) {
			OnAfterInitialize();
			ResumeSavedState();
			return;
		}
	}
#endif

#if defined(WITH_THREADS) && !defined(DEATH_TARGET_EMSCRIPTEN)
	// If threading support is enabled, refresh cache during intro cinematics and don't allow skip until it's completed
	Thread thread([](void* arg) {
		Thread::SetCurrentName("Parallel Initialization");

		auto handler = static_cast<GameEventHandler*>(arg);
		ASSERT(handler != nullptr);

		handler->OnAfterInitialize();
#	if !defined(DEATH_TARGET_EMSCRIPTEN)
		handler->CheckUpdates();
#	endif
	}, this);
#else
	OnAfterInitialize();
#	if !defined(DEATH_TARGET_EMSCRIPTEN)
	CheckUpdates();
#	endif
#endif

#if defined(WITH_MULTIPLAYER) && defined(DEDICATED_SERVER)
	const AppConfiguration& config = theApplication().GetAppConfiguration();
	StringView configPath;
	if (config.argc() > 0) {
		configPath = config.argv(0);
	}
	RunDedicatedServer(configPath);
#else
#	if (defined(WITH_MULTIPLAYER) || defined(DEATH_DEBUG)) && (defined(DEATH_TARGET_APPLE) || defined(DEATH_TARGET_UNIX) || (defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)))
	const AppConfiguration& config = theApplication().GetAppConfiguration();
	for (std::int32_t i = 0; i < config.argc(); i++) {
		auto arg = config.argv(i);
#		if defined(WITH_MULTIPLAYER)
		if ((arg == "/connect"_s || arg == "-c"_s) && i + 1 < config.argc()) {
			auto endpoint = config.argv(i + 1);
			if (!endpoint.empty()) {
				WaitForVerify();
				SetStateHandler(std::make_shared<LoadingHandler>(this, true));
				ConnectToServer(endpoint, MultiplayerDefaultPort);
				return;
			}
		}
#			if !defined(DEATH_TARGET_WINDOWS)
		else if (arg == "/server"_s || arg == "--server"_s) {
			StringView configPath;
			if (i + 1 < config.argc()) {
				configPath = config.argv(i + 1);
			}
			RunDedicatedServer(configPath);
			return;
		}
#			endif
#		endif
#		if defined(DEATH_DEBUG)
		if (arg == "/level"_s && i + 1 < config.argc()) {
			String levelName = config.argv(i + 1);
			if (!levelName.contains('/')) {
				levelName = "unknown/"_s + levelName;
			}
			LevelInitialization levelInit(levelName, (GameDifficulty)((std::int32_t)GameDifficulty::Normal),
				PreferencesCache::EnableReforgedGameplay, false, PlayerType::Jazz);

			WaitForVerify();
			ChangeLevel(std::move(levelInit));
			return;
		}
#		endif
	}
#	endif

#	if defined(WITH_THREADS) && !defined(DEATH_TARGET_EMSCRIPTEN)
	SetStateHandler(std::make_shared<Cinematics>(this, "intro"_s, [](IRootController* root, bool endOfStream) mutable {
		if ((root->GetFlags() & Flags::IsVerified) == Flags::IsVerified) {
			root->GoToMainMenu(endOfStream);
		} else if (!endOfStream) {
			// Parallel initialization is not done yet, don't allow to skip the intro video
			return false;
		} else {
			// The intro video is over, show loading screen instead
			root->InvokeAsync([root]() {
				static_cast<GameEventHandler*>(root)->SetStateHandler(std::make_shared<LoadingHandler>(root, false, [](IRootController* root) {
					if ((root->GetFlags() & Flags::IsVerified) == Flags::IsVerified) {
						root->GoToMainMenu(true);
						return true;
					}
					return false;
				}));
			});
		}

		return true;
	}));
#	else
	SetStateHandler(std::make_shared<Cinematics>(this, "intro"_s, [](IRootController* root, bool endOfStream) {
		root->GoToMainMenu(endOfStream);
		return true;
	}));
#	endif

	Vector2i viewSize;
	if (_currentHandler != nullptr) {
		viewSize = _currentHandler->GetViewSize();
	}

	Vector2i res = theApplication().GetResolution();
	LOGI("Rendering resolution: %ix%i (%ix%i)", res.X, res.Y, viewSize.X, viewSize.Y);
#endif
}

void GameEventHandler::OnBeginFrame()
{
	if (!_pendingCallbacks.empty()) {
		ZoneScopedNC("Pending callbacks", 0x888888);

#if defined(WITH_THREADS)
		std::unique_lock<Spinlock> lock(_pendingCallbacksLock);
#endif
		std::weak_ptr<void> emptyRef;
		for (std::size_t i = 0; i < _pendingCallbacks.size(); i++) {
			auto& callback = _pendingCallbacks[i];
			auto& callbackRef = callback.first();
			// Invoke the callback only if it has no corresponding reference or the reference is still alive
			if (!callbackRef.expired() || !(callbackRef.owner_before(emptyRef) || emptyRef.owner_before(callbackRef))) {
				callback.second()();
			} else {
				LOGW("Deferred callback dropped due to dead reference");
			}
		}

		_pendingCallbacks.clear();
	}

	_currentHandler->OnBeginFrame();
}

void GameEventHandler::OnPostUpdate()
{
	_currentHandler->OnEndFrame();

	if (_backInvokedTimeLeft > 0) {
		_backInvokedTimeLeft--;
		if (_backInvokedTimeLeft <= 0) {
			KeyboardEvent event{};
			event.sym = Keys::Back;
			_currentHandler->OnKeyReleased(event);
		}
	}
}

void GameEventHandler::OnResizeWindow(std::int32_t width, std::int32_t height)
{
	// Resolution was changed, all viewports have to be recreated
	Viewport::GetChain().clear();

	Vector2i viewSize;
	if (_currentHandler != nullptr) {
		_currentHandler->OnInitializeViewport(width, height);
		viewSize = _currentHandler->GetViewSize();
	}

#if defined(NCINE_HAS_WINDOWS)
	PreferencesCache::EnableFullscreen = theApplication().GetGfxDevice().isFullscreen();
#endif

	LOGI("Rendering resolution: %ix%i (%ix%i)", width, height, viewSize.X, viewSize.Y);
}

void GameEventHandler::OnShutdown()
{
	ZoneScopedC(0x888888);

#if defined(DEATH_TARGET_ANDROID)
	ApplyActivityIcon();
#endif

	_currentHandler = nullptr;
#if defined(WITH_MULTIPLAYER)
	if (_networkManager != nullptr) {
		_networkManager->Dispose();
		_networkManager = nullptr;
	}
#endif

	if ((_flags & Flags::IsInitialized) == Flags::IsInitialized) {
		ContentResolver::Get().Release();
	}
}

void GameEventHandler::OnSuspend()
{
#if !defined(SHAREWARE_DEMO_ONLY)
	if (SaveCurrentStateIfAny()) {
		PreferencesCache::ResumeOnStart = true;
		PreferencesCache::Save();
	}
#endif

#if defined(DEATH_TARGET_ANDROID)
	ApplyActivityIcon();
#endif
}

void GameEventHandler::OnResume()
{
#if defined(DEATH_TARGET_ANDROID)
	if (Backends::AndroidJniWrap_Activity::hasExternalStoragePermission()) {
		_flags |= Flags::HasExternalStoragePermissionOnResume;
	} else {
		_flags &= ~Flags::HasExternalStoragePermissionOnResume;
	}
#endif

#if !defined(SHAREWARE_DEMO_ONLY)
	PreferencesCache::ResumeOnStart = false;
	PreferencesCache::Save();
#endif
}

void GameEventHandler::OnBackInvoked()
{
	if (_currentHandler != nullptr && _backInvokedTimeLeft <= 0) {
		// The back button needs to be pressed for at least 2 frames
		_backInvokedTimeLeft = 2;

		KeyboardEvent event{};
		event.sym = Keys::Back;
		_currentHandler->OnKeyPressed(event);
	}
}

void GameEventHandler::OnKeyPressed(const KeyboardEvent& event)
{
#if defined(NCINE_HAS_WINDOWS)
	// Allow F11 and Alt+Enter to switch fullscreen
	// TODO: Don't override F11 in web browser with newer version of `contrib.glfw3`
	if (event.sym == Keys::F11 || (event.sym == Keys::Return && (event.mod & KeyMod::Mask) == KeyMod::LAlt)) {
#	if defined(DEATH_TARGET_WINDOWS_RT)
		// Xbox is always fullscreen
		if (Environment::CurrentDeviceType == DeviceType::Xbox) {
			return;
		}
#	endif
		PreferencesCache::EnableFullscreen = !PreferencesCache::EnableFullscreen;
		if (PreferencesCache::EnableFullscreen) {
			theApplication().GetGfxDevice().setResolution(true);
			theApplication().GetInputManager().setCursor(IInputManager::Cursor::Hidden);
		} else {
			theApplication().GetGfxDevice().setResolution(false);
			theApplication().GetInputManager().setCursor(IInputManager::Cursor::Arrow);
		}
		return;
	}
#endif

	_currentHandler->OnKeyPressed(event);
}

void GameEventHandler::OnKeyReleased(const KeyboardEvent& event)
{
	_currentHandler->OnKeyReleased(event);
}

void GameEventHandler::OnTextInput(const TextInputEvent& event)
{
	_currentHandler->OnTextInput(event);
}

void GameEventHandler::OnTouchEvent(const TouchEvent& event)
{
	_currentHandler->OnTouchEvent(event);
}

void GameEventHandler::InvokeAsync(Function<void()>&& callback)
{
#if defined(WITH_THREADS)
	std::unique_lock<Spinlock> lock(_pendingCallbacksLock);
#endif
	_pendingCallbacks.emplace_back(std::weak_ptr<void>{}, std::move(callback));
}

void GameEventHandler::InvokeAsync(std::weak_ptr<void> reference, Function<void()>&& callback)
{
#if defined(WITH_THREADS)
	std::unique_lock<Spinlock> lock(_pendingCallbacksLock);
#endif
	_pendingCallbacks.emplace_back(std::move(reference), std::move(callback));
}

void GameEventHandler::GoToMainMenu(bool afterIntro)
{
	InvokeAsync([this, afterIntro]() {
		ZoneScopedNC("GameEventHandler::GoToMainMenu", 0x888888);

		LOGI("Going to main menu");

#if defined(WITH_MULTIPLAYER)
		if (_networkManager != nullptr) {
			_networkManager->Dispose();
			_networkManager = nullptr;
		}
#endif
		InGameConsole::Clear();
		if (auto* mainMenu = runtime_cast<Menu::MainMenu*>(_currentHandler)) {
			mainMenu->Reset();
		} else {
			SetStateHandler(std::make_shared<Menu::MainMenu>(this, afterIntro));
		}
	});
}

void GameEventHandler::ChangeLevel(LevelInitialization&& levelInit)
{
	InvokeAsync([this, levelInit = std::move(levelInit)]() mutable {
		ZoneScopedNC("GameEventHandler::ChangeLevel", 0x888888);

		auto p = levelInit.LevelName.partition('/');
		auto levelName = (!p[2].empty() ? p[2] : p[0]);

		std::shared_ptr<IStateHandler> newHandler;
		if (levelInit.LevelName.empty()) {
			// Next level not specified, so show main menu
			InGameConsole::Clear();
			newHandler = std::make_shared<Menu::MainMenu>(this, false);
#if defined(WITH_MULTIPLAYER)
			// TODO: This should show some server console instead of exiting
			if (_networkManager != nullptr) {
				LOGW("Failed to load level \"%s\", disposing network manager", levelInit.LevelName.data());

				_networkManager->Dispose();
				_networkManager = nullptr;
			}
#endif
		} else if (levelName == ":end"_s) {
			// End of episode
			SaveEpisodeEnd(levelInit);

			auto& resolver = ContentResolver::Get();
			std::optional<Episode> lastEpisode = resolver.GetEpisode(levelInit.LastEpisodeName);
			if (lastEpisode) {
				// Redirect to next episode
				if (std::optional<Episode> nextEpisode = resolver.GetEpisode(lastEpisode->NextEpisode)) {
					levelInit.LevelName = lastEpisode->NextEpisode + '/' + nextEpisode->FirstLevel;

					p = levelInit.LevelName.partition('/');
					levelName = (!p[2].empty() ? p[2] : p[0]);
				}
			}

			if (levelName != ":end"_s) {
				if (!SetLevelHandler(levelInit)) {
					InGameConsole::Clear();
					auto mainMenu = std::make_shared<Menu::MainMenu>(this, false);
					mainMenu->SwitchToSection<Menu::SimpleMessageSection>(_("\f[c:#704a4a]Cannot load specified level!\f[/c]\n\n\nMake sure all necessary files\nare accessible and try it again."), true);
					newHandler = std::move(mainMenu);
#if defined(WITH_MULTIPLAYER)
					// TODO: This should show some server console instead of exiting
					if (_networkManager != nullptr) {
						LOGW("Failed to load level \"%s\", disposing network manager", levelInit.LevelName.data());

						_networkManager->Dispose();
						_networkManager = nullptr;
					}
#endif
				}
			} else {
				HandleEndOfGame(levelInit, false);
#if defined(WITH_MULTIPLAYER)
				// TODO: This should show some server console instead of exiting
				if (_networkManager != nullptr) {
					LOGW("Failed to load level \"%s\", disposing network manager", levelInit.LevelName.data());

					_networkManager->Dispose();
					_networkManager = nullptr;
				}
#endif
			}
		} else if (levelName == ":credits"_s) {
			// End of game
			SaveEpisodeEnd(levelInit);

			newHandler = std::make_shared<Cinematics>(this, "ending"_s, [levelInit = std::move(levelInit)](IRootController* root, bool endOfStream) mutable {
				auto* _this = static_cast<GameEventHandler*>(root);
				_this->InvokeAsync([_this, levelInit = std::move(levelInit)]() {
					_this->HandleEndOfGame(levelInit, false);
				});
				return true;
			});
		} else if (levelName == ":gameover"_s) {
			// Player died
			HandleEndOfGame(levelInit, true);
		} else {
			SaveEpisodeContinue(levelInit);

#if defined(SHAREWARE_DEMO_ONLY)
			// Check if specified episode is unlocked, used only if compiled with SHAREWARE_DEMO_ONLY
			bool isEpisodeLocked = (p[0] == "unknown"_s) ||
				(p[0] == "prince"_s && (PreferencesCache::UnlockedEpisodes & UnlockableEpisodes::FormerlyAPrince) == UnlockableEpisodes::None) ||
				(p[0] == "rescue"_s && (PreferencesCache::UnlockedEpisodes & UnlockableEpisodes::JazzInTime) == UnlockableEpisodes::None) ||
				(p[0] == "flash"_s && (PreferencesCache::UnlockedEpisodes & UnlockableEpisodes::Flashback) == UnlockableEpisodes::None) ||
				(p[0] == "monk"_s && (PreferencesCache::UnlockedEpisodes & UnlockableEpisodes::FunkyMonkeys) == UnlockableEpisodes::None) ||
				((p[0] == "xmas98"_s || p[0] == "xmas99"_s) && (PreferencesCache::UnlockedEpisodes & UnlockableEpisodes::ChristmasChronicles) == UnlockableEpisodes::None) ||
				(p[0] == "secretf"_s && (PreferencesCache::UnlockedEpisodes & UnlockableEpisodes::TheSecretFiles) == UnlockableEpisodes::None);

			if (isEpisodeLocked) {
				newHandler = std::make_shared<Menu::MainMenu>(this, false);
			} else
#endif
			{
				if (!SetLevelHandler(levelInit)) {
					InGameConsole::Clear();
					auto mainMenu = std::make_shared<Menu::MainMenu>(this, false);
					mainMenu->SwitchToSection<Menu::SimpleMessageSection>(_("\f[c:#704a4a]Cannot load specified level!\f[/c]\n\n\nMake sure all necessary files\nare accessible and try it again."), true);
					newHandler = std::move(mainMenu);
#if defined(WITH_MULTIPLAYER)
					// TODO: This should show some server console instead of exiting
					if (_networkManager != nullptr) {
						LOGW("Failed to load level \"%s\", disposing network manager", levelInit.LevelName.data());

						_networkManager->Dispose();
						_networkManager = nullptr;
					}
#endif
				}
			}
		}

		if (newHandler != nullptr) {
			SetStateHandler(std::move(newHandler));
		}
	});
}

bool GameEventHandler::HasResumableState() const
{
	auto configDir = PreferencesCache::GetDirectory();
	return fs::FileExists(fs::CombinePath(configDir, StateFileName));
}

void GameEventHandler::ResumeSavedState()
{
	InvokeAsync([this]() {
		ZoneScopedNC("GameEventHandler::ResumeSavedState", 0x888888);

		LOGI("Resuming saved state...");

		auto configDir = PreferencesCache::GetDirectory();
		auto s = fs::Open(fs::CombinePath(configDir, StateFileName), FileAccess::Read);
		if (*s) {
			std::uint64_t signature = s->ReadValue<std::uint64_t>();
			std::uint8_t fileType = s->ReadValue<std::uint8_t>();
			std::uint16_t version = s->ReadValue<std::uint16_t>();
			if (signature != 0x2095A59FF0BFBBEF || fileType != ContentResolver::StateFile || version > StateVersion) {
				return;
			}

			if (version == 1) {
				// Version 1 included compressedSize and decompressedSize, it's not needed anymore
				/*std::int32_t compressedSize =*/ s->ReadVariableInt32();
				/*std::int32_t decompressedSize =*/ s->ReadVariableInt32();
			}

			DeflateStream uc(*s);
			auto levelHandler = std::make_shared<LevelHandler>(this);
			if (levelHandler->Initialize(uc, version)) {
				SetStateHandler(std::move(levelHandler));
				return;
			}
		}

		InGameConsole::Clear();
		auto mainMenu = std::make_shared<Menu::MainMenu>(this, false);
		mainMenu->SwitchToSection<Menu::SimpleMessageSection>(_("\f[c:#704a4a]Cannot resume saved state!\f[/c]\n\n\nMake sure all necessary files\nare accessible and try it again."), true);
		SetStateHandler(std::move(mainMenu));
	});
}

bool GameEventHandler::SaveCurrentStateIfAny()
{
	ZoneScopedNC("GameEventHandler::SaveCurrentStateIfAny", 0x888888);

	if (auto* levelHandler = runtime_cast<LevelHandler*>(_currentHandler)) {
		if (levelHandler->IsLocalSession()) {
			auto configDir = PreferencesCache::GetDirectory();
			auto s = fs::Open(fs::CombinePath(configDir, StateFileName), FileAccess::Write);
			if (*s) {
				s->WriteValue<std::uint64_t>(0x2095A59FF0BFBBEF);	// Signature
				s->WriteValue<std::uint8_t>(ContentResolver::StateFile);
				s->WriteValue<std::uint16_t>(StateVersion);

				DeflateWriter co(*s);
				if (!levelHandler->SerializeResumableToStream(co)) {
					LOGE("Failed to save current state");
				}
				return true;
			}
		}
	}

	return false;
}

void GameEventHandler::RemoveResumableStateIfAny()
{
	auto configDir = PreferencesCache::GetDirectory();
	auto path = fs::CombinePath(configDir, StateFileName);
	if (fs::FileExists(path)) {
		fs::RemoveFile(path);
	}
}

#if defined(DEATH_TARGET_ANDROID)
void GameEventHandler::ApplyActivityIcon()
{
	if (PreferencesCache::EnableReforgedMainMenuInitial == PreferencesCache::EnableReforgedMainMenu) {
		return;
	}

	PreferencesCache::EnableReforgedMainMenuInitial = PreferencesCache::EnableReforgedMainMenu;

	// These calls will kill the app in a second, so it should be called only on exit
	if (PreferencesCache::EnableReforgedMainMenu) {
		LOGI("Changed activity icon to \"Reforged\"");
		Backends::AndroidJniWrap_Activity::setActivityEnabled(".MainActivityReforged"_s, true);
		Backends::AndroidJniWrap_Activity::setActivityEnabled(".MainActivityLegacy"_s, false);
	} else {
		LOGI("Changed activity icon to \"Legacy\"");
		Backends::AndroidJniWrap_Activity::setActivityEnabled(".MainActivityLegacy"_s, true);
		Backends::AndroidJniWrap_Activity::setActivityEnabled(".MainActivityReforged"_s, false);
	}
}
#endif

#if defined(WITH_MULTIPLAYER)
void GameEventHandler::RunDedicatedServer(StringView configPath)
{
	ServerInitialization serverInit;
	if (!configPath.empty()) {
		serverInit.Configuration = NetworkManager::LoadServerConfigurationFromFile(configPath);
	} else {
		serverInit.Configuration = NetworkManager::CreateDefaultServerConfiguration();
	}
	serverInit.InitialLevel.IsLocalSession = false;
	serverInit.InitialLevel.IsReforged = PreferencesCache::EnableReforgedGameplay;
	serverInit.Configuration.GameMode = MpGameMode::Cooperation;
	if (!serverInit.Configuration.Playlist.empty()) {
		if (serverInit.Configuration.PlaylistIndex < 0 || serverInit.Configuration.PlaylistIndex >= serverInit.Configuration.Playlist.size()) {
			serverInit.Configuration.PlaylistIndex = 0;
		}
		if (serverInit.Configuration.RandomizePlaylist) {
			Random().Shuffle<PlaylistEntry>(serverInit.Configuration.Playlist);
		}
	}

	WaitForVerify();
	if (!CreateServer(std::move(serverInit))) {
		LOGE("Server cannot be started because of invalid configuration");
		theApplication().Quit();
	} else {
		StartProcessingStdin();
	}
}

#	if defined(WITH_THREADS)
void GameEventHandler::StartProcessingStdin()
{
	Thread thread([](void* arg) {
		auto _this = static_cast<GameEventHandler*>(arg);
		ASSERT(_this != nullptr);

		char buffer[4096];
		while (true) {
			if (!::fgets(buffer, sizeof(buffer), stdin)) {
				LOGW("Failed to read from stdin");
				break;
			}

			StringView line = buffer;
			line = line.trimmed();

			if (!line.empty()) {
				if (line == "/exit"_s || line == "/quit"_s) {
					if (_this->_networkManager != nullptr) {
						_this->_networkManager->Dispose();
						_this->_networkManager = nullptr;
					}
					theApplication().Quit();
					break;
				} else if (auto* levelHandler = runtime_cast<MpLevelHandler*>(_this->_currentHandler)) {
					levelHandler->ProcessCommand({}, line, true);
				}
			}
		}
	}, this);
}
#	endif

void GameEventHandler::ConnectToServer(StringView endpoint, std::uint16_t defaultPort)
{
	LOGI("[MP] Preparing connection to %s...", endpoint.data());

	_networkManager = std::make_unique<NetworkManager>();
	_networkManager->CreateClient(this, endpoint, defaultPort, 0xDEA00000 | (MultiplayerProtocolVersion & 0x000FFFFF));
}

bool GameEventHandler::CreateServer(ServerInitialization&& serverInit)
{
	_networkManager = std::make_unique<NetworkManager>();

	if (serverInit.Configuration.ServerName.empty()) {
		serverInit.Configuration.ServerName = _("Unnamed server");
	}

	if (serverInit.Configuration.PlaylistIndex >= 0 && serverInit.Configuration.PlaylistIndex < serverInit.Configuration.Playlist.size()) {
		auto& playlistEntry = serverInit.Configuration.Playlist[serverInit.Configuration.PlaylistIndex];

		auto level = playlistEntry.LevelName.partition('/');
		if (playlistEntry.LevelName.contains('/')) {
			serverInit.InitialLevel.LevelName = playlistEntry.LevelName;
		} else {
			serverInit.InitialLevel.LevelName = "unknown/"_s + playlistEntry.LevelName;
		}

		// Override properties
		serverInit.Configuration.ReforgedGameplay = playlistEntry.ReforgedGameplay;
		serverInit.Configuration.Elimination = playlistEntry.Elimination;
		serverInit.Configuration.InitialPlayerHealth = playlistEntry.InitialPlayerHealth;
		serverInit.Configuration.MaxGameTimeSecs = playlistEntry.MaxGameTimeSecs;
		serverInit.Configuration.PreGameSecs = playlistEntry.PreGameSecs;
		serverInit.Configuration.TotalKills = playlistEntry.TotalKills;
		serverInit.Configuration.TotalLaps = playlistEntry.TotalLaps;
		serverInit.Configuration.TotalTreasureCollected = playlistEntry.TotalTreasureCollected;
		serverInit.Configuration.GameMode = playlistEntry.GameMode;
	}

	if (serverInit.InitialLevel.LevelName.empty()) {
		LOGE("Initial level is not specified");
		return false;
	} else if (!ContentResolver::Get().LevelExists(serverInit.InitialLevel.LevelName)) {
		LOGE("Cannot find initial level \"%s\"", serverInit.InitialLevel.LevelName.data());
		return false;
	}

	serverInit.InitialLevel.IsReforged = serverInit.Configuration.ReforgedGameplay;

	if (!_networkManager->CreateServer(this, std::move(serverInit.Configuration))) {
		return false;
	}

	auto& serverConfig = _networkManager->GetServerConfiguration();
	LOGI("[MP] Creating %s server \"%s\" on port %u...", serverConfig.IsPrivate ? "private" : "public", serverConfig.ServerName.data(), serverConfig.ServerPort);

	InvokeAsync([this, serverInit = std::move(serverInit)]() mutable {
		auto levelHandler = std::make_shared<MpLevelHandler>(this,
			_networkManager.get(), MpLevelHandler::LevelState::InitialUpdatePending, true);
		levelHandler->Initialize(serverInit.InitialLevel);
		SetStateHandler(std::move(levelHandler));
	});

	return true;
}

ConnectionResult GameEventHandler::OnPeerConnected(const Peer& peer, std::uint32_t clientData)
{
	LOGI("[MP] Peer connected (%s) [%08llx]", NetworkManagerBase::AddressToString(peer).data(), (std::uint64_t)peer._enet);

	if (_networkManager->GetState() == NetworkState::Listening) {
		if ((clientData & 0xFFF00000) != 0xDEA00000 || (clientData & 0x000FFFFF) > MultiplayerProtocolVersion) {
			// Connected client is newer than server, reject it
			LOGI("[MP] Peer kicked (%s) [%08llx]: Incompatible version", NetworkManagerBase::AddressToString(peer).data(), (std::uint64_t)peer._enet);
			return Reason::IncompatibleVersion;
		}

		const auto& serverConfig = _networkManager->GetServerConfiguration();
		if (_networkManager->GetPeerCount() > serverConfig.MaxPlayerCount) {
			return Reason::ServerIsFull;
		}
	} else {
		MemoryStream packet(64 + MaxPlayerNameLength);
		packet.Write("J2R ", 4);

		constexpr std::uint64_t currentVersion = parseVersion(NCINE_VERSION_s);
		packet.WriteVariableUint64(currentVersion);

		packet.Write(PreferencesCache::UniquePlayerID, sizeof(PreferencesCache::UniquePlayerID));

		// TODO: Password
		packet.WriteVariableUint32(0);

		auto playerName = PreferencesCache::GetEffectivePlayerName();
		if (playerName.empty()) {
			playerName = "Unknown"_s;
		}
		if (playerName.size() > MaxPlayerNameLength) {
			auto [_, prevChar] = Utf8::PrevChar(playerName, MaxPlayerNameLength);
			playerName = playerName.prefix(prevChar);
		}
		packet.WriteValue<std::uint8_t>((std::uint8_t)playerName.size());
		packet.Write(playerName.data(), (std::uint32_t)playerName.size());

#if defined(DEATH_TARGET_ANDROID)
		auto androidId = Backends::AndroidJniWrap_Secure::getAndroidId();
		std::size_t androidIdLength = std::min(androidId.size(), (std::size_t)UINT8_MAX);
		packet.WriteValue<std::uint8_t>((std::uint8_t)androidIdLength);
		packet.Write(androidId.data(), (std::uint32_t)androidIdLength);
#else
		packet.WriteValue<std::uint8_t>(0);	// Device ID
#endif

		std::uint64_t playerUserId = 0;
#if (defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)) || defined(DEATH_TARGET_UNIX)
		if (PreferencesCache::EnableDiscordIntegration && UI::DiscordRpcClient::Get().IsSupported()) {
			playerUserId = UI::DiscordRpcClient::Get().GetUserId();
		}
#endif
		packet.WriteVariableUint64(playerUserId);

		_networkManager->SendTo(peer, NetworkChannel::Main, (std::uint8_t)ClientPacketType::Auth, packet);
	}

	return true;
}

void GameEventHandler::OnPeerDisconnected(const Peer& peer, Reason reason)
{
	if (auto peerDesc = _networkManager->GetPeerDescriptor(peer)) {
		LOGI("[MP] Peer disconnected \"%s\" (%s) [%08llx]: %s (%u)", peerDesc->PlayerName.data(),
			NetworkManagerBase::AddressToString(peer).data(), (std::uint64_t)peer._enet, NetworkManagerBase::ReasonToString(reason), (std::uint32_t)reason);
	} else if (peer) {
		LOGI("[MP] Peer disconnected (%s) [%08llx]: %s (%u)", NetworkManagerBase::AddressToString(peer).data(),
			(std::uint64_t)peer._enet, NetworkManagerBase::ReasonToString(reason), (std::uint32_t)reason);
	} else {
		LOGI("[MP] Peer disconnected [%08llx]: %s (%u)", (std::uint64_t)peer._enet, NetworkManagerBase::ReasonToString(reason), (std::uint32_t)reason);
	}

	if (auto* multiLevelHandler = runtime_cast<MpLevelHandler*>(_currentHandler)) {
		if (multiLevelHandler->OnPeerDisconnected(peer)) {
			return;
		}
	}

	if (_networkManager != nullptr && _networkManager->GetState() != NetworkState::Listening) {
		InvokeAsync([this, reason]() {
#if defined(WITH_MULTIPLAYER)
			if (_networkManager != nullptr) {
				_networkManager->Dispose();
				_networkManager = nullptr;
			}
#endif
			InGameConsole::Clear();
			Menu::MainMenu* mainMenu;
			if (mainMenu = runtime_cast<Menu::MainMenu*>(_currentHandler)) {
				if (!dynamic_cast<Menu::SimpleMessageSection*>(mainMenu->GetCurrentSection())) {
				mainMenu->Reset();
				}
			} else {
				auto newHandler = std::make_shared<Menu::MainMenu>(this, false);
				mainMenu = newHandler.get();
				SetStateHandler(std::move(newHandler));
			}

			switch (reason) {
				case Reason::InvalidParameter: mainMenu->SwitchToSection<Menu::SimpleMessageSection>(_("\f[c:#704a4a]Cannot connect to the server!\f[/c]\n\n\nInvalid parameter specified."), true); break;
				case Reason::IncompatibleVersion: mainMenu->SwitchToSection<Menu::SimpleMessageSection>(_("\f[c:#704a4a]Cannot connect to the server!\f[/c]\n\n\nYour client version is not compatible with the server."), true); break;
				case Reason::AuthFailed: mainMenu->SwitchToSection<Menu::SimpleMessageSection>(_("\f[c:#704a4a]Cannot connect to the server!\f[/c]\n\n\nAuthentication failed.\nContact server administrators for more information."), true); break;
				case Reason::InvalidPassword: mainMenu->SwitchToSection<Menu::SimpleMessageSection>(_("\f[c:#704a4a]Cannot connect to the server!\f[/c]\n\n\nInvalid password specified."), true); break;
				case Reason::InvalidPlayerName: mainMenu->SwitchToSection<Menu::SimpleMessageSection>(_("\f[c:#704a4a]Cannot connect to the server!\f[/c]\n\n\nInvalid player name specified.\nPlease check your profile and try it again."), true); break;
				case Reason::NotInWhitelist: mainMenu->SwitchToSection<Menu::SimpleMessageSection>(_("\f[c:#704a4a]Cannot connect to the server!\f[/c]\n\n\nThis client is not in the server whitelist.\nContact server administrators for more information."), true); break;
				case Reason::Requires3rdPartyAuthProvider: mainMenu->SwitchToSection<Menu::SimpleMessageSection>(_("\f[c:#704a4a]Cannot connect to the server!\f[/c]\n\n\nServer requires 3rd party authentication provider.\nContact server administrators for more information."), true); break;
				case Reason::ServerIsFull: mainMenu->SwitchToSection<Menu::SimpleMessageSection>(_("\f[c:#704a4a]Cannot connect to the server!\f[/c]\n\n\nServer capacity is full.\nPlease try it later."), true); break;
				case Reason::ServerNotReady: mainMenu->SwitchToSection<Menu::SimpleMessageSection>(_("\f[c:#704a4a]Cannot connect to the server!\f[/c]\n\n\nServer is not in a state where it can process your request.\nPlease try again in a few seconds."), true); break;
				case Reason::ServerStopped: mainMenu->SwitchToSection<Menu::SimpleMessageSection>(_("\f[c:#704a4a]Connection has been closed!\f[/c]\n\n\nServer is shutting down.\nPlease try it later."), true); break;
				case Reason::ServerStoppedForMaintenance: mainMenu->SwitchToSection<Menu::SimpleMessageSection>(_("\f[c:#704a4a]Connection has been closed!\f[/c]\n\n\nServer is shutting down for maintenance.\nPlease try it again later."), true); break;
				case Reason::ServerStoppedForReconfiguration: mainMenu->SwitchToSection<Menu::SimpleMessageSection>(_("\f[c:#704a4a]Connection has been closed!\f[/c]\n\n\nServer is shutting down for reconfiguration.\nPlease try it again later."), true); break;
				case Reason::ServerStoppedForUpdate: mainMenu->SwitchToSection<Menu::SimpleMessageSection>(_("\f[c:#704a4a]Connection has been closed!\f[/c]\n\n\nServer is shutting down for update.\nPlease check your client version and try it again in a minute."), true); break;
				case Reason::ConnectionLost: mainMenu->SwitchToSection<Menu::SimpleMessageSection>(_("\f[c:#704a4a]Connection has been lost!\f[/c]\n\n\nPlease try it again and if the problem persists,\ncheck your network connection."), true); break;
				case Reason::ConnectionTimedOut: mainMenu->SwitchToSection<Menu::SimpleMessageSection>(_("\f[c:#704a4a]Cannot connect to the server!\f[/c]\n\n\nThe server is not responding for connection request."), true); break;
				case Reason::Kicked: mainMenu->SwitchToSection<Menu::SimpleMessageSection>(_("\f[c:#704a4a]Connection has been closed!\f[/c]\n\n\nYou have been \f[c:#907050]kicked\f[/c] off the server.\nContact server administrators for more information."), true); break;
				case Reason::Banned: mainMenu->SwitchToSection<Menu::SimpleMessageSection>(_("\f[c:#704a4a]Connection has been closed!\f[/c]\n\n\nYou have been \f[c:#725040]banned\f[/c] off the server.\nContact server administrators for more information."), true); break;
				case Reason::CheatingDetected: mainMenu->SwitchToSection<Menu::SimpleMessageSection>(_("\f[c:#704a4a]Connection has been closed!\f[/c]\n\n\nCheating detected."), true); break;
			}
		});
	}
}

void GameEventHandler::OnPacketReceived(const Peer& peer, std::uint8_t channelId, std::uint8_t packetType, ArrayView<const std::uint8_t> data)
{
	bool isServer = (_networkManager->GetState() == NetworkState::Listening);
	if (isServer) {
		switch ((ClientPacketType)packetType) {
			case ClientPacketType::Ping: {
				_networkManager->SendTo(peer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::Pong, {});
				break;
			}
			case ClientPacketType::Auth: {
				MemoryStream packet(data);
				char gameID[4];
				packet.Read(gameID, 4);
				std::uint64_t gameVersion = packet.ReadVariableUint64();

				constexpr std::uint64_t VersionMask = ~0xFFFFFFFFULL; // Exclude patch from version check
				constexpr std::uint64_t currentVersion = parseVersion(NCINE_VERSION_s);

				if (strncmp("J2R ", gameID, 4) != 0 || (gameVersion & VersionMask) != (currentVersion & VersionMask)) {
					_networkManager->Kick(peer, Reason::IncompatibleVersion);
					return;
				}

				StaticArray<16, std::uint8_t> uuid;
				packet.Read(uuid.data(), uuid.size());
				String uniquePlayerId = NetworkManager::UuidToString(uuid);

				LOGD("[MP] ClientPacketType::Auth [%08llx] - gameID: \"%.*s\", gameVersion: 0x%llx, uuid: \"%s\"",
					(std::uint64_t)peer._enet, 4, gameID, gameVersion, uniquePlayerId.data());

				std::uint32_t passwordLength = packet.ReadVariableUint32();
				String password{NoInit, passwordLength};
				packet.Read(password.data(), passwordLength);

				std::uint8_t playerNameLength = packet.ReadValue<std::uint8_t>();

				// TODO: Sanitize (\n,\r,\t) and strip formatting (\f) from player name
				if (playerNameLength == 0 || playerNameLength > MaxPlayerNameLength) {
					LOGD("[MP] ClientPacketType::Auth [%08llx] - player name length out of bounds (%u)", (std::uint64_t)peer._enet, playerNameLength);
					_networkManager->Kick(peer, Reason::InvalidPlayerName);
					return;
				}

				String playerName{NoInit, playerNameLength};
				packet.Read(playerName.data(), playerNameLength);

				const auto& serverConfig = _networkManager->GetServerConfiguration();
				if (serverConfig.BannedUniquePlayerIDs.contains(uniquePlayerId)) {
					LOGI("[MP] Peer kicked \"%s\" (%s) [%08llx]: Banned by unique player ID", playerName.data(), NetworkManagerBase::AddressToString(peer).data(), (std::uint64_t)peer._enet);
					_networkManager->Kick(peer, Reason::Banned);
					return;
				}
				if (!serverConfig.WhitelistedUniquePlayerIDs.empty() && !serverConfig.WhitelistedUniquePlayerIDs.contains(uniquePlayerId)) {
					LOGI("[MP] Peer kicked \"%s\" (%s) [%08llx]: Not in whitelist", playerName.data(), NetworkManagerBase::AddressToString(peer).data(), (std::uint64_t)peer._enet);
					_networkManager->Kick(peer, Reason::NotInWhitelist);
					return;
				}

				if (!serverConfig.ServerPassword.empty() && password != serverConfig.ServerPassword) {
					LOGI("[MP] Peer kicked \"%s\" (%s) [%08llx]: Invalid password", playerName.data(), NetworkManagerBase::AddressToString(peer).data(), (std::uint64_t)peer._enet);
					_networkManager->Kick(peer, Reason::InvalidPassword);
					return;
				}

				std::uint8_t deviceIdLength = packet.ReadValue<std::uint8_t>();
				String deviceId{NoInit, deviceIdLength};
				packet.Read(deviceId.data(), deviceIdLength);

				std::uint64_t playerUserId = packet.ReadVariableUint64();
				if (serverConfig.RequiresDiscordAuth && playerUserId == 0) {
					LOGI("[MP] Peer kicked \"%s\" (%s) [%08llx]: Discord authentication is required", playerName.data(), NetworkManagerBase::AddressToString(peer).data(), (std::uint64_t)peer._enet);
					_networkManager->Kick(peer, Reason::Requires3rdPartyAuthProvider);
					return;
				}
				// TODO: Check playerUserId for whitelist (Reason::NotInWhitelist)

				if (auto peerDesc = _networkManager->GetPeerDescriptor(peer)) {
					peerDesc->Uuid = std::move(uuid);
					peerDesc->PlayerName = std::move(playerName);
					peerDesc->IsAuthenticated = true;

					if (serverConfig.AdminUniquePlayerIDs.contains(uniquePlayerId)) {
						peerDesc->IsAdmin = true;
					}

					LOGI("[MP] Peer authenticated as \"%s\" (%s)%s [%08llx]", peerDesc->PlayerName.data(), NetworkManagerBase::AddressToString(peer).data(),
						peerDesc->IsAdmin ? " [Admin]" : "", (std::uint64_t)peer._enet);

					MemoryStream packet(17);
					packet.WriteValue<std::uint8_t>(0);	// Flags
					packet.Write(PreferencesCache::UniqueServerID, sizeof(PreferencesCache::UniqueServerID));
					_networkManager->SendTo(peer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::AuthResponse, packet);
				} else {
					DEATH_ASSERT_UNREACHABLE();
				}
				break;
			}
		}

	} else {
		switch ((ServerPacketType)packetType) {
			case ServerPacketType::AuthResponse: {
				MemoryStream packet(data);
				std::uint8_t flags = packet.ReadValue<std::uint8_t>();
				StaticArray<16, std::uint8_t> uuid;
				packet.Read(uuid.data(), uuid.size());
				_networkManager->RemoteServerID = NetworkManager::UuidToString(uuid);
				return;
			}
			case ServerPacketType::LoadLevel: {
				MemoryStream packet(data);
				std::uint32_t flags = packet.ReadVariableUint32();
				MpLevelHandler::LevelState levelState = (MpLevelHandler::LevelState)packet.ReadValue<std::uint8_t>();
				MpGameMode gameMode = (MpGameMode)packet.ReadValue<std::uint8_t>();
				ExitType lastExitType = (ExitType)packet.ReadValue<std::uint8_t>();
				std::uint32_t levelNameLength = packet.ReadVariableUint32();
				String levelName{NoInit, levelNameLength};
				packet.Read(levelName.data(), levelNameLength);
				std::int32_t initialPlayerHealth = packet.ReadVariableInt32();
				std::uint32_t maxGameTimeSecs = packet.ReadVariableUint32();
				std::uint32_t totalKills = packet.ReadVariableUint32();
				std::uint32_t totalLaps = packet.ReadVariableUint32();
				std::uint32_t totalTreasureCollected = packet.ReadVariableUint32();

				// TODO: Use AssetChunk packet instead
				/*if (containsLevelAssets) {
					auto& resolver = ContentResolver::Get();

					String downloadsPath = fs::CombinePath({ resolver.GetCachePath(), "Downloads"_s, StringUtils::replaceAll(_networkManager->RemoteServerID, ":"_s, ""_s) });
					String levelTargetPath = fs::CombinePath({ downloadsPath, String(levelName + ".j2l"_s) });
					fs::CreateDirectories(fs::GetDirectoryName(levelTargetPath));

					std::int64_t levelFileSize = packet.ReadVariableInt64();
					auto s = fs::Open(levelTargetPath, FileAccess::Write);
					if (s->IsValid()) {		
						s->Write(packet.GetCurrentPointer(levelFileSize), levelFileSize);
					} else {
						packet.Seek(levelFileSize, SeekOrigin::Current);
					}

					std::uint32_t tileSetCount = packet.ReadVariableUint32();
					for (std::uint32_t i = 0; i < tileSetCount; ++i) {
						std::uint32_t tileSetNameLength = packet.ReadVariableUint32();
						String tileSetName{NoInit, tileSetNameLength};
						packet.Read(tileSetName.data(), tileSetNameLength);
						std::int64_t tileSetFileSize = packet.ReadVariableInt64();

						auto s = fs::Open(fs::CombinePath({ downloadsPath, String(tileSetName + ".j2t"_s) }), FileAccess::Write);
						if (s->IsValid()) {
							s->Write(packet.GetCurrentPointer(tileSetFileSize), tileSetFileSize);
						} else {
							packet.Seek(tileSetFileSize, SeekOrigin::Current);
						}
					}
				}*/

				LOGI("[MP] ServerPacketType::LoadLevel - flags: 0x%02x, gameMode: %u, level: \"%s\"", flags, (std::uint32_t)gameMode, levelName.data());

				InvokeAsync([this, flags, levelState, gameMode, lastExitType, levelName = std::move(levelName), initialPlayerHealth, maxGameTimeSecs, totalKills, totalLaps, totalTreasureCollected]() {
					bool isReforged = (flags & 0x01) != 0;
					bool enableLedgeClimb = (flags & 0x02) != 0;
					bool elimination = (flags & 0x04) != 0;

					LevelInitialization levelInit(levelName, GameDifficulty::Normal, isReforged);
					levelInit.IsLocalSession = false;
					levelInit.LastExitType = lastExitType;

					auto& serverConfig = _networkManager->GetServerConfiguration();
					serverConfig.GameMode = gameMode;
					serverConfig.ReforgedGameplay = isReforged;
					serverConfig.Elimination = elimination;
					serverConfig.InitialPlayerHealth = initialPlayerHealth;
					serverConfig.MaxGameTimeSecs = maxGameTimeSecs;
					serverConfig.TotalKills = totalKills;
					serverConfig.TotalLaps = totalLaps;
					serverConfig.TotalTreasureCollected = totalTreasureCollected;

					auto levelHandler = std::make_shared<MpLevelHandler>(this,
						_networkManager.get(), levelState, enableLedgeClimb);
					if (levelHandler->Initialize(levelInit)) {
						SetStateHandler(std::move(levelHandler));
						return;
					}

					// Level failed to initialize, but probably some assets are missing, try to request them from the server
					MemoryStream packet(1);
					packet.WriteValue<std::uint8_t>(0);	// Reserved
					_networkManager->SendTo(AllPeers, NetworkChannel::Main, (std::uint8_t)ClientPacketType::RequestLevelAssets, packet);
				});
				break;
			}
		}
	}

	if (auto* multiLevelHandler = runtime_cast<MpLevelHandler*>(_currentHandler)) {
		if (multiLevelHandler->OnPacketReceived(peer, channelId, packetType, data)) {
			return;
		}
	}

	if (isServer && (ClientPacketType)packetType == ClientPacketType::Auth) {
		// Message was not processed by level handler, kick the client
		_networkManager->Kick(peer, Reason::ServerNotReady);
	}
}
#endif

void GameEventHandler::OnBeginInitialize()
{
#if defined(WITH_IMGUI)
	theApplication().GetDebugOverlaySettings().showInterface = true;
#endif

	_flags |= Flags::IsInitialized;

	auto& resolver = ContentResolver::Get();

#if defined(DEATH_TARGET_ANDROID)
	theApplication().SetAutoSuspension(true);

	if (Backends::AndroidJniWrap_Activity::hasExternalStoragePermission()) {
		_flags |= Flags::HasExternalStoragePermission;
	}

	// Try to load gamepad mappings from parent directory of `Source` on Android
	String mappingsPath = fs::CombinePath(fs::GetDirectoryName(resolver.GetSourcePath()), "gamecontrollerdb.txt"_s);
	if (fs::IsReadableFile(mappingsPath)) {
		theApplication().GetInputManager().addJoyMappingsFromFile(mappingsPath);
	}
#elif !defined(DEATH_TARGET_EMSCRIPTEN) && !defined(DEATH_TARGET_IOS) && !defined(DEATH_TARGET_SWITCH) && !defined(DEATH_TARGET_WINDOWS_RT)
	// Try to load gamepad mappings from `Content` directory
	String mappingsPath = fs::CombinePath(resolver.GetContentPath(), "gamecontrollerdb.txt"_s);
	if (fs::IsReadableFile(mappingsPath)) {
		theApplication().GetInputManager().addJoyMappingsFromFile(mappingsPath);
	}
#endif

#if !defined(DEATH_TARGET_EMSCRIPTEN) && !defined(DEATH_TARGET_SWITCH) && !defined(DEATH_TARGET_WINDOWS_RT)
	// Try to load gamepad mappings also from config directory
	auto configDir = PreferencesCache::GetDirectory();
	if (!configDir.empty()) {
		String mappingsPath2 = fs::CombinePath(configDir, "gamecontrollerdb.txt"_s);
		if (fs::IsReadableFile(mappingsPath2)) {
			theApplication().GetInputManager().addJoyMappingsFromFile(mappingsPath2);
		}
	}
#endif

#if defined(NCINE_HAS_WINDOWS)
#	if defined(DEATH_TARGET_WINDOWS_RT)
	// Xbox is always fullscreen
	if (PreferencesCache::EnableFullscreen || Environment::CurrentDeviceType == DeviceType::Xbox) {
#	else
	if (PreferencesCache::EnableFullscreen) {
#	endif
		theApplication().GetGfxDevice().setResolution(true);
		theApplication().GetInputManager().setCursor(IInputManager::Cursor::Hidden);
	}
#endif

	resolver.CompileShaders();
}

void GameEventHandler::OnAfterInitialize()
{
#if (defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)) || defined(DEATH_TARGET_UNIX)
	if (PreferencesCache::EnableDiscordIntegration) {
		DiscordRpcClient::Get().Connect("591586859960762378"_s);
	}
#endif

	if (PreferencesCache::Language[0] != '\0') {
		auto& resolver = ContentResolver::Get();
		auto& i18n = I18n::Get();
		if (!i18n.LoadFromFile(fs::CombinePath({ resolver.GetContentPath(), "Translations"_s, String(PreferencesCache::Language + ".mo"_s) }))) {
			i18n.LoadFromFile(fs::CombinePath({ resolver.GetCachePath(), "Translations"_s, String(PreferencesCache::Language + ".mo"_s) }));
		}
	}

#if defined(DEATH_TARGET_EMSCRIPTEN)
	// All required files are already included in Emscripten version, so nothing is verified
	_flags |= Flags::IsVerified | Flags::IsPlayable;
#else
	RefreshCache();
#endif
}

void GameEventHandler::SetStateHandler(std::shared_ptr<IStateHandler>&& handler)
{
	_currentHandler = std::move(handler);

	Viewport::GetChain().clear();
	Vector2i res = theApplication().GetResolution();
	_currentHandler->OnInitializeViewport(res.X, res.Y);
}

void GameEventHandler::WaitForVerify()
{
	while ((_flags & Flags::IsVerified) != Flags::IsVerified) {
		Thread::Sleep(33);
	}
}

#if !defined(DEATH_TARGET_EMSCRIPTEN)
void GameEventHandler::RefreshCache()
{
	ZoneScopedC(0x888888);

	if (PreferencesCache::BypassCache) {
		LOGI("Cache is bypassed by command-line parameter");
		_flags |= Flags::IsVerified | Flags::IsPlayable;
		return;
	}

	constexpr std::uint64_t currentVersion = parseVersion(NCINE_VERSION_s);

	auto& resolver = ContentResolver::Get();
	auto cachePath = fs::CombinePath(resolver.GetCachePath(), "Source.idx"_s);

	// Check cache state
	{
		auto s = fs::Open(cachePath, FileAccess::Read);
		if (s->GetSize() < 16) {
			goto RecreateCache;
		}

		std::uint64_t signature = s->ReadValue<std::uint64_t>();
		std::uint8_t fileType = s->ReadValue<std::uint8_t>();
		std::uint16_t version = s->ReadValue<std::uint16_t>();
		if (signature != 0x2095A59FF0BFBBEF || fileType != ContentResolver::CacheIndexFile || version != Compatibility::JJ2Anims::CacheVersion) {
			goto RecreateCache;
		}

		std::uint8_t flags = s->ReadValue<std::uint8_t>();
		if ((flags & 0x01) == 0x01) {
			// Don't overwrite cache
			LOGI("Cache is protected");
			_flags |= Flags::IsVerified | Flags::IsPlayable;
			return;
		}

		String animsPath = fs::FindPathCaseInsensitive(fs::CombinePath(resolver.GetSourcePath(), "Anims.j2a"_s));
		if (!fs::IsReadableFile(animsPath)) {
			animsPath = fs::FindPathCaseInsensitive(fs::CombinePath(resolver.GetSourcePath(), "AnimsSw.j2a"_s));
		}
		std::int64_t animsCached = s->ReadValue<std::int64_t>();
		std::int64_t animsModified = fs::GetLastModificationTime(animsPath).ToUnixMilliseconds();
		if (animsModified != 0 && animsCached != animsModified) {
			goto RecreateCache;
		}

		// If some events were added, recreate cache
		std::uint16_t eventTypeCount = s->ReadValue<std::uint16_t>();
		if (eventTypeCount != (std::uint16_t)EventType::Count) {
			goto RecreateCache;
		}

		// Cache is up-to-date
		std::uint64_t lastVersion = s->ReadValue<std::uint64_t>();

		// Close the file, so it can be writable for possible update
		s = nullptr;

		RefreshCacheLevels(false);

		if (currentVersion != lastVersion) {
			if ((lastVersion & 0xFFFFFFFFULL) == 0x0FFFFFFFULL) {
				LOGI("Cache is already up-to-date, but created in experimental build v%i.%i.0", (lastVersion >> 48) & 0xFFFFULL, (lastVersion >> 32) & 0xFFFFULL);
			} else {
				LOGI("Cache is already up-to-date, but created in different build v%i.%i.%i", (lastVersion >> 48) & 0xFFFFULL, (lastVersion >> 32) & 0xFFFFULL, lastVersion & 0xFFFFFFFFULL);
			}

			WriteCacheDescriptor(cachePath, currentVersion, animsModified);

			if (!resolver.IsHeadless()) {
				std::uint32_t filesRemoved = RenderResources::binaryShaderCache().Prune();
				LOGI("Pruning binary shader cache (removed %u files)...", filesRemoved);
			}
		} else {
			LOGI("Cache is already up-to-date");
		}

		_flags |= Flags::IsVerified | Flags::IsPlayable;
		return;
	}

RecreateCache:
	// "Source" directory must be case in-sensitive
	String animsPath = fs::FindPathCaseInsensitive(fs::CombinePath(resolver.GetSourcePath(), "Anims.j2a"_s));
	if (!fs::IsReadableFile(animsPath)) {
		animsPath = fs::FindPathCaseInsensitive(fs::CombinePath(resolver.GetSourcePath(), "AnimsSw.j2a"_s));
		if (!fs::IsReadableFile(animsPath)) {
			String sourcePath = fs::GetAbsolutePath(resolver.GetSourcePath());
			if (sourcePath.empty()) {
				// If `Source` directory doesn't exist, GetAbsolutePath() will fail
				sourcePath = fs::CombinePath(fs::GetWorkingDirectory(), resolver.GetSourcePath());
			}

			LOGE("Cannot open \"…%sSource%sAnims.j2a\" file! Make sure Jazz Jackrabbit 2 files are present in \"%s\" directory.", fs::PathSeparator, fs::PathSeparator, sourcePath.data());
			_flags |= Flags::IsVerified;
			return;
		}
	}

	fs::CreateDirectories(resolver.GetCachePath());

	// Delete cache from previous versions
	String animationsPath = fs::CombinePath(resolver.GetCachePath(), "Animations"_s);
	fs::RemoveDirectoryRecursive(animationsPath);
	fs::RemoveDirectoryRecursive(fs::CombinePath(resolver.GetCachePath(), "Downloads"_s));

	// Create .pak file
	{
		std::int32_t t = 1;
		std::unique_ptr<PakWriter> pakWriter = std::make_unique<PakWriter>(fs::CombinePath(resolver.GetCachePath(), "Source.pak"_s));
		while (!pakWriter->IsValid()) {
			if (t > 5) {
				LOGE("Cannot open \"…%sCache%sSource.pak\" file for writing! Please check if this file is accessible and try again.", fs::PathSeparator, fs::PathSeparator);
				_flags |= Flags::IsVerified;
				return;
			}

			pakWriter = nullptr;
			Thread::Sleep(t * 100);
			t++;
			pakWriter = std::make_unique<PakWriter>(fs::CombinePath(resolver.GetCachePath(), "Source.pak"_s));
		}

		Compatibility::JJ2Version version = Compatibility::JJ2Anims::Convert(animsPath, *pakWriter);
		if (version == Compatibility::JJ2Version::Unknown) {
			LOGE("Provided Jazz Jackrabbit 2 version is not supported. Make sure supported Jazz Jackrabbit 2 version is present in \"%s\" directory.", resolver.GetSourcePath().data());
			_flags |= Flags::IsVerified;
			return;
		}

		Compatibility::JJ2Data data;
		if (data.Open(fs::CombinePath(resolver.GetSourcePath(), "Data.j2d"_s), false)) {
			data.Convert(*pakWriter, version);
		}
	}

	RefreshCacheLevels(true);

	LOGI("Cache was recreated");
	std::int64_t animsModified = fs::GetLastModificationTime(animsPath).ToUnixMilliseconds();
	WriteCacheDescriptor(cachePath, currentVersion, animsModified);

	if (!resolver.IsHeadless()) {
		std::uint32_t filesRemoved = RenderResources::binaryShaderCache().Prune();
		LOGI("Pruning binary shader cache (removed %u files)...", filesRemoved);
	}

	resolver.RemountPaks();

	_flags |= Flags::IsVerified | Flags::IsPlayable;
}

void GameEventHandler::RefreshCacheLevels(bool recreateAll)
{
	ZoneScopedC(0x888888);

	LOGI("Searching for levels...");

	auto& resolver = ContentResolver::Get();

	Compatibility::EventConverter eventConverter;

	bool hasChristmasChronicles = fs::IsReadableFile(fs::FindPathCaseInsensitive(fs::CombinePath(resolver.GetSourcePath(), "xmas99.j2e"_s)));
	const HashMap<String, Pair<String, String>> knownLevels = {
		{ "trainer"_s, { "prince"_s, {} } },
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

		{ "xmas1"_s, { "xmas99"_s, "01"_s } },
		{ "xmas2"_s, { "xmas99"_s, "02"_s } },
		{ "xmas3"_s, { "xmas99"_s, "03"_s } },

		{ "easter1"_s, { "secretf"_s, "01"_s } },
		{ "easter2"_s, { "secretf"_s, "02"_s } },
		{ "easter3"_s, { "secretf"_s, "03"_s } },
		{ "haunted1"_s, { "secretf"_s, "04"_s } },
		{ "haunted2"_s, { "secretf"_s, "05"_s } },
		{ "haunted3"_s, { "secretf"_s, "06"_s } },
		{ "town1"_s, { "secretf"_s, "07"_s } },
		{ "town2"_s, { "secretf"_s, "08"_s } },
		{ "town3"_s, { "secretf"_s, "09"_s } },

		// Holiday Hare '17
		{ "hh17_level00"_s, { "hh17"_s, {} } },
		{ "hh17_level01"_s, { "hh17"_s, {} } },
		{ "hh17_level01_save"_s, { "hh17"_s, {} } },
		{ "hh17_level02"_s, { "hh17"_s, {} } },
		{ "hh17_level02_save"_s, { "hh17"_s, {} } },
		{ "hh17_level03"_s, { "hh17"_s, {} } },
		{ "hh17_level03_save"_s, { "hh17"_s, {} } },
		{ "hh17_level04"_s, { "hh17"_s, {} } },
		{ "hh17_level04_save"_s, { "hh17"_s, {} } },
		{ "hh17_level05"_s, { "hh17"_s, {} } },
		{ "hh17_level05_save"_s, { "hh17"_s, {} } },
		{ "hh17_level06"_s, { "hh17"_s, {} } },
		{ "hh17_level06_save"_s, { "hh17"_s, {} } },
		{ "hh17_level07"_s, { "hh17"_s, {} } },
		{ "hh17_level07_save"_s, { "hh17"_s, {} } },
		{ "hh17_ending"_s, { "hh17"_s, {} } },
		{ "hh17_guardian"_s, { "hh17"_s, {} } },

		// Holiday Hare '18
		{ "hh18_level01"_s, { "hh18"_s, {} } },
		{ "hh18_level02"_s, { "hh18"_s, {} } },
		{ "hh18_level03"_s, { "hh18"_s, {} } },
		{ "hh18_level04"_s, { "hh18"_s, {} } },
		{ "hh18_level05"_s, { "hh18"_s, {} } },
		{ "hh18_level06"_s, { "hh18"_s, {} } },
		{ "hh18_level07"_s, { "hh18"_s, {} } },
		{ "hh18_save01"_s, { "hh18"_s, {} } },
		{ "hh18_save02"_s, { "hh18"_s, {} } },
		{ "hh18_save03"_s, { "hh18"_s, {} } },
		{ "hh18_save04"_s, { "hh18"_s, {} } },
		{ "hh18_save05"_s, { "hh18"_s, {} } },
		{ "hh18_save06"_s, { "hh18"_s, {} } },
		{ "hh18_save07"_s, { "hh18"_s, {} } },
		{ "hh18_ending"_s, { "hh18"_s, {} } },
		{ "hh18_guardian"_s, { "hh18"_s, {} } },

		// Special names
		{ "end"_s, { {}, ":end"_s } },
		{ "endepis"_s, { {}, ":end"_s } },
		{ "ending"_s, { {}, ":credits"_s } }
	};

	auto LevelTokenConversion = [&knownLevels](StringView levelToken) -> Compatibility::JJ2Level::LevelToken {
		auto it = knownLevels.find(levelToken);
		if (it != knownLevels.end()) {
			if (it->second.second().empty()) {
				return { it->second.first(), levelToken };
			}
			return { it->second.first(), (it->second.second()[0] == ':' ? it->second.second() : (it->second.second() + "_"_s + levelToken)) };
		}
		return { {}, levelToken };
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
		} else if (episode->Name == "hh24"_s && episode->DisplayName == "HH24"_s) {
			return "Holiday Hare '24"_s;
		} else {
			// Strip formatting
			return Compatibility::JJ2Strings::RecodeString(episode->DisplayName, true);
		}
	};

	auto EpisodePrevNext = [](Compatibility::JJ2Episode* episode) -> Pair<String, String> {
		if (episode->Name == "prince"_s) {
			return { {}, "rescue"_s };
		} else if (episode->Name == "rescue"_s) {
			return { "prince"_s, "flash"_s };
		} else if (episode->Name == "flash"_s) {
			return { "rescue"_s, "monk"_s };
		} else if (episode->Name == "monk"_s) {
			return { "flash"_s, {} };
		} else {
			return { {}, {} };
		}
	};

	String episodesPath = fs::CombinePath(resolver.GetCachePath(), "Episodes"_s);
	if (recreateAll) {
		fs::RemoveDirectoryRecursive(episodesPath);
		fs::CreateDirectories(episodesPath);
	}

	HashMap<String, bool> usedTilesets;

	for (auto item : fs::Directory(fs::FindPathCaseInsensitive(resolver.GetSourcePath()), fs::EnumerationOptions::SkipDirectories)) {
		auto extension = fs::GetExtension(item);
		if (extension == "j2e"_s || extension == "j2pe"_s) {
			// Episode
			if (!recreateAll) {
				String episodeName = fs::GetFileNameWithoutExtension(item);
				StringUtils::lowercaseInPlace(episodeName);
				String fullPath = fs::CombinePath(episodesPath, String((episodeName == "xmas98"_s ? "xmas99"_s : StringView(episodeName)) + ".j2e"_s));
				if (fs::FileExists(fullPath)) {
					continue;
				}
			}

			Compatibility::JJ2Episode episode;
			if (episode.Open(item)) {
				if (hasChristmasChronicles && episode.Name == "xmas98"_s) {
					continue;
				}
				if (episode.Name == "home"_s) {
					episode.FirstLevel = ":custom-levels"_s;
					episode.Position = UINT16_MAX;
				} else if (episode.Position >= UINT32_MAX) {
					episode.Position = UINT16_MAX - 1;
				}

				String fullPath = fs::CombinePath(episodesPath, String((episode.Name == "xmas98"_s ? "xmas99"_s : StringView(episode.Name)) + ".j2e"_s));
				episode.Convert(fullPath, std::move(LevelTokenConversion), std::move(EpisodeNameConversion), std::move(EpisodePrevNext));
			}
		} else if (extension == "j2l"_s) {
			// Level
			String levelName = fs::GetFileNameWithoutExtension(item);
			if (levelName.find("-MLLE-Data-"_s) == nullptr) {
				if (!recreateAll) {
					StringUtils::lowercaseInPlace(levelName);

					String fullPath;
					auto it = knownLevels.find(levelName);
					if (it != knownLevels.end()) {
						if (it->second.second().empty()) {
							fullPath = fs::CombinePath({ episodesPath, it->second.first(), String(levelName + ".j2l"_s) });
						} else {
							fullPath = fs::CombinePath({ episodesPath, it->second.first(), String(it->second.second() + '_' + levelName + ".j2l"_s) });
						}
					} else {
						fullPath = fs::CombinePath({ episodesPath, "unknown"_s, String(levelName + ".j2l"_s) });
					}

					if (fs::FileExists(fullPath)) {
						continue;
					}
				}

				Compatibility::JJ2Level level;
				if (level.Open(item, false)) {
					String fullPath;
					auto it = knownLevels.find(level.LevelName);
					if (it != knownLevels.end()) {
						if (it->second.second().empty()) {
							fullPath = fs::CombinePath({ episodesPath, it->second.first(), String(level.LevelName + ".j2l"_s) });
						} else {
							fullPath = fs::CombinePath({ episodesPath, it->second.first(), String(it->second.second() + '_' + level.LevelName + ".j2l"_s) });
						}
					} else {
						fullPath = fs::CombinePath({ episodesPath, "unknown"_s, String(level.LevelName + ".j2l"_s) });
					}

					fs::CreateDirectories(fs::GetDirectoryName(fullPath));
					level.Convert(fullPath, eventConverter, LevelTokenConversion);

					usedTilesets.emplace(level.Tileset, true);
					for (auto& extraTileset : level.ExtraTilesets) {
						usedTilesets.emplace(extraTileset.Name, true);
					}

					// Also copy level script file if exists
					StringView foundDot = item.findLastOr('.', item.end());
					String scriptPath = item.prefix(foundDot.begin()) + ".j2as"_s;
					auto adjustedPath = fs::FindPathCaseInsensitive(scriptPath);
					if (fs::IsReadableFile(adjustedPath)) {
						foundDot = fullPath.findLastOr('.', fullPath.end());
						fs::Copy(adjustedPath, String(fullPath.prefix(foundDot.begin()) + ".j2as"_s));
					}
				}
			}
		}
#if defined(DEATH_DEBUG)
		/*else if (extension == "j2s"_s && recreateAll) {
			// Translations
			Compatibility::JJ2Strings strings;
			strings.Open(item);

			String fullPath = fs::CombinePath({ resolver.GetCachePath(), "ExtractedTranslations"_s, String(strings.Name + ".h"_s) });
			fs::CreateDirectories(fs::GetDirectoryName(fullPath));
			strings.Convert(fullPath, LevelTokenConversion);
		}*/
#endif
	}

	if (recreateAll || !usedTilesets.empty()) {
		// Convert only used tilesets
		LOGI("Converting used tilesets...");
		String tilesetsPath = fs::CombinePath(resolver.GetCachePath(), "Tilesets"_s);
		if (recreateAll) {
			fs::RemoveDirectoryRecursive(tilesetsPath);
			fs::CreateDirectories(tilesetsPath);
		}

		for (auto& pair : usedTilesets) {
			String tilesetPath = fs::CombinePath(resolver.GetSourcePath(), String(pair.first + ".j2t"_s));
			auto adjustedPath = fs::FindPathCaseInsensitive(tilesetPath);
			if (fs::IsReadableFile(adjustedPath)) {
				Compatibility::JJ2Tileset tileset;
				if (tileset.Open(adjustedPath, false)) {
					tileset.Convert(fs::CombinePath({ tilesetsPath, String(pair.first + ".j2t"_s) }));
				}
			}
		}
	}
}

void GameEventHandler::CheckUpdates()
{
#if !defined(DEATH_DEBUG)
	ZoneScopedC(0x888888);

	String url = "https://deat.tk/downloads/games/jazz2/updates?v=" NCINE_VERSION "&d=" + PreferencesCache::GetDeviceID();
	auto request = WebSession::GetDefault().CreateRequest(url);
	auto result = request.Execute();
	if (result) {
		auto s = request.GetResponse().AsString();
		constexpr std::uint64_t currentVersion = parseVersion(NCINE_VERSION_s);
		std::uint64_t latestVersion = parseVersion(s);
		if (currentVersion < latestVersion) {
			_newestVersion = s;
		}
	} else {
		LOGW("Failed to check for updates: %s", result.error.data());
	}
#endif
}
#endif

bool GameEventHandler::SetLevelHandler(const LevelInitialization& levelInit)
{
#if defined(WITH_MULTIPLAYER)
	if (!levelInit.IsLocalSession) {
		// TODO: Set proper game mode and ledge climb
		auto levelHandler = std::make_shared<MpLevelHandler>(this,
			_networkManager.get(), MpLevelHandler::LevelState::InitialUpdatePending, true);
		if (!levelHandler->Initialize(levelInit)) {
			return false;
		}
		SetStateHandler(std::move(levelHandler));
		return true;
	}
#endif

	auto levelHandler = std::make_shared<LevelHandler>(this);
	if (!levelHandler->Initialize(levelInit)) {
		return false;
	}
	SetStateHandler(std::move(levelHandler));

#if !defined(SHAREWARE_DEMO_ONLY)
	if (levelInit.IsLocalSession) {
		RemoveResumableStateIfAny();
	}
#endif

	return true;
}

void GameEventHandler::HandleEndOfGame(const LevelInitialization& levelInit, bool playerDied)
{
	const PlayerCarryOver* firstPlayer;
	std::size_t playerCount = levelInit.GetPlayerCount(&firstPlayer);

	InGameConsole::Clear();
	auto mainMenu = std::make_shared<Menu::MainMenu>(this, false);
	if (playerCount == 1 && levelInit.IsLocalSession) {
		std::int32_t seriesIndex = Menu::HighscoresSection::TryGetSeriesIndex(levelInit.LastEpisodeName, playerDied);
		if (seriesIndex >= 0) {
			mainMenu->SwitchToSection<Menu::HighscoresSection>(seriesIndex, levelInit.Difficulty,
				levelInit.IsReforged, levelInit.CheatsUsed, levelInit.ElapsedMilliseconds, *firstPlayer);
		}
	}

	SetStateHandler(std::move(mainMenu));
}

void GameEventHandler::WriteCacheDescriptor(StringView path, std::uint64_t currentVersion, std::int64_t animsModified)
{
	auto so = fs::Open(path, FileAccess::Write);
	so->WriteValue<std::uint64_t>(0x2095A59FF0BFBBEF);	// Signature
	so->WriteValue<std::uint8_t>(ContentResolver::CacheIndexFile);
	so->WriteValue<std::uint16_t>(Compatibility::JJ2Anims::CacheVersion);
	so->WriteValue<std::uint8_t>(0x00);					// Flags
	so->WriteValue<std::int64_t>(animsModified);
	so->WriteValue<std::uint16_t>((std::uint16_t)EventType::Count);
	so->WriteValue<std::uint64_t>(currentVersion);
}

void GameEventHandler::SaveEpisodeEnd(const LevelInitialization& levelInit)
{
	if (levelInit.LastEpisodeName.empty() || levelInit.LastEpisodeName == "unknown"_s) {
		return;
	}

	const PlayerCarryOver* firstPlayer;
	std::size_t playerCount = levelInit.GetPlayerCount(&firstPlayer);

	PreferencesCache::RemoveEpisodeContinue(levelInit.LastEpisodeName);

	if (playerCount > 0) {
		auto* prevEnd = PreferencesCache::GetEpisodeEnd(levelInit.LastEpisodeName);

		bool shouldSaveEpisodeEnd = (prevEnd == nullptr);
		if (!shouldSaveEpisodeEnd) {
			switch (PreferencesCache::OverwriteEpisodeEnd) {
				// Don't overwrite existing data in multiplayer/split-screen
				default: shouldSaveEpisodeEnd = (playerCount == 1); break;
				case EpisodeEndOverwriteMode::NoCheatsOnly: shouldSaveEpisodeEnd = (playerCount == 1 && !levelInit.CheatsUsed); break;
				case EpisodeEndOverwriteMode::HigherScoreOnly: shouldSaveEpisodeEnd = (playerCount == 1 && !levelInit.CheatsUsed && firstPlayer->Score >= prevEnd->Score); break;
			}
		}

		if (shouldSaveEpisodeEnd) {
			auto* episodeEnd = PreferencesCache::GetEpisodeEnd(levelInit.LastEpisodeName, true);
			episodeEnd->Flags = EpisodeContinuationFlags::IsCompleted;
			if (levelInit.CheatsUsed) {
				episodeEnd->Flags |= EpisodeContinuationFlags::CheatsUsed;
			}

			episodeEnd->Lives = firstPlayer->Lives;
			episodeEnd->Score = firstPlayer->Score;
			episodeEnd->ElapsedMilliseconds = levelInit.ElapsedMilliseconds;
			std::memcpy(episodeEnd->Gems, firstPlayer->Gems, sizeof(firstPlayer->Gems));
			std::memcpy(episodeEnd->Ammo, firstPlayer->Ammo, sizeof(firstPlayer->Ammo));
			std::memcpy(episodeEnd->WeaponUpgrades, firstPlayer->WeaponUpgrades, sizeof(firstPlayer->WeaponUpgrades));
		}
	}

	PreferencesCache::Save();
}

void GameEventHandler::SaveEpisodeContinue(const LevelInitialization& levelInit)
{
	if (levelInit.LevelName.empty()) {
		return;
	}

	auto p = levelInit.LevelName.partition('/');
	if (!levelInit.IsLocalSession || p[0] == "unknown"_s || levelInit.LevelName == "prince/trainer"_s) {
		return;
	}

	std::optional<Episode> currentEpisode = ContentResolver::Get().GetEpisode(p[0]);
	if (!currentEpisode || currentEpisode->FirstLevel == p[2]) {
		return;
	}

	const PlayerCarryOver* firstPlayer;
	std::size_t playerCount = levelInit.GetPlayerCount(&firstPlayer);

	// Don't save continue in multiplayer
	if (playerCount == 1) {
		auto* episodeContinue = PreferencesCache::GetEpisodeContinue(p[0], true);
		episodeContinue->LevelName = p[2];
		episodeContinue->State.Flags = EpisodeContinuationFlags::None;
		if (levelInit.CheatsUsed) {
			episodeContinue->State.Flags |= EpisodeContinuationFlags::CheatsUsed;
		}

		episodeContinue->State.DifficultyAndPlayerType = ((std::int32_t)levelInit.Difficulty & 0x0f) | (((std::int32_t)firstPlayer->Type & 0x0f) << 4);
		episodeContinue->State.Lives = firstPlayer->Lives;
		episodeContinue->State.Score = firstPlayer->Score;
		episodeContinue->State.ElapsedMilliseconds = levelInit.ElapsedMilliseconds;
		std::memcpy(episodeContinue->State.Gems, firstPlayer->Gems, sizeof(firstPlayer->Gems));
		std::memcpy(episodeContinue->State.Ammo, firstPlayer->Ammo, sizeof(firstPlayer->Ammo));
		std::memcpy(episodeContinue->State.WeaponUpgrades, firstPlayer->WeaponUpgrades, sizeof(firstPlayer->WeaponUpgrades));

		PreferencesCache::TutorialCompleted = true;
		PreferencesCache::Save();
	} else if (!PreferencesCache::TutorialCompleted) {
		PreferencesCache::TutorialCompleted = true;
		PreferencesCache::Save();
	}
}

void GameEventHandler::ExtractPakFile(StringView pakFile, StringView targetPath)
{
	PakFile pak(pakFile);
	if (!pak.IsValid()) {
		LOGE("Invalid .pak file specified");
		return;
	}

	LOGI("Extracting files from \"%s\" to \"%s\"...", pakFile.data(), targetPath.data());

	SmallVector<String> queue;
	queue.emplace_back();	// Root

	std::int32_t successCount = 0, errorCount = 0;
	for (std::size_t i = 0; i < queue.size(); i++) {
		for (auto childItem : PakFile::Directory(pak, queue[i])) {
			auto sourceFile = pak.OpenFile(childItem);
			if (sourceFile != nullptr) {
				auto targetFilePath = fs::CombinePath(targetPath, childItem);
				fs::CreateDirectories(fs::GetDirectoryName(targetFilePath));
				auto targetFile = fs::Open(targetFilePath, FileAccess::Write);
				if (targetFile->IsValid()) {
					sourceFile->CopyTo(*targetFile);
					successCount++;
				} else {
					LOGE("Failed to create target file \"%s\"", targetFilePath.data());
					errorCount++;
				}
			} else {
				// Probably directory
				queue.emplace_back(childItem);
			}
		}
	}

	LOGI("%i files extracted successfully, %i files failed with error", successCount, errorCount);
}

#if defined(DEATH_TARGET_ANDROID)
std::unique_ptr<IAppEventHandler> CreateAppEventHandler()
{
	return std::make_unique<GameEventHandler>();
}
#elif defined(DEATH_TARGET_WINDOWS_RT)
int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR pCmdLine, int nCmdShow)
{
	return UwpApplication::Run([]() -> std::unique_ptr<IAppEventHandler> {
		return std::make_unique<GameEventHandler>();
	});
}
#elif defined(DEATH_TARGET_WINDOWS) && !defined(WITH_QT5)
#	if defined(WITH_MULTIPLAYER) && defined(DEDICATED_SERVER)
int wmain(int argc, wchar_t* argv[])
{
	return MainApplication::Run([]() -> std::unique_ptr<IAppEventHandler> {
		return std::make_unique<GameEventHandler>();
	}, argc, argv);
}
#	else
int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR pCmdLine, int nCmdShow)
{
	return MainApplication::Run([]() -> std::unique_ptr<IAppEventHandler> {
		return std::make_unique<GameEventHandler>();
	}, __argc, __wargv);
}
#	endif
#else
#if defined(DEATH_TARGET_UNIX)
int PrintVersion(bool logoVisible)
{
	if (logoVisible) {
		static const char Copyright[] = "© 2016-" NCINE_BUILD_YEAR " Dan R.";
		static const char Reset[] = "\033[0m";
		static const char Bold[] = "\033[1m";
		static const char Faint[] = "\033[2m";

		constexpr std::size_t LogoWidth = 58;

		char padding[(LogoWidth / 2) + 2];
		std::size_t paddingLength = (LogoWidth - (arraySize(NCINE_APP_NAME) - 1) - 1 - (arraySize(NCINE_VERSION) - 1) + 1) / 2;
		for (std::size_t j = 0; j < paddingLength; j++) {
			padding[j] = ' ';
		}
		padding[paddingLength] = '\0';
		fprintf(stdout, "%s%s%s %s%s%s\n", padding, Reset, NCINE_APP_NAME, Bold, NCINE_VERSION, Reset);

		paddingLength = (LogoWidth - (arraySize(Copyright) - 1) + 1) / 2;
		for (std::size_t j = 0; j < paddingLength; j++) {
			padding[j] = ' ';
		}
		padding[paddingLength] = '\0';
		fprintf(stdout, "%s%s%s%s%s\n\n", padding, Reset, Faint, Copyright, Reset);
	} else {
		fputs(NCINE_APP_NAME " " NCINE_VERSION "\n", stdout);
	}
	return 0;
}
#endif

int main(int argc, char** argv)
{
#if !defined(DEATH_TARGET_SWITCH)
	bool logoVisible = false;
#	if defined(DEATH_TRACE) && (defined(DEATH_TARGET_APPLE) || defined(DEATH_TARGET_UNIX))
	bool hasVirtualTerminal = ::isatty(1);
	if (hasVirtualTerminal) {
		const char* term = ::getenv("TERM");
		if (term != nullptr && (strstr(term, "truecolor") || strstr(term, "24bit") ||
			strstr(term, "256color") || strstr(term, "rxvt-xpm"))) {
			fwrite(TermLogo, sizeof(unsigned char), arraySize(TermLogo), stdout);
			logoVisible = true;
		}
	}
#	endif
#	if defined(DEATH_TARGET_UNIX)
	for (std::size_t i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--version") == 0) {
			// Just print current version below the logo and quit
			return PrintVersion(logoVisible);
		}
	}
#	endif
#endif

	return MainApplication::Run([]() -> std::unique_ptr<IAppEventHandler> {
		return std::make_unique<GameEventHandler>();
	}, argc, argv);
}
#endif