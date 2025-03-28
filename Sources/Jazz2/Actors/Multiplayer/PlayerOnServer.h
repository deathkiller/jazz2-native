#pragma once

#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../Player.h"

namespace Jazz2::Multiplayer
{
	class MpLevelHandler;
}

namespace Jazz2::Actors::Multiplayer
{
	/** @brief Interface that allows to access various player statistics */
	class IPlayerStatsProvider
	{
		DEATH_RUNTIME_OBJECT();

	public:
		/** @brief Deaths of the player */
		std::uint32_t Deaths;
		/** @brief Kills of the player */
		std::uint32_t Kills;
		/** @brief Laps of the player */
		std::uint32_t Laps;
		/** @brief  */
		TimeStamp LapStarted;
		/** @brief Treasure collected of the player */
		std::uint32_t TreasureCollected;

		IPlayerStatsProvider()
			: Deaths(0), Kills(0), Laps(0), TreasureCollected(0) {}
	};

	/** @brief Player in online session */
	class PlayerOnServer : public Player, public IPlayerStatsProvider
	{
		DEATH_RUNTIME_OBJECT(Player, IPlayerStatsProvider);

		friend class Jazz2::Multiplayer::MpLevelHandler;

	public:
		PlayerOnServer();

		bool OnHandleCollision(std::shared_ptr<ActorBase> other) override;
		bool CanCauseDamage(ActorBase* collider) override;

		/** @brief Returns team ID */
		std::uint8_t GetTeamId() const;
		/** @brief Sets team ID */
		void SetTeamId(std::uint8_t value);

	private:
		std::uint8_t _teamId;
		std::shared_ptr<ActorBase> _lastAttacker;

		bool IsAttacking() const;
	};
}

#endif