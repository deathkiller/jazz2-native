#pragma once

#include "ILevelHandler.h"
#include "IResumable.h"
#include "IStateHandler.h"
#include "IRootController.h"
#include "LevelDescriptor.h"
#include "RumbleProcessor.h"
#include "WeatherType.h"
#include "Events/EventMap.h"
#include "Events/EventSpawner.h"
#include "Tiles/ITileMapOwner.h"
#include "Tiles/TileMap.h"
#include "Collisions/DynamicTreeBroadPhase.h"
#include "UI/ControlScheme.h"
#include "UI/UpscaleRenderPass.h"
#include "UI/Menu/InGameMenu.h"

#include "../nCine/Graphics/Shader.h"
#include "../nCine/Audio/AudioBufferPlayer.h"
#include "../nCine/Audio/AudioStreamPlayer.h"

#if defined(WITH_IMGUI)
#	include <imgui.h>
#endif

namespace Jazz2
{
	class LightingRenderer;
	class BlurRenderPass;
	class CombineRenderer;
	class PlayerViewport;

	namespace Actors
	{
		class Player;
	}

	namespace Actors::Bosses
	{
		class BossBase;
	}

#if defined(WITH_ANGELSCRIPT)
	namespace Scripting
	{
		class LevelScriptLoader;
	}
#endif

	namespace UI
	{
		class HUD;
	}

	namespace UI::Menu
	{
		class InGameMenu;
	}

	class LevelHandler : public ILevelHandler, public IStateHandler, public IResumable, public Tiles::ITileMapOwner
	{
		DEATH_RUNTIME_OBJECT(ILevelHandler);

		friend class LightingRenderer;
		friend class BlurRenderPass;
		friend class CombineRenderer;
		friend class PlayerViewport;
#if defined(WITH_ANGELSCRIPT)
		friend class Scripting::LevelScriptLoader;
#endif
		friend class UI::HUD;
		friend class UI::Menu::InGameMenu;

	public:
		static constexpr std::int32_t DefaultWidth = 720;
		static constexpr std::int32_t DefaultHeight = 405;
		static constexpr std::int32_t ActivateTileRange = 26;

		LevelHandler(IRootController* root);
		~LevelHandler() override;

		bool Initialize(const LevelInitialization& levelInit) override;
		bool Initialize(Stream& src) override;

		Events::EventSpawner* EventSpawner() override;
		Events::EventMap* EventMap() override;
		Tiles::TileMap* TileMap() override;

		GameDifficulty Difficulty() const override;
		bool IsPausable() const override;
		bool IsReforged() const override;
		bool CanPlayersCollide() const override;
		Recti LevelBounds() const override;
		float ElapsedFrames() const override;
		float Gravity() const override;
		float WaterLevel() const override;

		ArrayView<const std::shared_ptr<Actors::ActorBase>> GetActors() const override;
		ArrayView<Actors::Player* const> GetPlayers() const override;

		float GetDefaultAmbientLight() const override;
		void SetAmbientLight(Actors::Player* player, float value) override;

		void OnBeginFrame() override;
		void OnEndFrame() override;
		void OnInitializeViewport(std::int32_t width, std::int32_t height) override;

		void OnKeyPressed(const KeyboardEvent& event) override;
		void OnKeyReleased(const KeyboardEvent& event) override;
		void OnTouchEvent(const TouchEvent& event) override;

		void AddActor(std::shared_ptr<Actors::ActorBase> actor) override;

		std::shared_ptr<AudioBufferPlayer> PlaySfx(Actors::ActorBase* self, const StringView identifier, AudioBuffer* buffer, const Vector3f& pos, bool sourceRelative, float gain, float pitch) override;
		std::shared_ptr<AudioBufferPlayer> PlayCommonSfx(const StringView identifier, const Vector3f& pos, float gain = 1.0f, float pitch = 1.0f) override;
		void WarpCameraToTarget(Actors::ActorBase* actor, bool fast = false) override;
		bool IsPositionEmpty(Actors::ActorBase* self, const AABBf& aabb, Tiles::TileCollisionParams& params, Actors::ActorBase** collider) override;
		void FindCollisionActorsByAABB(Actors::ActorBase* self, const AABBf& aabb, const std::function<bool(Actors::ActorBase*)>& callback) override;
		void FindCollisionActorsByRadius(float x, float y, float radius, const std::function<bool(Actors::ActorBase*)>& callback) override;
		void GetCollidingPlayers(const AABBf& aabb, const std::function<bool(Actors::ActorBase*)> callback) override;

		void BroadcastTriggeredEvent(Actors::ActorBase* initiator, EventType eventType, std::uint8_t* eventParams) override;
		void BeginLevelChange(Actors::ActorBase* initiator, ExitType exitType, const StringView nextLevel = {}) override;
		void HandleGameOver(Actors::Player* player) override;
		bool HandlePlayerDied(Actors::Player* player) override;
		void HandlePlayerWarped(Actors::Player* player, const Vector2f& prevPos, WarpFlags flags) override;
		void HandlePlayerCoins(Actors::Player* player, std::int32_t prevCount, std::int32_t newCount) override;
		void HandlePlayerGems(Actors::Player* player, std::int32_t prevCount, std::int32_t newCount) override;
		void SetCheckpoint(Actors::Player* player, const Vector2f& pos) override;
		void RollbackToCheckpoint(Actors::Player* player) override;
		void ActivateSugarRush(Actors::Player* player) override;
		void ShowLevelText(const StringView text) override;
		StringView GetLevelText(std::uint32_t textId, std::int32_t index = -1, std::uint32_t delimiter = 0) override;
		void OverrideLevelText(std::uint32_t textId, const StringView value) override;
		void LimitCameraView(Actors::Player* player, std::int32_t left, std::int32_t width) override;
		void ShakeCameraView(Actors::Player* player, float duration) override;
		void ShakeCameraViewNear(Vector2f pos, float duration) override;
		bool GetTrigger(std::uint8_t triggerId) override;
		void SetTrigger(std::uint8_t triggerId, bool newState) override;
		void SetWeather(WeatherType type, std::uint8_t intensity) override;
		bool BeginPlayMusic(const StringView path, bool setDefault = false, bool forceReload = false) override;

