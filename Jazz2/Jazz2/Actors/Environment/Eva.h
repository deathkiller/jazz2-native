#pragma once

#include "../ActorBase.h"

namespace Jazz2::Actors::Environment
{
	class Eva : public ActorBase
	{
	public:
		Eva();

		static void Preload(const ActorActivationDetails& details)
		{
			PreloadMetadataAsync("Object/Eva"_s);
		}

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		bool OnHandleCollision(std::shared_ptr<ActorBase> other) override;

	private:
		float _animationTime;
	};
}