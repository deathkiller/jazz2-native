#pragma once

#include "../../ActorBase.h"

namespace Jazz2::Actors::Environment
{
	class AmbientSound : public ActorBase
	{
	public:
		AmbientSound();
		~AmbientSound();

		static void Preload(const ActorActivationDetails& details)
		{
			PreloadMetadataAsync("Common/AmbientSound"_s);
		}

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;

	private:
		std::shared_ptr<AudioBufferPlayer> _sound;
	};
}