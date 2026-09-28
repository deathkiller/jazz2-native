#include "LevelHandler.h"
#include "ContentResolver.h"
#include "PreferencesCache.h"
#include "Rendering/PlayerViewport.h"
#include "UI/DiscordRpcClient.h"
#include "UI/HUD.h"
#include "UI/InGameConsole.h"
#include "UI/Menu/InGameMenu.h"
#include "../Main.h"

#if defined(WITH_ANGELSCRIPT)
#	include "Scripting/LevelScriptLoader.h"
#endif

#include "../nCine/I18n.h"
#include "../nCine/MainApplication.h"
#include "../nCine/ServiceLocator.h"
#include "../nCine/tracy.h"
#if defined(DEATH_TARGET_DREAMCAST)
#	include "../nCine/Backends/Dc/DcPlatform.h"
#endif
#include "../nCine/Base/Random.h"
#include "../nCine/Graphics/Camera.h"
#include "../nCine/Graphics/Texture.h"
#include "../nCine/Graphics/Viewport.h"
#include "../nCine/Input/JoyMapping.h"

#include "Actors/Player.h"
#include "Actors/SolidObjectBase.h"
#include "Actors/Enemies/Bosses/BossBase.h"
#include "Actors/Environment/IceBlock.h"

#if defined(WITH_PHYSICS_PROBE)
#	include "Tests/PhysicsProbe.h"
#endif

#include <float.h>

#include <Containers/StaticArray.h>
#include <Containers/StringConcatenable.h>
#include <Containers/StringUtils.h>
#include <IO/MemoryStream.h>
#include <Utf8.h>

using namespace nCine;
using namespace Jazz2::Tiles;

namespace Jazz2
{
	namespace Resources
	{
		static constexpr AnimState Snow = (AnimState)0;
		static constexpr AnimState Rain = (AnimState)1;
	}

	using namespace Jazz2::Resources;

	namespace
	{
		// Version of the level state snapshot format, snapshots are also stored in resumable states. Version 2 added
		// the special levels completed from the level at the end, version 1 is read the same way without them.
		constexpr std::uint16_t LevelStateVersion = 2;

		void WriteSnapshotString(Stream& dest, StringView value)
		{
			dest.WriteVariableUint32((std::uint32_t)value.size());
			dest.Write(value.data(), (std::int64_t)value.size());
		}

		String ReadSnapshotString(Stream& src)
		{
			std::uint32_t size = src.ReadVariableUint32();
			String value(NoInit, size);
			src.Read(value.data(), size);
			return value;
		}

		void WriteSnapshotData(Stream& dest, const LevelStateSnapshot* data)
		{
			std::uint32_t size = (data != nullptr ? (std::uint32_t)data->size() : 0);
			dest.WriteVariableUint32(size);
			if (size > 0) {
				dest.Write(data->data(), size);
			}
		}

		void ReadSnapshotData(Stream& src, LevelStateSnapshot& data)
		{
			std::uint32_t size = src.ReadVariableUint32();
			data.resize_for_overwrite(size);
			if (size > 0) {
				src.Read(data.data(), size);
			}
		}

		void AssignSnapshotData(LevelStateSnapshot& data, const MemoryStream& stream)
		{
			const std::uint8_t* buffer = stream.GetBuffer();
			data.assign(buffer, buffer + stream.GetSize());
		}

		void ReadSnapshotStrings(Stream& src, SmallVectorImpl<String>& values)
		{
			std::uint32_t count = src.ReadVariableUint32();
			values.clear();
			values.reserve(count);
			for (std::uint32_t i = 0; i < count; i++) {
				values.push_back(ReadSnapshotString(src));
			}
		}

		bool IsLevelStateVersionSupported(std::uint16_t version)
		{
			return (version >= 1 && version <= LevelStateVersion);
		}

		String ReadLevelStateName(const LevelStateSnapshot& state)
		{
			MemoryStream src(state.data(), (std::int64_t)state.size());
			if (!IsLevelStateVersionSupported(src.ReadValueAsLE<std::uint16_t>())) {
				return {};
			}
			return ReadSnapshotString(src);
		}
	}

#if defined(WITH_AUDIO)
	class AudioBufferPlayerForSplitscreen : public AudioBufferPlayer
	{
		DEATH_RUNTIME_OBJECT(AudioBufferPlayer);

	public:
		explicit AudioBufferPlayerForSplitscreen(AudioBuffer* audioBuffer, ArrayView<std::unique_ptr<Rendering::PlayerViewport>> viewports);

		Vector3f getAdjustedPosition(IAudioDevice& device, const Vector3f& pos, bool isSourceRelative, bool isAs2D) override;

		void updatePosition();
		void updateViewports(ArrayView<std::unique_ptr<Rendering::PlayerViewport>> viewports);

	private:
		ArrayView<std::unique_ptr<Rendering::PlayerViewport>> _viewports;
	};

	AudioBufferPlayerForSplitscreen::AudioBufferPlayerForSplitscreen(AudioBuffer* audioBuffer, ArrayView<std::unique_ptr<Rendering::PlayerViewport>> viewports)
		: AudioBufferPlayer(audioBuffer), _viewports(viewports)
	{
	}

	Vector3f AudioBufferPlayerForSplitscreen::getAdjustedPosition(IAudioDevice& device, const Vector3f& pos, bool isSourceRelative, bool isAs2D)
	{
		if (isSourceRelative || isAs2D) {
			return AudioBufferPlayer::getAdjustedPosition(device, pos, isSourceRelative, isAs2D);
		}

		std::size_t minIndex = 0;
		float minDistance = FLT_MAX;

		for (std::size_t i = 0; i < _viewports.size(); i++) {
			float distance = (pos.ToVector2() - _viewports[i]->_cameraPos).SqrLength();
			if (minDistance > distance) {
				minDistance = distance;
				minIndex = i;
			}
		}

		Vector3f relativePos = (pos - Vector3f(_viewports[minIndex]->_cameraPos, 0.0f));
		return AudioBufferPlayer::getAdjustedPosition(device, relativePos, false, false);
	}

	void AudioBufferPlayerForSplitscreen::updatePosition()
	{
		if (_state != PlayerState::Playing || GetFlags(PlayerFlags::SourceRelative) || GetFlags(PlayerFlags::As2D)) {
			return;
		}

		IAudioDevice& device = theServiceLocator().GetAudioDevice();
		setPositionInternal(getAdjustedPosition(device, _position, false, false));
	}

	void AudioBufferPlayerForSplitscreen::updateViewports(ArrayView<std::unique_ptr<Rendering::PlayerViewport>> viewports)
	{
		_viewports = viewports;
	}
#endif

	LevelHandler::LevelHandler(IRootController* root)
		: _root(root),
#if defined(RHI_CAP_SHADERS) && defined(RHI_CAP_FRAMEBUFFERS)
			_lightingMeshShader(nullptr), _blurShader(nullptr), _downsampleShader(nullptr), _combineShader(nullptr), _combineWithWaterShader(nullptr),
#endif
			_eventSpawner(this), _difficulty(GameDifficulty::Default), _isReforged(false),
			_cheatsUsed(false), _checkpointCreated(false), _nextLevelType(ExitType::None),
			_nextLevelTime(0.0f), _elapsedMillisecondsBegin(0), _elapsedFrames(0.0f), _checkpointFrames(0.0f),
			_waterLevel(FLT_MAX), _weatherType(WeatherType::None), _pressedKeys(ValueInit, (std::size_t)Keys::Count),
			_overrideActions(0), _overrideMovement(0.0f, 0.0f)
	{
	}

	LevelHandler::~LevelHandler()
	{
		_players.clear();

		// Remove nodes from UpscaleRenderPass
		for (auto& viewport : _assignedViewports) {
			viewport->_combineRenderer->setParent(nullptr);
		}
		// Not created if the level failed to load
		if (_hud != nullptr) {
			_hud->setParent(nullptr);
		}
		if (_console != nullptr) {
			_console->setParent(nullptr);
		}

		TracyPlot("Actors", 0LL);
	}

	bool LevelHandler::Initialize(const LevelInitialization& levelInit)
	{
		ZoneScopedC(0x4876AF);

		if (levelInit.RestoreLevelState != nullptr) {
			// Returning from a special level, the level is restored exactly as it was left
			return InitializeFromLevelState(*levelInit.RestoreLevelState, levelInit);
		}

		_levelName = levelInit.LevelName;
		_difficulty = levelInit.Difficulty;
		_isReforged = levelInit.IsReforged;
		_cheatsUsed = levelInit.CheatsUsed;
		_elapsedMillisecondsBegin = levelInit.ElapsedMilliseconds;

		auto& resolver = ContentResolver::Get();
		// Scoped, because both overloads return early when the level cannot be loaded - see LoadingScope
		ContentResolver::LoadingScope loadingScope(resolver);

#if defined(RHI_CAP_POSTPROCESSING)
		// Only the post-processing combine samples it (see CombineRenderer); the direct tier never binds it,
		// so an RGBA8 64x64 would sit there for the whole level without ever being read
		_noiseTexture = resolver.GetNoiseTexture();
#endif

		_rootNode = std::make_unique<SceneNode>();
		_rootNode->setVisitOrderState(SceneNode::VisitOrderState::Disabled);

		_console = std::make_unique<UI::InGameConsole>(this);

		auto p = _levelName.partition('/');

		// Try to search also "unknown" directory
		LevelDescriptor descriptor;
		if (!resolver.TryLoadLevel(_levelName, _difficulty, descriptor) &&
			(p[0] == "unknown"_s || !resolver.TryLoadLevel(String("unknown/"_s + p[2]), _difficulty, descriptor))) {
			LOGE("Cannot load level \"{}\"", _levelName);
			return false;
		}

		_console->WriteLine(UI::MessageLevel::Debug, _f("Level \"{}\" initialized", descriptor.DisplayName));

		AttachComponents(std::move(descriptor));
		SpawnPlayers(levelInit);		

		// The level was entered from another one, which the player returns to once this special level is completed
		_returnLevelState = levelInit.ReturnLevelState;

		// Behind the loading screen, where a sheet read costs nothing visible (see the implementation)
		resolver.PreloadDeferredAnimations();

		OnInitialized();

		return true;
	}

	bool LevelHandler::Initialize(Stream& src, std::uint16_t version)
	{
		ZoneScopedC(0x4876AF);

		std::uint8_t flags = src.ReadValue<std::uint8_t>();

		std::uint8_t stringSize = src.ReadValue<std::uint8_t>();
		String episodeName(NoInit, stringSize);
		src.Read(episodeName.data(), stringSize);

		stringSize = src.ReadValue<std::uint8_t>();
		String levelFileName(NoInit, stringSize);
		src.Read(levelFileName.data(), stringSize);

		_levelName = episodeName + '/' + levelFileName;

		_difficulty = (GameDifficulty)src.ReadValue<std::uint8_t>();
		_isReforged = (flags & 0x01) != 0;
		_cheatsUsed = (flags & 0x02) != 0;
		if (version >= 3) {
			_elapsedMillisecondsBegin = src.ReadVariableUint64();
		}
		_checkpointFrames = src.ReadValue<float>();

		auto& resolver = ContentResolver::Get();
		// Scoped, because both overloads return early when the level cannot be loaded - see LoadingScope
		ContentResolver::LoadingScope loadingScope(resolver);

#if defined(RHI_CAP_POSTPROCESSING)
		// Only the post-processing combine samples it (see CombineRenderer); the direct tier never binds it,
		// so an RGBA8 64x64 would sit there for the whole level without ever being read
		_noiseTexture = resolver.GetNoiseTexture();
#endif

		_rootNode = std::make_unique<SceneNode>();
		_rootNode->setVisitOrderState(SceneNode::VisitOrderState::Disabled);

		_console = std::make_unique<UI::InGameConsole>(this);

		LevelDescriptor descriptor;
		if (!resolver.TryLoadLevel(_levelName, _difficulty, descriptor)) {
			LOGE("Cannot load level \"{}\"", _levelName);
			return false;
		}

		_console->WriteLine(UI::MessageLevel::Debug, _f("Level \"{}\" initialized", descriptor.DisplayName));

		AttachComponents(std::move(descriptor));

		// All components are ready, deserialize the rest of state
		_waterLevel = src.ReadValueAsLE<float>();
		_weatherType = (WeatherType)src.ReadValue<std::uint8_t>();
		_weatherIntensity = src.ReadValue<std::uint8_t>();

		_tileMap->InitializeFromStream(src);
		_eventMap->InitializeFromStream(src);

		std::uint32_t playerCount = src.ReadValue<std::uint8_t>();
		_players.reserve(playerCount);

		for (std::uint32_t i = 0; i < playerCount; i++) {
			std::shared_ptr<Actors::Player> player = CreateResumablePlayer((std::int32_t)i);
			Actors::Player* ptr = player.get();

			// The viewport has to be assigned before the state is applied. InitializeFromStream() restores the
			// ambient light the checkpoint was taken in by calling SetAmbientLight(), and that only records the
			// value on the player itself when no viewport targets it yet - the viewport would then be constructed
			// with the level's default light and keep it, so a level saved in a darkened section resumed at full
			// brightness. SpawnPlayers() assigns it before ReceiveLevelCarryOver() for the same reason.
			AssignViewport(ptr);
			player->InitializeFromStream(this, src, version);

			_players.push_back(ptr);
			AddActor(player);
		}

		_hud = CreateHUD();
		_hud->BeginFadeIn(false);

		OnInitialized();

		// Set it at the end, so ambient light transition is skipped
		_elapsedFrames = _checkpointFrames;

		if (version >= 5) {
			// Actors as they were at the checkpoint, their events are marked active, so they are not spawned twice
			ReadSnapshotData(src, _checkpointSnapshot);
			if (!_checkpointSnapshot.empty()) {
				_eventMap->InitializeActiveStateFromStream(src);
				if (CanUseLevelStateSnapshots()) {
					RestoreCheckpointSnapshot();
				} else {
					_checkpointSnapshot.clear();
				}
			}

			LevelStateSnapshot returnLevelState;
			ReadSnapshotData(src, returnLevelState);
			if (!returnLevelState.empty()) {
				_returnLevelState = std::make_shared<LevelStateSnapshot>(std::move(returnLevelState));
			}
		}

		if (version >= 6) {
			ReadSnapshotStrings(src, _completedSpecialLevels);
		}

		return true;
	}

	void LevelHandler::OnInitialized()
	{
		auto& resolver = ContentResolver::Get();
		_commonResources = resolver.RequestMetadata("Common/Scenery"_s);
		resolver.PreloadMetadataAsync("Common/Explosions"_s);

		_eventMap->PreloadEventsAsync();

		InitializeRumbleEffects();
		UpdateRichPresence();

		_console->OnInitialized();

#if defined(WITH_ANGELSCRIPT)
		if (_scripts != nullptr) {
			_scripts->OnLevelLoad();
		}
#endif
	}

	Events::EventSpawner* LevelHandler::EventSpawner()
	{
		return &_eventSpawner;
	}

	Events::EventMap* LevelHandler::EventMap()
	{
		return _eventMap.get();
	}

	Tiles::TileMap* LevelHandler::TileMap()
	{
		return _tileMap.get();
	}

	GameDifficulty LevelHandler::GetDifficulty() const
	{
		return _difficulty;
	}

	bool LevelHandler::IsLocalSession() const
	{
		return true;
	}

	bool LevelHandler::IsServer() const
	{
		return true;
	}

	bool LevelHandler::IsPausable() const
	{
		return true;
	}

	bool LevelHandler::IsReforged() const
	{
		return _isReforged;
	}

	bool LevelHandler::CanActivateSugarRush() const
	{
		return true;
	}

	bool LevelHandler::CanEventDisappear(EventType eventType) const
	{
		return true;
	}

	bool LevelHandler::CanPlayersCollide() const
	{
		// Enable player-vs-player physical collision (bump apart + stand on each other) in local splitscreen co-op,
		// i.e. whenever more than one player shares the level. Online sessions override this to `false` and resolve
		// collisions server-side (see MpLevelHandler / PlayerOnServer).
		return (_players.size() > 1);
	}

	Recti LevelHandler::GetLevelBounds() const
	{
		return _levelBounds;
	}

	float LevelHandler::GetElapsedFrames() const
	{
		return _elapsedFrames;
	}

