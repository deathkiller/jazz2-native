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
#include "Jazz2/UI/Menu/MainMenu.h"
#include "Jazz2/UI/Menu/LoadingSection.h"
#include "Jazz2/UI/Menu/SimpleMessageSection.h"

#include "Jazz2/Compatibility/JJ2Anims.h"
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

#include <Cpu.h>
#include <Environment.h>
#include <IO/FileSystem.h>

#if !defined(DEATH_DEBUG)
#	include <IO/HttpRequest.h>
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
	static constexpr std::int32_t DefaultWidth = 720;
	static constexpr std::int32_t DefaultHeight = 405;

#if defined(WITH_MULTIPLAYER)
	static constexpr std::uint16_t MultiplayerDefaultPort = 7438;
	static constexpr std::uint32_t MultiplayerProtocolVersion = 1;
#endif

	void OnPreInit(AppConfiguration& config) override;
	void OnInit() override;
	void OnFrameStart() override;
	void OnPostUpdate() override;
	void OnResizeWindow(int width, int height) override;
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

#if defined(WITH_MULTIPLAYER)
	bool ConnectToServer(const StringView& address, std::uint16_t port) override;
	bool CreateServer(std::uint16_t port) override;

	ConnectionResult OnPeerConnected(const Peer& peer, std::uint32_t clientData) override;
	void OnPeerDisconnected(const Peer& peer, Reason reason) override;
	void OnPacketReceived(const Peer& peer, std::uint8_t channelId, std::uint8_t* data, std::size_t dataLength) override;
#endif

	Flags GetFlags() const override {
		return _flags;
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
	Flags _flags;
	std::unique_ptr<IStateHandler> _currentHandler;
	SmallVector<std::function<void()>> _pendingCallbacks;
	char _newestVersion[20];
#if defined(WITH_MULTIPLAYER)
	std::unique_ptr<NetworkManager> _networkManager;
#endif

	void SetStateHandler(std::unique_ptr<IStateHandler>&& handler);
#if !defined(DEATH_TARGET_EMSCRIPTEN)
	void RefreshCache();
	void CheckUpdates();
#endif
	bool SetLevelHandler(const LevelInitialization& levelInit);
	static void WriteCacheDescriptor(const StringView& path, std::uint64_t currentVersion, std::int64_t animsModified);
	static void SaveEpisodeEnd(const LevelInitialization& levelInit);
	static void SaveEpisodeContinue(const LevelInitialization& levelInit);
	static void UpdateRichPresence(const LevelInitialization& levelInit);
	static bool TryParseAddressAndPort(const StringView& input, String& address, std::uint16_t& port);
};

void GameEventHandler::OnPreInit(AppConfiguration& config)
{
	ZoneScopedC(0x888888);

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
	//config.withDebugOverlay = true;
#endif
}

