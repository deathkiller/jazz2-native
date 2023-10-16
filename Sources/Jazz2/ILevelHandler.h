#pragma once

#include "Actors/ActorBase.h"
#include "LevelInitialization.h"
#include "WeatherType.h"
#include "PlayerActions.h"

#include "../nCine/Audio/AudioBufferPlayer.h"

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

	class ILevelHandler
	{
	public:
		static constexpr int32_t MainPlaneZ = 500;
		static constexpr int32_t SpritePlaneZ = MainPlaneZ + 10;
		static constexpr int32_t PlayerZ = MainPlaneZ + 20;

		ILevelHandler() : Gravity(0.0f) { }

		virtual Events::EventSpawner* EventSpawner() = 0;
		virtual Events::EventMap* EventMap() = 0;
		virtual Tiles::TileMap* TileMap() = 0;

		virtual GameDifficulty Difficulty() const = 0;
		virtual bool IsReforged() const = 0;
		virtual Recti LevelBounds() const = 0;
		virtual float ElapsedFrames() const = 0;
		virtual float WaterLevel() const = 0;

		virtual const SmallVectorImpl<std::shared_ptr<Actors::ActorBase>>& GetActors() const = 0;
		virtual const SmallVectorImpl<Actors::Player*>& GetPlayers() const = 0;

		virtual float GetAmbientLight() const = 0;
		virtual void SetAmbientLight(float value) = 0;

		virtual void AddActor(std::shared_ptr<Actors::ActorBase> actor) = 0;

		virtual std::shared_ptr<AudioBufferPlayer> PlaySfx(AudioBuffer* buffer, const Vector3f& pos, bool sourceRelative, float gain = 1.0f, float pitch = 1.0f) = 0;
		virtual std::shared_ptr<AudioBufferPlayer> PlayCommonSfx(const StringView& identifier, const Vector3f& pos, float gain = 1.0f, float pitch = 1.0f) = 0;
		virtual void WarpCameraToTarget(const std::shared_ptr<Actors::ActorBase>& actor, bool fast = false) = 0;
		virtual bool IsPositionEmpty(Actors::ActorBase* self, const AABBf& aabb, TileCollisionParams& params, Actors::ActorBase** collider) = 0;

		bool IsPositionEmpty(Actors::ActorBase* self, const AABBf& aabb, TileCollisionParams& params)
		{
			Actors::ActorBase* collider;
			return IsPositionEmpty(self, aabb, params, &collider);
		}

		virtual void FindCollisionActorsByAABB(Actors::ActorBase* self, const AABBf& aabb, const std::function<bool(Actors::ActorBase*)>& callback) = 0;
		virtual void FindCollisionActorsByRadius(float x, float y, float radius, const std::function<bool(Actors::ActorBase*)>& callback) = 0;
		virtual void GetCollidingPlayers(const AABBf& aabb, const std::function<bool(Actors::ActorBase*)> callback) = 0;

		virtual void BroadcastTriggeredEvent(Actors::ActorBase* initiator, EventType eventType, uint8_t* eventParams) = 0;
		virtual void BeginLevelChange(ExitType exitType, const StringView& nextLevel) = 0;
		virtual void HandleGameOver() = 0;
		virtual bool HandlePlayerDied(const std::shared_ptr<Actors::ActorBase>& player) = 0;
		virtual void HandlePlayerWarped(const std::shared_ptr<Actors::ActorBase>& player, const Vector2f& prevPos, bool fast) = 0;
		virtual void SetCheckpoint(Vector2f pos) = 0;
		virtual void RollbackToCheckpoint() = 0;
		virtual void ActivateSugarRush() = 0;
		virtual void ShowLevelText(const StringView& text) = 0;
		virtual void ShowCoins(int32_t count) = 0;
		virtual void ShowGems(int32_t count) = 0;
		virtual StringView GetLevelText(uint32_t textId, int32_t index = -1, uint32_t delimiter = 0) = 0;
		virtual void OverrideLevelText(uint32_t textId, const StringView& value) = 0;
		virtual void LimitCameraView(int left, int width) = 0;
		virtual void ShakeCameraView(float duration) = 0;
		virtual void SetWeather(WeatherType type, uint8_t intensity) = 0;
		virtual bool BeginPlayMusic(const StringView& path, bool setDefault = false, bool forceReload = false) = 0;

		virtual bool PlayerActionPressed(int32_t index, PlayerActions action, bool includeGamepads = true) = 0;
		virtual bool PlayerActionPressed(int32_t index, PlayerActions action, bool includeGamepads, bool& isGamepad) = 0;
		virtual bool PlayerActionHit(int32_t index, PlayerActions action, bool includeGamepads = true) = 0;
		virtual bool PlayerActionHit(int32_t index, PlayerActions action, bool includeGamepads, bool& isGamepad) = 0;
		virtual float PlayerHorizontalMovement(int32_t index) = 0;
		virtual float PlayerVerticalMovement(int32_t index) = 0;

		float Gravity;
	};
}