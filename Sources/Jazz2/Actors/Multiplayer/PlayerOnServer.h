#pragma once

#if defined(WITH_MULTIPLAYER)

#include "../Player.h"

namespace Jazz2::Actors::Multiplayer
{
	class PlayerOnServer : public Player
	{
		DEATH_RUNTIME_OBJECT(Player);

	public:
		PlayerOnServer();

		bool OnHandleCollision(std::shared_ptr<ActorBase> other) override;

		std::uint8_t GetTeamId() const;
		void SetTeamId(std::uint8_t value);

	protected:
		std::uint8_t _teamId;
	};
}

#endif