	float LevelHandler::GetGravity() const
	{
		constexpr float DefaultGravity = 0.3f;

		// Higher gravity in Reforged mode
		return (_isReforged ? DefaultGravity : DefaultGravity * 0.8f);
	}

	float LevelHandler::GetWaterLevel() const
	{
		return _waterLevel;
	}

	float LevelHandler::GetHurtInvulnerableTime() const
	{
		return 180.0f;
	}

	bool LevelHandler::GetActiveBossHealth(std::int32_t& health, std::int32_t& maxHealth) const
	{
		health = 0;
		maxHealth = 0;

		if (_activeBoss == nullptr) {
			return false;
		}

		health = _activeBoss->GetHealth();
		maxHealth = _activeBoss->GetMaxHealth();

		// Bosses with unlimited health have no meaningful bar to show
		return (maxHealth > 0 && maxHealth != INT32_MAX);
	}

	ArrayView<const std::shared_ptr<Actors::ActorBase>> LevelHandler::GetActors() const
	{
		return _actors;
	}

	ArrayView<Actors::Player* const> LevelHandler::GetPlayers() const
	{
		return _players;
	}

	Actors::ActorBase* LevelHandler::FindPlayerToStandOn(Actors::Player* player, float timeMult)
	{
		// No game-mode check here, unlike MpLevelHandler::IsPlayerStackingEnabled(): this handler only ever runs the
		// campaign, alone or in local splitscreen co-op, so every session it serves is already cooperative. Every
		// other mode - online and local splitscreen alike - goes through MpLevelHandler, which is where stacking is
		// restricted to Cooperation.
		//
		// Minimum horizontal overlap required to count as "on top of" the other player (not brushing its side)
		constexpr float MinHorizontalOverlap = 4.0f;
		// How close the feet must be to the other player's head to count as standing/landing on it
		constexpr float StandThreshold = 6.0f;

		// Only grab onto another player while falling or already resting, never while moving up (jumping off)
		Vector2f speed = player->GetSpeed();
		if (speed.Y < -0.1f) {
			return nullptr;
		}

		const AABBf& self = player->AABBInner;
		float feet = self.B;
		float nextFeet = feet + speed.Y * timeMult;

		// Treat the other player as a one-way platform: stand on it if our feet are at (or about to cross) its head
		// this frame, with enough horizontal overlap that we're really on top of it (not just brushing its side).
		for (auto* other : _players) {
			if (other == player || other->GetPlayerType() == PlayerType::Spectate) {
				continue;
			}
			const AABBf& o = other->AABBInner;
			if (self.R - o.L < MinHorizontalOverlap || o.R - self.L < MinHorizontalOverlap) {
				continue;
			}
			float top = o.T;
			if (feet <= top + StandThreshold && nextFeet >= top - StandThreshold) {
				return other;
			}
		}
		return nullptr;
	}

	float LevelHandler::GetDefaultAmbientLight() const
	{
		return _defaultAmbientLight.W;
	}

	float LevelHandler::GetAmbientLight(Actors::Player* player) const
	{
		for (auto& viewport : _assignedViewports) {
			if (viewport->_targetActor == player) {
				return viewport->_ambientLightTarget;
			}
		}
		return 0.0f;
	}

	void LevelHandler::SetAmbientLight(Actors::Player* player, float value)
	{
		// Remember it on the player as well, so checkpoints can restore the light they were activated in even where
		// there is no viewport to read it back from (a dedicated server, or a server-side shadow of a remote player)
		if (player != nullptr) {
			player->SetCurrentAmbientLight(value);
		}

		for (auto& viewport : _assignedViewports) {
			if (viewport->_targetActor == player) {
				viewport->_ambientLightTarget = value;

				// Skip transition if it was changed at the beginning of level
				if (_elapsedFrames < FrameTimer::FramesPerSecond * 0.25f) {
					viewport->_ambientLight.W = value;
				}
			}
		}
	}

	void LevelHandler::InvokeAsync(Function<void()>&& callback)
	{
		_root->InvokeAsync(weak_from_this(), std::move(callback));
	}

	void LevelHandler::AttachComponents(LevelDescriptor&& descriptor)
	{
		ZoneScopedC(0x4876AF);

		_levelDisplayName = std::move(descriptor.DisplayName);

		LOGI("Level \"{}\" (from \"{}.j2l\") loaded", _levelDisplayName, _levelName);

		if (!_levelDisplayName.empty()) {
			theApplication().GetGfxDevice().setWindowTitle(String(NCINE_APP_NAME " - " + _levelDisplayName));
		} else {
			theApplication().GetGfxDevice().setWindowTitle(NCINE_APP_NAME);
		}

		_defaultNextLevel = std::move(descriptor.NextLevel);
		_defaultSecretLevel = std::move(descriptor.SecretLevel);

		_tileMap = std::move(descriptor.TileMap);
		_tileMap->SetOwner(this);
		_tileMap->setParent(_rootNode.get());

		_eventMap = std::move(descriptor.EventMap);
		_eventMap->SetLevelHandler(this);

		Vector2i levelBounds = _tileMap->GetLevelBounds();
		_levelBounds = Recti(0, 0, levelBounds.X, levelBounds.Y);
		_viewBoundsTarget = _levelBounds.As<float>();

		_defaultAmbientLight = descriptor.AmbientColor;

		_weatherType = descriptor.Weather;
		_weatherIntensity = descriptor.WeatherIntensity;
		_waterLevel = descriptor.WaterLevel;

		_musicCurrentPath = std::move(descriptor.MusicPath);
		_musicDefaultPath = _musicCurrentPath;

#if defined(WITH_AUDIO)
		if (!_musicCurrentPath.empty()) {
			_music = ContentResolver::Get().GetMusic(_musicCurrentPath);
			if (_music != nullptr) {
				_music->setLooping(true);
				_music->setGain(PreferencesCache::MasterVolume * PreferencesCache::MusicVolume);
				_music->setSourceRelative(true);
				// Not started here: the rest of the level (the actors, their metadata on first sight) is
				// still to load, and the stream is fed from the main thread once per frame, so a stream
				// started now ran dry against that loading and came back with an audible gap after a second
				// of playing. OnBeginFrame() starts it once the first frames are through.
				_musicStartDelay = 2;
			}
		}
#endif

		_levelTexts = std::move(descriptor.LevelTexts);

#if defined(WITH_ANGELSCRIPT) || defined(DEATH_TRACE)
		// TODO: Allow script signing
		if (PreferencesCache::AllowUnsignedScripts) {
			const StringView foundDot = descriptor.FullPath.findLastOr('.', descriptor.FullPath.end());
			String scriptPath = (foundDot == descriptor.FullPath.end() ? StringView(descriptor.FullPath) : descriptor.FullPath.prefix(foundDot.begin())) + ".j2as"_s;
			if (auto scriptPathCaseInsensitive = fs::FindPathCaseInsensitive(scriptPath)) {
				if (fs::IsReadableFile(scriptPathCaseInsensitive)) {
#	if defined(WITH_ANGELSCRIPT)
					_scripts = std::make_unique<Scripting::LevelScriptLoader>(this, scriptPathCaseInsensitive);
#	else
					LOGW("Level requires scripting, but scripting support is disabled in this build");
#	endif
				}
			}
		}
#endif

#if defined(DEATH_TARGET_DREAMCAST)
		// The heap window is the binding constraint of this console and a level is the largest thing that
		// is ever loaded into it, so what one costs is worth a line in the log every time
		nCine::Backends::DcPlatform::LogMemoryStatus("level loaded");
#endif
	}

	std::unique_ptr<UI::HUD> LevelHandler::CreateHUD()
	{
		return std::make_unique<UI::HUD>(this);
	}

	void LevelHandler::SpawnPlayers(const LevelInitialization& levelInit)
	{
		std::int32_t playerCount = levelInit.GetPlayerCount();

		for (std::int32_t i = 0; i < std::int32_t(arraySize(levelInit.PlayerCarryOvers)); i++) {
			if (levelInit.PlayerCarryOvers[i].Type == PlayerType::None) {
				continue;
			}

			Vector2 spawnPosition = _eventMap->GetSpawnPosition(levelInit.PlayerCarryOvers[i].Type);
			if (spawnPosition.X < 0.0f && spawnPosition.Y < 0.0f) {
				spawnPosition = _eventMap->GetSpawnPosition(PlayerType::Jazz);
				if (spawnPosition.X < 0.0f && spawnPosition.Y < 0.0f) {
					// Nothing is drawn without a player, so a level that ends up here shows only a black
					// screen - worth a line in the log, because nothing else will say why
					LOGW("Level has no start position for player {}, the player cannot be spawned", i);
					continue;
				}
			}

			std::shared_ptr<Actors::Player> player = std::make_shared<Actors::Player>();
			std::uint8_t playerParams[2] = { (std::uint8_t)levelInit.PlayerCarryOvers[i].Type, (std::uint8_t)i };
			player->OnActivated(Actors::ActorActivationDetails(
				this,
				Vector3i((std::int32_t)spawnPosition.X + (i * 10) - ((playerCount - 1) * 5), (std::int32_t)spawnPosition.Y - (i * 20) + ((playerCount - 1) * 5), PlayerZ - i),
				playerParams
			));

			Actors::Player* ptr = player.get();
			_players.push_back(ptr);
			AddActor(player);
			AssignViewport(ptr);

			ptr->ReceiveLevelCarryOver(levelInit.LastExitType, levelInit.PlayerCarryOvers[i]);
		}

		_hud = CreateHUD();
		_hud->BeginFadeIn((levelInit.LastExitType & ExitType::FastTransition) == ExitType::FastTransition);
	}

	std::shared_ptr<Actors::Player> LevelHandler::CreateResumablePlayer(std::int32_t index)
	{
		return std::make_shared<Actors::Player>();
	}

	bool LevelHandler::IsCheatingAllowed(Actors::Player* player)
	{
		return PreferencesCache::AllowCheats;
	}

	Vector2i LevelHandler::GetViewSize() const
	{
		return _viewSize;
	}

	void LevelHandler::OnBeginFrame()
	{
		ZoneScopedC(0x4876AF);

		float timeMult = theApplication().GetTimeMult();

#if defined(WITH_AUDIO)
		// The level music is started only after the first frames have loaded what they load (see the level
		// loading above); it keeps playing under the pause menu, so a pause in the meantime changes nothing
		if DEATH_UNLIKELY(_musicStartDelay > 0 && --_musicStartDelay == 0 && _music != nullptr) {
			_music->play();
		} else if DEATH_UNLIKELY(_musicStartDelay == 0 && _music != nullptr && _music->isStopped()) {
			// A looping stream that reports itself stopped was stopped by something other than the game:
			// the only in-game paths either replace it or clear the pointer, and a pause does not stop it.
			// The audio device releases every player when it decides the output has gone away (see
			// AudioDeviceBase::checkForStalledSources() and ALAudioDevice::checkDeviceConnection()), which
			// frees the sources for new sounds but leaves anything long-lived stopped for good - nothing
			// else in the game ever starts the music a second time. Rescheduling the start handles that,
			// and costs one retry a second rather than one a frame if the device is genuinely gone.
			_musicStartDelay = MusicRestartDelay;
		}
#endif

		if DEATH_LIKELY(_pauseMenu == nullptr) {
			UpdatePressedActions();

			bool isGamepad;
			if DEATH_UNLIKELY(PlayerActionHit(nullptr, PlayerAction::Menu)) {
				if (_console->IsVisible()) {
					HideConsole();
				} else if (_nextLevelType == ExitType::None) {
					PauseGame();
				}
			} else if DEATH_UNLIKELY(PlayerActionHit(nullptr, PlayerAction::Console, true, isGamepad)) {
				if (_console->IsVisible()) {
					if (isGamepad) {
						HideConsole();
					}
				} else {
					ShowConsole();
				}
			}

			// The open console suppresses every gameplay action (see PlayerActionHit()), so its on-screen keyboard
			// toggle has to read the raw input state instead. It's the same button the menu uses for its text fields,
			// so a player without a hardware keyboard can type commands and chat messages here as well. Only the gamepad
			// bit is tested, because the key bound to the same action is also typed into the input line.
			if DEATH_UNLIKELY(_console->IsVisible() && theApplication().CanShowScreenKeyboard()) {
				constexpr std::uint64_t ChangeWeaponGamepadBit = (1ull << (32 + (std::int32_t)PlayerAction::ChangeWeapon));
				const auto& rawInput = _playerInputs[0];
				if ((rawInput.PressedActions & ChangeWeaponGamepadBit) != 0 && (rawInput.PressedActionsLast & ChangeWeaponGamepadBit) == 0) {
					_console->ToggleScreenKeyboard();
				}
			}
#if defined(DEATH_DEBUG)
			if DEATH_UNLIKELY(IsCheatingAllowed(nullptr) && PlayerActionPressed(nullptr, PlayerAction::ChangeWeapon) && PlayerActionHit(0, PlayerAction::Jump)) {
				_cheatsUsed = true;
				BeginLevelChange(nullptr, ExitType::Warp | ExitType::FastTransition);
			}
#endif
		}

#if defined(WITH_AUDIO)
		// Destroy stopped players and resume music after Sugar Rush
		if (_sugarRushMusic != nullptr && _sugarRushMusic->isStopped()) {
			_sugarRushMusic = nullptr;
			if (_music != nullptr) {
				_music->play();
			}
		}

		auto it = _playingSounds.begin();
		while (it != _playingSounds.end()) {
			if ((*it)->isStopped()) {
				it = _playingSounds.eraseUnordered(it);
				continue;
			}
			++it;
		}
#endif

		if DEATH_LIKELY(!IsPausable() || _pauseMenu == nullptr) {
			if (_nextLevelType != ExitType::None) {
				_nextLevelTime -= timeMult;
				ProcessQueuedNextLevel();
			}

			ProcessEvents(timeMult);
			ProcessWeather(timeMult);

#if defined(WITH_PHYSICS_PROBE)
			// Before the actors update, so the input it puts in takes effect this tick and the state it logs
			// is what the previous tick left behind
			if (PreferencesCache::PhysicsProbe) {
				if (_physicsProbe == nullptr) {
					_physicsProbe = std::make_unique<Tests::PhysicsProbe>(this);
				}
				_physicsProbe->OnUpdate(timeMult);
			}
#endif

			// Active Boss
			if (_activeBoss != nullptr && _activeBoss->GetHealth() <= 0) {
				_activeBoss = nullptr;
				BeginLevelChange(nullptr, ExitType::Boss);
			}

#if defined(WITH_ANGELSCRIPT)
			if (_scripts != nullptr) {
				_scripts->OnLevelUpdate(timeMult);
			}
#endif
		}
	}

