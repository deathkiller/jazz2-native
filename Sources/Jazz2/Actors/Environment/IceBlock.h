#pragma once

#include "../SolidObjectBase.h"

namespace Jazz2::Actors::Environment
{
	class IceBlock : public SolidObjectBase
	{
	public:
		IceBlock();

		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult);
		bool OnHandleCollision(std::shared_ptr<ActorBase> other);

	private:
		float _timeLeft;
	};
}