void GameEventHandler::OnInit()
{
	ZoneScopedC(0x888888);

#if defined(WITH_IMGUI)
	//theApplication().debugOverlaySettings().showInterface = true;
#endif

	_flags = Flags::None;

	std::memset(_newestVersion, 0, sizeof(_newestVersion));

	auto& resolver = ContentResolver::Get();

#if defined(DEATH_TARGET_ANDROID)
	theApplication().setAutoSuspension(true);

	if (AndroidJniWrap_Activity::hasExternalStoragePermission()) {
		_flags |= Flags::HasExternalStoragePermission;
	}
#elif !defined(DEATH_TARGET_IOS) && !defined(DEATH_TARGET_SWITCH)
#	if defined(DEATH_TARGET_WINDOWS_RT)
	// Xbox is always fullscreen
	if (PreferencesCache::EnableFullscreen || Environment::CurrentDeviceType == DeviceType::Xbox) {
#	else
	if (PreferencesCache::EnableFullscreen) {
#	endif
		theApplication().gfxDevice().setResolution(true);
		theApplication().inputManager().setCursor(IInputManager::Cursor::Hidden);
	}

	String mappingsPath = fs::CombinePath(resolver.GetContentPath(), "gamecontrollerdb.txt"_s);
	if (fs::IsReadableFile(mappingsPath)) {
		theApplication().inputManager().addJoyMappingsFromFile(mappingsPath);
	}
#endif

	resolver.CompileShaders();

#if defined(WITH_THREADS) && !defined(DEATH_TARGET_EMSCRIPTEN)
	// If threading support is enabled, refresh cache during intro cinematics and don't allow skip until it's completed
	Thread thread([](void* arg) {
		Thread::SetCurrentName("Parallel initialization");

		auto handler = static_cast<GameEventHandler*>(arg);
#	if (defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)) || defined(DEATH_TARGET_UNIX)
		if (PreferencesCache::EnableDiscordIntegration) {
			DiscordRpcClient::Get().Connect("591586859960762378"_s);
		}
#	endif

		if (PreferencesCache::Language[0] != '\0') {
			auto& resolver = ContentResolver::Get();
			auto& i18n = I18n::Get();
			if (!i18n.LoadFromFile(fs::CombinePath({ resolver.GetContentPath(), "Translations"_s, StringView(PreferencesCache::Language) + ".mo"_s }))) {
				i18n.LoadFromFile(fs::CombinePath({ resolver.GetCachePath(), "Translations"_s, StringView(PreferencesCache::Language) + ".mo"_s }));
			}
		}

		handler->RefreshCache();
		handler->CheckUpdates();
	}, this);

#	if defined(WITH_MULTIPLAYER)
	if (PreferencesCache::InitialState == "/server"_s) {
		thread.Join();

		auto mainMenu = std::make_unique<Menu::MainMenu>(this, false);
		mainMenu->SwitchToSection<Menu::LoadingSection>(_("Creating server..."));
		SetStateHandler(std::move(mainMenu));

		// TODO: Hardcoded port
		CreateServer(MultiplayerDefaultPort);
	} else if (PreferencesCache::InitialState.hasPrefix("/connect:"_s)) {
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
		if ((root->GetFlags() & Jazz2::IRootController::Flags::IsVerified) != Jazz2::IRootController::Flags::IsVerified) {
			return false;
		}

		thread.Join();
		root->GoToMainMenu(endOfStream);
		return true;
	}));
#else
	// Building without threading support is not recommended, so it can look ugly
#	if (defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)) || defined(DEATH_TARGET_UNIX)
	if (PreferencesCache::EnableDiscordIntegration) {
		DiscordRpcClient::Get().Connect("591586859960762378"_s);
	}
#	endif

	if (PreferencesCache::Language[0] != '\0') {
		auto& resolver = ContentResolver::Get();
		auto& i18n = I18n::Get();
		if (!i18n.LoadFromFile(fs::CombinePath({ resolver.GetContentPath(), "Translations"_s, StringView(PreferencesCache::Language) + ".mo"_s }))) {
			i18n.LoadFromFile(fs::CombinePath({ resolver.GetCachePath(), "Translations"_s, StringView(PreferencesCache::Language) + ".mo"_s }));
		}
	}

#	if defined(DEATH_TARGET_EMSCRIPTEN)
	// All required files are already included in Emscripten version, so nothing is verified
	_flags |= Flags::IsVerified | Flags::IsPlayable;
#	else
	RefreshCache();
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

	Vector2i res = theApplication().resolution();
	LOGI("Rendering resolution: %ix%i", res.X, res.Y);
}

void GameEventHandler::OnFrameStart()
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

void GameEventHandler::OnResizeWindow(int width, int height)
{
	// Resolution was changed, all viewports have to be recreated
	Viewport::chain().clear();

	if (_currentHandler != nullptr) {
		_currentHandler->OnInitializeViewport(width, height);
	}

	PreferencesCache::EnableFullscreen = theApplication().gfxDevice().isFullscreen();

	LOGI("Rendering resolution: %ix%i", width, height);
}

void GameEventHandler::OnShutdown()
{
	ZoneScopedC(0x888888);

	_currentHandler = nullptr;
#if defined(WITH_MULTIPLAYER)
	_networkManager = nullptr;
#endif

	ContentResolver::Get().Release();
}

void GameEventHandler::OnSuspend()
{
	// TODO: Save current game state on suspend
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
}