	void LevelHandler::OnEndFrame()
	{
		ZoneScopedC(0x4876AF);

		float timeMult = theApplication().GetTimeMult();
		auto& resolver = ContentResolver::Get();

		if DEATH_UNLIKELY(_checkpointSnapshotPending) {
			CreateCheckpointSnapshot();
		}

		_tileMap->OnEndFrame();

		if DEATH_LIKELY(!IsPausable() || _pauseMenu == nullptr) {
			// Every actor applies its position to its renderer when it updates itself, but an actor can still be
			// moved afterwards by another actor that updates later - a platform carrying it, most visibly - and
			// would then be drawn a whole frame behind. This runs after all the updates and before the scene is
			// visited, so the drawn position is always the final one. It has to happen before ResolveCollisions()
			// clears `IsDirty`, which is what decides whether the position keeps its sub-pixel part.
			if (!resolver.IsHeadless()) {
				for (auto& actor : _actors) {
					actor->UpdateRendererPosition();
				}
			}

			ResolveCollisions(timeMult);

			if (!resolver.IsHeadless()) {
#if defined(NCINE_HAS_GAMEPAD_RUMBLE)
				_rumble.OnEndFrame(timeMult);
#endif

				for (auto& viewport : _assignedViewports) {
					viewport->UpdateCamera(timeMult);
				}

#if defined(WITH_AUDIO)
				if (!_assignedViewports.empty()) {
					// Update audio listener position
					IAudioDevice& audioDevice = theServiceLocator().GetAudioDevice();
					if (_assignedViewports.size() == 1) {
						audioDevice.updateListener(Vector3f(_assignedViewports[0]->_cameraPos, 0.0f),
							Vector3f(_assignedViewports[0]->_targetActor->GetSpeed(), 0.0f));
					} else {
						audioDevice.updateListener(Vector3f::Zero, Vector3f::Zero);

						// All audio players must be updated to the nearest listener
						for (auto& current : _playingSounds) {
							if (auto* currentForSplitscreen = runtime_cast<AudioBufferPlayerForSplitscreen>(current.get())) {
								currentForSplitscreen->updatePosition();
							}
						}
					}
				}
#endif
			}

			_elapsedFrames += timeMult;
		}

		if (!resolver.IsHeadless()) {
			for (auto& viewport : _assignedViewports) {
				viewport->OnEndFrame();
			}

#if defined(DEATH_DEBUG) && defined(WITH_IMGUI)
			if DEATH_UNLIKELY(PreferencesCache::PerformanceMetrics == PerformanceMetricsLevel::Detailed && !_assignedViewports.empty()) {
				ImDrawList* drawList = ImGui::GetBackgroundDrawList();
				const auto& mainViewport = *_assignedViewports[0];

				std::size_t actorsCount = _actors.size();
				for (std::size_t i = 0; i < actorsCount; i++) {
					auto* actor = _actors[i].get();

					auto pos = WorldPosToScreenSpace(actor->_pos, mainViewport);
					auto aabbMin = WorldPosToScreenSpace({ actor->AABB.L, actor->AABB.T }, mainViewport);
					auto aabbMax = WorldPosToScreenSpace({ actor->AABB.R, actor->AABB.B }, mainViewport);
					auto aabbInnerMin = WorldPosToScreenSpace({ actor->AABBInner.L, actor->AABBInner.T }, mainViewport);
					auto aabbInnerMax = WorldPosToScreenSpace({ actor->AABBInner.R, actor->AABBInner.B }, mainViewport);

					drawList->AddRect(ImVec2(pos.x - 2.4f, pos.y - 2.4f), ImVec2(pos.x + 2.4f, pos.y + 2.4f), ImColor(0, 0, 0, 220));
					drawList->AddRect(ImVec2(pos.x - 1.0f, pos.y - 1.0f), ImVec2(pos.x + 1.0f, pos.y + 1.0f), ImColor(120, 255, 200, 220));
					drawList->AddRect(aabbMin, aabbMax, ImColor(120, 200, 255, 180));
					drawList->AddRect(aabbInnerMin, aabbInnerMax, ImColor(255, 255, 255));
				}
			}
#endif
		}

		TracyPlot("Actors", static_cast<std::int64_t>(_actors.size()));
	}

	void LevelHandler::OnInitializeViewport(std::int32_t width, std::int32_t height)
	{
		ZoneScopedC(0x4876AF);

		auto& resolver = ContentResolver::Get();
		if (resolver.IsHeadless()) {
			// Use only the main viewport in headless mode
			_rootNode->setParent(&theApplication().GetRootNode());
			return;
		}

		// The logical view, bounded by DefaultWidth/Height scaled by the rendering resolution preference
		_viewSize = Rendering::UpscaleRenderPass::CalculateViewSize(width, height, DefaultWidth, DefaultHeight);
		std::int32_t w = _viewSize.X;
		std::int32_t h = _viewSize.Y;

		// When zooming out in splitscreen, each player's camera shows a full-size view but their on-screen region is
		// only a fraction of the framebuffer, so the per-player image would be minified to roughly half resolution.
		// Supersampling the shared framebuffer restores the per-player resolution; the logical coordinate space (and
		// thus the HUD layout) stays unchanged.
		bool useHalfRes = (PreferencesCache::PreferZoomOut && _assignedViewports.size() >= 3);
		std::int32_t supersample = (useHalfRes ? 2 : 1);
		_upscalePass.Initialize(w, h, width, height, supersample);

		// When the scene is supersampled, the HUD and in-game menu are rendered through a separate overlay pass at the
		// native resolution and then composited (nearest, so clean integer scaling) into the supersampled scene buffer,
		// on top of the player viewports. The combined buffer is then upscaled to the window by the scene pass, so the
		// rescale/upscale effect (HQ2x etc.) applies to everything at the end. When not supersampling, the HUD stays in
		// the scene pass directly as before. The pass can refuse to supersample (the direct tier renders straight into
		// the screen framebuffer, and it is forced off on Vita), and there the overlay would render its UI before the
		// scene instead of compositing on top of it, so it follows the factor the pass actually applied.
		_hudOverlayActive = (_upscalePass.GetSupersample() > 1);
		if (_hudOverlayActive) {
			// The overlay renders at the logical size; its composite quad is also logical, so it fills the logical view
			// and is rasterized into the (supersampled) scene buffer at an integer scale
			_hudUpscalePass.Initialize(w, h, w, h, 1, true);
			// Composite the overlay into the scene buffer (on top of the player viewports) instead of to the screen
			_hudUpscalePass.setParent(_upscalePass.GetNode());
		} else {
			_hudUpscalePass.setParent(nullptr);
		}

#if defined(RHI_CAP_SHADERS) && defined(RHI_CAP_FRAMEBUFFERS)
		// The bloom + lighting post-processing shaders are only used by the shader render path; the software
		// backend skips the whole chain and renders the scene directly to the screen (see RhiFwd.h)
		bool notInitialized = (_combineShader == nullptr);
		if (notInitialized) {
			LOGI("Acquiring required shaders");

			// Every light of a viewport goes out as one mesh, so there is no per-light program to acquire
			_lightingMeshShader = resolver.GetShader(PrecompiledShader::LightingMesh);
			if (_lightingMeshShader == nullptr) { LOGW("PrecompiledShader::LightingMesh failed"); }
			_blurShader = resolver.GetShader(PrecompiledShader::Blur);
			if (_blurShader == nullptr) { LOGW("PrecompiledShader::Blur failed"); }
			_downsampleShader = resolver.GetShader(PrecompiledShader::Downsample);
			if (_downsampleShader == nullptr) { LOGW("PrecompiledShader::Downsample failed"); }
			_combineShader = resolver.GetShader(PrecompiledShader::Combine);
			if (_combineShader == nullptr) { LOGW("PrecompiledShader::Combine failed"); }
		}
#endif
#if !defined(RHI_CAP_SHADERS) || !defined(RHI_CAP_FRAMEBUFFERS)
		// Software renderer: the bloom + shader-lighting chain is capability-gated out, but the viewport compositor
		// still needs the Combine program so the software device recognizes the draw (object label "Combine" ->
		// SwEffect::Combine) and runs the CPU dynamic-lighting combine in its place instead of a fragment.
		if (_combineShader == nullptr) {
			_combineShader = resolver.GetShader(PrecompiledShader::Combine);
			if (_combineShader == nullptr) { LOGW("PrecompiledShader::Combine failed"); }
		}
#endif

		// Attach the HUD and console to the active overlay/scene node (re-evaluated every time, as the overlay can be
		// toggled on or off when the player count changes)
		SceneNode* hudParent = GetHudParentNode();
		if (_hud != nullptr) {
			_hud->setParent(hudParent);
		}
		if (_console != nullptr) {
			_console->setParent(hudParent);
		}

		// Acquired on every tier: the console fixed-function backends need this program bound for the water
		// description in its fixed_function block to run at all, exactly as the shader path needs it for the
		// fragment stage. Both quality variants carry the same console block, so the preference still picks
		// the program - there is simply nothing left for it to reduce there.
		_combineWithWaterShader = resolver.GetShader(PreferencesCache::LowWaterQuality
			? PrecompiledShader::CombineWithWaterLow
			: PrecompiledShader::CombineWithWater);
		if (_combineWithWaterShader == nullptr) {
			if (PreferencesCache::LowWaterQuality) {
				LOGW("PrecompiledShader::CombineWithWaterLow failed");
			} else {
				LOGW("PrecompiledShader::CombineWithWater failed");
			}
		}

		for (std::size_t i = 0; i < _assignedViewports.size(); i++) {
			Rendering::PlayerViewport& viewport = *_assignedViewports[i];
			Recti bounds = GetPlayerViewportBounds(w, h, (std::int32_t)i);
			if (viewport.Initialize(_rootNode.get(), _upscalePass.GetNode(), bounds, useHalfRes)) {
				InitializeCamera(viewport);
			}
		}

		// Viewports must be registered in reverse order (registered later = drawn earlier). The scene pass composites
		// everything to the screen, so it is registered first; the overlay pass must render its UI texture before the
		// scene pass reads it (to composite it into the scene buffer), so it is registered after.
		_upscalePass.Register();
		if (_hudOverlayActive) {
			_hudUpscalePass.Register();
		}

		for (std::size_t i = 0; i < _assignedViewports.size(); i++) {
			Rendering::PlayerViewport& viewport = *_assignedViewports[i];
			viewport.Register();

			if DEATH_UNLIKELY(_pauseMenu != nullptr) {
				viewport.UpdateCamera(0.0f);	// Force update camera if game is paused
			}
		}

		if (_tileMap != nullptr) {
			_tileMap->OnInitializeViewport();
		}

		if DEATH_UNLIKELY(_pauseMenu != nullptr) {
			_pauseMenu->OnInitializeViewport(_viewSize.X, _viewSize.Y);
		}
	}

	bool LevelHandler::OnConsoleCommand(StringView line)
	{
		if (line == "/help"_s) {
			_console->WriteLine(UI::MessageLevel::Echo, line);
			_console->WriteLine(UI::MessageLevel::Confirm, _("For more information, visit the official website:") + " \f[w:80]\f[c:#707070]https://de4th.dev/jazz2/help\f[/c]\f[/w]"_s);
			return true;
		}

		return TryInvokeCheat(line);
	}

	void LevelHandler::OnKeyPressed(const KeyboardEvent& event)
	{
		_pressedKeys.set((std::size_t)event.sym);

		if DEATH_UNLIKELY(_pauseMenu != nullptr) {
			_pauseMenu->OnKeyPressed(event);
		} else if DEATH_UNLIKELY(_console->IsVisible()) {
			_console->OnKeyPressed(event);
		}
	}

	void LevelHandler::OnKeyReleased(const KeyboardEvent& event)
	{
		_pressedKeys.reset((std::size_t)event.sym);

		if DEATH_UNLIKELY(_pauseMenu != nullptr) {
			_pauseMenu->OnKeyReleased(event);
		}
	}

	void LevelHandler::OnTextInput(const TextInputEvent& event)
	{
		if (_console->IsVisible()) {
			_console->OnTextInput(event);
		}
	}

#if defined(NCINE_HAS_TOUCH_CONTROLS)
	void LevelHandler::OnTouchEvent(const  TouchEvent& event)
	{
		if DEATH_UNLIKELY(_pauseMenu != nullptr) {
			_pauseMenu->OnTouchEvent(event);
		} else {
			if DEATH_UNLIKELY(_console->IsVisible()) {
				_console->OnTouchEvent(event, _viewSize);
			}
			_hud->OnTouchEvent(event, _overrideActions, _overrideMovement);
		}
	}
#endif

	void LevelHandler::AddActor(std::shared_ptr<Actors::ActorBase> actor)
	{
		actor->SetParent(_rootNode.get());

		if (!actor->GetState(Actors::ActorState::ForceDisableCollisions)) {
			actor->UpdateAABB();
			actor->_collisionProxyID = _collisions.CreateProxy(actor->AABB, actor.get());
		}

		_actors.push_back(std::move(actor));
	}

	std::shared_ptr<AudioBufferPlayer> LevelHandler::PlaySfx(Actors::ActorBase* self, StringView identifier, AudioBuffer* buffer, const Vector3f& pos, bool sourceRelative, float gain, float pitch)
	{
#if defined(WITH_AUDIO)
		if (buffer != nullptr) {
			auto& player = _playingSounds.emplace_back(_assignedViewports.size() > 1
				? std::make_shared<AudioBufferPlayerForSplitscreen>(buffer, _assignedViewports)
				: std::make_shared<AudioBufferPlayer>(buffer));
			player->setPosition(Vector3f(pos.X, pos.Y, 100.0f));
			player->setGain(gain * PreferencesCache::MasterVolume * PreferencesCache::SfxVolume);
			player->setSourceRelative(sourceRelative);

			if (pos.Y >= _waterLevel) {
				player->setLowPass(0.05f);
				player->setPitch(pitch * 0.7f);
			} else {
				player->setPitch(pitch);
			}

			player->play();
			return player;
		}
#endif
		return nullptr;
	}

	std::shared_ptr<AudioBufferPlayer> LevelHandler::PlayCommonSfx(StringView identifier, const Vector3f& pos, float gain, float pitch)
	{
#if defined(WITH_AUDIO)
		auto it = _commonResources->Sounds.find(String::nullTerminatedView(identifier));
		if (it != _commonResources->Sounds.end() && ContentResolver::Get().ResolveSound(it->second)) {
			std::int32_t idx = (it->second.Buffers.size() > 1 ? Random().Next(0, (std::int32_t)it->second.Buffers.size()) : 0);
			auto* buffer = &it->second.Buffers[idx]->Buffer;
			auto& player = _playingSounds.emplace_back(_assignedViewports.size() > 1
				? std::make_shared<AudioBufferPlayerForSplitscreen>(buffer, _assignedViewports)
				: std::make_shared<AudioBufferPlayer>(buffer));
			player->setPosition(Vector3f(pos.X, pos.Y, 100.0f));
			player->setGain(gain * PreferencesCache::MasterVolume * PreferencesCache::SfxVolume);

			if (pos.Y >= _waterLevel) {
				player->setLowPass(0.05f);
				player->setPitch(pitch * 0.7f);
			} else {
				player->setPitch(pitch);
			}

			player->play();
			return player;
		}	
#endif
		return nullptr;
	}

	void LevelHandler::WarpCameraToTarget(Actors::ActorBase* actor, bool fast)
	{
		for (auto& viewport : _assignedViewports) {
			if (viewport->_targetActor == actor) {
				viewport->WarpCameraToTarget(fast);
			}
		}
	}

	bool LevelHandler::IsPositionEmpty(Actors::ActorBase* self, const AABBf& aabb, TileCollisionParams& params, Actors::ActorBase** collider)
	{
		*collider = nullptr;

		if (self->GetState(Actors::ActorState::CollideWithTileset)) {
			if (_tileMap != nullptr) {
				if (self->GetState(Actors::ActorState::CollideWithTilesetReduced) && aabb.B - aabb.T >= 20.0f) {
					// If hitbox height is larger than 20px, check bottom and top separately (and top only if going upwards)
					AABBf aabbTop = aabb;
					aabbTop.B = aabbTop.T + 6.0f;
					AABBf aabbBottom = aabb;
					aabbBottom.T = aabbBottom.B - std::max(14.0f, (aabb.B - aabb.T) - 10.0f);
					if (!_tileMap->IsTileEmpty(aabbBottom, params)) {
						return false;
					}
					if (!params.Downwards) {
						params.Downwards = false;
						if (!_tileMap->IsTileEmpty(aabbTop, params)) {
							return false;
						}
					}
				} else {
					if (!_tileMap->IsTileEmpty(aabb, params)) {
						return false;
					}
				}
			}
		}

		// Check for solid objects
		if (self->GetState(Actors::ActorState::CollideWithSolidObjects)) {
			Actors::ActorBase* colliderActor = nullptr;
			FindCollisionActorsByAABB(self, aabb, [self, &colliderActor, &params](Actors::ActorBase* actor) -> bool {
				if ((actor->GetState() & (Actors::ActorState::IsSolidObject | Actors::ActorState::IsDestroyed)) != Actors::ActorState::IsSolidObject) {
					return true;
				}
				if (self->GetState(Actors::ActorState::ExcludeSimilar) && actor->GetState(Actors::ActorState::ExcludeSimilar)) {
					// If both objects have ExcludeSimilar, ignore it
					return true;
				}
				if (self->GetState(Actors::ActorState::CollideWithSolidObjectsBelow) &&
					self->AABBInner.B > (actor->AABBInner.T + actor->AABBInner.B) * 0.5f) {
					return true;
				}

				auto* solidObject = runtime_cast<Actors::SolidObjectBase>(actor);
				if (solidObject == nullptr || !solidObject->IsOneWay || params.Downwards) {
					if (!self->OnHandleCollision(actor) && !actor->OnHandleCollision(self)) {
						colliderActor = actor;
						return false;
					}
				}

				return true;
			});

			*collider = colliderActor;
		}

		return (*collider == nullptr);
	}

