#pragma once

#include "../ActorBase.h"

namespace Jazz2::Actors::Environment
{
	class SteamNote : public ActorBase
	{
		DEATH_RUNTIME_OBJECT(ActorBase);

	public:
		SteamNote();

		static void Preload(const ActorActivationDetails& details)
		{
			PreloadMetadataAsync("Object/SteamNote"_s);
		}

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		void OnUpdateHitbox() override;
		void OnAnimationFinished() override;

	private:
		float _cooldown;
	};
}