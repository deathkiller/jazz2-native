#pragma once

#include "../../ActorBase.h"

namespace Jazz2::Actors::Environment
{
	class Moth : public ActorBase
	{
	public:
		Moth();

		static void Preload(const ActorActivationDetails& details)
		{
			PreloadMetadataAsync("Object/Moth"_s);
		}

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		bool OnHandleCollision(std::shared_ptr<ActorBase> other) override;

	private:
		float _timer;
		int _direction;
	};
}