	void LevelHandler::FindCollisionActorsByAABB(const Actors::ActorBase* self, const AABBf& aabb, Function<bool(Actors::ActorBase*)>&& callback)
	{
		struct QueryHelper {
			const LevelHandler* Handler;
			const Actors::ActorBase* Self;
			const AABBf& AABB;
			Function<bool(Actors::ActorBase*)>& Callback;

			bool OnCollisionQuery(std::int32_t nodeId) {
				Actors::ActorBase* actor = (Actors::ActorBase*)Handler->_collisions.GetUserData(nodeId);
				if (Self == actor || (actor->GetState() & (Actors::ActorState::CollideWithOtherActors | Actors::ActorState::IsDestroyed)) != Actors::ActorState::CollideWithOtherActors) {
					return true;
				}
				if (actor->IsCollidingWith(AABB)) {
					return Callback(actor);
				}
				return true;
			}
		};

		QueryHelper helper = { this, self, aabb, callback };
		_collisions.Query(&helper, aabb);
	}

	void LevelHandler::FindCollisionActorsByRadius(float x, float y, float radius, Function<bool(Actors::ActorBase*)>&& callback)
	{
		AABBf aabb = AABBf(x - radius, y - radius, x + radius, y + radius);
		float radiusSquared = (radius * radius);

		struct QueryHelper {
			const LevelHandler* Handler;
			const float x, y;
			const float RadiusSquared;
			Function<bool(Actors::ActorBase*)>& Callback;

			bool OnCollisionQuery(std::int32_t nodeId) {
				Actors::ActorBase* actor = (Actors::ActorBase*)Handler->_collisions.GetUserData(nodeId);
				if ((actor->GetState() & (Actors::ActorState::CollideWithOtherActors | Actors::ActorState::IsDestroyed)) != Actors::ActorState::CollideWithOtherActors) {
					return true;
				}

				// Find the closest point to the circle within the rectangle
				float closestX = std::clamp(x, actor->AABB.L, actor->AABB.R);
				float closestY = std::clamp(y, actor->AABB.T, actor->AABB.B);

				// Calculate the distance between the circle's center and this closest point
				float distanceX = (x - closestX);
				float distanceY = (y - closestY);

				// If the distance is less than the circle's radius, an intersection occurs
				float distanceSquared = (distanceX * distanceX) + (distanceY * distanceY);
				if (distanceSquared < RadiusSquared) {
					return Callback(actor);
				}

				return true;
			}
		};

		QueryHelper helper = { this, x, y, radiusSquared, callback };
		_collisions.Query(&helper, aabb);
	}

	void LevelHandler::GetCollidingPlayers(const AABBf& aabb, Function<bool(Actors::ActorBase*)>&& callback)
	{
		for (auto& player : _players) {
			if (aabb.Overlaps(player->AABB)) {
				if (!callback(player)) {
					break;
				}
			}
		}
	}

	void LevelHandler::BroadcastTriggeredEvent(Actors::ActorBase* initiator, EventType eventType, std::uint8_t* eventParams)
	{
		switch (eventType) {
			case EventType::AreaActivateBoss: {
				if (_activeBoss == nullptr && _nextLevelType == ExitType::None) {
					for (auto& actor : _actors) {
						_activeBoss = runtime_cast<Actors::Bosses::BossBase>(actor);
						if (_activeBoss != nullptr) {
							break;
						}
					}

					if (_activeBoss == nullptr) {
						// No boss was found, it's probably a bug in the level, so go to the next level
						LOGW("No boss was found, skipping to the next level");
						BeginLevelChange(nullptr, ExitType::Boss);
						return;
					}

					if (_activeBoss->OnActivatedBoss()) {
						HandleBossActivated(_activeBoss.get(), initiator);

						if (eventParams != nullptr) {
							size_t musicPathLength = strnlen((const char*)eventParams, 16);
							StringView musicPath((const char*)eventParams, musicPathLength);
							BeginPlayMusic(musicPath);
						}
					}
				}
				break;
			}
			case EventType::AreaCallback: {
#if defined(WITH_ANGELSCRIPT)
				if (_scripts != nullptr) {
					_scripts->OnLevelCallback(initiator, eventParams);
				}
#endif
				break;
			}
			case EventType::ModifierSetWater: {
				// TODO: Implement Instant (non-instant transition), Lighting
				_waterLevel = Actors::EventParamsReader(eventParams).GetUint16(0);
				break;
			}
		}

		for (auto& actor : _actors) {
			actor->OnTriggeredEvent(eventType, eventParams);
		}
	}

	void LevelHandler::BeginLevelChange(Actors::ActorBase* initiator, ExitType exitType, StringView nextLevel)
	{
		if (_nextLevelType != ExitType::None) {
			return;
		}

		// A special level returns back to the level it was entered from once it's completed, so the current state
		// has to be remembered now, before the players start leaving
		if (CanUseLevelStateSnapshots()) {
			String targetLevel = ResolveNextLevelName(exitType, nextLevel);
			if (!targetLevel.empty() && !StringUtils::equalsIgnoreCase(targetLevel, _levelName) && IsReturnLevel(targetLevel)) {
				MemoryStream dest(64 * 1024);
				SerializeLevelState(dest, targetLevel);
				auto state = std::make_shared<LevelStateSnapshot>();
				AssignSnapshotData(*state, dest);
				_leftLevelState = std::move(state);
				LOGI("Level \"{}\" is a special level, the current state was saved ({} bytes)", targetLevel, _leftLevelState->size());
			}
		}

		_nextLevelName = nextLevel;
		_nextLevelType = exitType;
		
		if ((exitType & ExitType::FastTransition) == ExitType::FastTransition) {
			ExitType exitTypeMasked = (exitType & ExitType::TypeMask);
			if (exitTypeMasked == ExitType::Warp || exitTypeMasked == ExitType::Bonus || exitTypeMasked == ExitType::Boss) {
				_nextLevelTime = 70.0f;
			} else {
				_nextLevelTime = 0.0f;
			}
		} else {
			_nextLevelTime = 360.0f;

			if (_hud != nullptr) {
				_hud->BeginFadeOut(_nextLevelTime - 40.0f);
			}

#if defined(WITH_AUDIO)
			if (_sugarRushMusic != nullptr) {
				_sugarRushMusic->stop();
				_sugarRushMusic = nullptr;
			}
			if (_music != nullptr) {
				_music->stop();
				_music = nullptr;
			}
#endif
		}

		for (auto player : _players) {
			player->OnLevelChanging(initiator, exitType);
		}
	}

	bool LevelHandler::CanTakeLevelExit(ExitType exitType, StringView nextLevel) const
	{
		if (_completedSpecialLevels.empty()) {
			return true;
		}

		// The player already returned from the special level, so it cannot be entered again
		String targetLevel = ResolveNextLevelName(exitType, nextLevel);
		for (const String& level : _completedSpecialLevels) {
			if (StringUtils::equalsIgnoreCase(level, targetLevel)) {
				return false;
			}
		}
		return true;
	}

	void LevelHandler::SendPacket(const Actors::ActorBase* self, ArrayView<const std::uint8_t> data)
	{
		// Packet cannot be sent anywhere in local sessions
	}

	void LevelHandler::HandleBossActivated(Actors::Bosses::BossBase* boss, Actors::ActorBase* initiator)
	{
		// Used only in derived classes
	}

	void LevelHandler::HandleLevelChange(LevelInitialization&& levelInit)
	{
		_root->ChangeLevel(std::move(levelInit));
	}

	void LevelHandler::HandleGameOver(Actors::Player* player)
	{
		LevelInitialization levelInit;
		PrepareNextLevelInitialization(levelInit);
		levelInit.LevelName = ":gameover"_s;
		HandleLevelChange(std::move(levelInit));
	}

	bool LevelHandler::HandlePlayerDied(Actors::Player* player)
	{
#if defined(WITH_ANGELSCRIPT)
		if (_scripts != nullptr) {
			// TODO: killer
			_scripts->OnPlayerDied(player, nullptr);
		}
#endif

		if (_activeBoss != nullptr) {
			if (_activeBoss->OnPlayerDied()) {
				_activeBoss = nullptr;
			}

			// Warp all other players to checkpoint without transition to avoid issues
			for (auto* otherPlayer : _players) {
				if (otherPlayer != player) {
					otherPlayer->WarpToCheckpoint();
				}
			}
		}

		RollbackToCheckpoint(player);

		// Single player can respawn immediately
		return true;
	}

	void LevelHandler::HandlePlayerWarped(Actors::Player* player, Vector2f prevPos, WarpFlags flags)
	{
		if ((flags & WarpFlags::Fast) == WarpFlags::Fast) {
			WarpCameraToTarget(player, true);
		} else {
			Vector2f pos = player->GetPos();
			if ((prevPos - pos).Length() > 250.0f) {
				WarpCameraToTarget(player);
			}
		}
	}

	void LevelHandler::HandlePlayerCoins(Actors::Player* player, std::int32_t prevCount, std::int32_t newCount)
	{
		// Coins are shared in cooperation, add it also to all other local players
		if (prevCount < newCount) {
			std::int32_t increment = (newCount - prevCount);
			for (auto current : _players) {
				if (current != player) {
					current->AddCoinsInternal(increment);
				}
			}
		}

		_hud->ShowCoins(newCount);
	}

	void LevelHandler::HandlePlayerGems(Actors::Player* player, std::uint8_t gemType, std::int32_t prevCount, std::int32_t newCount)
	{
		_hud->ShowGems(gemType, newCount);
	}

	void LevelHandler::SetCheckpoint(Actors::Player* player, Vector2f pos)
	{
		_checkpointFrames = GetElapsedFrames();

		// All players will be respawned at the checkpoint, so also set the same ambient light. It's taken from the
		// activating player rather than from its viewport, so it's correct on a dedicated server too.
		float ambientLight = (player != nullptr ? player->GetCurrentAmbientLight() : _defaultAmbientLight.W);

		for (auto& player : _players) {
			player->SetCheckpoint(pos, ambientLight);
		}

		if (IsLocalSession()) {
			_eventMap->CreateCheckpointForRollback();
			_tileMap->CreateCheckpointForRollback();
			// The checkpoint is activated in the middle of the frame, actors are stored once they're all updated
			_checkpointSnapshotPending = true;
		}
	}

	void LevelHandler::RollbackToCheckpoint(Actors::Player* player)
	{
		// Reset the camera
		LimitCameraView(player, player->_pos, 0, 0);

		WarpCameraToTarget(player);

		if (IsLocalSession() && CanUseLevelStateSnapshots() && (_checkpointSnapshotPending || !_checkpointSnapshot.empty())) {
			if (_checkpointSnapshotPending) {
				CreateCheckpointSnapshot();
			}

			// All actors are destroyed and resurrected exactly as they were at the checkpoint
			for (auto& actor : _actors) {
				if (runtime_cast<Actors::Player>(actor) == nullptr && !actor->GetState(Actors::ActorState::PreserveOnRollback)) {
					actor->_state |= Actors::ActorState::IsDestroyed;
				}
			}

			if (_activeBoss != nullptr && _activeBoss->GetState(Actors::ActorState::IsDestroyed)) {
				_activeBoss->OnDeactivatedBoss();
				_activeBoss = nullptr;
			}

			_eventMap->RollbackToCheckpoint(false);
			_tileMap->RollbackToCheckpoint();
			// Some objects derive their phase from the elapsed time when they're spawned
			_elapsedFrames = _checkpointFrames;
			RestoreCheckpointSnapshot();
		} else if (IsLocalSession()) {
			for (auto& actor : _actors) {
				// Despawn all actors that were created after the last checkpoint
				if (actor->_spawnFrames > _checkpointFrames && !actor->GetState(Actors::ActorState::PreserveOnRollback)) {
					if ((actor->_state & (Actors::ActorState::IsCreatedFromEventMap | Actors::ActorState::IsFromGenerator)) != Actors::ActorState::None) {
						Vector2i originTile = actor->_originTile;
						if ((actor->_state & Actors::ActorState::IsFromGenerator) == Actors::ActorState::IsFromGenerator) {
							_eventMap->ResetGenerator(originTile.X, originTile.Y);
						}

						_eventMap->Deactivate(originTile.X, originTile.Y);
					}

					actor->_state |= Actors::ActorState::IsDestroyed;
				}
			}

			_eventMap->RollbackToCheckpoint();
			// Don't rollback the tilemap in local sessions for now
			//_tileMap->RollbackToCheckpoint();
			_elapsedFrames = _checkpointFrames;
		}

		BeginPlayMusic(_musicDefaultPath);

#if defined(WITH_ANGELSCRIPT)
		if (_scripts != nullptr) {
			_scripts->OnLevelReload();
		}
#endif
	}

	void LevelHandler::HandleActivateSugarRush(Actors::Player* player)
	{
#if defined(WITH_AUDIO)
		if (_sugarRushMusic != nullptr) {
			return;
		}

		auto it = _commonResources->Sounds.find(String::nullTerminatedView("SugarRush"_s));
		if (it != _commonResources->Sounds.end() && ContentResolver::Get().ResolveSound(it->second)) {
			std::int32_t idx = (it->second.Buffers.size() > 1 ? Random().Next(0, (std::int32_t)it->second.Buffers.size()) : 0);
			_sugarRushMusic = _playingSounds.emplace_back(std::make_shared<AudioBufferPlayer>(&it->second.Buffers[idx]->Buffer));
			_sugarRushMusic->setPosition(Vector3f(0.0f, 0.0f, 100.0f));
			_sugarRushMusic->setGain(PreferencesCache::MasterVolume * PreferencesCache::MusicVolume);
			_sugarRushMusic->setSourceRelative(true);
			_sugarRushMusic->play();

			if (_music != nullptr) {
				_music->pause();
			}
		}
#endif
	}

	void LevelHandler::HandleCreateParticleDebrisOnPerish(const Actors::ActorBase* self, Actors::ParticleDebrisEffect effect, Vector2f speed)
	{
		// Used only in derived classes
	}

	void LevelHandler::HandleCreateSpriteDebris(const Actors::ActorBase* self, AnimState state, std::int32_t count)
	{
		// Used only in derived classes
	}

	void LevelHandler::ShowLevelText(StringView text, Actors::ActorBase* initiator)
	{
		_hud->ShowLevelText(text);
	}

	StringView LevelHandler::GetLevelText(std::uint32_t textId, std::int32_t index, std::uint32_t delimiter)
	{
		if (textId >= _levelTexts.size()) {
			return {};
		}

		StringView text = _levelTexts[textId];
		std::int32_t textSize = (std::int32_t)text.size();

		if (textSize > 0 && index >= 0) {
			std::int32_t delimiterCount = 0;
			std::int32_t start = 0;
			std::int32_t idx = 0;
			do {
				Pair<char32_t, std::size_t> cursor = Death::Utf8::NextChar(text, idx);

				if (cursor.first() == delimiter) {
					if (delimiterCount == index - 1) {
						start = idx + 1;
					} else if (delimiterCount == index) {
						return StringView(text.data() + start, idx - start);
					}
					delimiterCount++;
				}

				idx = (std::int32_t)cursor.second();
			} while (idx < textSize);

			if (delimiterCount == index) {
				return StringView(text.data() + start, text.size() - start);
			} else {
				return {};
			}
		}

		return _x(_levelName, text.data());
	}

	void LevelHandler::OverrideLevelText(std::uint32_t textId, StringView value)
	{
		if (textId >= _levelTexts.size()) {
			if (value.empty()) {
				return;
			}

			_levelTexts.resize(textId + 1);
		}

		_levelTexts[textId] = value;
	}

	bool LevelHandler::PlayerActionPressed(Actors::Player* player, PlayerAction action, bool includeGamepads)
	{
		bool isGamepad;
		return PlayerActionPressed(player, action, includeGamepads, isGamepad);
	}

	bool LevelHandler::PlayerActionPressed(Actors::Player* player, PlayerAction action, bool includeGamepads, bool& isGamepad)
	{
		if (_console->IsVisible() && action != PlayerAction::Menu && action != PlayerAction::Console) {
			return false;
		}

		isGamepad = false;
		std::int32_t playerIndex = (player ? player->GetPlayerIndex() : 0);
		auto& input = _playerInputs[playerIndex];
		if ((input.PressedActions & (1ull << (std::int32_t)action)) != 0) {
			isGamepad = (input.PressedActions & (1ull << (32 + (std::int32_t)action))) != 0;
			return true;
		}

		return false;
	}

