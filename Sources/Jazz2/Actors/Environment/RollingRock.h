#pragma once

#include "../Enemies/EnemyBase.h"

namespace Jazz2::Actors::Environment
{
	/**
	 * @brief Rolling rock
	 *
	 * Large boulder hazard that stays put until triggered, then rolls and bounces through the
	 * level under gravity. While rolling it crushes the player on contact and shoves them aside,
	 * and can collide with other rolling rocks.
	 */
	class RollingRock : public Enemies::EnemyBase
	{
		DEATH_RUNTIME_OBJECT(Enemies::EnemyBase);

	public:
		/** @brief Creates a new instance */
		RollingRock();

		/** @brief Preloads all assets required by this actor */
		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		void OnUpdateHitbox() override;
		bool OnHandleCollision(ActorBase* other) override;
		void OnTriggeredEvent(EventType eventType, std::uint8_t* eventParams) override;

	private:
		std::uint8_t _id;
		float _triggerSpeedX, _triggerSpeedY;
		float _delayLeft;
		bool _triggered;
		float  _soundCooldown;

		bool IsPositionEmpty(float x, float y);
	};
}