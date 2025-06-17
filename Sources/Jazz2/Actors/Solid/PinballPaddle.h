#pragma once

#include "../SolidObjectBase.h"

namespace Jazz2::Actors::Solid
{
	/** @brief Pinball paddle */
	class PinballPaddle : public SolidObjectBase
	{
		DEATH_RUNTIME_OBJECT(SolidObjectBase);

	public:
		PinballPaddle();

		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		void OnUpdateHitbox() override;

	private:
		float _cooldown;
	};
}