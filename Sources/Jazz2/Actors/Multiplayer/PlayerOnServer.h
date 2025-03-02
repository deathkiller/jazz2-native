#pragma once

#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../Player.h"

namespace Jazz2::Actors::Multiplayer
{
	/** @brief Player in online session */
	class PlayerOnServer : public Player
	{
		DEATH_RUNTIME_OBJECT(Player);

	public:
		PlayerOnServer();

		bool OnHandleCollision(std::shared_ptr<ActorBase> other) override;

		/** @brief Returns team ID */
		std::uint8_t GetTeamId() const;
		/** @brief Sets team ID */
		void SetTeamId(std::uint8_t value);



	protected:
#ifndef DOXYGEN_GENERATING_OUTPUT
		std::uint8_t _teamId;
#endif
	};
}

#endif