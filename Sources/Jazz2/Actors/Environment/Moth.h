#pragma once

#include "../ActorBase.h"

namespace Jazz2::Actors::Environment
{
	class Moth : public ActorBase
	{
		DEATH_RUNTIME_OBJECT(ActorBase);

	public:
		Moth();

		bool OnHandleCollision(std::shared_ptr<ActorBase> other) override;

		static void Preload(const ActorActivationDetails& details)
		{
			PreloadMetadataAsync("Object/Moth"_s);
		}

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;

	private:
		float _timer;
		int _direction;
	};
}