#pragma once

#if defined(WITH_MULTIPLAYER)

#include "../Player.h"

namespace Jazz2::Actors::Multiplayer
{
	class RemotablePlayer : public Player
	{
		DEATH_RTTI_OBJECT(Player);

	public:
		RemotablePlayer();

		std::uint8_t GetTeamId() const;
		void SetTeamId(std::uint8_t value);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		bool OnPerish(ActorBase* collider) override;
		void OnUpdate(float timeMult) override;

		void OnWaterSplash(const Vector2f& pos, bool inwards) override;

		bool FireCurrentWeapon(WeaponType weaponType) override;

	private:
		std::uint8_t _teamId;
	};
}

#endif