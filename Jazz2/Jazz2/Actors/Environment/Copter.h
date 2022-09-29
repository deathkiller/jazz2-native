#pragma once

#include "../ActorBase.h"

namespace Jazz2::Actors::Environment
{
	class Copter : public ActorBase
	{
	public:
		Copter();

		static void Preload(const ActorActivationDetails& details)
		{
			PreloadMetadataAsync("Enemy/LizardFloat"_s);
		}

		bool OnHandleCollision(std::shared_ptr<ActorBase> other);

		void Unmount(float timeLeft);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;

	private:
		static constexpr int StateFree = 0;
		static constexpr int StateMounted = 1;
		static constexpr int StateUnmounted = 2;

		Vector2f _originPos;
		float _phase;
		int _state;
	};
}