	bool LevelHandler::PlayerActionHit(Actors::Player* player, PlayerAction action, bool includeGamepads)
	{
		bool isGamepad;
		return PlayerActionHit(player, action, includeGamepads, isGamepad);
	}

	bool LevelHandler::PlayerActionHit(Actors::Player* player, PlayerAction action, bool includeGamepads, bool& isGamepad)
	{
		if (_console->IsVisible() && action != PlayerAction::Menu && action != PlayerAction::Console) {
			return false;
		}

		isGamepad = false;
		std::int32_t playerIndex = (player ? player->GetPlayerIndex() : 0);
		auto& input = _playerInputs[playerIndex];
		if ((input.PressedActions & (1ull << (std::int32_t)action)) != 0 && (input.PressedActionsLast & (1ull << (std::int32_t)action)) == 0) {
			isGamepad = (input.PressedActions & (1ull << (32 + (std::int32_t)action))) != 0;
			return true;
		}

		return false;
	}

	float LevelHandler::PlayerHorizontalMovement(Actors::Player* player)
	{
		if (_console->IsVisible()) {
			return 0.0f;
		}

		auto& input = _playerInputs[player->GetPlayerIndex()];
		return (input.Frozen ? input.FrozenMovement.X : input.RequiredMovement.X);
	}

	float LevelHandler::PlayerVerticalMovement(Actors::Player* player)
	{
		if (_console->IsVisible()) {
			return 0.0f;
		}

		auto& input = _playerInputs[player->GetPlayerIndex()];
		return (input.Frozen ? input.FrozenMovement.Y : input.RequiredMovement.Y);
	}

	void LevelHandler::PlayerExecuteRumble(Actors::Player* player, StringView rumbleEffect)
	{
#if defined(NCINE_HAS_GAMEPAD_RUMBLE)
		auto it = _rumbleEffects.find(String::nullTerminatedView(rumbleEffect));
		if (it == _rumbleEffects.end()) {
			return;
		}

		std::int32_t joyIdx = ControlScheme::GetGamepadForPlayer(player->GetPlayerIndex());
		if (joyIdx >= 0) {
			_rumble.ExecuteEffect(joyIdx, it->second);
		}
#endif
	}

	bool LevelHandler::SerializeResumableToStream(Stream& dest)
	{
		std::uint8_t flags = 0;
		if (_isReforged) flags |= 0x01;
		if (_cheatsUsed) flags |= 0x02;
		dest.WriteValue<std::uint8_t>(flags);

		auto p = _levelName.partition('/');

		dest.WriteValue<std::uint8_t>((std::uint8_t)p[0].size());
		dest.Write(p[0].data(), (std::uint32_t)p[0].size());
		dest.WriteValue<std::uint8_t>((std::uint8_t)p[2].size());
		dest.Write(p[2].data(), (std::uint32_t)p[2].size());

		dest.WriteValue<std::uint8_t>((std::uint8_t)_difficulty);
		dest.WriteVariableUint64(_elapsedMillisecondsBegin);
		dest.WriteValueAsLE<float>(_checkpointFrames);
		dest.WriteValueAsLE<float>(_waterLevel);
		dest.WriteValue<std::uint8_t>((std::uint8_t)_weatherType);
		dest.WriteValue<std::uint8_t>(_weatherIntensity);

		_tileMap->SerializeResumableToStream(dest, true);
		_eventMap->SerializeResumableToStream(dest, true);

		std::size_t playerCount = _players.size();
		dest.WriteValue<std::uint8_t>((std::uint8_t)playerCount);
		for (std::size_t i = 0; i < playerCount; i++) {
			_players[i]->SerializeResumableToStream(dest);
		}

		// Actors as they were at the checkpoint, with the active state of events that goes with them (since v5)
		if (_checkpointSnapshotPending) {
			CreateCheckpointSnapshot();
		}
		WriteSnapshotData(dest, &_checkpointSnapshot);
		if (!_checkpointSnapshot.empty()) {
			_eventMap->SerializeActiveStateToStream(dest, true);
		}

		// Also the level to return to, if it's a special level
		WriteSnapshotData(dest, _returnLevelState.get());

		// Special levels that cannot be entered again, they stay completed even if the level is resumed at an earlier
		// checkpoint (since v6)
		dest.WriteVariableUint32((std::uint32_t)_completedSpecialLevels.size());
		for (const String& level : _completedSpecialLevels) {
			WriteSnapshotString(dest, level);
		}

		return true;
	}

	bool LevelHandler::CanUseLevelStateSnapshots() const
	{
		return IsLocalSession();
	}

	String LevelHandler::ResolveNextLevelName(ExitType exitType, StringView nextLevel) const
	{
		StringView realNextLevel;
		if (!nextLevel.empty()) {
			realNextLevel = nextLevel;
		} else {
			realNextLevel = ((exitType & ExitType::TypeMask) == ExitType::Bonus ? _defaultSecretLevel : _defaultNextLevel);
		}

		if (realNextLevel.empty()) {
			return {};
		}
		if (realNextLevel.contains('/')) {
			return realNextLevel;
		}
		return _levelName.partition('/')[0] + '/' + realNextLevel;
	}

	bool LevelHandler::IsReturnLevel(StringView levelName)
	{
		String nextLevel;
		if (!ContentResolver::Get().TryGetNextLevelName(levelName, nextLevel) || nextLevel.empty()) {
			return false;
		}

		// Next level without an episode refers to the same episode
		if (!nextLevel.contains('/')) {
			nextLevel = levelName.partition('/')[0] + '/' + nextLevel;
		}
		return StringUtils::equalsIgnoreCase(nextLevel, levelName);
	}

	void LevelHandler::CreateCheckpointSnapshot()
	{
		_checkpointSnapshotPending = false;

		if (!CanUseLevelStateSnapshots()) {
			return;
		}

		MemoryStream dest(16 * 1024);
		dest.WriteValueAsLE<float>(_waterLevel);
		dest.WriteValue<std::uint8_t>((std::uint8_t)_weatherType);
		dest.WriteValue<std::uint8_t>(_weatherIntensity);
		SerializeActorsToStream(dest);
		AssignSnapshotData(_checkpointSnapshot, dest);
	}

	void LevelHandler::RestoreCheckpointSnapshot()
	{
		MemoryStream src(_checkpointSnapshot.data(), (std::int64_t)_checkpointSnapshot.size());
		_waterLevel = src.ReadValueAsLE<float>();
		_weatherType = (WeatherType)src.ReadValue<std::uint8_t>();
		_weatherIntensity = src.ReadValue<std::uint8_t>();
		InitializeActorsFromStream(src);
	}

	void LevelHandler::SerializeActorsToStream(Stream& dest)
	{
		constexpr Actors::ActorState InstantiationFlags = Actors::ActorState::IsCreatedFromEventMap |
			Actors::ActorState::IsFromGenerator | Actors::ActorState::Illuminated;

		SmallVector<Actors::ActorBase*, 0> serializable;
		SmallVector<Actors::ActorBase*, 0> respawnable;
		for (auto& actor : _actors) {
			// Players are handled separately, dying objects (with a death transition running) are already dead
			if (actor->GetState(Actors::ActorState::IsDestroyed) || actor->GetState(Actors::ActorState::PreserveOnRollback) ||
				actor->GetHealth() <= 0 || runtime_cast<Actors::Player>(actor) != nullptr) {
				continue;
			}

			if (actor->GetState(Actors::ActorState::Initialized) && actor->IsSerializable() && _eventSpawner.CanSpawn(actor->_spawnEventType)) {
				serializable.push_back(actor.get());
			} else if ((actor->_state & (Actors::ActorState::IsCreatedFromEventMap | Actors::ActorState::IsFromGenerator)) != Actors::ActorState::None) {
				// Objects that cannot be stored are spawned again from their events, the others are transient
				respawnable.push_back(actor.get());
			}
		}

		dest.WriteVariableUint32((std::uint32_t)serializable.size());
		for (Actors::ActorBase* actor : serializable) {
			dest.WriteValueAsLE<std::uint16_t>((std::uint16_t)actor->_spawnEventType);
			dest.Write(actor->_spawnEventParams, sizeof(actor->_spawnEventParams));
			dest.WriteVariableInt32(actor->_spawnPos.X);
			dest.WriteVariableInt32(actor->_spawnPos.Y);
			dest.WriteVariableInt32(actor->_spawnPos.Z);
			dest.WriteVariableUint32((std::uint32_t)(actor->_state & InstantiationFlags));
			dest.WriteValueAsLE<float>(actor->_spawnFrames);

			// Each object is stored with its size, so a single object that doesn't read back exactly what it wrote
			// cannot break all the objects that follow
			MemoryStream actorState(256);
			actor->OnSerializeState(actorState);
			dest.WriteVariableUint32((std::uint32_t)actorState.GetSize());
			dest.Write(actorState.GetBuffer(), actorState.GetSize());
		}

		dest.WriteVariableUint32((std::uint32_t)respawnable.size());
		for (Actors::ActorBase* actor : respawnable) {
			dest.WriteVariableInt32(actor->_originTile.X);
			dest.WriteVariableInt32(actor->_originTile.Y);
			dest.WriteValue<std::uint8_t>(actor->GetState(Actors::ActorState::IsFromGenerator) ? 1 : 0);
		}
	}

	void LevelHandler::InitializeActorsFromStream(Stream& src)
	{
		SmallVector<std::uint8_t, 0> actorState;

		std::uint32_t actorCount = src.ReadVariableUint32();
		for (std::uint32_t i = 0; i < actorCount; i++) {
			EventType eventType = (EventType)src.ReadValueAsLE<std::uint16_t>();
			std::uint8_t eventParams[Events::EventSpawner::SpawnParamsSize];
			src.Read(eventParams, sizeof(eventParams));
			Vector3i spawnPos;
			spawnPos.X = src.ReadVariableInt32();
			spawnPos.Y = src.ReadVariableInt32();
			spawnPos.Z = src.ReadVariableInt32();
			Actors::ActorState flags = (Actors::ActorState)src.ReadVariableUint32();
			float spawnFrames = src.ReadValueAsLE<float>();
			std::uint32_t actorStateSize = src.ReadVariableUint32();
			actorState.resize_for_overwrite(actorStateSize);
			src.Read(actorState.data(), actorStateSize);

			// The object is spawned again exactly as the first time, and only then its live state is applied
			std::shared_ptr<Actors::ActorBase> actor = _eventSpawner.SpawnEvent(eventType, eventParams, flags, spawnPos);
			if (actor == nullptr) {
				if ((flags & (Actors::ActorState::IsCreatedFromEventMap | Actors::ActorState::IsFromGenerator)) != Actors::ActorState::None) {
					Vector2i originTile = Vector2i(spawnPos.X / Tiles::TileSet::DefaultTileSize, spawnPos.Y / Tiles::TileSet::DefaultTileSize);
					if ((flags & Actors::ActorState::IsFromGenerator) == Actors::ActorState::IsFromGenerator) {
						_eventMap->ResetGenerator(originTile.X, originTile.Y);
					}
					_eventMap->Deactivate(originTile.X, originTile.Y);
				}
				continue;
			}

			actor->_spawnFrames = spawnFrames;

			MemoryStream actorStateStream(actorState.data(), (std::int64_t)actorStateSize);
			actor->OnDeserializeState(actorStateStream);
			if (actorStateStream.GetPosition() != (std::int64_t)actorStateSize) {
				LOGW("Object of event type {} read {} bytes of its state instead of {}", (std::uint32_t)eventType,
					actorStateStream.GetPosition(), actorStateSize);
			}

			AddActor(actor);

			// The events are marked as active, so the objects are not spawned for the second time
			Vector2i originTile = actor->_originTile;
			if (actor->GetState(Actors::ActorState::IsFromGenerator)) {
				_eventMap->AttachGeneratorActor(originTile.X, originTile.Y, actor);
			} else if (actor->GetState(Actors::ActorState::IsCreatedFromEventMap)) {
				_eventMap->Activate(originTile.X, originTile.Y);
			}
		}

		std::uint32_t respawnCount = src.ReadVariableUint32();
		for (std::uint32_t i = 0; i < respawnCount; i++) {
			std::int32_t x = src.ReadVariableInt32();
			std::int32_t y = src.ReadVariableInt32();
			bool fromGenerator = (src.ReadValue<std::uint8_t>() != 0);
			if (fromGenerator) {
				_eventMap->ResetGenerator(x, y);
			}
			_eventMap->Deactivate(x, y);
		}
	}

	void LevelHandler::SerializeLevelState(Stream& dest, StringView specialLevel)
	{
		if (_checkpointSnapshotPending) {
			CreateCheckpointSnapshot();
		}

		dest.WriteValueAsLE<std::uint16_t>(LevelStateVersion);
		WriteSnapshotString(dest, _levelName);
		dest.WriteValue<std::uint8_t>((std::uint8_t)_difficulty);
		std::uint8_t flags = 0;
		if (_isReforged) flags |= 0x01;
		if (_cheatsUsed) flags |= 0x02;
		if (_checkpointCreated) flags |= 0x04;
		dest.WriteValue<std::uint8_t>(flags);
		dest.WriteValueAsLE<float>(_elapsedFrames);
		dest.WriteValueAsLE<float>(_checkpointFrames);
		dest.WriteValueAsLE<float>(_waterLevel);
		dest.WriteValue<std::uint8_t>((std::uint8_t)_weatherType);
		dest.WriteValue<std::uint8_t>(_weatherIntensity);
		WriteSnapshotString(dest, _musicDefaultPath);
		WriteSnapshotString(dest, _musicCurrentPath);
		dest.WriteVariableInt32(_levelBounds.X);
		dest.WriteVariableInt32(_levelBounds.W);

		_tileMap->SerializeSnapshotToStream(dest);
		_eventMap->SerializeSnapshotToStream(dest);

		dest.WriteValue<std::uint8_t>((std::uint8_t)_players.size());
		for (Actors::Player* player : _players) {
			dest.WriteValue<std::uint8_t>(player->GetPlayerIndex());
			dest.WriteValue<std::uint8_t>((std::uint8_t)player->GetPlayerType());
			MemoryStream playerState(512);
			static_cast<Actors::ActorBase*>(player)->OnSerializeState(playerState);
			dest.WriteVariableUint32((std::uint32_t)playerState.GetSize());
			dest.Write(playerState.GetBuffer(), playerState.GetSize());
		}

		SerializeActorsToStream(dest);

		WriteSnapshotData(dest, &_checkpointSnapshot);
		// A special level can lead to another special level, then both of them have to be returned from
		WriteSnapshotData(dest, _returnLevelState.get());

		// The special level the state is left to is stored as completed already, because the state is restored only
		// once it's completed (see PrepareNextLevelInitialization())
		dest.WriteVariableUint32((std::uint32_t)_completedSpecialLevels.size() + 1);
		for (const String& level : _completedSpecialLevels) {
			WriteSnapshotString(dest, level);
		}
		WriteSnapshotString(dest, specialLevel);
	}

