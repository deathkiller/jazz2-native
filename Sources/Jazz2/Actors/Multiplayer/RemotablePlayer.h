﻿#pragma once

#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../Player.h"

namespace Jazz2::Actors::Multiplayer
{
	/** @brief Remotable player in online session */
	class RemotablePlayer : public Player
	{
		DEATH_RUNTIME_OBJECT(Player);

	public:
		RemotablePlayer();

		bool ChangingWeaponFromServer;

		/** @brief Returns team ID */
		std::uint8_t GetTeamId() const;
		/** @brief Sets team ID */
		void SetTeamId(std::uint8_t value);

		/** @brief Warps the player in */
		void WarpIn(ExitType exitType);
		/** @brief Moves the player remotely */
		void MoveRemotely(Vector2f pos, Vector2f speed);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		bool OnPerish(ActorBase* collider) override;
		void OnUpdate(float timeMult) override;

		void OnWaterSplash(Vector2f pos, bool inwards) override;

		bool FireCurrentWeapon(WeaponType weaponType) override;
		void SetCurrentWeapon(WeaponType weaponType) override;

	private:
		std::uint8_t _teamId;
		bool _warpPending;
	};
}

#endif