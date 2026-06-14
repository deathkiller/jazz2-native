#pragma once

#include "../SolidObjectBase.h"

namespace Jazz2::Actors::Solid
{
	/**
	 * @brief Pinball paddle
	 *
	 * A flipper found in pinball-themed levels that, when the player lands on it, kicks them upward and away,
	 * launching them across the table and awarding score. The launch strength depends on where along the
	 * paddle the player landed.
	 */
	class PinballPaddle : public SolidObjectBase
	{
		DEATH_RUNTIME_OBJECT(SolidObjectBase);

	public:
		/** @brief Creates a new instance */
		PinballPaddle();

		/** @brief Preloads all assets required by this actor */
		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		void OnUpdateHitbox() override;

	private:
		float _cooldown;
	};
}