	bool LevelHandler::InitializeFromLevelState(const LevelStateSnapshot& state, const LevelInitialization& levelInit)
	{
		ZoneScopedC(0x4876AF);

		MemoryStream src(state.data(), (std::int64_t)state.size());
		std::uint16_t version = src.ReadValueAsLE<std::uint16_t>();
		if (!IsLevelStateVersionSupported(version)) {
			LOGE("Level state has unsupported version {}", version);
			return false;
		}

		_levelName = ReadSnapshotString(src);
		_difficulty = (GameDifficulty)src.ReadValue<std::uint8_t>();
		std::uint8_t flags = src.ReadValue<std::uint8_t>();
		// Cheats could have been used also in the special level
		_isReforged = levelInit.IsReforged;
		_cheatsUsed = (levelInit.CheatsUsed || (flags & 0x02) != 0);
		_checkpointCreated = ((flags & 0x04) != 0);
		float elapsedFrames = src.ReadValueAsLE<float>();
		_checkpointFrames = src.ReadValueAsLE<float>();
		float waterLevel = src.ReadValueAsLE<float>();
		WeatherType weatherType = (WeatherType)src.ReadValue<std::uint8_t>();
		std::uint8_t weatherIntensity = src.ReadValue<std::uint8_t>();
		String musicDefaultPath = ReadSnapshotString(src);
		String musicCurrentPath = ReadSnapshotString(src);
		std::int32_t levelBoundsLeft = src.ReadVariableInt32();
		std::int32_t levelBoundsWidth = src.ReadVariableInt32();

		// The time spent in the special level counts too
		std::uint64_t elapsedMilliseconds = (std::uint64_t)(elapsedFrames * FrameTimer::SecondsPerFrame * 1000.0f);
		_elapsedMillisecondsBegin = (levelInit.ElapsedMilliseconds > elapsedMilliseconds ? levelInit.ElapsedMilliseconds - elapsedMilliseconds : 0);

		auto& resolver = ContentResolver::Get();
		// Scoped, because it returns early when the level cannot be loaded - see LoadingScope
		ContentResolver::LoadingScope loadingScope(resolver);

#if defined(RHI_CAP_POSTPROCESSING)
		_noiseTexture = resolver.GetNoiseTexture();
#endif

		_rootNode = std::make_unique<SceneNode>();
		_rootNode->setVisitOrderState(SceneNode::VisitOrderState::Disabled);

		_console = std::make_unique<UI::InGameConsole>(this);

		auto p = _levelName.partition('/');

		LevelDescriptor descriptor;
		if (!resolver.TryLoadLevel(_levelName, _difficulty, descriptor) &&
			(p[0] == "unknown"_s || !resolver.TryLoadLevel(String("unknown/"_s + p[2]), _difficulty, descriptor))) {
			LOGE("Cannot load level \"{}\"", _levelName);
			return false;
		}

		_console->WriteLine(UI::MessageLevel::Debug, _f("Level \"{}\" restored", descriptor.DisplayName));

		// The music that was playing when the level was left is started instead of the default one
		descriptor.MusicPath = musicCurrentPath;
		AttachComponents(std::move(descriptor));
		_musicDefaultPath = std::move(musicDefaultPath);
		_waterLevel = waterLevel;
		_weatherType = weatherType;
		_weatherIntensity = weatherIntensity;

		if (!_tileMap->InitializeSnapshotFromStream(src) || !_eventMap->InitializeSnapshotFromStream(src)) {
			LOGE("Cannot restore level \"{}\"", _levelName);
			return false;
		}

		SmallVector<std::uint8_t, 0> playerState;
		std::uint32_t playerCount = src.ReadValue<std::uint8_t>();
		_players.reserve(playerCount);
		for (std::uint32_t i = 0; i < playerCount; i++) {
			std::uint8_t playerIndex = src.ReadValue<std::uint8_t>();
			PlayerType playerType = (PlayerType)src.ReadValue<std::uint8_t>();
			std::uint32_t playerStateSize = src.ReadVariableUint32();
			playerState.resize_for_overwrite(playerStateSize);
			src.Read(playerState.data(), playerStateSize);

			std::shared_ptr<Actors::Player> player = CreateResumablePlayer((std::int32_t)i);
			Actors::Player* ptr = player.get();

			std::uint8_t playerParams[2] = { (std::uint8_t)playerType, playerIndex };
			player->OnActivated(Actors::ActorActivationDetails(this, Vector3i(0, 0, PlayerZ - playerIndex), playerParams));

			// The viewport has to be assigned before the state is applied, see Initialize(Stream&, std::uint16_t)
			AssignViewport(ptr);
			MemoryStream playerStateStream(playerState.data(), (std::int64_t)playerStateSize);
			static_cast<Actors::ActorBase*>(ptr)->OnDeserializeState(playerStateStream);

			_players.push_back(ptr);
			AddActor(player);

			// Apply the progress made in the special level, the player appears on the spot the level was left from
			if (i < (std::uint32_t)LevelInitialization::MaxPlayerCount && levelInit.PlayerCarryOvers[i].Type != PlayerType::None) {
				ExitType exitType = ExitType::Warp | (levelInit.LastExitType & ExitType::FastTransition);
				ptr->ReceiveReturnCarryOver(exitType, levelInit.PlayerCarryOvers[i]);
			}
		}

		_hud = CreateHUD();
		_hud->BeginFadeIn((levelInit.LastExitType & ExitType::FastTransition) == ExitType::FastTransition);

		// Set it after the players are restored, so ambient light transition is skipped, but before the objects,
		// because some of them derive their phase from the elapsed time when they're spawned
		_elapsedFrames = elapsedFrames;

		InitializeActorsFromStream(src);

		ReadSnapshotData(src, _checkpointSnapshot);

		LevelStateSnapshot returnLevelState;
		ReadSnapshotData(src, returnLevelState);
		if (!returnLevelState.empty()) {
			_returnLevelState = std::make_shared<LevelStateSnapshot>(std::move(returnLevelState));
		}

		if (version >= 2) {
			ReadSnapshotStrings(src, _completedSpecialLevels);
		}

		if (!_players.empty() && (levelBoundsLeft != _levelBounds.X || levelBoundsWidth != _levelBounds.W)) {
			LimitCameraView(_players[0], _players[0]->GetPos(), levelBoundsLeft, levelBoundsWidth);
		}
		for (Actors::Player* player : _players) {
			WarpCameraToTarget(player, true);
		}

		resolver.PreloadDeferredAnimations();

		OnInitialized();

		return true;
	}

	void LevelHandler::OnTileFrozen(std::int32_t x, std::int32_t y)
	{
		bool iceBlockFound = false;
		FindCollisionActorsByAABB(nullptr, AABBf(x - 1.0f, y - 1.0f, x + 1.0f, y + 1.0f), [&iceBlockFound](Actors::ActorBase* actor) -> bool {
			if ((actor->GetState() & Actors::ActorState::IsDestroyed) != Actors::ActorState::None) {
				return true;
			}

			auto* iceBlock = runtime_cast<Actors::Environment::IceBlock>(actor);
			if (iceBlock != nullptr) {
				iceBlock->ResetTimeLeft();
				iceBlockFound = true;
				return false;
			}

			return true;
		});

		if (!iceBlockFound) {
			std::shared_ptr<Actors::Environment::IceBlock> iceBlock = std::make_shared<Actors::Environment::IceBlock>();
			iceBlock->OnActivated(Actors::ActorActivationDetails(
				this,
				Vector3i(x - 1, y - 2, ILevelHandler::MainPlaneZ)
			));
			AddActor(iceBlock);
		}
	}

	void LevelHandler::BeforeActorDestroyed(Actors::ActorBase* actor)
	{
		// Nothing to do here
	}

	void LevelHandler::ProcessEvents(float timeMult)
	{
		ZoneScopedC(0x4876AF);

		if (!_players.empty()) {
			std::size_t playerCount = _players.size();
			SmallVector<AABBi, ControlScheme::MaxSupportedPlayers * 2> playerZones;
			playerZones.reserve(playerCount * 2);
			for (std::size_t i = 0; i < playerCount; i++) {
				auto pos = _players[i]->GetPos();
				std::int32_t tx = (std::int32_t)pos.X / TileSet::DefaultTileSize;
				std::int32_t ty = (std::int32_t)pos.Y / TileSet::DefaultTileSize;

				const auto& activationRange = playerZones.emplace_back(tx - ActivateTileRange, ty - ActivateTileRange, tx + ActivateTileRange, ty + ActivateTileRange);
				playerZones.emplace_back(activationRange.L - 4, activationRange.T - 4, activationRange.R + 4, activationRange.B + 4);
			}

			// Deactivation asks where an actor started, never where it is, so one that has wandered from its
			// origin could be destroyed in plain sight. The camera leads the player and stops at the level
			// bounds, so the visible world is taken from it rather than inferred from the player. Headless is
			// skipped because a viewport assigned there is never initialized and would have no render target.
			SmallVector<AABBf, ControlScheme::MaxSupportedPlayers> visibleRects;
			if (!ContentResolver::Get().IsHeadless()) {
				for (auto& viewport : _assignedViewports) {
					// `AABB` is the hitbox, which a sprite can overhang, so the rectangle is grown by a tile. That
					// is still far inside the margin the activation box itself keeps beyond the edge of the view.
					const Vector2i halfView = viewport->GetViewportSize() / 2;
					const float slack = (float)TileSet::DefaultTileSize;
					const Vector2f center = viewport->_cameraPos;
					visibleRects.emplace_back(center.X - halfView.X - slack, center.Y - halfView.Y - slack,
						center.X + halfView.X + slack, center.Y + halfView.Y + slack);
				}
			}

			for (auto& actor : _actors) {
				if ((actor->_state & (Actors::ActorState::IsCreatedFromEventMap | Actors::ActorState::IsFromGenerator)) != Actors::ActorState::None) {
					Vector2i originTile = actor->_originTile;
					bool isInside = false;
					for (std::size_t i = 1; i < playerZones.size(); i += 2) {
						if (playerZones[i].Contains(originTile)) {
							isInside = true;
							break;
						}
					}

					if (!isInside) {
						for (const AABBf& visibleRect : visibleRects) {
							if (visibleRect.Overlaps(actor->AABB)) {
								isInside = true;
								break;
							}
						}
					}

					if (!isInside && actor->OnTileDeactivated()) {
						if ((actor->_state & Actors::ActorState::IsFromGenerator) == Actors::ActorState::IsFromGenerator) {
							_eventMap->ResetGenerator(originTile.X, originTile.Y);
						}

						_eventMap->Deactivate(originTile.X, originTile.Y);
						actor->_state |= Actors::ActorState::IsDestroyed;
					}
				}
			}

			for (std::size_t i = 0; i < playerZones.size(); i += 2) {
				const auto& activationZone = playerZones[i];
				_eventMap->ActivateEvents(activationZone.L, activationZone.T, activationZone.R, activationZone.B, true);
			}

			if (!_checkpointCreated) {
				// Create checkpoint after first call to ActivateEvents() to avoid duplication of objects that are spawned near player spawn
				_checkpointCreated = true;
				_eventMap->CreateCheckpointForRollback();
				_tileMap->CreateCheckpointForRollback();
				CreateCheckpointSnapshot();
#if defined(WITH_ANGELSCRIPT)
				if (_scripts != nullptr) {
					_scripts->OnLevelBegin();
				}
#endif
			}
		}

		_eventMap->ProcessGenerators(timeMult);
	}

	void LevelHandler::ProcessQueuedNextLevel()
	{
		bool playersReady = true;
		for (auto player : _players) {
			// Exit type was already provided in BeginLevelChange()
			playersReady &= player->OnLevelChanging(nullptr, ExitType::None);
		}

		if (playersReady && _nextLevelTime <= 0.0f) {
			LevelInitialization levelInit;
			PrepareNextLevelInitialization(levelInit);
			HandleLevelChange(std::move(levelInit));
		}
	}

	void LevelHandler::PrepareNextLevelInitialization(LevelInitialization& levelInit)
	{
		String nextLevel = ResolveNextLevelName(_nextLevelType, _nextLevelName);
		if (_returnLevelState != nullptr && StringUtils::equalsIgnoreCase(nextLevel, _levelName)) {
			// The special level is completed, so return back to the level it was entered from
			levelInit.LevelName = ReadLevelStateName(*_returnLevelState);
			levelInit.RestoreLevelState = _returnLevelState;
		} else {
			levelInit.LevelName = std::move(nextLevel);
			levelInit.ReturnLevelState = _leftLevelState;
		}

		auto p = _levelName.partition('/');

		levelInit.Difficulty = _difficulty;
		levelInit.IsReforged = _isReforged;
		levelInit.CheatsUsed = _cheatsUsed;
		levelInit.LastExitType = _nextLevelType;
		levelInit.LastEpisodeName = p[0];
		levelInit.ElapsedMilliseconds = _elapsedMillisecondsBegin + (std::uint64_t)(_elapsedFrames * FrameTimer::SecondsPerFrame * 1000.0f);

		std::int32_t playerCount = (std::int32_t)std::min(_players.size(), arraySize(levelInit.PlayerCarryOvers));
		for (std::int32_t i = 0; i < playerCount; i++) {
			levelInit.PlayerCarryOvers[i] = _players[i]->PrepareLevelCarryOver();
		}
	}

	Recti LevelHandler::GetPlayerViewportBounds(std::int32_t w, std::int32_t h, std::int32_t index)
	{
		std::int32_t count = (std::int32_t)_assignedViewports.size();

		switch (count) {
			default:
			case 1: {
				return Recti(0, 0, w, h);
			}
			case 2: {
				if (PreferencesCache::PreferVerticalSplitscreen) {
					std::int32_t halfW = w / 2;
					return Recti(index * halfW, 0, halfW, h);
				} else {
					std::int32_t halfH = h / 2;
					return Recti(0, index * halfH, w, halfH);
				}
			}
			case 3:
			case 4: {
				std::int32_t halfW = (w + 1) / 2;
				std::int32_t halfH = (h + 1) / 2;
				return Recti((index % 2) * halfW, (index / 2) * halfH, halfW, halfH);
			}
		}
	}

	void LevelHandler::ProcessWeather(float timeMult)
	{
		// Weather is by far the busiest particle producer (it respawns every frame), so the particle quality
		// preference reaches it too: none of it when particles are off, half the density at the low quality.
		// A headless server has no viewport to spawn the debris around and loads no textures to size it by,
		// so none of the work below applies there - and the weather type turns non-empty as soon as the first
		// player activates an `AreaWeather` tile.
		if (_weatherType == WeatherType::None || PreferencesCache::Particles == ParticleQuality::Off ||
			ContentResolver::Get().IsHeadless()) {
			return;
		}

		std::size_t playerCount = _assignedViewports.size();
		SmallVector<Rectf, ControlScheme::MaxSupportedPlayers> playerZones;
		playerZones.reserve(playerCount);
		for (std::size_t i = 0; i < playerCount; i++) {
			Rectf cullingRect = _assignedViewports[i]->_view->GetCullingRect();

			bool found = false;
			for (std::size_t j = 0; j < playerZones.size(); j++) {
				if (playerZones[j].Overlaps(cullingRect)) {
					playerZones[j].Union(cullingRect);
					found = true;
					break;
				}
			}

			if (!found) {
				playerZones.push_back(std::move(cullingRect));
			}
		}

		float effectiveIntensity = (PreferencesCache::Particles == ParticleQuality::Low ? _weatherIntensity * 0.5f : (float)_weatherIntensity);
		std::int32_t weatherIntensity = std::max<std::int32_t>((std::int32_t)(effectiveIntensity * timeMult), 1);

		bool isRain = ((_weatherType & ~WeatherType::OutdoorsOnly) == WeatherType::Rain);
		auto* res = _commonResources->FindAnimation(isRain ? Rain : Snow);
		if (res == nullptr) {
			return;
		}

		auto& resBase = res->Base;
		Vector2i texSize = resBase->TextureDiffuse->GetSize();
		// The weather sprite is loaded indexed now, so the debris must recolor through its palette offset (-1 = baked)
		std::int32_t paletteOffset = (((resBase->Flags & GenericGraphicResourceFlags::Indexed) == GenericGraphicResourceFlags::Indexed) ? (std::int32_t)res->PaletteOffset : -1);

		for (auto& zone : playerZones) {
			for (std::int32_t i = 0; i < weatherIntensity; i++) {
				// Weather respawns every frame while its particles live for about three seconds, so it settles at
				// a few hundred live particles - on the consoles that is the whole debris budget, and a death
				// burst (the effect that actually matters) would find nothing left. Give it its own smaller share.
				if (TileMap::MaxWeatherDebrisCount > 0 && _tileMap->GetDebrisCount() >= TileMap::MaxWeatherDebrisCount) {
					return;
				}

				TileMap::DebrisFlags debrisFlags;
				if ((_weatherType & WeatherType::OutdoorsOnly) == WeatherType::OutdoorsOnly) {
					debrisFlags = TileMap::DebrisFlags::Disappear;
				} else {
					debrisFlags = (Random().FastFloat() > 0.7f
						? TileMap::DebrisFlags::None
						: TileMap::DebrisFlags::Disappear);
				}

				Vector2f debrisPos = Vector2f(zone.X + Random().FastFloat(zone.W * -1.0f, zone.W * 2.0f),
					zone.Y + Random().NextFloat(zone.H * -1.0f, zone.H * 2.0f));

				float scale = Random().FastFloat(0.4f, 1.1f);

				std::uint32_t curAnimFrame = res->FrameOffset + Random().Next(0, res->FrameCount);
				Recti frameRect = resBase->GetFrameRect(curAnimFrame);
				Vector2i frameOffset = resBase->GetFrameOffset(curAnimFrame);

				TileMap::DestructibleDebris debris = { };
				debris.Pos = debrisPos;
				debris.Depth = MainPlaneZ - 100 + (std::uint16_t)(200 * scale);
				// Sized by the frame's own area rather than the logical cell: with trimmed frames the two
				// differ per frame, and stretching a trimmed frame over the whole cell visibly distorts it
				debris.Size = Vector2f((float)frameRect.W, (float)frameRect.H);
				debris.FrameOffset = Vector2f(frameOffset.X + (frameRect.W - resBase->FrameDimensions.X) * 0.5f,
					frameOffset.Y + (frameRect.H - resBase->FrameDimensions.Y) * 0.5f);

				if (isRain) {
					float speedX = Random().FastFloat(2.2f, 2.7f) * scale;
					float speedY = Random().FastFloat(7.6f, 8.6f) * scale;
					debris.Speed = Vector2f(speedX, speedY);
					debris.Acceleration = Vector2f(0.0f, 0.0f);
					debris.Angle = atan2Approx(speedY, speedX);
					debris.AngleSpeed = 0.0f;
				} else {
					float speedX = Random().FastFloat(-1.6f, -1.2f) * scale;
					float speedY = Random().FastFloat(3.0f, 4.0f) * scale;
					float accel = Random().FastFloat(-0.008f, 0.008f) * scale;
					debris.Speed = Vector2f(speedX, speedY);
					debris.Acceleration = Vector2f(accel, -std::abs(accel));
					debris.Angle = Random().FastFloat(0.0f, fTwoPi);
					debris.AngleSpeed = speedX * 0.02f;
				}

				debris.Scale = scale;
				debris.ScaleSpeed = 0.0f;
				debris.Alpha = 1.0f;
				debris.AlphaSpeed = 0.0f;

				debris.Time = 180.0f;

				debris.TexScaleX = (float(frameRect.W) / float(texSize.X));
				debris.TexBiasX = (float(frameRect.X) / float(texSize.X));
				debris.TexScaleY = (float(frameRect.H) / float(texSize.Y));
				debris.TexBiasY = (float(frameRect.Y) / float(texSize.Y));

				debris.DiffuseTexture = resBase->TextureDiffuse.get();
				debris.PaletteOffset = paletteOffset;
				debris.Flags = debrisFlags;

				_tileMap->CreateDebris(debris);
			}
		}
	}

