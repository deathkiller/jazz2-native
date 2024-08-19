#include "Common.h"

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
#include "nCine/Base/Timer.h"
#include "nCine/Graphics/BinaryShaderCache.h"
#include "nCine/Graphics/RenderResources.h"
#include "nCine/Input/IInputEventHandler.h"
#include "nCine/Threading/Thread.h"

#include "Jazz2/IRootController.h"
#include "Jazz2/ContentResolver.h"
#include "Jazz2/LevelHandler.h"
#include "Jazz2/PreferencesCache.h"
#include "Jazz2/UI/Cinematics.h"
#include "Jazz2/UI/ControlScheme.h"
#include "Jazz2/UI/LoadingHandler.h"
#include "Jazz2/UI/Menu/MainMenu.h"
#include "Jazz2/UI/Menu/LoadingSection.h"
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
#	include "Jazz2/Multiplayer/MultiLevelHandler.h"
#	include "Jazz2/Multiplayer/PacketTypes.h"
using namespace Jazz2::Multiplayer;
#endif

#if defined(DEATH_TRACE) && (defined(DEATH_TARGET_APPLE) || defined(DEATH_TARGET_UNIX))
#	include "TermLogo.h"
#endif

#if (defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)) || defined(DEATH_TARGET_UNIX)
#	include "Jazz2/UI/DiscordRpcClient.h"
#endif

#if defined(DEATH_TARGET_WINDOWS) && !defined(WITH_QT5)
#	include <cstdlib> // for `__argc` and `__argv`
#endif

#include <Containers/StringConcatenable.h>
#include <Cpu.h>
#include <Environment.h>
#include <IO/DeflateStream.h>
#include <IO/FileSystem.h>
#include <IO/PakFile.h>

#if !defined(DEATH_DEBUG)
#	include <IO/HttpRequest.h>
#	if defined(DEATH_TARGET_WINDOWS)
#		include <Utf8.h>
#	endif
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
	static constexpr std::uint16_t StateVersion = 2;
	static constexpr char StateFileName[] = "Jazz2.resume";

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

	void OnKeyPressed(const KeyboardEvent& event) override;
	void OnKeyReleased(const KeyboardEvent& event) override;
	void OnTouchEvent(const TouchEvent& event) override;

	void InvokeAsync(const std::function<void()>& callback) override;
	void InvokeAsync(std::function<void()>&& callback) override;
	void GoToMainMenu(bool afterIntro) override;
	void ChangeLevel(LevelInitialization&& levelInit) override;
	bool HasResumableState() const override;
	void ResumeSavedState() override;
	bool SaveCurrentStateIfAny() override;

#if defined(WITH_MULTIPLAYER)
	bool ConnectToServer(const StringView address, std::uint16_t port) override;
	bool CreateServer(LevelInitialization&& levelInit, std::uint16_t port) override;

	ConnectionResult OnPeerConnected(const Peer& peer, std::uint32_t clientData) override;
	void OnPeerDisconnected(const Peer& peer, Reason reason) override;
	void OnPacketReceived(const Peer& peer, std::uint8_t channelId, std::uint8_t* data, std::size_t dataLength) override;
#endif

	Flags GetFlags() const override {
		return _flags;
	}

	StringView GetNewestVersion() const override {
		return _newestVersion;
	}

#if !defined(DEATH_TARGET_EMSCRIPTEN)
	void RefreshCacheLevels() override;
#else
	void RefreshCacheLevels() override { }
#endif

private:
	Flags _flags = Flags::None;
	std::unique_ptr<IStateHandler> _currentHandler;
	SmallVector<std::function<void()>> _pendingCallbacks;
	char _newestVersion[20];
#if defined(WITH_MULTIPLAYER)
	std::unique_ptr<NetworkManager> _networkManager;
#endif

	void OnBeforeInitialize();
	void OnAfterInitialize();
	void SetStateHandler(std::unique_ptr<IStateHandler>&& handler);
#if !defined(DEATH_TARGET_EMSCRIPTEN)
	void RefreshCache();
	void CheckUpdates();
#endif
	bool SetLevelHandler(const LevelInitialization& levelInit);
	void RemoveResumableStateIfAny();
#if defined(DEATH_TARGET_ANDROID)
	void ApplyActivityIcon();
#endif
	static void WriteCacheDescriptor(const StringView path, std::uint64_t currentVersion, std::int64_t animsModified);
	static void SaveEpisodeEnd(const LevelInitialization& levelInit);
	static void SaveEpisodeContinue(const LevelInitialization& levelInit);
	static bool TryParseAddressAndPort(const StringView input, String& address, std::uint16_t& port);
	static void ExtractPakFile(const StringView pakFile, const StringView targetPath);
};

void GameEventHandler::OnPreInitialize(AppConfiguration& config)
{
	ZoneScopedC(0x888888);

#if defined(DEATH_TARGET_APPLE) || defined(DEATH_TARGET_UNIX) || (defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT))
	// Allow `/extract-pak` only on PC platforms
	if (config.argc() >= 3) {
		for (std::int32_t i = 0; i < config.argc() - 2; i++) {
			auto arg = config.argv(i);
			if (arg == "/extract-pak"_s) {
#	if defined(DEATH_TRACE) && defined(DEATH_TARGET_WINDOWS)
				// Always attach to console in this case
				theApplication().AttachTraceTarget(MainApplication::ConsoleTarget);
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
		}
	}
#endif

	PreferencesCache::Initialize(config);

	config.windowTitle = NCINE_APP_NAME;
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

#if defined(WITH_IMGUI)
	config.withDebugOverlay = true;
#endif
}

void GameEventHandler::OnInitialize()
{
	ZoneScopedC(0x888888);

	OnBeforeInitialize();

#if !defined(SHAREWARE_DEMO_ONLY)
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
		Thread::SetCurrentName("Parallel initialization");

		auto handler = static_cast<GameEventHandler*>(arg);
		ASSERT(handler != nullptr);

		handler->OnAfterInitialize();
#	if !defined(DEATH_TARGET_EMSCRIPTEN)
		handler->CheckUpdates();
#	endif
	}, this);

