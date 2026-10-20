#include "PlayTestMpLevelHandler.h"

#if defined(WITH_EDITOR) && defined(WITH_MULTIPLAYER)

#include "PlayTestDebug.h"
#include "../../ContentResolver.h"
#include "../../PreferencesCache.h"
#include "../../Actors/Player.h"
#include "../../Multiplayer/MpGameMode.h"
#include "../../Rendering/PlayerViewport.h"
#include "../../Tiles/TileMap.h"
#include "../../UI/HUD.h"

#include "../../../nCine/Application.h"
#include "../../../nCine/Audio/AudioStreamPlayer.h"

#include <IO/MemoryStream.h>

using namespace Death::IO;
using namespace Jazz2::Multiplayer;

namespace Jazz2::Editor
{
	namespace
	{
		// A move longer than this between two frames is a warp, which starts a new segment
		constexpr float PathWarpDistance = 200.0f;
	}

	namespace Implementation
	{
		PlayTestLocalSession::PlayTestLocalSession(const PlayTestOptions& options)
		{
			// As the game starts local splitscreen, see GameEventHandler::SetLevelHandler()
			auto serverConfig = NetworkManager::CreateDefaultServerConfiguration();
			serverConfig.GameMode = MpGameMode(options.GameMode);
			serverConfig.ReforgedGameplay = PreferencesCache::EnableReforgedGameplay;
			serverConfig.MinPlayerCount = 1;
			serverConfig.PreGameSecs = 0;
			serverConfig.AllowLedgeClimb = PreferencesCache::EnableLedgeClimb;

			_playTestNetwork = std::make_unique<NetworkManager>();
			_playTestNetwork->CreateLocalServer(this, Death::move(serverConfig));
		}

		PlayTestLocalSession::~PlayTestLocalSession()
		{
			if (_playTestNetwork != nullptr) {
				_playTestNetwork->Dispose();
			}
		}

		ConnectionResult PlayTestLocalSession::OnPeerConnected(const Peer& peer, std::uint32_t clientData)
		{
			// A local session has no peers but the local players
			return false;
		}

		void PlayTestLocalSession::OnPeerDisconnected(const Peer& peer, Reason reason)
		{
		}

		void PlayTestLocalSession::OnPacketReceived(const Peer& peer, std::uint8_t channelId, std::uint8_t packetType, ArrayView<const std::uint8_t> data)
		{
		}
	}

	PlayTestMpLevelHandler::PlayTestMpLevelHandler(IRootController* root, const PlayTestOptions& options)
		: PlayTestLocalSession(options), MpLevelHandler(root, _playTestNetwork.get(), MpLevelHandler::LevelState::InitialUpdatePending), _options(options)
	{
		// Before the level is initialized, it makes the node the objects are under
		_playTestDebug = std::make_unique<PlayTestDebug>(*this);
	}

	PlayTestMpLevelHandler::~PlayTestMpLevelHandler()
	{
#if defined(WITH_AUDIO)
		// A play-test that isn't kept anymore takes its sounds with it, also the paused ones and those an actor still holds
		for (auto& sound : _playingSounds) {
			sound->stop();
		}
#endif
	}

	bool PlayTestMpLevelHandler::Initialize(const LevelDocument& document, StringView levelName, StringView fullPath)
	{
		if (!SaveLevel(document, levelName, _levelData)) {
			return false;
		}
		_fullPath = fullPath;

		// The first player as chosen, the others take the next characters
		PlayerType players[LevelInitialization::MaxPlayerCount];
		std::int32_t count = std::clamp<std::int32_t>(_options.PlayerCount, 1, LevelInitialization::MaxPlayerCount);
		for (std::int32_t i = 0; i < count; i++) {
			std::int32_t character = (std::int32_t(_options.Player) - std::int32_t(PlayerType::Jazz) + i) % 3;
			players[i] = PlayerType(std::int32_t(PlayerType::Jazz) + character);
		}
		LevelInitialization levelInit(levelName, _options.Difficulty, PreferencesCache::EnableReforgedGameplay, false, arrayView(players, std::size_t(count)));
		levelInit.IsLocalSession = true;
		levelInit.LocalMultiplayerGameMode = _options.GameMode;
		bool result = MpLevelHandler::Initialize(levelInit);
		_levelData = SmallVector<std::uint8_t, 0>();
		if (!result) {
			return false;
		}
		RetainResources();

		if (_options.HasSpawnPosition) {
			// The players start where they were asked to, next to each other as they start in the game
			for (std::size_t i = 0; i < _players.size(); i++) {
				Vector2f pos;
				if (!_playTestDebug->FindClearPlayerPosition(_options.SpawnPosition + Vector2f(float(i) * 30.0f, 0.0f), 160.0f, pos)) {
					_spawnFellBack = true;
					break;
				}
				_players[i]->WarpToPosition(pos, WarpFlags::Fast);
				SetCheckpoint(_players[i], pos);
			}
		}
		if (_hud != nullptr) {
			// The editor reveals the level in its own way, not from black
			_hud->CancelFadeIn();
		}
		return true;
	}

