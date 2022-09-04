#pragma once

#include "../ActorBase.h"

namespace Jazz2::Actors::Solid
{
	class Pole : public ActorBase
	{
	public:
		Pole();

		bool OnHandleCollision(std::shared_ptr<ActorBase> other);

		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;

	private:
		static constexpr int BouncesMax = 3;

		enum class FallDirection {
			None,
			Right,
			Left,
			Fallen
		};

		FallDirection _fall;
		float _angleVel;
		float _angleVelLast;
		float _fallTime;
		int _bouncesLeft = BouncesMax;

		void Fall(FallDirection dir);
		bool IsPositionBlocked();
	};
}