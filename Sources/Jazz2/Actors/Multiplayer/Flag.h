#pragma once

#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../ActorBase.h"

namespace Jazz2::Actors::Multiplayer
{
	/**
		@brief Capture The Flag flag/base marker

		Purely visual actor used in @ref Jazz2::Multiplayer::MpGameMode::CaptureTheFlag to represent a team's
		flag. It is spawned and positioned by the server (sitting at the home base, following its carrier, or
		lying where it was dropped) and remoted to clients like any other actor; all capture logic lives in
		@ref Jazz2::Multiplayer::MpLevelHandler.
	*/
	class Flag : public ActorBase
	{
		DEATH_RUNTIME_OBJECT(ActorBase);

	public:
		Flag();

		/** @brief Returns the team this flag belongs to */
		std::uint8_t GetTeam() const {
			return _team;
		}

		/** @brief Preloads all assets required by this actor */
		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;

	private:
		std::uint8_t _team;
	};
}

#endif