#	if defined(WITH_MULTIPLAYER)
	// TODO: Multiplayer
	/*if (PreferencesCache::InitialState == "/server"_s) {
		thread.Join();

		auto mainMenu = std::make_unique<Menu::MainMenu>(this, false);
		mainMenu->SwitchToSection<Menu::LoadingSection>(_("Creating server..."));
		SetStateHandler(std::move(mainMenu));

		// TODO: Hardcoded port
		CreateServer(MultiplayerDefaultPort);
	} else*/ if (PreferencesCache::InitialState.hasPrefix("/connect:"_s)) {
		thread.Join();

		String address; std::uint16_t port;
		if (TryParseAddressAndPort(PreferencesCache::InitialState.exceptPrefix(9), address, port)) {
			if (port == 0) {
				port = MultiplayerDefaultPort;
			}

			auto mainMenu = std::make_unique<Menu::MainMenu>(this, false);
			mainMenu->SwitchToSection<Menu::LoadingSection>(_f("Connecting to %s:%u...", address.data(), port));
			SetStateHandler(std::move(mainMenu));

			ConnectToServer(address.data(), (std::uint16_t)port);
			return;
		}
	}
#	endif

	SetStateHandler(std::make_unique<Cinematics>(this, "intro"_s, [thread](IRootController* root, bool endOfStream) mutable {
		if ((root->GetFlags() & Flags::IsVerified) == Flags::IsVerified) {
			root->GoToMainMenu(endOfStream);
		} else if (!endOfStream) {
			// Parallel initialization is not done yet, don't allow to skip the intro video
			return false;
		} else {
			// The intro video is over, show loading screen instead
			root->InvokeAsync([root]() {
				static_cast<GameEventHandler*>(root)->SetStateHandler(std::make_unique<LoadingHandler>(root, [](IRootController* root) {
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
#else
	// Building without threading support is not recommended, so it can look ugly
	OnAfterInitialize();
#	if !defined(DEATH_TARGET_EMSCRIPTEN)
	CheckUpdates();
#	endif

#	if defined(WITH_MULTIPLAYER)
	if (PreferencesCache::InitialState == "/server"_s) {
		LOGI("Starting server on port %u...", MultiplayerDefaultPort);

		auto mainMenu = std::make_unique<Menu::MainMenu>(this, false);
		mainMenu->SwitchToSection<Menu::LoadingSection>(_("Creating server..."));
		SetStateHandler(std::move(mainMenu));

		// TODO: Hardcoded port
		CreateServer(MultiplayerDefaultPort);
	} else if (PreferencesCache::InitialState.hasPrefix("/connect:"_s)) {
		String address; std::uint16_t port;
		if (TryParseAddressAndPort(PreferencesCache::InitialState.exceptPrefix(9), address, port)) {
			if (port == 0) {
				port = MultiplayerDefaultPort;
			}

			auto mainMenu = std::make_unique<Menu::MainMenu>(this, false);
			mainMenu->SwitchToSection<Menu::LoadingSection>(_f("Connecting to %s:%u...", address.data(), port));
			SetStateHandler(std::move(mainMenu));

			ConnectToServer(address.data(), (std::uint16_t)port);
			return;
		}
	}
#	endif

	SetStateHandler(std::make_unique<Cinematics>(this, "intro"_s, [](IRootController* root, bool endOfStream) {
		root->GoToMainMenu(endOfStream);
		return true;
	}));
#endif

	Vector2i res = theApplication().GetResolution();
	LOGI("Rendering resolution: %ix%i", res.X, res.Y);
}

void GameEventHandler::OnBeginFrame()
{
	if (!_pendingCallbacks.empty()) {
		ZoneScopedNC("Pending callbacks", 0x888888);

		for (std::size_t i = 0; i < _pendingCallbacks.size(); i++) {
			_pendingCallbacks[i]();
		}
		LOGD("%i async callbacks executed", _pendingCallbacks.size());
		_pendingCallbacks.clear();
	}

	_currentHandler->OnBeginFrame();
}

void GameEventHandler::OnPostUpdate()
{
	_currentHandler->OnEndFrame();
}

void GameEventHandler::OnResizeWindow(std::int32_t width, std::int32_t height)
{
	// Resolution was changed, all viewports have to be recreated
	Viewport::chain().clear();

	if (_currentHandler != nullptr) {
		_currentHandler->OnInitializeViewport(width, height);
	}

#if defined(NCINE_HAS_WINDOWS)
	PreferencesCache::EnableFullscreen = theApplication().GetGfxDevice().isFullscreen();
#endif

	LOGI("Rendering resolution: %ix%i", width, height);
}

void GameEventHandler::OnShutdown()
{
	ZoneScopedC(0x888888);

#if defined(DEATH_TARGET_ANDROID)
	ApplyActivityIcon();
#endif

	_currentHandler = nullptr;
#if defined(WITH_MULTIPLAYER)
	_networkManager = nullptr;
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
	if (AndroidJniWrap_Activity::hasExternalStoragePermission()) {
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

void GameEventHandler::OnKeyPressed(const KeyboardEvent& event)
{
#if defined(NCINE_HAS_WINDOWS) && !defined(DEATH_TARGET_EMSCRIPTEN)
	// Allow F11 and Alt+Enter to switch fullscreen
	if (event.sym == KeySym::F11 || (event.sym == KeySym::RETURN && (event.mod & KeyMod::MASK) == KeyMod::LALT)) {
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

void GameEventHandler::OnTouchEvent(const TouchEvent& event)
{
	_currentHandler->OnTouchEvent(event);
}

void GameEventHandler::InvokeAsync(const std::function<void()>& callback)
{
	_pendingCallbacks.emplace_back(callback);
	LOGD("Callback queued for async execution");
}

void GameEventHandler::InvokeAsync(std::function<void()>&& callback)
{
	_pendingCallbacks.emplace_back(std::move(callback));
	LOGD("Callback queued for async execution");
}

void GameEventHandler::GoToMainMenu(bool afterIntro)
{
	InvokeAsync([this, afterIntro]() {
		ZoneScopedNC("GameEventHandler::GoToMainMenu", 0x888888);

#if defined(WITH_MULTIPLAYER)
		_networkManager = nullptr;
#endif
		if (auto mainMenu = dynamic_cast<Menu::MainMenu*>(_currentHandler.get())) {
			mainMenu->Reset();
		} else {
			SetStateHandler(std::make_unique<Menu::MainMenu>(this, afterIntro));
		}
	});
}

void GameEventHandler::ChangeLevel(LevelInitialization&& levelInit)
{
	InvokeAsync([this, levelInit = std::move(levelInit)]() mutable {
		ZoneScopedNC("GameEventHandler::ChangeLevel", 0x888888);

		std::unique_ptr<IStateHandler> newHandler;
		if (levelInit.LevelName.empty()) {
			// Next level not specified, so show main menu
			newHandler = std::make_unique<Menu::MainMenu>(this, false);
		} else if (levelInit.LevelName == ":end"_s) {
			// End of episode
			SaveEpisodeEnd(levelInit);

			auto& resolver = ContentResolver::Get();
			std::optional<Episode> lastEpisode = resolver.GetEpisode(levelInit.LastEpisodeName);
			if (lastEpisode) {
				// Redirect to next episode
				std::optional<Episode> nextEpisode = resolver.GetEpisode(lastEpisode->NextEpisode);
				if (nextEpisode) {
					levelInit.EpisodeName = lastEpisode->NextEpisode;
					levelInit.LevelName = nextEpisode->FirstLevel;
				}
			}

			if (levelInit.LevelName != ":end"_s) {
				if (!SetLevelHandler(levelInit)) {
					auto mainMenu = std::make_unique<Menu::MainMenu>(this, false);
					mainMenu->SwitchToSection<Menu::SimpleMessageSection>(_("\f[c:#704a4a]Cannot load specified level!\f[/c]\n\n\nMake sure all necessary files\nare accessible and try it again."), true);
					newHandler = std::move(mainMenu);
				}
			} else {
				newHandler = std::make_unique<Menu::MainMenu>(this, false);
			}
		} else if (levelInit.LevelName == ":credits"_s) {
			// End of game
			SaveEpisodeEnd(levelInit);

			newHandler = std::make_unique<Cinematics>(this, "ending"_s, [](IRootController* root, bool endOfStream) {
				root->GoToMainMenu(false);
				return true;
			});
		} else {
			SaveEpisodeContinue(levelInit);

#if defined(SHAREWARE_DEMO_ONLY)
			// Check if specified episode is unlocked, used only if compiled with SHAREWARE_DEMO_ONLY
			bool isEpisodeLocked = (levelInit.EpisodeName == "unknown"_s) ||
				(levelInit.EpisodeName == "prince"_s && (PreferencesCache::UnlockedEpisodes & UnlockableEpisodes::FormerlyAPrince) == UnlockableEpisodes::None) ||
				(levelInit.EpisodeName == "rescue"_s && (PreferencesCache::UnlockedEpisodes & UnlockableEpisodes::JazzInTime) == UnlockableEpisodes::None) ||
				(levelInit.EpisodeName == "flash"_s && (PreferencesCache::UnlockedEpisodes & UnlockableEpisodes::Flashback) == UnlockableEpisodes::None) ||
				(levelInit.EpisodeName == "monk"_s && (PreferencesCache::UnlockedEpisodes & UnlockableEpisodes::FunkyMonkeys) == UnlockableEpisodes::None) ||
				((levelInit.EpisodeName == "xmas98"_s || levelInit.EpisodeName == "xmas99"_s) && (PreferencesCache::UnlockedEpisodes & UnlockableEpisodes::ChristmasChronicles) == UnlockableEpisodes::None) ||
				(levelInit.EpisodeName == "secretf"_s && (PreferencesCache::UnlockedEpisodes & UnlockableEpisodes::TheSecretFiles) == UnlockableEpisodes::None);

			if (isEpisodeLocked) {
				newHandler = std::make_unique<Menu::MainMenu>(this, false);
			} else
#endif
			{
				if (!SetLevelHandler(levelInit)) {
					auto mainMenu = std::make_unique<Menu::MainMenu>(this, false);
					mainMenu->SwitchToSection<Menu::SimpleMessageSection>(_("\f[c:#704a4a]Cannot load specified level!\f[/c]\n\n\nMake sure all necessary files\nare accessible and try it again."), true);
					newHandler = std::move(mainMenu);
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
		if (s->IsValid()) {
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
			auto levelHandler = std::make_unique<LevelHandler>(this);
			if (levelHandler->Initialize(uc)) {
				SetStateHandler(std::move(levelHandler));
				return;
			}
		}

		auto mainMenu = std::make_unique<Menu::MainMenu>(this, false);
		mainMenu->SwitchToSection<Menu::SimpleMessageSection>(_("\f[c:#704a4a]Cannot resume saved state!\f[/c]\n\n\nMake sure all necessary files\nare accessible and try it again."), true);
		SetStateHandler(std::move(mainMenu));
	});
}

bool GameEventHandler::SaveCurrentStateIfAny()
{
	ZoneScopedNC("GameEventHandler::SaveCurrentStateIfAny", 0x888888);

	if (auto* levelHandler = dynamic_cast<LevelHandler*>(_currentHandler.get())) {
		if (levelHandler->Difficulty() != GameDifficulty::Multiplayer) {
			auto configDir = PreferencesCache::GetDirectory();
			auto s = fs::Open(fs::CombinePath(configDir, StateFileName), FileAccess::Write);
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
		AndroidJniWrap_Activity::setActivityEnabled(".MainActivityReforged"_s, true);
		AndroidJniWrap_Activity::setActivityEnabled(".MainActivityLegacy"_s, false);
	} else {
		LOGI("Changed activity icon to \"Legacy\"");
		AndroidJniWrap_Activity::setActivityEnabled(".MainActivityLegacy"_s, true);
		AndroidJniWrap_Activity::setActivityEnabled(".MainActivityReforged"_s, false);
	}
}
#endif

#if defined(WITH_MULTIPLAYER)
bool GameEventHandler::ConnectToServer(const StringView address, std::uint16_t port)
{
	LOGI("Connecting to %s:%u...", address.data(), port);

	if (_networkManager == nullptr) {
		_networkManager = std::make_unique<NetworkManager>();
	}

	return _networkManager->CreateClient(this, address, port, 0xCA000000 | MultiplayerProtocolVersion);
}

bool GameEventHandler::CreateServer(LevelInitialization&& levelInit, std::uint16_t port)
{
	LOGI("Creating server on port %u...", port);

	if (_networkManager == nullptr) {
		_networkManager = std::make_unique<NetworkManager>();
	}

	if (!_networkManager->CreateServer(this, port)) {
		return false;
	}

	InvokeAsync([this, levelInit = std::move(levelInit)]() mutable {
		auto levelHandler = std::make_unique<MultiLevelHandler>(this, _networkManager.get());
		levelHandler->Initialize(levelInit);
		SetStateHandler(std::move(levelHandler));
	});

	return true;
}

ConnectionResult GameEventHandler::OnPeerConnected(const Peer& peer, std::uint32_t clientData)
{
	LOGI("Peer connected");

	if (_networkManager->GetState() == NetworkState::Listening) {
		if ((clientData & 0xFF000000) != 0xCA000000 || (clientData & 0x00FFFFFF) > MultiplayerProtocolVersion) {
			// Connected client is newer than server, reject it
			return Reason::IncompatibleVersion;
		}
	} else {
		// TODO: Auth packet
		std::uint8_t data[] = { (std::uint8_t)ClientPacketType::Auth, 0x01, 0x02, 0x03, 0x04 };
		_networkManager->SendToPeer(peer, NetworkChannel::Main, data, sizeof(data));
	}

	return true;
}

void GameEventHandler::OnPeerDisconnected(const Peer& peer, Reason reason)
{
	LOGI("Peer disconnected (%u)", (std::uint32_t)reason);

	if (auto multiLevelHandler = dynamic_cast<MultiLevelHandler*>(_currentHandler.get())) {
		if (multiLevelHandler->OnPeerDisconnected(peer)) {
			return;
		}
	}

	if (_networkManager != nullptr && _networkManager->GetState() != NetworkState::Listening) {
		InvokeAsync([this, reason]() {
#if defined(WITH_MULTIPLAYER)
			_networkManager = nullptr;
#endif
			Menu::MainMenu* mainMenu;
			if (mainMenu = dynamic_cast<Menu::MainMenu*>(_currentHandler.get())) {
				mainMenu->Reset();
			} else {
				auto newHandler = std::make_unique<Menu::MainMenu>(this, false);
				mainMenu = newHandler.get();
				SetStateHandler(std::move(newHandler));
			}

			switch (reason) {
				case Reason::IncompatibleVersion: mainMenu->SwitchToSection<Menu::SimpleMessageSection>(_("\f[c:#704a4a]Cannot connect to the server!\f[/c]\n\n\nYour client version is not compatible with the server.")); break;
				case Reason::ServerIsFull: mainMenu->SwitchToSection<Menu::SimpleMessageSection>(_("\f[c:#704a4a]Cannot connect to the server!\f[/c]\n\n\nServer capacity is full.\nPlease try it later.")); break;
				case Reason::ServerNotReady: mainMenu->SwitchToSection<Menu::SimpleMessageSection>(_("\f[c:#704a4a]Cannot connect to the server!\f[/c]\n\n\nServer is not in a state where it can process your request.\nPlease try again in a few seconds.")); break;
				case Reason::ServerStopped: mainMenu->SwitchToSection<Menu::SimpleMessageSection>(_("\f[c:#704a4a]Connection has been closed!\f[/c]\n\n\nServer is shutting down.\nPlease try it later.")); break;
				case Reason::ConnectionLost: mainMenu->SwitchToSection<Menu::SimpleMessageSection>(_("\f[c:#704a4a]Connection has been lost!\f[/c]\n\n\nPlease try it again and if the problem persists,\ncheck your network connection.")); break;
				case Reason::ConnectionTimedOut: mainMenu->SwitchToSection<Menu::SimpleMessageSection>(_("\f[c:#704a4a]Cannot connect to the server!\f[/c]\n\n\nThe server is not responding for connection request.")); break;
				case Reason::Kicked: mainMenu->SwitchToSection<Menu::SimpleMessageSection>(_("\f[c:#704a4a]Connection has been closed!\f[/c]\n\n\nYou have been \f[c:#907050]kicked\f[/c] off the server.\nContact server administrators for more information.")); break;
				case Reason::Banned: mainMenu->SwitchToSection<Menu::SimpleMessageSection>(_("\f[c:#704a4a]Connection has been closed!\f[/c]\n\n\nYou have been \f[c:#725040]banned\f[/c] off the server.\nContact server administrators for more information.")); break;
			}
		});
	}
}

void GameEventHandler::OnPacketReceived(const Peer& peer, std::uint8_t channelId, std::uint8_t* data, std::size_t dataLength)
{
	bool isServer = (_networkManager->GetState() == NetworkState::Listening);
	if (isServer) {
		auto packetType = (ClientPacketType)data[0];
		switch (packetType) {
			case ClientPacketType::Ping: {
				std::uint8_t data[] = { (std::uint8_t)ServerPacketType::Pong };
				_networkManager->SendToPeer(peer, NetworkChannel::Main, data, sizeof(data));
				break;
			}
			/*case ClientPacketType::Auth: {
				// TODO: Move this to MultiLevelHandler
				std::uint8_t flags = 0;
				if (PreferencesCache::EnableReforged) {
					flags |= 0x01;
				}

				// TODO: Hardcoded level
				String episodeName = "prince"_s;
				String levelName = "01_castle1"_s;

				MemoryStream packet(10 + episodeName.size() + levelName.size());
				packet.WriteValue<std::uint8_t>((std::uint8_t)ServerPacketType::LoadLevel);
				packet.WriteValue<std::uint8_t>(flags);
				packet.WriteVariableUint32(episodeName.size());
				packet.Write(episodeName.data(), episodeName.size());
				packet.WriteVariableUint32(levelName.size());
				packet.Write(levelName.data(), levelName.size());

				_networkManager->SendToPeer(peer, NetworkChannel::Main, packet.GetBuffer(), packet.GetSize());
				break;
			}*/
		}

	} else {
		auto packetType = (ServerPacketType)data[0];
		switch (packetType) {
			case ServerPacketType::LoadLevel: {
				MemoryStream packet(data + 1, dataLength - 1);
				std::uint8_t flags = packet.ReadValue<std::uint8_t>();
				MultiplayerGameMode gameMode = (MultiplayerGameMode)packet.ReadValue<std::uint8_t>();
				std::uint32_t episodeLength = packet.ReadVariableUint32();
				String episodeName = String(NoInit, episodeLength);
				packet.Read(episodeName.data(), episodeLength);
				std::uint32_t levelLength = packet.ReadVariableUint32();
				String levelName = String(NoInit, levelLength);
				packet.Read(levelName.data(), levelLength);

				InvokeAsync([this, flags, gameMode, episodeName = std::move(episodeName), levelName = std::move(levelName)]() {
					bool isReforged = (flags & 0x01) != 0;
					LevelInitialization levelInit(episodeName, levelName, GameDifficulty::Multiplayer, isReforged);
					auto levelHandler = std::make_unique<MultiLevelHandler>(this, _networkManager.get());
					levelHandler->SetGameMode(gameMode);
					levelHandler->Initialize(levelInit);
					SetStateHandler(std::move(levelHandler));
				});
				break;
			}
		}
	}

	if (auto* multiLevelHandler = dynamic_cast<MultiLevelHandler*>(_currentHandler.get())) {
		if (multiLevelHandler->OnPacketReceived(peer, channelId, data, dataLength)) {
			return;
		}
	}

	if (isServer && (ClientPacketType)data[0] == ClientPacketType::Auth) {
		// Message was not processed by level handler, kick the client
		_networkManager->KickClient(peer, Reason::ServerNotReady);
	}
}
#endif

void GameEventHandler::OnBeforeInitialize()
{
#if defined(WITH_IMGUI)
	theApplication().GetDebugOverlaySettings().showInterface = true;
#endif

	_flags |= Flags::IsInitialized;

	std::memset(_newestVersion, 0, sizeof(_newestVersion));

	auto& resolver = ContentResolver::Get();

#if defined(DEATH_TARGET_ANDROID)
	theApplication().SetAutoSuspension(true);

	if (AndroidJniWrap_Activity::hasExternalStoragePermission()) {
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

void GameEventHandler::SetStateHandler(std::unique_ptr<IStateHandler>&& handler)
{
	_currentHandler = std::move(handler);

	Viewport::chain().clear();
	Vector2i res = theApplication().GetResolution();
	_currentHandler->OnInitializeViewport(res.X, res.Y);
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

	constexpr std::uint64_t currentVersion = parseVersion({ NCINE_VERSION, arraySize(NCINE_VERSION) - 1 });

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
		std::int64_t animsModified = fs::GetLastModificationTime(animsPath).GetValue();
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

		if (currentVersion != lastVersion) {
			if ((lastVersion & 0xFFFFFFFFULL) == 0x0FFFFFFFULL) {
				LOGI("Cache is already up-to-date, but created in experimental build v%i.%i.0", (lastVersion >> 48) & 0xFFFFULL, (lastVersion >> 32) & 0xFFFFULL);
			} else {
				LOGI("Cache is already up-to-date, but created in different build v%i.%i.%i", (lastVersion >> 48) & 0xFFFFULL, (lastVersion >> 32) & 0xFFFFULL, lastVersion & 0xFFFFFFFFULL);
			}

			WriteCacheDescriptor(cachePath, currentVersion, animsModified);

			std::uint32_t filesRemoved = RenderResources::binaryShaderCache().prune();
			LOGI("Pruning binary shader cache (removed %u files)...", filesRemoved);
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
			LOGE("Cannot open \"…%sSource%sAnims.j2a\" file! Make sure Jazz Jackrabbit 2 files are present in \"%s\" directory.", fs::PathSeparator, fs::PathSeparator, resolver.GetSourcePath().data());
			_flags |= Flags::IsVerified;
			return;
		}
	}

	fs::CreateDirectories(resolver.GetCachePath());

	// Delete cache from previous versions
	String animationsPath = fs::CombinePath(resolver.GetCachePath(), "Animations"_s);
	fs::RemoveDirectoryRecursive(animationsPath);

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
			Timer::sleep(t * 100);
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

	RefreshCacheLevels();

	LOGI("Cache was recreated");
	std::int64_t animsModified = fs::GetLastModificationTime(animsPath).GetValue();
	WriteCacheDescriptor(cachePath, currentVersion, animsModified);

	std::uint32_t filesRemoved = RenderResources::binaryShaderCache().prune();
	LOGI("Pruning binary shader cache (removed %u files)...", filesRemoved);

	resolver.RemountPaks();

	_flags |= Flags::IsVerified | Flags::IsPlayable;
}

void GameEventHandler::RefreshCacheLevels()
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

	auto LevelTokenConversion = [&knownLevels](const StringView& levelToken) -> Compatibility::JJ2Level::LevelToken {
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
	fs::RemoveDirectoryRecursive(episodesPath);
	fs::CreateDirectories(episodesPath);

	HashMap<String, bool> usedTilesets;

	for (auto item : fs::Directory(fs::FindPathCaseInsensitive(resolver.GetSourcePath()), fs::EnumerationOptions::SkipDirectories)) {
		auto extension = fs::GetExtension(item);
		if (extension == "j2e"_s || extension == "j2pe"_s) {
			// Episode
			Compatibility::JJ2Episode episode;
			if (episode.Open(item)) {
				if (episode.Name == "home"_s || (hasChristmasChronicles && episode.Name == "xmas98"_s)) {
					continue;
				}

				String fullPath = fs::CombinePath(episodesPath, String((episode.Name == "xmas98"_s ? "xmas99"_s : StringView(episode.Name)) + ".j2e"_s));
				episode.Convert(fullPath, LevelTokenConversion, EpisodeNameConversion, EpisodePrevNext);
			}
		} else if (extension == "j2l"_s) {
			// Level
			String levelName = fs::GetFileName(item);
			if (levelName.find("-MLLE-Data-"_s) == nullptr) {
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
		/*else if (extension == "j2s"_s) {
			// Episode
			Compatibility::JJ2Strings strings;
			strings.Open(item);

			String fullPath = fs::CombinePath({ resolver.GetCachePath(), "Translations"_s, strings.Name + ".h"_s });
			fs::CreateDirectories(fs::GetDirectoryName(fullPath));
			strings.Convert(fullPath, LevelTokenConversion);
		}*/
#endif
	}

	// Convert only used tilesets
	LOGI("Converting used tilesets...");
	String tilesetsPath = fs::CombinePath(resolver.GetCachePath(), "Tilesets"_s);
	fs::RemoveDirectoryRecursive(tilesetsPath);
	fs::CreateDirectories(tilesetsPath);

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

void GameEventHandler::CheckUpdates()
{
#if !defined(DEATH_DEBUG)
	ZoneScopedC(0x888888);

#if defined(DEATH_TARGET_X86)
	std::int32_t arch = 1;
	Cpu::Features cpuFeatures = Cpu::runtimeFeatures();
	if (cpuFeatures & Cpu::Avx) {
		arch |= 0x400;
	}
	if (cpuFeatures & Cpu::Avx2) {
		arch |= 0x800;
	}
	if (cpuFeatures & Cpu::Avx512f) {
		arch |= 0x1000;
	}
#elif defined(DEATH_TARGET_ARM)
	std::int32_t arch = 2;
	Cpu::Features cpuFeatures = Cpu::runtimeFeatures();
	if (cpuFeatures & Cpu::Neon) {
		arch |= 0x2000;
	}
#elif defined(DEATH_TARGET_POWERPC)
	std::int32_t arch = 3;
#elif defined(DEATH_TARGET_RISCV)
	std::int32_t arch = 5;
#elif defined(DEATH_TARGET_WASM)
	std::int32_t arch = 4;
	Cpu::Features cpuFeatures = Cpu::runtimeFeatures();
	if (cpuFeatures & Cpu::Simd128) {
		arch |= 0x4000;
	}
#else
	std::int32_t arch = 0;
#endif
#if defined(DEATH_TARGET_32BIT)
	arch |= 0x100;
#endif
#if defined(DEATH_TARGET_BIG_ENDIAN)
	arch |= 0x200;
#endif
#if defined(DEATH_TARGET_CYGWIN)
	arch |= 0x200000;
#endif
#if defined(DEATH_TARGET_MINGW)
	arch |= 0x400000;
#endif

#if defined(DEATH_TARGET_ANDROID)
	auto sanitizeName = [](char* dst, std::size_t dstMaxLength, std::size_t& dstLength, const StringView& name, bool isBrand) {
		bool wasSpace = true;
		std::size_t lowercaseLength = 0;

		if (isBrand) {
			for (char c : name) {
				if (c == '\0' || c == ' ') {
					break;
				}
				lowercaseLength++;
			}
			if (lowercaseLength < 5 || name[0] < 'A' || name[0] > 'Z' || name[lowercaseLength - 1] < 'A' || name[lowercaseLength - 1] > 'Z') {
				lowercaseLength = 0;
			}
		}
		
		for (char c : name) {
			if (c == '\0' || dstLength >= dstMaxLength) {
				break;
			}
			if (isalnum(c) || c == ' ' || c == '.' || c == ',' || c == ':' || c == '_' || c == '-' || c == '+' || c == '/' || c == '*' ||
				c == '!' || c == '(' || c == ')' || c == '[' || c == ']' || c == '@' || c == '&' || c == '#' || c == '\'' || c == '"') {
				if (wasSpace && c >= 'a' && c <= 'z') {
					c &= ~0x20;
					if (lowercaseLength > 0) {
						lowercaseLength--;
					}
				} else if (lowercaseLength > 0) {
					if (c >= 'A' && c <= 'Z') {
						c |= 0x20;
					}
					lowercaseLength--;
				}
				dst[dstLength++] = c;
			}
			wasSpace = (c == ' ');
		}
	};

	auto sdkVersion = AndroidJniHelper::SdkVersion();
	auto androidId = AndroidJniWrap_Secure::AndroidId();
	auto deviceBrand = AndroidJniClass_Version::deviceBrand();
	auto deviceModel = AndroidJniClass_Version::deviceModel();

	char deviceName[64];
	std::size_t deviceNameLength = 0;
	if (deviceModel.empty()) {
		sanitizeName(deviceName, arraySize(deviceName) - 1, deviceNameLength, deviceBrand, false);
	} else if (deviceModel.hasPrefix(deviceBrand)) {
		sanitizeName(deviceName, arraySize(deviceName) - 1, deviceNameLength, deviceModel, true);
	} else {
		if (!deviceBrand.empty()) {
			sanitizeName(deviceName, arraySize(deviceName) - 8, deviceNameLength, deviceBrand, true);
			deviceName[deviceNameLength++] = ' ';
		}
		sanitizeName(deviceName, arraySize(deviceName) - 1, deviceNameLength, deviceModel, false);
	}
	deviceName[deviceNameLength] = '\0';

	char DeviceDesc[128];
	std::int32_t DeviceDescLength = formatString(DeviceDesc, arraySize(DeviceDesc), "%s|Android %i|%s|2|%i", androidId.data(), sdkVersion, deviceName, arch);
#elif defined(DEATH_TARGET_APPLE)
	char DeviceDesc[256] { }; std::int32_t DeviceDescLength;
	if (::gethostname(DeviceDesc, arraySize(DeviceDesc)) == 0) {
		DeviceDesc[arraySize(DeviceDesc) - 1] = '\0';
		DeviceDescLength = std::strlen(DeviceDesc);
	} else {
		DeviceDescLength = 0;
	}
	String appleVersion = Environment::GetAppleVersion();
	DeviceDescLength += formatString(DeviceDesc + DeviceDescLength, arraySize(DeviceDesc) - DeviceDescLength, "|macOS %s||5|%i", appleVersion.data(), arch);
#elif defined(DEATH_TARGET_SWITCH)
	std::uint32_t switchVersion = Environment::GetSwitchVersion();
	bool isAtmosphere = Environment::HasSwitchAtmosphere();

	char DeviceDesc[128];
	std::int32_t DeviceDescLength = formatString(DeviceDesc, arraySize(DeviceDesc), "|Nintendo Switch %u.%u.%u%s||9|%i",
		((switchVersion >> 16) & 0xFF), ((switchVersion >> 8) & 0xFF), (switchVersion & 0xFF), isAtmosphere ? " (Atmosphère)" : "", arch);
#elif defined(DEATH_TARGET_UNIX)
#	if defined(DEATH_TARGET_CLANG)
	arch |= 0x100000;
#	endif

	char DeviceDesc[256] { }; std::int32_t DeviceDescLength;
	if (::gethostname(DeviceDesc, arraySize(DeviceDesc)) == 0) {
		DeviceDesc[arraySize(DeviceDesc) - 1] = '\0';
		DeviceDescLength = std::strlen(DeviceDesc);
	} else {
		DeviceDescLength = 0;
	}
	String unixVersion = Environment::GetUnixVersion();
	DeviceDescLength += formatString(DeviceDesc + DeviceDescLength, arraySize(DeviceDesc) - DeviceDescLength, "|%s||4|%i",
		unixVersion.empty() ? "Unix" : unixVersion.data(), arch);
#elif defined(DEATH_TARGET_WINDOWS) || defined(DEATH_TARGET_WINDOWS_RT)
#	if defined(DEATH_TARGET_CLANG)
	arch |= 0x100000;
#	endif

	auto osVersion = Environment::WindowsVersion;
	wchar_t deviceNameW[128]; DWORD DeviceDescLength = (DWORD)arraySize(deviceNameW);
	if (!::GetComputerNameW(deviceNameW, &DeviceDescLength)) {
		DeviceDescLength = 0;
	}
	
	char DeviceDesc[256];
	DeviceDescLength = Utf8::FromUtf16(DeviceDesc, deviceNameW, DeviceDescLength);

#	if defined(DEATH_TARGET_WINDOWS_RT)
	const char* deviceType;
	switch (Environment::CurrentDeviceType) {
		case DeviceType::Desktop: deviceType = "Desktop"; break;
		case DeviceType::Mobile: deviceType = "Mobile"; break;
		case DeviceType::Iot: deviceType = "Iot"; break;
		case DeviceType::Xbox: deviceType = "Xbox"; break;
		default: deviceType = "Unknown"; break;
	}
	DeviceDescLength += formatString(DeviceDesc + DeviceDescLength, arraySize(DeviceDesc) - DeviceDescLength, "|Windows %i.%i.%i (%s)||7|%i",
		(std::int32_t)((osVersion >> 48) & 0xffffu), (std::int32_t)((osVersion >> 32) & 0xffffu), (std::int32_t)(osVersion & 0xffffffffu), deviceType, arch);
#	else
	HMODULE hNtdll = ::GetModuleHandle(L"ntdll.dll");
	bool isWine = (hNtdll != nullptr && ::GetProcAddress(hNtdll, "wine_get_host_version") != nullptr);
	DeviceDescLength += formatString(DeviceDesc + DeviceDescLength, arraySize(DeviceDesc) - DeviceDescLength,
		isWine ? "|Windows %i.%i.%i (Wine)||3|%i" : "|Windows %i.%i.%i||3|%i",
		(std::int32_t)((osVersion >> 48) & 0xffffu), (std::int32_t)((osVersion >> 32) & 0xffffu), (std::int32_t)(osVersion & 0xffffffffu), arch);
#	endif
#else
	static const char DeviceDesc[] = "||||"; std::int32_t DeviceDescLength = sizeof(DeviceDesc) - 1;
#endif
	using namespace Death::IO;
	String url = "http://deat.tk/downloads/games/jazz2/updates?v=" NCINE_VERSION "&d=" + Http::EncodeBase64(DeviceDesc, DeviceDesc + DeviceDescLength);
	Http::Request req(url, Http::InternetProtocol::V4);
	Http::Response resp = req.Send("GET"_s, std::chrono::seconds(10));
	if (resp.Status.Code == Http::HttpStatus::Ok && !resp.Body.empty() && resp.Body.size() < sizeof(_newestVersion) - 1) {
		constexpr std::uint64_t currentVersion = parseVersion({ NCINE_VERSION, arraySize(NCINE_VERSION) - 1 });
		std::uint64_t latestVersion = parseVersion(StringView(reinterpret_cast<char*>(resp.Body.data()), resp.Body.size()));
		if (currentVersion < latestVersion) {
			std::memcpy(_newestVersion, resp.Body.data(), resp.Body.size());
			_newestVersion[resp.Body.size()] = '\0';
		}
	}
#endif
}
#endif

bool GameEventHandler::SetLevelHandler(const LevelInitialization& levelInit)
{
#if defined(WITH_MULTIPLAYER)
	if (levelInit.Difficulty == GameDifficulty::Multiplayer) {
		auto levelHandler = std::make_unique<MultiLevelHandler>(this, _networkManager.get());
		if (!levelHandler->Initialize(levelInit)) {
			return false;
		}
		SetStateHandler(std::move(levelHandler));
		return true;
	}
#endif

	auto levelHandler = std::make_unique<LevelHandler>(this);
	if (!levelHandler->Initialize(levelInit)) {
		return false;
	}
	SetStateHandler(std::move(levelHandler));

#if !defined(SHAREWARE_DEMO_ONLY)
	if (levelInit.Difficulty != GameDifficulty::Multiplayer) {
		RemoveResumableStateIfAny();
	}
#endif

	return true;
}

void GameEventHandler::WriteCacheDescriptor(const StringView path, std::uint64_t currentVersion, std::int64_t animsModified)
{
	auto so = fs::Open(path, FileAccess::Write);
	so->WriteValue<std::uint64_t>(0x2095A59FF0BFBBEF);	// Signature
	so->WriteValue<std::uint8_t>(ContentResolver::CacheIndexFile);
	so->WriteValue<std::uint16_t>(Compatibility::JJ2Anims::CacheVersion);
	so->WriteValue<std::uint8_t>(0x00);				// Flags
	so->WriteValue<std::int64_t>(animsModified);
	so->WriteValue<std::uint16_t>((std::uint16_t)EventType::Count);
	so->WriteValue<std::uint64_t>(currentVersion);
}

void GameEventHandler::SaveEpisodeEnd(const LevelInitialization& levelInit)
{
	if (levelInit.LastEpisodeName.empty() || levelInit.LastEpisodeName == "unknown"_s) {
		return;
	}

	std::size_t playerCount = 0;
	const PlayerCarryOver* firstPlayer = nullptr;
	for (std::size_t i = 0; i < arraySize(levelInit.PlayerCarryOvers); i++) {
		if (levelInit.PlayerCarryOvers[i].Type != PlayerType::None) {
			firstPlayer = &levelInit.PlayerCarryOvers[i];
			playerCount++;
		}
	}

	PreferencesCache::RemoveEpisodeContinue(levelInit.LastEpisodeName);

	if (playerCount > 0) {
		auto* prevEnd = PreferencesCache::GetEpisodeEnd(levelInit.LastEpisodeName);

		bool shouldSaveEpisodeEnd = (prevEnd == nullptr);
		if (!shouldSaveEpisodeEnd) {
			switch (PreferencesCache::OverwriteEpisodeEnd) {
				// Don't overwrite existing data in multiplayer/splitscreen
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
			std::memcpy(episodeEnd->Ammo, firstPlayer->Ammo, sizeof(firstPlayer->Ammo));
			std::memcpy(episodeEnd->WeaponUpgrades, firstPlayer->WeaponUpgrades, sizeof(firstPlayer->WeaponUpgrades));
		}
	}

	PreferencesCache::Save();
}

void GameEventHandler::SaveEpisodeContinue(const LevelInitialization& levelInit)
{
	if (levelInit.EpisodeName.empty() || levelInit.LevelName.empty() ||
		levelInit.EpisodeName == "unknown"_s || levelInit.Difficulty == GameDifficulty::Multiplayer ||
		(levelInit.EpisodeName == "prince"_s && levelInit.LevelName == "trainer"_s)) {
		return;
	}

	std::optional<Episode> currentEpisode = ContentResolver::Get().GetEpisode(levelInit.EpisodeName);
	if (!currentEpisode || currentEpisode->FirstLevel == levelInit.LevelName) {
		return;
	}

	std::size_t playerCount = 0;
	const PlayerCarryOver* firstPlayer = nullptr;
	for (std::size_t i = 0; i < arraySize(levelInit.PlayerCarryOvers); i++) {
		if (levelInit.PlayerCarryOvers[i].Type != PlayerType::None) {
			firstPlayer = &levelInit.PlayerCarryOvers[i];
			playerCount++;
		}
	}

	// Don't save continue in multiplayer
	if (playerCount == 1) {
		auto* episodeContinue = PreferencesCache::GetEpisodeContinue(levelInit.EpisodeName, true);
		episodeContinue->LevelName = levelInit.LevelName;
		episodeContinue->State.Flags = EpisodeContinuationFlags::None;
		if (levelInit.CheatsUsed) {
			episodeContinue->State.Flags |= EpisodeContinuationFlags::CheatsUsed;
		}

		episodeContinue->State.DifficultyAndPlayerType = ((std::int32_t)levelInit.Difficulty & 0x0f) | (((std::int32_t)firstPlayer->Type & 0x0f) << 4);
		episodeContinue->State.Lives = firstPlayer->Lives;
		episodeContinue->State.Score = firstPlayer->Score;
		std::memcpy(episodeContinue->State.Ammo, firstPlayer->Ammo, sizeof(firstPlayer->Ammo));
		std::memcpy(episodeContinue->State.WeaponUpgrades, firstPlayer->WeaponUpgrades, sizeof(firstPlayer->WeaponUpgrades));

		PreferencesCache::TutorialCompleted = true;
		PreferencesCache::Save();
	} else if (!PreferencesCache::TutorialCompleted) {
		PreferencesCache::TutorialCompleted = true;
		PreferencesCache::Save();
	}
}

bool GameEventHandler::TryParseAddressAndPort(const StringView input, String& address, std::uint16_t& port)
{
	auto portSep = input.findLast(':');
	if (portSep == nullptr) {
		return false;
	}

	address = String(input.prefix(portSep.begin()));
	if (address.empty()) {
		return false;
	}

	auto portString = input.suffix(portSep.begin() + 1);
	port = (std::uint16_t)stou32(portString.data(), portString.size());
	return true;
}

void GameEventHandler::ExtractPakFile(const StringView pakFile, const StringView targetPath)
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
int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR pCmdLine, int nCmdShow)
{
	return MainApplication::Run([]() -> std::unique_ptr<IAppEventHandler> {
		return std::make_unique<GameEventHandler>();
	}, __argc, __wargv);
}
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
	bool logoVisible = false;
#if defined(DEATH_TRACE) && (defined(DEATH_TARGET_APPLE) || defined(DEATH_TARGET_UNIX))
	bool hasVirtualTerminal = isatty(1);
	if (hasVirtualTerminal) {
		const char* term = ::getenv("TERM");
		if (term != nullptr && (strstr(term, "truecolor") || strstr(term, "24bit") ||
			strstr(term, "256color") || strstr(term, "rxvt-xpm"))) {
			fwrite(TermLogo, sizeof(unsigned char), arraySize(TermLogo), stdout);
			logoVisible = true;
		}
	}
#endif
#if defined(DEATH_TARGET_UNIX)
	for (std::size_t i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--version") == 0) {
			// Just print current version below the logo and quit
			return PrintVersion(logoVisible);
		}
	}
#endif

	return MainApplication::Run([]() -> std::unique_ptr<IAppEventHandler> {
		return std::make_unique<GameEventHandler>();
	}, argc, argv);
}
#endif