		bool PlayerActionPressed(std::int32_t index, PlayerActions action, bool includeGamepads = true) override;
		bool PlayerActionPressed(std::int32_t index, PlayerActions action, bool includeGamepads, bool& isGamepad) override;
		bool PlayerActionHit(std::int32_t index, PlayerActions action, bool includeGamepads = true) override;
		bool PlayerActionHit(std::int32_t index, PlayerActions action, bool includeGamepads, bool& isGamepad) override;
		float PlayerHorizontalMovement(std::int32_t index) override;
		float PlayerVerticalMovement(std::int32_t index) override;
		void PlayerExecuteRumble(std::int32_t index, StringView rumbleEffect) override;

		bool SerializeResumableToStream(Stream& dest) override;

		void OnAdvanceDestructibleTileAnimation(std::int32_t tx, std::int32_t ty, std::int32_t amount) override { }
		void OnTileFrozen(std::int32_t x, std::int32_t y) override;

		Vector2i GetViewSize() const;

		virtual void AttachComponents(LevelDescriptor&& descriptor);
		virtual void SpawnPlayers(const LevelInitialization& levelInit);

	protected:
		struct PlayerInput {
			std::uint64_t PressedActions;
			std::uint64_t PressedActionsLast;
			Vector2f RequiredMovement;
			Vector2f FrozenMovement;
			bool Frozen;

			PlayerInput();
		};

		IRootController* _root;

		Shader* _lightingShader;
		Shader* _blurShader;
		Shader* _downsampleShader;
		Shader* _combineShader;
		Shader* _combineWithWaterShader;

		UI::UpscaleRenderPassWithClipping _upscalePass;

		std::unique_ptr<SceneNode> _rootNode;
		std::unique_ptr<Texture> _noiseTexture;
		SmallVector<std::unique_ptr<PlayerViewport>, 0> _assignedViewports;

#if defined(WITH_ANGELSCRIPT)
		std::unique_ptr<Scripting::LevelScriptLoader> _scripts;
#endif
		SmallVector<std::shared_ptr<Actors::ActorBase>, 0> _actors;
		SmallVector<Actors::Player*, LevelInitialization::MaxPlayerCount> _players;

		String _levelFileName;
		String _episodeName;
		String _defaultNextLevel;
		String _defaultSecretLevel;
		GameDifficulty _difficulty;
		WeatherType _weatherType;
		std::uint8_t _weatherIntensity;
		ExitType _nextLevelType;
		float _nextLevelTime;
		String _nextLevelName;
		String _musicDefaultPath, _musicCurrentPath;
		Recti _levelBounds;
		bool _isReforged, _cheatsUsed;
		bool _checkpointCreated;
		char _cheatsBuffer[9];
		std::uint32_t _cheatsBufferLength;
		SmallVector<String, 0> _levelTexts;

		Events::EventSpawner _eventSpawner;
		std::unique_ptr<Events::EventMap> _eventMap;
		std::unique_ptr<Tiles::TileMap> _tileMap;
		Collisions::DynamicTreeBroadPhase _collisions;

		Vector2i _viewSize;
		Rectf _viewBoundsTarget;
		float _elapsedFrames;
		float _checkpointFrames;
		float _waterLevel;
		Vector4f _defaultAmbientLight;
#if defined(WITH_AUDIO)
		std::unique_ptr<AudioStreamPlayer> _music;
		SmallVector<std::shared_ptr<AudioBufferPlayer>> _playingSounds;
		std::shared_ptr<AudioBufferPlayer> _sugarRushMusic;
#endif
		Metadata* _commonResources;
		std::unique_ptr<UI::HUD> _hud;
		std::shared_ptr<UI::Menu::InGameMenu> _pauseMenu;
		std::shared_ptr<Actors::Bosses::BossBase> _activeBoss;

		BitArray _pressedKeys;
		std::uint32_t _overrideActions;
		PlayerInput _playerInputs[UI::ControlScheme::MaxSupportedPlayers];

#if defined(NCINE_HAS_GAMEPAD_RUMBLE)
		RumbleProcessor _rumble;
		HashMap<String, std::shared_ptr<RumbleDescription>> _rumbleEffects;
#endif

		virtual void OnInitialized();
		virtual void BeforeActorDestroyed(Actors::ActorBase* actor);
		virtual void ProcessEvents(float timeMult);
		virtual void ProcessQueuedNextLevel();
		virtual void PrepareNextLevelInitialization(LevelInitialization& levelInit);

		Recti GetPlayerViewportBounds(std::int32_t w, std::int32_t h, std::int32_t index);
		void ProcessWeather(float timeMult);
		void ResolveCollisions(float timeMult);
		void AssignViewport(Actors::Player* player);
		void InitializeCamera(PlayerViewport& viewport);
		void UpdatePressedActions();
		void UpdateRichPresence();
		void InitializeRumbleEffects();
		RumbleDescription* RegisterRumbleEffect(StringView name);

		void PauseGame();
		void ResumeGame();
		
#if defined(WITH_IMGUI)
		ImVec2 WorldPosToScreenSpace(const Vector2f pos);
#endif
	};
}