void GameEventHandler::OnKeyPressed(const KeyboardEvent& event)
{
#if !defined(DEATH_TARGET_ANDROID) && !defined(DEATH_TARGET_EMSCRIPTEN) && !defined(DEATH_TARGET_IOS) && !defined(DEATH_TARGET_SWITCH)
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
			UpdateRichPresence({});
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

			PreferencesCache::RemoveEpisodeContinue(levelInit.LastEpisodeName);

			std::optional<Episode> lastEpisode = ContentResolver::Get().GetEpisode(levelInit.LastEpisodeName);
			if (lastEpisode.has_value()) {
				// Redirect to next episode
				std::optional<Episode> nextEpisode = ContentResolver::Get().GetEpisode(lastEpisode->NextEpisode);
				if (nextEpisode.has_value()) {
					levelInit.EpisodeName = lastEpisode->NextEpisode;
					levelInit.LevelName = nextEpisode->FirstLevel;
				}
			}

			if (levelInit.LevelName != ":end"_s) {
				if (SetLevelHandler(levelInit)) {
					UpdateRichPresence(levelInit);
				} else {
					auto mainMenu = std::make_unique<Menu::MainMenu>(this, false);
					mainMenu->SwitchToSection<Menu::SimpleMessageSection>(_("\f[c:0x704a4a]Cannot load specified level!\f[c]\n\n\nMake sure all necessary files\nare accessible and try it again."));
					newHandler = std::move(mainMenu);
				}
			} else {
				newHandler = std::make_unique<Menu::MainMenu>(this, false);
			}
		} else if (levelInit.LevelName == ":credits"_s) {
			// End of game
			SaveEpisodeEnd(levelInit);

			PreferencesCache::RemoveEpisodeContinue(levelInit.LastEpisodeName);

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
				if (SetLevelHandler(levelInit)) {
					UpdateRichPresence(levelInit);
				} else {
					auto mainMenu = std::make_unique<Menu::MainMenu>(this, false);
					mainMenu->SwitchToSection<Menu::SimpleMessageSection>(_("\f[c:0x704a4a]Cannot load specified level!\f[c]\n\n\nMake sure all necessary files\nare accessible and try it again."));
					newHandler = std::move(mainMenu);
				}
			}
		}

		if (newHandler != nullptr) {
			SetStateHandler(std::move(newHandler));
			UpdateRichPresence({});
		}
	});
}

#if defined(WITH_MULTIPLAYER)
bool GameEventHandler::ConnectToServer(const StringView& address, std::uint16_t port)
{
	LOGI("Connecting to %s:%u...", address.data(), port);

	if (_networkManager == nullptr) {
		_networkManager = std::make_unique<NetworkManager>();
	}

	return _networkManager->CreateClient(this, address, port, 0xCA000000 | MultiplayerProtocolVersion);
}

