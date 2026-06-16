#pragma once

#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../ActorBase.h"

namespace Jazz2::Actors::Multiplayer
{
	/**
		@brief Capture The Flag base

		Static visual structure marking a team's home base in @ref Jazz2::Multiplayer::MpGameMode::CaptureTheFlag.
		It is spawned by the server and remoted to clients; the carryable flag (see @ref Flag) rests on it and the
		capture logic lives in @ref Jazz2::Multiplayer::MpLevelHandler.
	*/
	class CtfBase : public ActorBase
	{
		DEATH_RUNTIME_OBJECT(ActorBase);

	public:
		CtfBase();

		/** @brief Returns the team this base belongs to */
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
