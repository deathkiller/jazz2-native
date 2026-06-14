#pragma once

#include "../ActorBase.h"

namespace Jazz2::Actors::Environment
{
	/**
	 * @brief Ambient sound
	 *
	 * Invisible level object that plays a looping background sound effect (such as wind,
	 * fire, or machinery noise) to add atmosphere to a region. It has no collision and
	 * does not interact with the player.
	 */
	class AmbientSound : public ActorBase
	{
		DEATH_RUNTIME_OBJECT(ActorBase);

	public:
		/** @brief Creates a new instance */
		AmbientSound();
		~AmbientSound();

		/** @brief Preloads all assets required by this actor */
		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;

	private:
		std::shared_ptr<AudioBufferPlayer> _sound;
		std::uint8_t _sfx;
		float _gain;
	};
}