bool GameEventHandler::CreateServer(std::uint16_t port)
{
	LOGI("Creating server on port %u...", port);

	if (_networkManager == nullptr) {
		_networkManager = std::make_unique<NetworkManager>();
	}

	if (!_networkManager->CreateServer(this, port)) {
		return false;
	}

	InvokeAsync([this]() {
		// TODO: Hardcoded level
		LevelInitialization levelInit("rescue", "01_colon1", GameDifficulty::Multiplayer, true, false, PlayerType::Jazz);
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
				UpdateRichPresence({});
			}

			switch (reason) {
				case Reason::IncompatibleVersion: mainMenu->SwitchToSection<Menu::SimpleMessageSection>(_("\f[c:0x704a4a]Cannot connect to the server!\f[c]\n\n\nYour client version is not compatible with the server.")); break;
				case Reason::ServerIsFull: mainMenu->SwitchToSection<Menu::SimpleMessageSection>(_("\f[c:0x704a4a]Cannot connect to the server!\f[c]\n\n\nServer capacity is full.\nPlease try it later.")); break;
				case Reason::ServerNotReady: mainMenu->SwitchToSection<Menu::SimpleMessageSection>(_("\f[c:0x704a4a]Cannot connect to the server!\f[c]\n\n\nServer is not in a state where it can process your request.\nPlease try again in a few seconds.")); break;
				case Reason::ServerStopped: mainMenu->SwitchToSection<Menu::SimpleMessageSection>(_("\f[c:0x704a4a]Connection has been closed!\f[c]\n\n\nServer is shutting down.\nPlease try it later.")); break;
				case Reason::ConnectionLost: mainMenu->SwitchToSection<Menu::SimpleMessageSection>(_("\f[c:0x704a4a]Connection has been lost!\f[c]\n\n\nPlease try it again and if the problem persists,\ncheck your network connection.")); break;
				case Reason::ConnectionTimedOut: mainMenu->SwitchToSection<Menu::SimpleMessageSection>(_("\f[c:0x704a4a]Cannot connect to the server!\f[c]\n\n\nThe server is not responding for connection request.")); break;
				case Reason::Kicked: mainMenu->SwitchToSection<Menu::SimpleMessageSection>(_("\f[c:0x704a4a]Connection has been closed!\f[c]\n\n\nYou have been \f[c:0x907050]kicked\f[c] off the server.\nContact server administrators for more information.")); break;
				case Reason::Banned: mainMenu->SwitchToSection<Menu::SimpleMessageSection>(_("\f[c:0x704a4a]Connection has been closed!\f[c]\n\n\nYou have been \f[c:0x725040]banned\f[c] off the server.\nContact server administrators for more information.")); break;
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
				std::uint32_t episodeLength = packet.ReadVariableUint32();
				String episodeName = String(NoInit, episodeLength);
				packet.Read(episodeName.data(), episodeLength);
				std::uint32_t levelLength = packet.ReadVariableUint32();
				String levelName = String(NoInit, levelLength);
				packet.Read(levelName.data(), levelLength);

				InvokeAsync([this, flags, episodeName = std::move(episodeName), levelName = std::move(levelName)]() {
					bool isReforged = (flags & 0x01) != 0;
					LevelInitialization levelInit(episodeName, levelName, GameDifficulty::Multiplayer, isReforged);
					auto levelHandler = std::make_unique<MultiLevelHandler>(this, _networkManager.get());
					levelHandler->Initialize(levelInit);
					SetStateHandler(std::move(levelHandler));
				});
				break;
			}
		}
	}

	if (auto multiLevelHandler = dynamic_cast<MultiLevelHandler*>(_currentHandler.get())) {
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

void GameEventHandler::SetStateHandler(std::unique_ptr<IStateHandler>&& handler)
{
	_currentHandler = std::move(handler);

	Viewport::chain().clear();
	Vector2i res = theApplication().resolution();
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

	constexpr std::uint64_t currentVersion = parseVersion({ NCINE_VERSION, countof(NCINE_VERSION) - 1 });

	auto& resolver = ContentResolver::Get();
	auto cachePath = fs::CombinePath({ resolver.GetCachePath(), "Animations"_s, "cache.index"_s });

	// Check cache state
	{
		auto s = fs::Open(cachePath, FileAccessMode::Read);
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

	String animationsPath = fs::CombinePath(resolver.GetCachePath(), "Animations"_s);
	fs::RemoveDirectoryRecursive(animationsPath);
	if (!Compatibility::JJ2Anims::Convert(animsPath, animationsPath, false)) {
		LOGE("Provided Jazz Jackrabbit 2 version is not supported. Make sure supported Jazz Jackrabbit 2 version is present in \"%s\" directory.", resolver.GetSourcePath().data());
		_flags |= Flags::IsVerified;
		return;
	}

	RefreshCacheLevels();

	LOGI("Cache was recreated");
	std::int64_t animsModified = fs::GetLastModificationTime(animsPath).GetValue();
	WriteCacheDescriptor(cachePath, currentVersion, animsModified);

	std::uint32_t filesRemoved = RenderResources::binaryShaderCache().prune();
	LOGI("Pruning binary shader cache (removed %u files)...", filesRemoved);

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
			if (episode.Open(item)) {
				if (episode.Name == "home"_s || (hasChristmasChronicles && episode.Name == "xmas98"_s)) {
					continue;
				}

				String fullPath = fs::CombinePath(episodesPath, (episode.Name == "xmas98"_s ? "xmas99"_s : StringView(episode.Name)) + ".j2e"_s);
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
							fullPath = fs::CombinePath({ episodesPath, it->second.first(), level.LevelName + ".j2l"_s });
						} else {
							fullPath = fs::CombinePath({ episodesPath, it->second.first(), it->second.second() + "_"_s + level.LevelName + ".j2l"_s });
						}
					} else {
						fullPath = fs::CombinePath({ episodesPath, "unknown"_s, level.LevelName + ".j2l"_s });
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
						fs::Copy(adjustedPath, fullPath.prefix(foundDot.begin()) + ".j2as"_s);
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
		String tilesetPath = fs::CombinePath(resolver.GetSourcePath(), pair.first + ".j2t"_s);
		auto adjustedPath = fs::FindPathCaseInsensitive(tilesetPath);
		if (fs::IsReadableFile(adjustedPath)) {
			Compatibility::JJ2Tileset tileset;
			if (tileset.Open(adjustedPath, false)) {
				tileset.Convert(fs::CombinePath({ tilesetsPath, pair.first + ".j2t"_s }));
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
	char DeviceDesc[256]; DWORD DeviceDescLength = (DWORD)arraySize(DeviceDesc);
	if (!::GetComputerNameA(DeviceDesc, &DeviceDescLength)) {
		DeviceDescLength = 0;
	}
	
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
		constexpr std::uint64_t currentVersion = parseVersion({ NCINE_VERSION, countof(NCINE_VERSION) - 1 });
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
	return true;
}

void GameEventHandler::WriteCacheDescriptor(const StringView& path, std::uint64_t currentVersion, std::int64_t animsModified)
{
	auto so = fs::Open(path, FileAccessMode::Write);
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
	if (levelInit.LastEpisodeName.empty()) {
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

	if (playerCount == 1) {
		auto episodeEnd = PreferencesCache::GetEpisodeEnd(levelInit.LastEpisodeName, true);
		episodeEnd->Flags = EpisodeContinuationFlags::IsCompleted;
		if (levelInit.CheatsUsed) {
			episodeEnd->Flags |= EpisodeContinuationFlags::CheatsUsed;
		}

		episodeEnd->Lives = firstPlayer->Lives;
		episodeEnd->Score = firstPlayer->Score;
		std::memcpy(episodeEnd->Ammo, firstPlayer->Ammo, sizeof(firstPlayer->Ammo));
		std::memcpy(episodeEnd->WeaponUpgrades, firstPlayer->WeaponUpgrades, sizeof(firstPlayer->WeaponUpgrades));

		PreferencesCache::Save();
	}
}

void GameEventHandler::SaveEpisodeContinue(const LevelInitialization& levelInit)
{
	if (levelInit.EpisodeName.empty() || levelInit.LevelName.empty() ||
		levelInit.EpisodeName == "unknown"_s ||
		(levelInit.EpisodeName == "prince"_s && levelInit.LevelName == "trainer"_s)) {
		return;
	}

	std::optional<Episode> currentEpisode = ContentResolver::Get().GetEpisode(levelInit.EpisodeName);
	if (!currentEpisode.has_value() || currentEpisode->FirstLevel == levelInit.LevelName) {
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

	if (playerCount == 1) {
		auto episodeContinue = PreferencesCache::GetEpisodeContinue(levelInit.EpisodeName, true);
		episodeContinue->LevelName = levelInit.LevelName;
		episodeContinue->State.Flags = EpisodeContinuationFlags::None;
		if (levelInit.CheatsUsed) {
			episodeContinue->State.Flags |= EpisodeContinuationFlags::CheatsUsed;
		}

		episodeContinue->State.DifficultyAndPlayerType = ((int32_t)levelInit.Difficulty & 0x0f) | (((int32_t)firstPlayer->Type & 0x0f) << 4);
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

void GameEventHandler::UpdateRichPresence(const LevelInitialization& levelInit)
{
#if (defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)) || defined(DEATH_TARGET_UNIX)
	if (!PreferencesCache::EnableDiscordIntegration || !DiscordRpcClient::Get().IsSupported()) {
		return;
	}

	DiscordRpcClient::RichPresence richPresence;
	if (levelInit.LevelName.empty()) {
		richPresence.State = "Resting in main menu"_s;
		richPresence.LargeImage = "main-transparent"_s;
	} else {
		if (levelInit.EpisodeName == "prince"_s) {
			if (levelInit.LevelName == "01_castle1"_s || levelInit.LevelName == "02_castle1n"_s) {
				richPresence.LargeImage = "level-prince-01"_s;
			} else if (levelInit.LevelName == "03_carrot1"_s || levelInit.LevelName == "04_carrot1n"_s) {
				richPresence.LargeImage = "level-prince-02"_s;
			} else if (levelInit.LevelName == "05_labrat1"_s || levelInit.LevelName == "06_labrat2"_s || levelInit.LevelName == "bonus_labrat3"_s) {
				richPresence.LargeImage = "level-prince-03"_s;
			}
		} else if (levelInit.EpisodeName == "rescue"_s) {
			if (levelInit.LevelName == "01_colon1"_s || levelInit.LevelName == "02_colon2"_s) {
				richPresence.LargeImage = "level-rescue-01"_s;
			} else if (levelInit.LevelName == "03_psych1"_s || levelInit.LevelName == "04_psych2"_s || levelInit.LevelName == "bonus_psych3"_s) {
				richPresence.LargeImage = "level-rescue-02"_s;
			} else if (levelInit.LevelName == "05_beach"_s || levelInit.LevelName == "06_beach2"_s) {
				richPresence.LargeImage = "level-rescue-03"_s;
			}
		} else if (levelInit.EpisodeName == "flash"_s) {
			if (levelInit.LevelName == "01_diam1"_s || levelInit.LevelName == "02_diam3"_s) {
				richPresence.LargeImage = "level-flash-01"_s;
			} else if (levelInit.LevelName == "03_tube1"_s || levelInit.LevelName == "04_tube2"_s || levelInit.LevelName == "bonus_tube3"_s) {
				richPresence.LargeImage = "level-flash-02"_s;
			} else if (levelInit.LevelName == "05_medivo1"_s || levelInit.LevelName == "06_medivo2"_s || levelInit.LevelName == "bonus_garglair"_s) {
				richPresence.LargeImage = "level-flash-03"_s;
			}
		} else if (levelInit.EpisodeName == "monk"_s) {
			if (levelInit.LevelName == "01_jung1"_s || levelInit.LevelName == "02_jung2"_s) {
				richPresence.LargeImage = "level-monk-01"_s;
			} else if (levelInit.LevelName == "03_hell"_s || levelInit.LevelName == "04_hell2"_s) {
				richPresence.LargeImage = "level-monk-02"_s;
			} else if (levelInit.LevelName == "05_damn"_s || levelInit.LevelName == "06_damn2"_s) {
				richPresence.LargeImage = "level-monk-03"_s;
			}
		} else if (levelInit.EpisodeName == "secretf"_s) {
			if (levelInit.LevelName == "01_easter1"_s || levelInit.LevelName == "02_easter2"_s || levelInit.LevelName == "03_easter3"_s) {
				richPresence.LargeImage = "level-secretf-01"_s;
			} else if (levelInit.LevelName == "04_haunted1"_s || levelInit.LevelName == "05_haunted2"_s || levelInit.LevelName == "06_haunted3"_s) {
				richPresence.LargeImage = "level-secretf-02"_s;
			} else if (levelInit.LevelName == "07_town1"_s || levelInit.LevelName == "08_town2"_s || levelInit.LevelName == "09_town3"_s) {
				richPresence.LargeImage = "level-secretf-03"_s;
			}
		} else if (levelInit.EpisodeName == "xmas98"_s || levelInit.EpisodeName == "xmas99"_s) {
			richPresence.LargeImage = "level-xmas"_s;
		} else if (levelInit.EpisodeName == "share"_s) {
			richPresence.LargeImage = "level-share"_s;
		}

		if (richPresence.LargeImage.empty()) {
			richPresence.Details = "Playing as "_s;
			richPresence.LargeImage = "main-transparent"_s;

			switch (levelInit.PlayerCarryOvers[0].Type) {
				default:
				case PlayerType::Jazz: richPresence.SmallImage = "playing-jazz"_s; break;
				case PlayerType::Spaz: richPresence.SmallImage = "playing-spaz"_s; break;
				case PlayerType::Lori: richPresence.SmallImage = "playing-lori"_s; break;
			}
		} else {
			richPresence.Details = "Playing episode as "_s;
		}

		switch (levelInit.PlayerCarryOvers[0].Type) {
			default:
			case PlayerType::Jazz: richPresence.Details += "Jazz"_s; break;
			case PlayerType::Spaz: richPresence.Details += "Spaz"_s; break;
			case PlayerType::Lori: richPresence.Details += "Lori"_s; break;
		}
	}

	DiscordRpcClient::Get().SetRichPresence(richPresence);
#endif
}

bool GameEventHandler::TryParseAddressAndPort(const StringView& input, String& address, std::uint16_t& port)
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

#if defined(DEATH_TARGET_ANDROID)
std::unique_ptr<IAppEventHandler> CreateAppEventHandler()
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
	return MainApplication::start([]() -> std::unique_ptr<IAppEventHandler> {
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
		fprintf(stdout, "%s%s%s%s%s\n", padding, Reset, Faint, Copyright, Reset);
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
		if (term != nullptr && strcmp(term, "xterm-256color") == 0) {
			fwrite(TermLogo, sizeof(unsigned char), arraySize(TermLogo), stdout);
			logoVisible = true;
		}
	}
#endif
#if defined(DEATH_TARGET_UNIX)
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--version") == 0) {
			// Just print current version below the logo and quit
			return PrintVersion(logoVisible);
		}
	}
#endif

	return MainApplication::start([]() -> std::unique_ptr<IAppEventHandler> {
		return std::make_unique<GameEventHandler>();
	}, argc, argv);
}
#endif