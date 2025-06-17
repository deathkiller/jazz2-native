#pragma once

#include "../ActorBase.h"

namespace Jazz2::Actors::Environment
{
	/** @brief Ambient sound */
	class AmbientSound : public ActorBase
	{
		DEATH_RUNTIME_OBJECT(ActorBase);

	public:
		AmbientSound();
		~AmbientSound();

		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;

	private:
		std::shared_ptr<AudioBufferPlayer> _sound;
		std::uint8_t _sfx;
		float _gain;
	};
}