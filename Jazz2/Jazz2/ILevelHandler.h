#pragma once

#include "ActorBase.h"
#include "LevelInitialization.h"
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
		static constexpr int MainPlaneZ = 500;
		static constexpr int SpritePlaneZ = MainPlaneZ + 10;
		static constexpr int PlayerZ = MainPlaneZ + 20;

		static constexpr float DefaultGravity = 0.3f;

		ILevelHandler() : Gravity(DefaultGravity) { }

		virtual Events::EventSpawner* EventSpawner() = 0;
		virtual Events::EventMap* EventMap() = 0;
		virtual Tiles::TileMap* TileMap() = 0;

		virtual GameDifficulty Difficulty() const = 0;
		virtual bool ReduxMode() const = 0;
		virtual Recti LevelBounds() const = 0;
		virtual float WaterLevel() const = 0;

		virtual const SmallVectorImpl<Actors::Player*>& GetPlayers() const = 0;

		virtual float GetAmbientLight() const = 0;
		virtual void SetAmbientLight(float value) = 0;

		virtual void AddActor(const std::shared_ptr<ActorBase>& actor) = 0;

		virtual const std::shared_ptr<AudioBufferPlayer>& PlaySfx(AudioBuffer* buffer, const Vector3f& pos, bool sourceRelative, float gain = 1.0f, float pitch = 1.0f) = 0;
		virtual const std::shared_ptr<AudioBufferPlayer>& PlayCommonSfx(const StringView& identifier, const Vector3f& pos, float gain = 1.0f, float pitch = 1.0f) = 0;
		virtual void WarpCameraToTarget(const std::shared_ptr<ActorBase>& actor) = 0;
		virtual bool IsPositionEmpty(ActorBase* self, const AABBf& aabb, TileCollisionParams& params, ActorBase** collider) = 0;

		bool IsPositionEmpty(ActorBase* self, const AABBf& aabb, TileCollisionParams& params)
		{
			ActorBase* collider;
			return IsPositionEmpty(self, aabb, params, &collider);
		}

		virtual void FindCollisionActorsByAABB(ActorBase* self, const AABBf& aabb, const std::function<bool(ActorBase*)>& callback) = 0;
		virtual void FindCollisionActorsByRadius(float x, float y, float radius, const std::function<bool(ActorBase*)>& callback) = 0;
		virtual void GetCollidingPlayers(const AABBf& aabb, const std::function<bool(ActorBase*)> callback) = 0;

		virtual void BeginLevelChange(ExitType exitType, const StringView& nextLevel) = 0;
		virtual void HandleGameOver() = 0;
		virtual bool HandlePlayerDied(const std::shared_ptr<ActorBase>& player) = 0;
		virtual void SetCheckpoint(Vector2f pos) = 0;
		virtual void RollbackToCheckpoint() = 0;
		virtual void ActivateSugarRush() = 0;
		virtual void ShowLevelText(const StringView& text) = 0;
		virtual void ShowCoins(int count) = 0;
		virtual void ShowGems(int count) = 0;
		virtual StringView GetLevelText(int textId, int index = -1, uint32_t delimiter = 0) = 0;
		virtual void LimitCameraView(float left, float width) = 0;
		virtual void ShakeCameraView(float duration) = 0;
		virtual void SetWaterLevel(float value) = 0;

		virtual bool PlayerActionPressed(int index, PlayerActions action, bool includeGamepads = true) = 0;
		virtual bool PlayerActionPressed(int index, PlayerActions action, bool includeGamepads, bool& isGamepad) = 0;
		virtual bool PlayerActionHit(int index, PlayerActions action, bool includeGamepads = true) = 0;
		virtual bool PlayerActionHit(int index, PlayerActions action, bool includeGamepads, bool& isGamepad) = 0;
		virtual float PlayerHorizontalMovement(int index) = 0;
		virtual float PlayerVerticalMovement(int index) = 0;
		virtual void PlayerFreezeMovement(int index, bool enable) = 0;

		float Gravity;
	};
}