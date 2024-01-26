#pragma once

#include "../ActorBase.h"

namespace Jazz2::Actors::Environment
{
	class Spring : public ActorBase
	{
		DEATH_RUNTIME_OBJECT(ActorBase);

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

		enum class Orientation : uint8_t {
			Bottom,
			Right,
			Top,
			Left
		};

		uint8_t _type;
		Orientation _orientation;
		float _strength;
		uint8_t _delay;
		State _state;
		float _cooldown;
	};
}