	void LevelHandler::ResolveCollisions(float timeMult)
	{
		ZoneScopedC(0x4876AF);

		auto it = _actors.begin();
		while (it != _actors.end()) {
			Actors::ActorBase* actor = it->get();
			if (actor->GetState(Actors::ActorState::IsDestroyed)) {
				BeforeActorDestroyed(actor);
				if (actor->_collisionProxyID != Collisions::NullNode) {
					_collisions.DestroyProxy(actor->_collisionProxyID);
					actor->_collisionProxyID = Collisions::NullNode;
				}
				it = _actors.eraseUnordered(it);
				continue;
			}
			
			if (actor->GetState(Actors::ActorState::IsDirty) && actor->_collisionProxyID != Collisions::NullNode) {
				actor->UpdateAABB();

				// The proxy has to cover the path the actor took, not just where it ended up. The tree only
				// extends a proxy *forwards* (it predicts the next step from the speed), so a small object the
				// actor passed over during this frame ends up behind the proxy and the pair is never reported
				// to UpdatePairs() at all - no amount of testing in OnPairAdded() can recover it then. Stretching
				// the box back over the path only widens the candidate set; both the pair handler and
				// FindCollisionActorsByAABB() still decide with a precise test. A warp or any other forced
				// relocation is not a path and resets it (see Actors::ActorBase::ResetPathTracking()), so the
				// box stays as small as the actor's own movement.
				AABBf sweptAABB = actor->AABB;
				Vector2f delta = actor->_pos - actor->_frameStartPos;
				if (delta.X > 0.0f) {
					sweptAABB.L -= delta.X;
				} else {
					sweptAABB.R -= delta.X;
				}
				if (delta.Y > 0.0f) {
					sweptAABB.T -= delta.Y;
				} else {
					sweptAABB.B -= delta.Y;
				}

				_collisions.MoveProxy(actor->_collisionProxyID, sweptAABB, actor->_speed * timeMult);
				actor->SetState(Actors::ActorState::IsDirty, false);
			}
			++it;
		}

		struct UpdatePairsHelper {
			void OnPairAdded(void* proxyA, void* proxyB) {
				Actors::ActorBase* actorA = (Actors::ActorBase*)proxyA;
				Actors::ActorBase* actorB = (Actors::ActorBase*)proxyB;
				if (((actorA->GetState() | actorB->GetState()) & (Actors::ActorState::CollideWithOtherActors | Actors::ActorState::IsDestroyed)) != Actors::ActorState::CollideWithOtherActors) {
					return;
				}

				// The swept tests catch the pair that a fast object skipped over between two frames, which
				// the plain overlap test at the end positions cannot see (a spring or another small object
				// is easy to jump clean over at a low frame rate)
				if (actorA->IsCollidingWith(actorB) || actorA->HasCrossedOver(actorB) || actorB->HasCrossedOver(actorA)) {
					if (!actorA->OnHandleCollision(actorB)) {
						actorB->OnHandleCollision(actorA);
					}
				}
			}
		};
		UpdatePairsHelper helper;
		_collisions.UpdatePairs(&helper);
	}

	void LevelHandler::AssignViewport(Actors::Player* player)
	{
		_assignedViewports.push_back(std::make_unique<Rendering::PlayerViewport>(this, player));

#if defined(WITH_AUDIO)
		for (auto& current : _playingSounds) {
			if (auto* currentForSplitscreen = runtime_cast<AudioBufferPlayerForSplitscreen>(current.get())) {
				currentForSplitscreen->updateViewports(_assignedViewports);
			}
		}
#endif
	}

	void LevelHandler::UnassignViewport(Actors::Player* player)
	{
		bool success = false;
		for (std::size_t i = 0; i < _assignedViewports.size(); i++) {
			if (_assignedViewports[i]->_targetActor == player) {
				_assignedViewports.eraseUnordered(i);
				success = true;
				break;
			}
		}
		
#if defined(WITH_AUDIO)
		if (success) {
			for (auto& current : _playingSounds) {
				if (auto* currentForSplitscreen = runtime_cast<AudioBufferPlayerForSplitscreen>(current.get())) {
					currentForSplitscreen->updateViewports(_assignedViewports);
				}
			}
		}
#endif
	}

	void LevelHandler::CommitViewports()
	{
		Viewport::GetChain().clear();
		Vector2i res = theApplication().GetResolution();
		OnInitializeViewport(res.X, res.Y);
	}

	Rendering::UpscaleRenderPassWithClipping& LevelHandler::GetActiveOverlayPass()
	{
		// When the scene is supersampled, the HUD and in-game menu live in a separate native-resolution overlay pass;
		// otherwise they are drawn into the scene pass as usual
		return (_hudOverlayActive ? _hudUpscalePass : _upscalePass);
	}

	SceneNode* LevelHandler::GetHudParentNode()
	{
		return GetActiveOverlayPass().GetNode();
	}

	void LevelHandler::InitializeCamera(Rendering::PlayerViewport& viewport)
	{
		if (viewport._targetActor == nullptr) {
			return;
		}

		viewport._viewBounds = _viewBoundsTarget;

		// The position to focus on
		Vector2f focusPos = viewport._targetActor->_pos;
		Vector2i halfView = viewport._view->GetSize() / 2;

		// Clamp camera position to level bounds
		if (viewport._viewBounds.W > halfView.X * 2) {
			viewport._cameraPos.X = roundFast(std::clamp(focusPos.X, viewport._viewBounds.X + halfView.X, viewport._viewBounds.X + viewport._viewBounds.W - halfView.X));
		} else {
			viewport._cameraPos.X = roundFast(viewport._viewBounds.X + viewport._viewBounds.W * 0.5f);
		}
		if (viewport._viewBounds.H > halfView.Y * 2) {
			viewport._cameraPos.Y = roundFast(std::clamp(focusPos.Y, viewport._viewBounds.Y + halfView.Y, viewport._viewBounds.Y + viewport._viewBounds.H - halfView.Y));
		} else {
			viewport._cameraPos.Y = roundFast(viewport._viewBounds.Y + viewport._viewBounds.H * 0.5f);
		}

		viewport._cameraLastPos = viewport._cameraPos;
		viewport._camera->SetView(viewport._cameraPos, 0.0f, 1.0f);
		// The look-ahead of a small view starts where it rests rather than drifting into place (see ResetLookAhead())
		viewport.ResetLookAhead();
	}

	Vector2f LevelHandler::GetCameraPos(Actors::Player* player) const
	{
		for (auto& viewport : _assignedViewports) {
			if (viewport->_targetActor == player) {
				return viewport->_cameraPos;
			}
		}
		return {};
	}

	void LevelHandler::LimitCameraView(Actors::Player* player, Vector2f playerPos, std::int32_t left, std::int32_t width)
	{
		_levelBounds.X = left;
		if (width > 0.0f) {
			_levelBounds.W = width;
		} else {
			_levelBounds.W = _tileMap->GetLevelBounds().X - left;
		}

		Rectf bounds = _levelBounds.As<float>();
		if (left == 0 && width == 0) {
			for (auto& viewport : _assignedViewports) {
				viewport->_viewBounds = bounds;
			}
			_viewBoundsTarget = bounds;
		} else {
			Rendering::PlayerViewport* currentViewport = nullptr;
			float maxViewWidth = 0.0f;
			for (auto& viewport : _assignedViewports) {
				auto size = viewport->GetViewportSize();
				if (maxViewWidth < size.X) {
					maxViewWidth = (float)size.X;
				}
				if (viewport->_targetActor == player) {
					currentViewport = viewport.get();
				}
			}

			if (bounds.W < maxViewWidth) {
				bounds.X -= (maxViewWidth - bounds.W);
				bounds.W = maxViewWidth;
			}

			if (_viewBoundsTarget != bounds) {
				_viewBoundsTarget = bounds;

				if (currentViewport != nullptr) {
					float limit = currentViewport->_cameraPos.X - (maxViewWidth * 0.6f);
					if (currentViewport->_viewBounds.X < limit) {
						currentViewport->_viewBounds.W += (currentViewport->_viewBounds.X - limit);
						currentViewport->_viewBounds.X = limit;
					}
				}

				// Warp all other distant players to this player
				for (auto& viewport : _assignedViewports) {
					if (viewport->_targetActor != player) {
						float limit = viewport->_cameraPos.X - (maxViewWidth * 0.6f);
						if (viewport->_viewBounds.X < limit) {
							viewport->_viewBounds.W += (viewport->_viewBounds.X - limit);
							viewport->_viewBounds.X = limit;
						}

						auto pos = viewport->_targetActor->_pos;
						if ((pos.X < bounds.X || pos.X >= bounds.X + bounds.W) && (pos - playerPos).Length() > 100.0f) {
							if (auto* otherPlayer = runtime_cast<Actors::Player>(viewport->_targetActor)) {
								otherPlayer->WarpToPosition(playerPos, WarpFlags::SkipWarpIn);
								if (currentViewport != nullptr) {
									viewport->_ambientLight = currentViewport->_ambientLight;
									viewport->_ambientLightTarget = currentViewport->_ambientLightTarget;
								}
							}
						}
					}
				}
			}
		}
	}

	void LevelHandler::OverrideCameraView(Actors::Player* player, float x, float y, bool topLeft)
	{
		for (auto& viewport : _assignedViewports) {
			if (viewport->_targetActor == player) {
				viewport->OverrideCamera(x, y, topLeft);
			}
		}
	}

	void LevelHandler::ShakeCameraView(Actors::Player* player, float duration)
	{
		for (auto& viewport : _assignedViewports) {
			if (viewport->_targetActor == player) {
				viewport->ShakeCameraView(duration);
			}
		}

		PlayerExecuteRumble(player, "Shake"_s);
	}

	void LevelHandler::ShakeCameraViewNear(Vector2f pos, float duration)
	{
		constexpr float MaxDistance = 800.0f;

		for (auto& viewport : _assignedViewports) {
			if ((viewport->_targetActor->_pos - pos).Length() <= MaxDistance) {
				viewport->ShakeCameraView(duration);

				if (auto* player = runtime_cast<Actors::Player>(viewport->_targetActor)) {
					PlayerExecuteRumble(player, "Shake"_s);
				}
			}
		}
	}

	bool LevelHandler::GetTrigger(std::uint8_t triggerId)
	{
		return _tileMap->GetTrigger(triggerId);
	}

	void LevelHandler::SetTrigger(std::uint8_t triggerId, bool newState)
	{
		_tileMap->SetTrigger(triggerId, newState);
	}

	void LevelHandler::SetWeather(WeatherType type, std::uint8_t intensity)
	{
		_weatherType = type;
		_weatherIntensity = intensity;
	}

	bool LevelHandler::BeginPlayMusic(StringView path, bool setDefault, bool forceReload)
	{
		bool result = false;

#if defined(WITH_AUDIO)
		if (_sugarRushMusic != nullptr) {
			_sugarRushMusic->stop();
		}

		if (!forceReload && _musicCurrentPath == path) {
			// Music is already playing or is paused
			if (_music != nullptr) {
				_music->play();
			}
			if (setDefault) {
				_musicDefaultPath = path;
			}
			return false;
		}

		if (_music != nullptr) {
			_music->stop();
		}

		if (!path.empty()) {
			_music = ContentResolver::Get().GetMusic(path);
			if (_music != nullptr) {
				_music->setLooping(true);
				_music->setGain(PreferencesCache::MasterVolume * PreferencesCache::MusicVolume);
				_music->setSourceRelative(true);
				_music->play();
				result = true;
			}
		} else {
			_music = nullptr;
		}

		_musicCurrentPath = path;
		if (setDefault) {
			_musicDefaultPath = path;
		}
#endif

		return result;
	}

	void LevelHandler::UpdatePressedActions()
	{
		ZoneScopedC(0x4876AF);

		auto& input = theApplication().GetInputManager();

		const JoyMappedState* joyStates[ControlScheme::MaxConnectedGamepads];
		std::int32_t joyStatesCount = 0;
		for (std::int32_t i = 0; i < JoyMapping::MaxNumJoysticks && joyStatesCount < std::int32_t(arraySize(joyStates)); i++) {
			if (input.isJoyMapped(i)) {
				joyStates[joyStatesCount++] = &input.joyMappedState(i);
			}
		}

		for (std::int32_t i = 0; i < ControlScheme::MaxSupportedPlayers; i++) {
			auto& input = _playerInputs[i];
			auto processedInput = ControlScheme::FetchProcessedInput(i,
				_pressedKeys, ArrayView(joyStates, joyStatesCount), input.PressedActions,
				_hud == nullptr || !_hud->IsWeaponWheelVisible(i));

			input.PressedActionsLast = input.PressedActions;
			input.PressedActions = processedInput.PressedActions;
			input.RequiredMovement = processedInput.Movement;
		}

		// Also apply overriden actions (by touch controls)
		{
			auto& input = _playerInputs[0];
			input.PressedActions |= _overrideActions;

			// overrideMovement is non-zero when a floating joystick is active - use it directly
			if (_overrideMovement.X != 0.0f || _overrideMovement.Y != 0.0f) {
				input.RequiredMovement = _overrideMovement;
			} else {
				// Fallback: derive movement from D-pad override actions
				if ((_overrideActions & (1 << (std::int32_t)PlayerAction::Right)) != 0) {
					input.RequiredMovement.X = 1.0f;
				} else if ((_overrideActions & (1 << (std::int32_t)PlayerAction::Left)) != 0) {
					input.RequiredMovement.X = -1.0f;
				}
				if ((_overrideActions & (1 << (std::int32_t)PlayerAction::Down)) != 0) {
					input.RequiredMovement.Y = 1.0f;
				} else if ((_overrideActions & (1 << (std::int32_t)PlayerAction::Up)) != 0) {
					input.RequiredMovement.Y = -1.0f;
				}
			}
		}
	}

