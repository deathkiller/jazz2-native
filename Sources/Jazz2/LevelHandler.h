#pragma once

#include "ILevelHandler.h"
#include "IResumable.h"
#include "IStateHandler.h"
#include "IRootController.h"
#include "LevelDescriptor.h"
#include "WeatherType.h"
#include "Events/EventMap.h"
#include "Events/EventSpawner.h"
#include "Tiles/ITileMapOwner.h"
#include "Tiles/TileMap.h"
#include "Collisions/DynamicTreeBroadPhase.h"
#include "Input/RumbleProcessor.h"
#include "Input/ControlScheme.h"
#include "Rendering/UpscaleRenderPass.h"

#include "../nCine/Graphics/Shader.h"
#include "../nCine/Audio/AudioBufferPlayer.h"
#include "../nCine/Audio/AudioStreamPlayer.h"

#if defined(WITH_IMGUI)
#	include <imgui.h>
#endif

using namespace Jazz2::Input;

namespace Jazz2
{
	namespace Actors
	{
		class Player;
	}

	namespace Actors::Bosses
	{
		class BossBase;
	}

	namespace Rendering
	{
		class LightingRenderer;
		class BlurRenderPass;
		class CombineRenderer;
		class PlayerViewport;
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
		class InGameConsole;
	}

	namespace UI::Menu
	{
		class InGameMenu;
	}

	/** @brief Level handler of a local game session */
	class LevelHandler : public ILevelHandler, public IStateHandler, public IResumable, public Tiles::ITileMapOwner
	{
		DEATH_RUNTIME_OBJECT(ILevelHandler, IStateHandler);

		friend class Rendering::LightingRenderer;
		friend class Rendering::BlurRenderPass;
		friend class Rendering::CombineRenderer;
		friend class Rendering::PlayerViewport;
#if defined(WITH_ANGELSCRIPT)
		friend class Scripting::LevelScriptLoader;
#endif
		friend class UI::HUD;
		friend class UI::Menu::InGameMenu;

	public:
		/** @{ @name Constants */

		/** @brief Default width of viewport */
		static constexpr std::int32_t DefaultWidth = 720;
		/** @brief Default height of viewport */
		static constexpr std::int32_t DefaultHeight = 405;
		/** @brief Range of tile activation */
		static constexpr std::int32_t ActivateTileRange = 26;

		/** @} */

		LevelHandler(IRootController* root);
		~LevelHandler() override;

		bool Initialize(const LevelInitialization& levelInit) override;
		bool Initialize(Stream& src, std::uint16_t version) override;

		Events::EventSpawner* EventSpawner() override;
		Events::EventMap* EventMap() override;
		Tiles::TileMap* TileMap() override;

		GameDifficulty GetDifficulty() const override;
		bool IsLocalSession() const override;
		bool IsServer() const override;
		bool IsPausable() const override;
		bool IsReforged() const override;
		bool CanActivateSugarRush() const override;
		bool CanEventDisappear(EventType eventType) const override;
		bool CanPlayersCollide() const override;
		Recti GetLevelBounds() const override;
		float GetElapsedFrames() const override;
		float GetGravity() const override;
		float GetWaterLevel() const override;
		float GetHurtInvulnerableTime() const override;

		ArrayView<const std::shared_ptr<Actors::ActorBase>> GetActors() const override;
		ArrayView<Actors::Player* const> GetPlayers() const override;

		float GetDefaultAmbientLight() const override;
		float GetAmbientLight(Actors::Player* player) const override;
		void SetAmbientLight(Actors::Player* player, float value) override;

		Vector2i GetViewSize() const override;

		void OnBeginFrame() override;
		void OnEndFrame() override;
		void OnInitializeViewport(std::int32_t width, std::int32_t height) override;
		/** @brief Called when a console command is entered */
		virtual bool OnConsoleCommand(StringView line);

		void OnKeyPressed(const KeyboardEvent& event) override;
		void OnKeyReleased(const KeyboardEvent& event) override;
		void OnTextInput(const TextInputEvent& event) override;
		void OnTouchEvent(const TouchEvent& event) override;

		void AddActor(std::shared_ptr<Actors::ActorBase> actor) override;