	LevelHandler& PlayTestMpLevelHandler::GetLevelHandler()
	{
		return *this;
	}

	Rendering::PlayerViewport* PlayTestMpLevelHandler::GetViewport() const
	{
		return (!_assignedViewports.empty() ? _assignedViewports[0].get() : nullptr);
	}

	Actors::Player* PlayTestMpLevelHandler::GetPlayer() const
	{
		return (!_players.empty() ? _players[0] : nullptr);
	}

	Texture* PlayTestMpLevelHandler::GetFrameTexture() const
	{
		return _upscalePass.GetTargetTexture();
	}

	void PlayTestMpLevelHandler::Suspend()
	{
#if defined(WITH_AUDIO)
		if (_music != nullptr) {
			_music->pause();
		}
		// As PlayTestLevelHandler::Suspend(), the sound effects wait, the music of Sugar Rush ends
		if (_sugarRushMusic != nullptr) {
			_sugarRushMusic->stop();
		}
		PauseAllSfx();
#endif
	}

	void PlayTestMpLevelHandler::Resume()
	{
#if defined(WITH_AUDIO)
		if (_music != nullptr && _musicStartDelay == 0) {
			_music->play();
		}
		// Under the pause menu they go on once it's closed
		if (!IsPausable() || _pauseMenu == nullptr) {
			ResumeAllSfx();
		}
#endif
		_playPath.BreakSegment(PlayPath::Break::None);
	}

	bool PlayTestMpLevelHandler::IsEditorPlayTest() const
	{
		return true;
	}

	void PlayTestMpLevelHandler::ExitEditor()
	{
		if (OnExitEditor) {
			OnExitEditor();
		}
	}

	bool PlayTestMpLevelHandler::SerializeResumableToStream(Stream& dest)
	{
		return false;
	}

	void PlayTestMpLevelHandler::OnEndFrame()
	{
		MpLevelHandler::OnEndFrame();

		// By the time of the level, which stands still while it's paused, see PlayTestDebug
		float timeMult = GetLevelTimeMult();
		Actors::Player* player = GetPlayer();
		if (player != nullptr && timeMult > 0.0f) {
			_playPath.Record(player->GetPos(), player->GetSpeed().Length(), timeMult);
		}
	}

	void PlayTestMpLevelHandler::HandleLevelChange(LevelInitialization&& levelInit)
	{
		// Without a playlist, and never online, the round or the level ends in the editor (see EditorSession::ChangeLevel()),
		// a level that follows is played on in local splitscreen as the play-test was, not by a session that isn't there
		levelInit.IsLocalSession = true;
		levelInit.LocalMultiplayerGameMode = _options.GameMode;
		LevelHandler::HandleLevelChange(Death::move(levelInit));
	}

	void PlayTestMpLevelHandler::HandlePlayerWarped(Actors::Player* player, Vector2f prevPos, WarpFlags flags)
	{
		MpLevelHandler::HandlePlayerWarped(player, prevPos, flags);
		if (player == GetPlayer() && (player->GetPos() - prevPos).Length() > PathWarpDistance) {
			_playPath.BreakSegment(PlayPath::Break::Warped);
		}
	}

	bool PlayTestMpLevelHandler::HandlePlayerDied(Actors::Player* player)
	{
		if (player == GetPlayer()) {
			_playPath.BreakSegment(PlayPath::Break::Died);
		}
		return MpLevelHandler::HandlePlayerDied(player);
	}

	bool PlayTestMpLevelHandler::LoadLevel(LevelDescriptor& descriptor)
	{
		MemoryStream s(_levelData.data(), std::int64_t(_levelData.size()));
		return ContentResolver::Get().TryLoadLevel(s, _fullPath, GetLevelLoadOptions(), descriptor);
	}
}

#endif