	void LevelHandler::UpdateRichPresence()
	{
#if (defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)) || defined(DEATH_TARGET_UNIX)
		if (!PreferencesCache::EnableDiscordIntegration || !UI::DiscordRpcClient::Get().IsSupported()) {
			return;
		}

		auto p = _levelName.partition('/');

		UI::DiscordRpcClient::RichPresence richPresence;
		if (p[0] == "prince"_s) {
			if (p[2] == "01_castle1"_s || p[2] == "02_castle1n"_s) {
				richPresence.LargeImage = "level-prince-01"_s;
			} else if (p[2] == "03_carrot1"_s || p[2] == "04_carrot1n"_s) {
				richPresence.LargeImage = "level-prince-02"_s;
			} else if (p[2] == "05_labrat1"_s || p[2] == "06_labrat2"_s || p[2] == "bonus_labrat3"_s) {
				richPresence.LargeImage = "level-prince-03"_s;
			}
		} else if (p[0] == "rescue"_s) {
			if (p[2] == "01_colon1"_s || p[2] == "02_colon2"_s) {
				richPresence.LargeImage = "level-rescue-01"_s;
			} else if (p[2] == "03_psych1"_s || p[2] == "04_psych2"_s || p[2] == "bonus_psych3"_s) {
				richPresence.LargeImage = "level-rescue-02"_s;
			} else if (p[2] == "05_beach"_s || p[2] == "06_beach2"_s) {
				richPresence.LargeImage = "level-rescue-03"_s;
			}
		} else if (p[0] == "flash"_s) {
			if (p[2] == "01_diam1"_s || p[2] == "02_diam3"_s) {
				richPresence.LargeImage = "level-flash-01"_s;
			} else if (p[2] == "03_tube1"_s || p[2] == "04_tube2"_s || p[2] == "bonus_tube3"_s) {
				richPresence.LargeImage = "level-flash-02"_s;
			} else if (p[2] == "05_medivo1"_s || p[2] == "06_medivo2"_s || p[2] == "bonus_garglair"_s) {
				richPresence.LargeImage = "level-flash-03"_s;
			}
		} else if (p[0] == "monk"_s) {
			if (p[2] == "01_jung1"_s || p[2] == "02_jung2"_s) {
				richPresence.LargeImage = "level-monk-01"_s;
			} else if (p[2] == "03_hell"_s || p[2] == "04_hell2"_s) {
				richPresence.LargeImage = "level-monk-02"_s;
			} else if (p[2] == "05_damn"_s || p[2] == "06_damn2"_s) {
				richPresence.LargeImage = "level-monk-03"_s;
			}
		} else if (p[0] == "secretf"_s) {
			if (p[2] == "01_easter1"_s || p[2] == "02_easter2"_s || p[2] == "03_easter3"_s) {
				richPresence.LargeImage = "level-secretf-01"_s;
			} else if (p[2] == "04_haunted1"_s || p[2] == "05_haunted2"_s || p[2] == "06_haunted3"_s) {
				richPresence.LargeImage = "level-secretf-02"_s;
			} else if (p[2] == "07_town1"_s || p[2] == "08_town2"_s || p[2] == "09_town3"_s) {
				richPresence.LargeImage = "level-secretf-03"_s;
			}
		} else if (p[0] == "xmas98"_s || p[0] == "xmas99"_s) {
			richPresence.LargeImage = "level-xmas"_s;
		} else if (p[0] == "share"_s) {
			richPresence.LargeImage = "level-share"_s;
		}

		if (richPresence.LargeImage.empty()) {
			richPresence.Details = "Playing"_s;
			richPresence.LargeImage = "main-transparent"_s;

			if (!_players.empty())
				switch (_players[0]->GetPlayerType()) {
					default:
					case PlayerType::Jazz: richPresence.SmallImage = "playing-jazz"_s; break;
					case PlayerType::Spaz: richPresence.SmallImage = "playing-spaz"_s; break;
					case PlayerType::Lori: richPresence.SmallImage = "playing-lori"_s; break;
				}
		} else {
			richPresence.Details = "Playing episode"_s;
		}

		if (!_players.empty()) {
			switch (_players[0]->GetPlayerType()) {
				default:
				case PlayerType::Jazz: richPresence.Details += " as Jazz"_s; break;
				case PlayerType::Spaz: richPresence.Details += " as Spaz"_s; break;
				case PlayerType::Lori: richPresence.Details += " as Lori"_s; break;
			}
		}

		UI::DiscordRpcClient::Get().SetRichPresence(richPresence);
#endif
	}

	void LevelHandler::InitializeRumbleEffects()
	{
#if defined(NCINE_HAS_GAMEPAD_RUMBLE)
		if (auto* breakTile = RegisterRumbleEffect("BreakTile"_s)) {
			breakTile->AddToTimeline(10, 1.0f, 0.0f);
		}

		if (auto* hurt = RegisterRumbleEffect("Hurt"_s)) {
			hurt->AddToTimeline(4, 0.15f, 0.0f);
			hurt->AddToTimeline(8, 0.45f, 0.0f);
			hurt->AddToTimeline(12, 0.15f, 0.0f);
		}

		if (auto* die = RegisterRumbleEffect("Die"_s)) {
			die->AddToTimeline(4, 0.9f, 0.3f);
			die->AddToTimeline(8, 0.3f, 0.9f);
			die->AddToTimeline(12, 0.0f, 0.9f);
		}

		if (auto* land = RegisterRumbleEffect("Land"_s)) {
			land->AddToTimeline(4, 0.0f, 0.525f);
		}

		if (auto* spring = RegisterRumbleEffect("Spring"_s)) {
			spring->AddToTimeline(10, 0.0f, 0.8f);
		}

		if (auto* fire = RegisterRumbleEffect("Fire"_s)) {
			fire->AddToTimeline(4, 0.0f, 0.0f, 0.0f, 0.3f);
		}

		if (auto* fireWeak = RegisterRumbleEffect("FireWeak"_s)) {
			fireWeak->AddToTimeline(16, 0.0f, 0.0f, 0.0f, 0.04f);
		}

		if (auto* warp = RegisterRumbleEffect("Warp"_s)) {
			warp->AddToTimeline(2, 0.0f, 0.0f, 0.02f, 0.01f);
			warp->AddToTimeline(6, 0.3f, 0.0f, 0.04f, 0.02f);
			warp->AddToTimeline(10, 0.2f, 0.0f, 0.08f, 0.02f);
			warp->AddToTimeline(13, 0.1f, 0.0f, 0.04f, 0.04f);
			warp->AddToTimeline(16, 0.0f, 0.0f, 0.02f, 0.08f);
			warp->AddToTimeline(20, 0.0f, 0.0f, 0.0f, 0.04f);
			warp->AddToTimeline(22, 0.0f, 0.0f, 0.0f, 0.02f);
		}

		if (auto* shake = RegisterRumbleEffect("Shake"_s)) {
			shake->AddToTimeline(20, 1.0f, 1.0f);
			shake->AddToTimeline(20, 0.6f, 0.6f);
			shake->AddToTimeline(30, 0.2f, 0.2f);
			shake->AddToTimeline(40, 0.2f, 0.0f);
		}
#endif
	}

	RumbleDescription* LevelHandler::RegisterRumbleEffect(StringView name)
	{
#if defined(NCINE_HAS_GAMEPAD_RUMBLE)
		auto it = _rumbleEffects.emplace(name, std::make_shared<RumbleDescription>());
		return (it.second ? it.first->second.get() : nullptr);
#else
		return nullptr;
#endif
	}

	void LevelHandler::PauseGame()
	{
		// Show in-game pause menu
		_pauseMenu = std::make_shared<UI::Menu::InGameMenu>(this);
		if (IsPausable()) {
			// Prevent updating of all level objects
			_rootNode->setUpdateEnabled(false);
#if defined(NCINE_HAS_GAMEPAD_RUMBLE)
			_rumble.CancelAllEffects();
#endif
		}

#if defined(WITH_AUDIO)
		// Use low-pass filter on music and pause all SFX
		if (_music != nullptr) {
			_music->setLowPass(0.1f);
		}
		if (IsPausable()) {
			for (auto& sound : _playingSounds) {
				if (sound->isPlaying()) {
					sound->pause();
				}
			}
			// If Sugar Rush music is playing, pause it and play normal music instead
			if (_sugarRushMusic != nullptr && _music != nullptr) {
				_music->play();
			}
		}
#endif
	}

	void LevelHandler::ResumeGame()
	{
		// Resume all level objects
		_rootNode->setUpdateEnabled(true);
		// Hide in-game pause menu
		_pauseMenu = nullptr;

#if defined(WITH_AUDIO)
		// If Sugar Rush music was playing, resume it and pause normal music again
		if (_sugarRushMusic != nullptr && _music != nullptr) {
			_music->pause();
		}
		// Resume all SFX
		for (auto& sound : _playingSounds) {
			if (sound->isPaused()) {
				sound->play();
			}
		}
		if (_music != nullptr) {
			_music->setLowPass(1.0f);
		}
#endif

		// Mark Menu button as already pressed to avoid some issues
		for (auto& input : _playerInputs) {
			input.PressedActions |= (1ull << (std::int32_t)PlayerAction::Menu);
			input.PressedActionsLast |= (1ull << (std::int32_t)PlayerAction::Menu);
		}
	}

	void LevelHandler::ShowConsole()
	{
		_console->Show();
	}

	void LevelHandler::HideConsole()
	{
		_console->Hide();
	}

	bool LevelHandler::TryInvokeCheat(StringView line)
	{
		if (!ApplyCheat(line, {})) {
			return false;
		}

		_console->WriteLine(UI::MessageLevel::Echo, line);
		if (IsCheatingAllowed(nullptr) && !_players.empty()) {
			_cheatsUsed = true;
			ApplyCheat(line, _players);
		} else {
			_console->WriteLine(UI::MessageLevel::Error, _("Cheats are not allowed in current context"));
		}
		return true;
	}

	bool LevelHandler::ApplyCheat(StringView line, ArrayView<Actors::Player* const> targets)
	{
		struct CheatCommand {
			StringView Command;
			StringView Alias;
			void (LevelHandler::*Handler)(ArrayView<Actors::Player* const> targets);
		};

		static const CheatCommand CheatCommands[] = {
			{ "jjkill"_s, "jjk"_s, &LevelHandler::CheatKill },
			{ "jjgod"_s, {}, &LevelHandler::CheatGod },
			{ "jjnext"_s, {}, &LevelHandler::CheatNext },
			{ "jjguns"_s, "jjammo"_s, &LevelHandler::CheatGuns },
			{ "jjrush"_s, {}, &LevelHandler::CheatRush },
			{ "jjgems"_s, {}, &LevelHandler::CheatGems },
			{ "jjbird"_s, {}, &LevelHandler::CheatBird },
			{ "jjlife"_s, {}, &LevelHandler::CheatLife },
			{ "jjpower"_s, {}, &LevelHandler::CheatPower },
			{ "jjcoins"_s, {}, &LevelHandler::CheatCoins },
			{ "jjmorph"_s, {}, &LevelHandler::CheatMorph },
			{ "jjshield"_s, {}, &LevelHandler::CheatShield },
			{ "jjfly"_s, {}, &LevelHandler::CheatFly },
		};

		for (const auto& cheat : CheatCommands) {
			if (line == cheat.Command || (!cheat.Alias.empty() && line == cheat.Alias)) {
				if (!targets.empty()) {
					(this->*cheat.Handler)(targets);
				}
				return true;
			}
		}

		return false;
	}

	void LevelHandler::CheatKill(ArrayView<Actors::Player* const> targets)
	{
		for (auto* player : targets) {
			player->TakeDamage(INT32_MAX, 0.0f, true);
		}
	}

	void LevelHandler::CheatGod(ArrayView<Actors::Player* const> targets)
	{
		for (auto* player : targets) {
			player->GrantInvulnerability(36000.0f, Actors::Player::InvulnerableType::Shielded);
		}
	}

	void LevelHandler::CheatNext(ArrayView<Actors::Player* const> targets)
	{
		// The invoking player acts as the initiator, so in multiplayer the active game mode can decide what
		// reaching the level exit means for them
		BeginLevelChange(targets[0], ExitType::Warp | ExitType::FastTransition);
	}

	void LevelHandler::CheatGuns(ArrayView<Actors::Player* const> targets)
	{
		for (auto* player : targets) {
			for (std::int32_t i = 0; i < (std::int32_t)WeaponType::Count; i++) {
				player->AddAmmo((WeaponType)i, 99);
			}
		}
	}

	void LevelHandler::CheatRush(ArrayView<Actors::Player* const> targets)
	{
		for (auto* player : targets) {
			player->ActivateSugarRush(1300.0f);
		}
	}

	void LevelHandler::CheatGems(ArrayView<Actors::Player* const> targets)
	{
		for (auto* player : targets) {
			player->AddGems(0, 5);
		}
	}

	void LevelHandler::CheatBird(ArrayView<Actors::Player* const> targets)
	{
		for (auto* player : targets) {
			player->SpawnBird(0, player->GetPos());
		}
	}

	void LevelHandler::CheatLife(ArrayView<Actors::Player* const> targets)
	{
		for (auto* player : targets) {
			player->AddLives(5);
		}
	}

	void LevelHandler::CheatPower(ArrayView<Actors::Player* const> targets)
	{
		for (auto* player : targets) {
			for (std::int32_t i = 0; i < (std::int32_t)WeaponType::Count; i++) {
				player->AddWeaponUpgrade((WeaponType)i, 0x01);
			}
		}
	}

	void LevelHandler::CheatCoins(ArrayView<Actors::Player* const> targets)
	{
		// Coins are synchronized automatically
		targets[0]->AddCoins(5);
	}

	void LevelHandler::CheatMorph(ArrayView<Actors::Player* const> targets)
	{
		PlayerType newType;
		switch (targets[0]->GetPlayerType()) {
			case PlayerType::Jazz: newType = PlayerType::Spaz; break;
			case PlayerType::Spaz: newType = PlayerType::Lori; break;
			default: newType = PlayerType::Jazz; break;
		}

		if (!targets[0]->MorphTo(newType)) {
			targets[0]->MorphTo(PlayerType::Jazz);
		}
	}

	void LevelHandler::CheatShield(ArrayView<Actors::Player* const> targets)
	{
		for (auto* player : targets) {
			ShieldType shieldType = (ShieldType)(((std::int32_t)player->GetActiveShield() + 1) % (std::int32_t)ShieldType::Count);
			player->SetShield(shieldType, 40.0f * FrameTimer::FramesPerSecond);
		}
	}


	void LevelHandler::CheatFly(ArrayView<Actors::Player* const> targets)
	{
		for (auto* player : targets) {
			PlayerType playerType = player->GetPlayerType();
			if (playerType != PlayerType::Jazz &&
				playerType != PlayerType::Spaz &&
				playerType != PlayerType::Lori) {
				continue;
			}

			if (player->IsInWater()) {
				player->EnableFlyCheat(false);
				continue;
			}

			Actors::Player::Modifier nextModifier;
			switch (player->GetModifier()) {
				case Actors::Player::Modifier::None:    nextModifier = Actors::Player::Modifier::Copter;   break;
				case Actors::Player::Modifier::Copter:  nextModifier = Actors::Player::Modifier::Airboard; break;
				default:                                nextModifier = Actors::Player::Modifier::None;     break;
			}
			if (nextModifier == Actors::Player::Modifier::Copter) {
				player->EnableFlyCheat(true);
			}
			player->SetModifier(nextModifier);
		}
	}

#if defined(WITH_IMGUI)
	ImVec2 LevelHandler::WorldPosToScreenSpace(Vector2f pos, const Rendering::PlayerViewport& viewport)
	{
		Rectf bounds = viewport.GetBounds();
		Vector2i originalSize = viewport._view->GetSize();
		Vector2f upscaledSize = _upscalePass.GetTargetSize();
		Vector2f halfView = bounds.Center();
		return ImVec2(
			(pos.X - viewport._cameraPos.X + halfView.X) * upscaledSize.X / originalSize.X,
			(pos.Y - viewport._cameraPos.Y + halfView.Y) * upscaledSize.Y / originalSize.Y
		);
	}
#endif

	LevelHandler::PlayerInput::PlayerInput()
		: PressedActions(0), PressedActionsLast(0), Frozen(false)
	{
	}
}