		std::shared_ptr<AudioBufferPlayer> PlaySfx(Actors::ActorBase* self, StringView identifier, AudioBuffer* buffer, const Vector3f& pos, bool sourceRelative, float gain, float pitch) override;
		std::shared_ptr<AudioBufferPlayer> PlayCommonSfx(StringView identifier, const Vector3f& pos, float gain = 1.0f, float pitch = 1.0f) override;
		void WarpCameraToTarget(Actors::ActorBase* actor, bool fast = false) override;
		bool IsPositionEmpty(Actors::ActorBase* self, const AABBf& aabb, Tiles::TileCollisionParams& params, Actors::ActorBase** collider) override;
		void FindCollisionActorsByAABB(const Actors::ActorBase* self, const AABBf& aabb, Function<bool(Actors::ActorBase*)>&& callback) override;
		void FindCollisionActorsByRadius(float x, float y, float radius, Function<bool(Actors::ActorBase*)>&& callback) override;
		void GetCollidingPlayers(const AABBf& aabb, Function<bool(Actors::ActorBase*)>&& callback) override;

		void BroadcastTriggeredEvent(Actors::ActorBase* initiator, EventType eventType, std::uint8_t* eventParams) override;
		void BeginLevelChange(Actors::ActorBase* initiator, ExitType exitType, StringView nextLevel = {}) override;

		void SendPacket(const Actors::ActorBase* self, ArrayView<const std::uint8_t> data) override;

		void HandleBossActivated(Actors::Bosses::BossBase* boss, Actors::ActorBase* initiator = nullptr) override;
		void HandleLevelChange(LevelInitialization&& levelInit) override;
		void HandleGameOver(Actors::Player* player) override;
		bool HandlePlayerDied(Actors::Player* player) override;
		void HandlePlayerWarped(Actors::Player* player, Vector2f prevPos, WarpFlags flags) override;
		void HandlePlayerCoins(Actors::Player* player, std::int32_t prevCount, std::int32_t newCount) override;
		void HandlePlayerGems(Actors::Player* player, std::uint8_t gemType, std::int32_t prevCount, std::int32_t newCount) override;
		void SetCheckpoint(Actors::Player* player, Vector2f pos) override;
		void RollbackToCheckpoint(Actors::Player* player) override;
		void HandleActivateSugarRush(Actors::Player* player) override;
		void HandleCreateParticleDebrisOnPerish(const Actors::ActorBase* self, Actors::ParticleDebrisEffect effect, Vector2f speed) override;
		void HandleCreateSpriteDebris(const Actors::ActorBase* self, AnimState state, std::int32_t count) override;
		void ShowLevelText(StringView text, Actors::ActorBase* initiator = nullptr) override;
		StringView GetLevelText(std::uint32_t textId, std::int32_t index = -1, std::uint32_t delimiter = 0) override;
		void OverrideLevelText(std::uint32_t textId, StringView value) override;
		Vector2f GetCameraPos(Actors::Player* player) const override;
		void LimitCameraView(Actors::Player* player, Vector2f playerPos, std::int32_t left, std::int32_t width) override;
		void OverrideCameraView(Actors::Player* player, float x, float y, bool topLeft = false) override;
		void ShakeCameraView(Actors::Player* player, float duration) override;
		void ShakeCameraViewNear(Vector2f pos, float duration) override;
		bool GetTrigger(std::uint8_t triggerId) override;
		void SetTrigger(std::uint8_t triggerId, bool newState) override;
		void SetWeather(WeatherType type, std::uint8_t intensity) override;
		bool BeginPlayMusic(StringView path, bool setDefault = false, bool forceReload = false) override;

		bool PlayerActionPressed(Actors::Player* player, PlayerAction action, bool includeGamepads = true) override;
		bool PlayerActionPressed(Actors::Player* player, PlayerAction action, bool includeGamepads, bool& isGamepad) override;
		bool PlayerActionHit(Actors::Player* player, PlayerAction action, bool includeGamepads = true) override;
		bool PlayerActionHit(Actors::Player* player, PlayerAction action, bool includeGamepads, bool& isGamepad) override;
		float PlayerHorizontalMovement(Actors::Player* player) override;
		float PlayerVerticalMovement(Actors::Player* player) override;
		void PlayerExecuteRumble(Actors::Player* player, StringView rumbleEffect) override;

		bool SerializeResumableToStream(Stream& dest) override;

		void OnAdvanceDestructibleTileAnimation(std::int32_t tx, std::int32_t ty, std::int32_t amount) override {}
		void OnTileFrozen(std::int32_t x, std::int32_t y) override;

	protected:
		/** @brief Describes current input state of a player */
		struct PlayerInput {
			std::uint64_t PressedActions;
			std::uint64_t PressedActionsLast;
			Vector2f RequiredMovement;
			Vector2f FrozenMovement;
			bool Frozen;

			PlayerInput();
		};

#ifndef DOXYGEN_GENERATING_OUTPUT
		// Hide these members from documentation before refactoring
		IRootController* _root;

		Shader* _lightingShader;
		Shader* _blurShader;
		Shader* _downsampleShader;
		Shader* _combineShader;
		Shader* _combineWithWaterShader;

