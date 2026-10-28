#include "PlayTestLevelHandler.h"

#if defined(WITH_EDITOR)

#include "PlayTestDebug.h"
#include "../../ContentResolver.h"
#include "../../PreferencesCache.h"
#include "../../Actors/Player.h"
#include "../../Events/EventMap.h"
#include "../../Rendering/PlayerViewport.h"
#include "../../Tiles/TileMap.h"
#include "../../UI/HUD.h"

#include "../../../nCine/Application.h"
#include "../../../nCine/Audio/AudioStreamPlayer.h"

#include <IO/MemoryStream.h>

using namespace Death::IO;

namespace Jazz2::Editor
{
	namespace
	{
		// A move longer than this between two frames is a warp, which starts a new segment
		constexpr float PathWarpDistance = 200.0f;
	}

	PlayTestLevelHandler::PlayTestLevelHandler(IRootController* root)
		: LevelHandler(root)
	{
		// Before the level is initialized, it makes the node the objects are under
		_playTestDebug = std::make_unique<PlayTestDebug>(*this);
	}

	PlayTestLevelHandler::~PlayTestLevelHandler()
	{
#if defined(WITH_AUDIO)
		// A play-test that isn't kept anymore takes its sounds with it, also the paused ones and those an actor still holds
		for (auto& sound : _playingSounds) {
			sound->stop();
		}
#endif
	}

	bool PlayTestLevelHandler::Initialize(const LevelDocument& document, StringView levelName, StringView fullPath, const PlayTestOptions& options)
	{
		if (!SaveLevel(document, levelName, _levelData)) {
			return false;
		}
		_fullPath = fullPath;
		_options = options;

		LevelInitialization levelInit(levelName, options.Difficulty, options.Reforged, false, options.Player);
		bool result = LevelHandler::Initialize(levelInit);
		_levelData = SmallVector<std::uint8_t, 0>();
		if (!result) {
			return false;
		}
		RetainResources();
		if (_hud != nullptr) {
			// The editor reveals the level in its own way, not from black
			_hud->CancelFadeIn();
		}
		return true;
	}

	LevelHandler& PlayTestLevelHandler::GetLevelHandler()
	{
		return *this;
	}

	Rendering::PlayerViewport* PlayTestLevelHandler::GetViewport() const
	{
		return (!_assignedViewports.empty() ? _assignedViewports[0].get() : nullptr);
	}

	Actors::Player* PlayTestLevelHandler::GetPlayer() const
	{
		return (!_players.empty() ? _players[0] : nullptr);
	}

	Texture* PlayTestLevelHandler::GetFrameTexture() const
	{
		return _upscalePass.GetTargetTexture();
	}

	void PlayTestLevelHandler::Suspend()
	{
#if defined(WITH_AUDIO)
		if (_music != nullptr) {
			_music->pause();
		}
		// The sound effects wait for the play-test to go on, as in the pause menu. The music of Sugar Rush ends, the music
		// of the level comes back instead, as it always did.
		if (_sugarRushMusic != nullptr) {
			_sugarRushMusic->stop();
		}
		PauseAllSfx();
#endif
	}

	void PlayTestLevelHandler::Resume()
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
		// A resumed run is a new part of the path
		_playPath.BreakSegment(PlayPath::Break::None);
	}

	bool PlayTestLevelHandler::IsLedgeClimbEnabled(const Actors::Player* player) const
	{
		return _options.LedgeClimb;
	}

	bool PlayTestLevelHandler::IsEditorPlayTest() const
	{
		return true;
	}

	void PlayTestLevelHandler::ExitEditor()
	{
		if (OnExitEditor) {
			OnExitEditor();
		}
	}

	bool PlayTestLevelHandler::SerializeResumableToStream(Stream& dest)
	{
		// A play-test is never resumed as a game
		return false;
	}

	void PlayTestLevelHandler::OnEndFrame()
	{
		LevelHandler::OnEndFrame();

		// By the time of the level, which stands still while it's paused, see PlayTestDebug
		float timeMult = GetLevelTimeMult();
		Actors::Player* player = GetPlayer();
		if (player != nullptr && timeMult > 0.0f) {
			_playPath.Record(player->GetPos(), player->GetSpeed().Length(), timeMult);
		}
	}

	void PlayTestLevelHandler::HandlePlayerWarped(Actors::Player* player, Vector2f prevPos, WarpFlags flags)
	{
		LevelHandler::HandlePlayerWarped(player, prevPos, flags);
		if (player == GetPlayer() && (player->GetPos() - prevPos).Length() > PathWarpDistance) {
			_playPath.BreakSegment(PlayPath::Break::Warped);
		}
	}

	bool PlayTestLevelHandler::HandlePlayerDied(Actors::Player* player)
	{
		if (player == GetPlayer()) {
			_playPath.BreakSegment(PlayPath::Break::Died);
		}
		return LevelHandler::HandlePlayerDied(player);
	}

	bool PlayTestLevelHandler::LoadLevel(LevelDescriptor& descriptor)
	{
		MemoryStream s(_levelData.data(), std::int64_t(_levelData.size()));
		return ContentResolver::Get().TryLoadLevel(s, _fullPath, GetLevelLoadOptions(), descriptor);
	}

	LevelLoadOptions PlayTestLevelHandler::GetLevelLoadOptions() const
	{
		LevelLoadOptions options;
		options.Difficulty = _options.Difficulty;
		return options;
	}

	void PlayTestLevelHandler::SpawnPlayers(const LevelInitialization& levelInit)
	{
		Vector2f spawnPosition;
		if (!_options.HasSpawnPosition || !_playTestDebug->FindClearPlayerPosition(_options.SpawnPosition, 160.0f, spawnPosition)) {
			_spawnFellBack = _options.HasSpawnPosition;
			LevelHandler::SpawnPlayers(levelInit);
			return;
		}

		// As in the game, only at another place
		std::shared_ptr<Actors::Player> player = std::make_shared<Actors::Player>();
		std::uint8_t playerParams[2] = { (std::uint8_t)levelInit.PlayerCarryOvers[0].Type, 0 };
		player->OnActivated(Actors::ActorActivationDetails(
			this,
			Vector3i((std::int32_t)spawnPosition.X, (std::int32_t)spawnPosition.Y, PlayerZ),
			playerParams
		));

		Actors::Player* ptr = player.get();
		_players.push_back(ptr);
		AddActor(player);
		AssignViewport(ptr);
		ptr->ReceiveLevelCarryOver(levelInit.LastExitType, levelInit.PlayerCarryOvers[0]);
		// The checkpoint is where the player started
		SetCheckpoint(ptr, spawnPosition);

		_hud = CreateHUD();
		_hud->BeginFadeIn(true);
	}
}

#endif
