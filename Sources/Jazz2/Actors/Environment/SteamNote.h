#pragma once

#include "../ActorBase.h"

namespace Jazz2::Actors::Environment
{
	/**
		@brief Steam note
		
		Decorative musical note that periodically puffs into view with a sound effect and then
		fades out, repeating on a cycle. It is an ambient effect and does not interact with the
		player.
	*/
	class SteamNote : public ActorBase
	{
		DEATH_RUNTIME_OBJECT(ActorBase);

	public:
		/** @brief Creates a new instance */
		SteamNote();

		/** @brief Preloads all assets required by this actor */
		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		void OnUpdateHitbox() override;
		void OnAnimationFinished() override;

	private:
		float _cooldown;
	};
}