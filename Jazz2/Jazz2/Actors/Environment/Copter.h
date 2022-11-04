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
		enum class State {
			Free,
			Unmounted,
			Mounted
		};

		Vector2f _originPos;
		float _phase;
		State _state;
	};
}