#pragma once

#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "MpPlayer.h"

namespace Jazz2::Actors::Multiplayer
{
	/** @brief Remotable player in online session */
	class RemotablePlayer : public MpPlayer
	{
		DEATH_RUNTIME_OBJECT(MpPlayer);

	public:
		/** @brief Whether current weapon is being changed by the server */
		bool ChangingWeaponFromServer;

		/** @brief Whether the player should be respawned */
		bool RespawnPending;

		/** @brief The position at which the player should respawn */
		Vector2f RespawnPos;

		RemotablePlayer(std::shared_ptr<PeerDescriptor> peerDesc);

		/** @brief Warps the player in */
		void WarpIn(ExitType exitType);
		/** @brief Moves the player remotely */
		void MoveRemotely(Vector2f pos, Vector2f speed);

		bool Respawn(Vector2f pos) override;

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		bool OnPerish(ActorBase* collider) override;
		void OnUpdate(float timeMult) override;

		void OnWaterSplash(Vector2f pos, bool inwards) override;

		bool FireCurrentWeapon(WeaponType weaponType) override;
		void SetCurrentWeapon(WeaponType weaponType) override;

	private:
		bool _warpPending;
	};
}

#endif