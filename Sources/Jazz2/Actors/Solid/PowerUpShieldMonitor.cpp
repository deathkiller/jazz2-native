#include "PowerUpShieldMonitor.h"
#include "../../ILevelHandler.h"
#include "../../Tiles/TileMap.h"
#include "../Player.h"
#include "../Weapons/ShotBase.h"
#include "../Weapons/TNT.h"

#include "../../../nCine/Base/FrameTimer.h"

namespace Jazz2::Actors::Solid
{
	PowerUpShieldMonitor::PowerUpShieldMonitor()
	{
	}

	void PowerUpShieldMonitor::Preload(const ActorActivationDetails& details)
	{
		PreloadMetadataAsync("Object/PowerUpMonitorShield"_s);
	}

	Task<bool> PowerUpShieldMonitor::OnActivatedAsync(const ActorActivationDetails& details)
	{
		_shieldType = (ShieldType)details.Params[0];

		SetState(ActorState::TriggersTNT, true);
		Movable = true;

		async_await RequestMetadataAsync("Object/PowerUpMonitorShield"_s);

		switch (_shieldType) {
			case ShieldType::Fire: SetAnimation("ShieldFire"_s); break;
			case ShieldType::Water: SetAnimation("ShieldWater"_s); break;
			case ShieldType::Laser: SetAnimation("ShieldLaser"_s); break;
			case ShieldType::Lightning: SetAnimation("ShieldLightning"_s); break;

			default: SetAnimation("Empty"_s); break;
		}

		async_return true;
	}

	bool PowerUpShieldMonitor::OnHandleCollision(std::shared_ptr<ActorBase> other)
	{
		if (_health == 0) {
			return SolidObjectBase::OnHandleCollision(other);
		}

		if (auto shotBase = dynamic_cast<Weapons::ShotBase*>(other.get())) {
			Player* owner = shotBase->GetOwner();
			WeaponType weaponType = shotBase->GetWeaponType();
			if (owner != nullptr && (weaponType == WeaponType::Blaster ||
				weaponType == WeaponType::RF || weaponType == WeaponType::Seeker ||
				weaponType == WeaponType::Pepper || weaponType == WeaponType::Electro)) {
				DestroyAndApplyToPlayer(owner);
				shotBase->DecreaseHealth(INT32_MAX);
			} else {
				shotBase->TriggerRicochet(this);
			}
			return true;
		} else if (auto tnt = dynamic_cast<Weapons::TNT*>(other.get())) {
			Player* owner = tnt->GetOwner();
			if (owner != nullptr) {
				DestroyAndApplyToPlayer(owner);
			}
			return true;
		} else if (auto player = dynamic_cast<Player*>(other.get())) {
			if (player->CanBreakSolidObjects()) {
				DestroyAndApplyToPlayer(player);
				return true;
			}
		}

		return SolidObjectBase::OnHandleCollision(other);
	}

	bool PowerUpShieldMonitor::OnPerish(ActorBase* collider)
	{
		CreateParticleDebris();

		return SolidObjectBase::OnPerish(collider);
	}

	void PowerUpShieldMonitor::DestroyAndApplyToPlayer(Player* player)
	{
		if (player->SetShield(_shieldType, 30.0f * FrameTimer::FramesPerSecond)) {			PlaySfx("Break"_s);
			DecreaseHealth(INT32_MAX, player);
		}
	}
}