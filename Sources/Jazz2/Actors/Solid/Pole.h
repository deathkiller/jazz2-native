#pragma once

#include "../ActorBase.h"

namespace Jazz2::Actors::Solid
{
	class Pole : public ActorBase
	{
		DEATH_RUNTIME_OBJECT(ActorBase);

	public:
		enum class FallDirection {
			None,
			Right,
			Left,
			Fallen
		};

		Pole();

		bool OnHandleCollision(std::shared_ptr<ActorBase> other) override;

		FallDirection GetFallDirection() const {
			return _fall;
		}

		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;

	private:
		static constexpr int BouncesMax = 3;

		FallDirection _fall;
		float _angleVel;
		float _angleVelLast;
		float _fallTime;
		int _bouncesLeft = BouncesMax;

		void Fall(FallDirection dir);
		bool IsPositionBlocked();
	};
}