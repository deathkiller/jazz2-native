#pragma once

#include "../Enemies/EnemyBase.h"

namespace Jazz2::Actors::Environment
{
	class RollingRock : public Enemies::EnemyBase
	{
	public:
		RollingRock();

		static void Preload(const ActorActivationDetails& details)
		{
			PreloadMetadataAsync("Object/RollingRock"_s);
		}

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		void OnUpdateHitbox() override;
		void OnTriggeredEvent(EventType eventType, uint8_t* eventParams) override;

	private:
		enum class SpeedUpDirection {
			None,
			Left,
			Right
		};

		uint8_t _id;
		float _triggerSpeedX, _triggerSpeedY;
		float _delayLeft;
		bool _triggered;
		SpeedUpDirection _speedUpDirection;
		float _speedUpDirectionCooldown, _soundCooldown;
	};
}