		Rendering::UpscaleRenderPassWithClipping _upscalePass;

		std::unique_ptr<SceneNode> _rootNode;
		std::unique_ptr<Texture> _noiseTexture;
		SmallVector<std::unique_ptr<Rendering::PlayerViewport>, 0> _assignedViewports;

#if defined(WITH_ANGELSCRIPT)
		std::unique_ptr<Scripting::LevelScriptLoader> _scripts;
#endif
		SmallVector<std::shared_ptr<Actors::ActorBase>, 0> _actors;
		SmallVector<Actors::Player*, LevelInitialization::MaxPlayerCount> _players;

		String _levelName;
		String _levelDisplayName;
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
		SmallVector<String, 0> _levelTexts;

		Events::EventSpawner _eventSpawner;
		std::unique_ptr<Events::EventMap> _eventMap;
		std::unique_ptr<Tiles::TileMap> _tileMap;
		Collisions::DynamicTreeBroadPhase _collisions;

		Vector2i _viewSize;
		Rectf _viewBoundsTarget;
		std::int64_t _elapsedMillisecondsBegin;
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
		std::unique_ptr<UI::InGameConsole> _console;
		std::shared_ptr<UI::Menu::InGameMenu> _pauseMenu;
		std::shared_ptr<Actors::Bosses::BossBase> _activeBoss;

		BitArray _pressedKeys;
		std::uint32_t _overrideActions;
		PlayerInput _playerInputs[ControlScheme::MaxSupportedPlayers];

#if defined(NCINE_HAS_GAMEPAD_RUMBLE)
		RumbleProcessor _rumble;
		HashMap<String, std::shared_ptr<RumbleDescription>> _rumbleEffects;
#endif
#endif

		/** @brief Invokes the specified callback asynchronously, usually at the end of current frame */
		void InvokeAsync(Function<void()>&& callback);

		/** @brief Attaches all required level components to the handler */
		virtual void AttachComponents(LevelDescriptor&& descriptor);
		/** @brief Creates HUD */
		virtual std::unique_ptr<UI::HUD> CreateHUD();
		/** @brief Spawns all players */
		virtual void SpawnPlayers(const LevelInitialization& levelInit);
		/** @brief Returns `true` if cheats are enabled */
		virtual bool IsCheatingAllowed();

		/** @brief Called after the level is loaded and all players were spawned */
		virtual void OnInitialized();
		/** @brief Called before an actor (object) is destroyed */
		virtual void BeforeActorDestroyed(Actors::ActorBase* actor);
		/** @brief Processes events */
		virtual void ProcessEvents(float timeMult);
		/** @brief Processes transition to the next level if queued */
		virtual void ProcessQueuedNextLevel();
		/** @brief Prepares @ref LevelInitialization for transition to the next level */
		virtual void PrepareNextLevelInitialization(LevelInitialization& levelInit);

		/** @brief Returns player viewport bounds */
		Recti GetPlayerViewportBounds(std::int32_t w, std::int32_t h, std::int32_t index);
		/** @brief Processes weather */
		void ProcessWeather(float timeMult);
		/** @brief Resolves collisions */
		void ResolveCollisions(float timeMult);
		/** @brief Assigns viewport */
		void AssignViewport(Actors::Player* player);
		/** @brief Unassigns viewport */
		void UnassignViewport(Actors::Player* player);
		/** @brief Commits changes in assigned viewports */
		void CommitViewports();
		/** @brief Initializes camera for specified viewport */
		void InitializeCamera(Rendering::PlayerViewport& viewport);
		/** @brief Updates pressed actions */
		void UpdatePressedActions();
		/** @brief Updates rich presence */
		void UpdateRichPresence();
		/** @brief Initializes common rumble effects */
		void InitializeRumbleEffects();
		/** @brief Registers a rumple effect */
		RumbleDescription* RegisterRumbleEffect(StringView name);

		/** @brief Pauses the game */
		virtual void PauseGame();
		/** @brief Resumes the paused game */
		virtual void ResumeGame();
		/** @brief Shows the in-game console */
		virtual void ShowConsole();
		/** @brief Hides the in-game console */
		virtual void HideConsole();
		
#if defined(WITH_IMGUI)
		ImVec2 WorldPosToScreenSpace(const Vector2f pos);
#endif

	private:
		bool CheatKill();
		bool CheatGod();
		bool CheatNext();
		bool CheatGuns();
		bool CheatRush();
		bool CheatGems();
		bool CheatBird();
		bool CheatLife();
		bool CheatPower();
		bool CheatCoins();
		bool CheatMorph();
		bool CheatShield();
	};
}