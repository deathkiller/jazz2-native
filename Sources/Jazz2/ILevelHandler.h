#pragma once

#include "Actors/ActorBase.h"
#include "LevelInitialization.h"
#include "PlayerActions.h"
#include "WarpFlags.h"
#include "WeatherType.h"

#include "../nCine/Audio/AudioBufferPlayer.h"

#include <Base/TypeInfo.h>

namespace Death::IO
{
	class Stream;
}

using namespace Death::IO;

namespace Jazz2
{
	namespace Events
	{
		class EventSpawner;
		class EventMap;
	}

	namespace Tiles
	{
		class TileMap;
	}

	namespace Actors
	{
		class Player;
	}

	/** @brief Base interface of a level handler */
	class ILevelHandler
	{
		DEATH_RUNTIME_OBJECT();

	public:
		/** @brief Layer of the main plane */
		static constexpr std::int32_t MainPlaneZ = 500;
		/** @brief Layer of sprites */
		static constexpr std::int32_t SpritePlaneZ = MainPlaneZ + 10;
		/** @brief Layer of players */
		static constexpr std::int32_t PlayerZ = MainPlaneZ + 20;

		/** @brief Initializes the level handler from @ref LevelInitialization */
		virtual bool Initialize(const LevelInitialization& levelInit) = 0;
		/** @brief Initializes the level handler from resumable state */
		virtual bool Initialize(Stream& src, std::uint16_t version) = 0;

		/** @brief Returns event spawner for the level */
		virtual Events::EventSpawner* EventSpawner() = 0;
		/** @brief Returns event map for the level */
		virtual Events::EventMap* EventMap() = 0;
		/** @brief Returns tile map for the level */
		virtual Tiles::TileMap* TileMap() = 0;

		/** @brief Return current difficulty */
		virtual GameDifficulty Difficulty() const = 0;
		/** @brief Return `true` if the level handler is on a local session */
		virtual bool IsLocalSession() const = 0;
		/** @brief Return `true` if the level handler is pausable */
		virtual bool IsPausable() const = 0;
		/** @brief Returns `true` if Reforged Gameplay is enabled */
		virtual bool IsReforged() const = 0;
		/** @brief Returns `true` if players can collide with each other */
		virtual bool CanPlayersCollide() const = 0;
		/** @brief Returns level bounds including camera limits */
		virtual Recti LevelBounds() const = 0;
		/** @brief Returns number of elapsed frames */
		virtual float ElapsedFrames() const = 0;
		/** @brief Returns current gravity force */
		virtual float Gravity() const = 0;
		/** @brief Returns current water level */
		virtual float WaterLevel() const = 0;

		/** @brief Returns list of actors (objects) */
		virtual ArrayView<const std::shared_ptr<Actors::ActorBase>> GetActors() const = 0;
		/** @brief Returns list of players */
		virtual ArrayView<Actors::Player* const> GetPlayers() const = 0;

		/** @brief Returns default ambient light intensity */
		virtual float GetDefaultAmbientLight() const = 0;
		/** @brief Returns current ambient light intensity */
		virtual float GetAmbientLight(Actors::Player* player) const = 0;
		/** @brief Sets current ambient light intensity */
		virtual void SetAmbientLight(Actors::Player* player, float value) = 0;

		/** @brief Adds an actor (object) to the level */
		virtual void AddActor(std::shared_ptr<Actors::ActorBase> actor) = 0;

		/** @brief Plays a sound effect for a given actor (object) */
		virtual std::shared_ptr<AudioBufferPlayer> PlaySfx(Actors::ActorBase* self, StringView identifier, AudioBuffer* buffer, const Vector3f& pos, bool sourceRelative, float gain = 1.0f, float pitch = 1.0f) = 0;
		/** @brief Plays a common sound effect */
		virtual std::shared_ptr<AudioBufferPlayer> PlayCommonSfx(StringView identifier, const Vector3f& pos, float gain = 1.0f, float pitch = 1.0f) = 0;
		/** @brief Warps a camera to its assigned target */
		virtual void WarpCameraToTarget(Actors::ActorBase* actor, bool fast = false) = 0;
		/** @brief Returns `true` if a specified AABB is empty */
		virtual bool IsPositionEmpty(Actors::ActorBase* self, const AABBf& aabb, Tiles::TileCollisionParams& params, Actors::ActorBase** collider) = 0;

