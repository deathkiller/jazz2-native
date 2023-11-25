#pragma once

#include "Actors/ActorBase.h"
#include "LevelInitialization.h"
#include "WeatherType.h"
#include "PlayerActions.h"

#include "../nCine/Audio/AudioBufferPlayer.h"

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

	class ILevelHandler
	{
	public:
		static constexpr std::int32_t MainPlaneZ = 500;
		static constexpr std::int32_t SpritePlaneZ = MainPlaneZ + 10;
		static constexpr std::int32_t PlayerZ = MainPlaneZ + 20;

		ILevelHandler() : Gravity(0.0f) { }

		virtual bool Initialize(const LevelInitialization& levelInit) = 0;
		virtual bool Initialize(Stream& src) = 0;

		virtual Events::EventSpawner* EventSpawner() = 0;
		virtual Events::EventMap* EventMap() = 0;
		virtual Tiles::TileMap* TileMap() = 0;

		virtual GameDifficulty Difficulty() const = 0;
		virtual bool IsPausable() const = 0;
		virtual bool IsReforged() const = 0;
		virtual Recti LevelBounds() const = 0;
		virtual float ElapsedFrames() const = 0;
		virtual float WaterLevel() const = 0;

		virtual const SmallVectorImpl<std::shared_ptr<Actors::ActorBase>>& GetActors() const = 0;
		virtual const SmallVectorImpl<Actors::Player*>& GetPlayers() const = 0;

		virtual Vector2f GetCameraPos() const = 0;
		virtual float GetAmbientLight() const = 0;
		virtual void SetAmbientLight(float value) = 0;

		virtual void AddActor(std::shared_ptr<Actors::ActorBase> actor) = 0;

		virtual std::shared_ptr<AudioBufferPlayer> PlaySfx(Actors::ActorBase* self, const StringView& identifier, AudioBuffer* buffer, const Vector3f& pos, bool sourceRelative, float gain = 1.0f, float pitch = 1.0f) = 0;
		virtual std::shared_ptr<AudioBufferPlayer> PlayCommonSfx(const StringView& identifier, const Vector3f& pos, float gain = 1.0f, float pitch = 1.0f) = 0;
		virtual void WarpCameraToTarget(Actors::ActorBase* actor, bool fast = false) = 0;
		virtual bool IsPositionEmpty(Actors::ActorBase* self, const AABBf& aabb, Tiles::TileCollisionParams& params, Actors::ActorBase** collider) = 0;

		bool IsPositionEmpty(Actors::ActorBase* self, const AABBf& aabb, Tiles::TileCollisionParams& params)
		{
			Actors::ActorBase* collider;
			return IsPositionEmpty(self, aabb, params, &collider);
		}

		virtual void FindCollisionActorsByAABB(Actors::ActorBase* self, const AABBf& aabb, const std::function<bool(Actors::ActorBase*)>& callback) = 0;
		virtual void FindCollisionActorsByRadius(float x, float y, float radius, const std::function<bool(Actors::ActorBase*)>& callback) = 0;
		virtual void GetCollidingPlayers(const AABBf& aabb, const std::function<bool(Actors::ActorBase*)> callback) = 0;

		virtual void BroadcastTriggeredEvent(Actors::ActorBase* initiator, EventType eventType, std::uint8_t* eventParams) = 0;
		virtual void BeginLevelChange(ExitType exitType, const StringView& nextLevel) = 0;
		virtual void HandleGameOver() = 0;
		virtual bool HandlePlayerDied(Actors::Player* player) = 0;
		virtual bool HandlePlayerFireWeapon(Actors::Player* player, WeaponType& weaponType, std::uint16_t& ammoDecrease) = 0;
		virtual bool HandlePlayerSpring(Actors::Player* player, const Vector2f& pos, const Vector2f& force, bool keepSpeedX, bool keepSpeedY) = 0;
		virtual void HandlePlayerWarped(Actors::Player* player, const Vector2f& prevPos, bool fast) = 0;
		virtual void SetCheckpoint(const Vector2f& pos) = 0;
		virtual void RollbackToCheckpoint() = 0;
		virtual void ActivateSugarRush() = 0;
		virtual void ShowLevelText(const StringView& text) = 0;
		virtual void ShowCoins(std::int32_t count) = 0;
		virtual void ShowGems(std::int32_t count) = 0;
		virtual StringView GetLevelText(std::uint32_t textId, std::int32_t index = -1, std::uint32_t delimiter = 0) = 0;
		virtual void OverrideLevelText(std::uint32_t textId, const StringView& value) = 0;
		virtual void LimitCameraView(int left, int width) = 0;
		virtual void ShakeCameraView(float duration) = 0;
		virtual bool GetTrigger(std::uint8_t triggerId) = 0;
		virtual void SetTrigger(std::uint8_t triggerId, bool newState) = 0;
		virtual void SetWeather(WeatherType type, std::uint8_t intensity) = 0;
		virtual bool BeginPlayMusic(const StringView& path, bool setDefault = false, bool forceReload = false) = 0;

		virtual bool PlayerActionPressed(std::int32_t index, PlayerActions action, bool includeGamepads = true) = 0;
		virtual bool PlayerActionPressed(std::int32_t index, PlayerActions action, bool includeGamepads, bool& isGamepad) = 0;
		virtual bool PlayerActionHit(std::int32_t index, PlayerActions action, bool includeGamepads = true) = 0;
		virtual bool PlayerActionHit(std::int32_t index, PlayerActions action, bool includeGamepads, bool& isGamepad) = 0;
		virtual float PlayerHorizontalMovement(std::int32_t index) = 0;
		virtual float PlayerVerticalMovement(std::int32_t index) = 0;

		float Gravity;
	};
}