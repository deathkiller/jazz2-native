#pragma once

#include "../Enemies/EnemyBase.h"

namespace Jazz2::Actors::Environment
{
	/**
		@brief Rolling rock
		
		Large boulder hazard that shakes when triggered, then rolls and bounces through the level
		under gravity until it slows down. While rolling fast it hurts the player on contact. The player
		can stand on its round top and push it from the side, and it pushes other rocks apart.
	*/
	class RollingRock : public Enemies::EnemyBase
	{
		DEATH_RUNTIME_OBJECT(Enemies::EnemyBase);

	public:
		/** @brief Creates a new instance */
		RollingRock();
		~RollingRock();

		/** @brief Preloads all assets required by this actor */
		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		void OnUpdateHitbox() override;
		void OnTriggeredEvent(EventType eventType, std::uint8_t* eventParams) override;
		void OnSerializeState(Stream& dest) override;
		void OnDeserializeState(Stream& src) override;

	private:
		enum class RockState : std::uint8_t {
			Idle,
			Shaking,
			Rolling,
			Done
		};

		Vector2f _speedPerTick;
		float _triggerSpeedX, _triggerSpeedY;
		float _accelerationX;
		float _time;
		float _tickTime;
		float _spinBoost;
		std::uint8_t _id;
		RockState _rockState;

		void AdvanceTick();
		void StartRolling(float speedX, float speedY, float accelerationX);
		void Roll();
		void HitOtherRocks();
		void HandlePlayerContact();
		void PushBySide(Player* player, float dx);
		void ShakeNearbyCameras(float falloff, float maxTicks);
		bool IsPositionEmpty(float x, float y, bool downwards);
		// Whether the rock's side towards `dir` (-1 or 1) is free
		bool IsSideEmpty(float dir, bool downwards);
	};
}