		/** @overload */
		bool IsPositionEmpty(Actors::ActorBase* self, const AABBf& aabb, Tiles::TileCollisionParams& params)
		{
			Actors::ActorBase* collider;
			return IsPositionEmpty(self, aabb, params, &collider);
		}

		/** @brief Calls the callback function for all colliding objects with specified AABB */
		virtual void FindCollisionActorsByAABB(const Actors::ActorBase* self, const AABBf& aabb, Function<bool(Actors::ActorBase*)>&& callback) = 0;
		/** @brief Calls the callback function for all colliding objects with specified circle */
		virtual void FindCollisionActorsByRadius(float x, float y, float radius, Function<bool(Actors::ActorBase*)>&& callback) = 0;
		/** @brief Calls the callback function for all colliding players with specified AABB */
		virtual void GetCollidingPlayers(const AABBf& aabb, Function<bool(Actors::ActorBase*)>&& callback) = 0;

		virtual void BroadcastTriggeredEvent(Actors::ActorBase* initiator, EventType eventType, std::uint8_t* eventParams) = 0;
		virtual void BeginLevelChange(Actors::ActorBase* initiator, ExitType exitType, StringView nextLevel = {}) = 0;
		virtual void HandleGameOver(Actors::Player* player) = 0;
		virtual bool HandlePlayerDied(Actors::Player* player, Actors::ActorBase* collider) = 0;
		virtual void HandlePlayerWarped(Actors::Player* player, Vector2f prevPos, WarpFlags flags) = 0;
		virtual void HandlePlayerCoins(Actors::Player* player, std::int32_t prevCount, std::int32_t newCount) = 0;
		virtual void HandlePlayerGems(Actors::Player* player, std::uint8_t gemType, std::int32_t prevCount, std::int32_t newCount) = 0;
		/** @brief Sets checkpoint for a given player */
		virtual void SetCheckpoint(Actors::Player* player, Vector2f pos) = 0;
		/** @brief Rolls back to the last checkpoint for a given player */
		virtual void RollbackToCheckpoint(Actors::Player* player) = 0;
		virtual void HandleActivateSugarRush(Actors::Player* player) = 0;
		/** @brief Shows a text notification */
		virtual void ShowLevelText(StringView text, Actors::ActorBase* initiator = nullptr) = 0;
		/** @brief Returns a level text */
		virtual StringView GetLevelText(std::uint32_t textId, std::int32_t index = -1, std::uint32_t delimiter = 0) = 0;
		/** @brief Override specified level text */
		virtual void OverrideLevelText(std::uint32_t textId, StringView value) = 0;
		/** @brief Returns camera position of a given player */
		virtual Vector2f GetCameraPos(Actors::Player* player) const = 0;
		/** @brief Limits camera viewport for a given player */
		virtual void LimitCameraView(Actors::Player* player, std::int32_t left, std::int32_t width) = 0;
		/** @brief Override camera viewport for a given player */
		virtual void OverrideCameraView(Actors::Player* player, float x, float y, bool topLeft = false) = 0;
		/** @brief Shake camera for a given player */
		virtual void ShakeCameraView(Actors::Player* player, float duration) = 0;
		/** @brief Shake camera for all players near a given position */
		virtual void ShakeCameraViewNear(Vector2f pos, float duration) = 0;
		/** @brief Returns state of a given trigger in the tile map */
		virtual bool GetTrigger(std::uint8_t triggerId) = 0;
		/** @brief Sets state of a given trigger in the tile map */
		virtual void SetTrigger(std::uint8_t triggerId, bool newState) = 0;
		/** @brief Sets current level weather */
		virtual void SetWeather(WeatherType type, std::uint8_t intensity) = 0;
		/** @brief Plays specified music */
		virtual bool BeginPlayMusic(StringView path, bool setDefault = false, bool forceReload = false) = 0;

		virtual bool PlayerActionPressed(std::int32_t index, PlayerActions action, bool includeGamepads = true) = 0;
		virtual bool PlayerActionPressed(std::int32_t index, PlayerActions action, bool includeGamepads, bool& isGamepad) = 0;
		virtual bool PlayerActionHit(std::int32_t index, PlayerActions action, bool includeGamepads = true) = 0;
		virtual bool PlayerActionHit(std::int32_t index, PlayerActions action, bool includeGamepads, bool& isGamepad) = 0;
		virtual float PlayerHorizontalMovement(std::int32_t index) = 0;
		virtual float PlayerVerticalMovement(std::int32_t index) = 0;
		virtual void PlayerExecuteRumble(std::int32_t index, StringView rumbleEffect) = 0;
	};
}