#pragma once

#include "../ActorBase.h"

namespace Jazz2::Actors::Environment
{
	class Spring : public ActorBase
	{
	public:
		Spring();

		bool KeepSpeedX, KeepSpeedY;

		bool OnHandleCollision(std::shared_ptr<ActorBase> other) override;

		static void Preload(const ActorActivationDetails& details)
		{
			PreloadMetadataAsync("Object/Spring"_s);
		}

		Vector2f Activate();

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		void OnUpdateHitbox() override;

	private:
		enum class State {
			Default,
			Frozen,
			Heated
		};

		uint8_t _type;
		uint8_t _orientation;
		float _strength;
		//uint16_t _delay;
		State _state;
		float _cooldown;
	};
}