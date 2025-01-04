#pragma once

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

		std::uint8_t GetTeamId() const;
		void SetTeamId(std::uint8_t value);

		void WarpIn(ExitType exitType);
		void MoveRemotely(Vector2f pos, Vector2f speed);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		bool OnPerish(ActorBase* collider) override;
		void OnUpdate(float timeMult) override;

		void OnWaterSplash(Vector2f pos, bool inwards) override;

		bool FireCurrentWeapon(WeaponType weaponType) override;

	private:
		std::uint8_t _teamId;
		bool _warpPending;
	};
}

#endif