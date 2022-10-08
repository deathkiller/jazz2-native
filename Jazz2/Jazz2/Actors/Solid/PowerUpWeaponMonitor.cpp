#include "PowerUpWeaponMonitor.h"
#include "../../LevelInitialization.h"
#include "../../ILevelHandler.h"
#include "../../Tiles/TileMap.h"
#include "../Player.h"
#include "../Weapons/ShotBase.h"
#include "../Weapons/TNT.h"

#include "../../../nCine/Base/Random.h"

namespace Jazz2::Actors::Solid
{
	PowerUpWeaponMonitor::PowerUpWeaponMonitor()
	{
	}

	void PowerUpWeaponMonitor::Preload(const ActorActivationDetails& details)
	{
		PreloadMetadataAsync("Object/PowerUpMonitor"_s);
	}

	Task<bool> PowerUpWeaponMonitor::OnActivatedAsync(const ActorActivationDetails& details)
	{
		Movable = true;

		_weaponType = (WeaponType)details.Params[0];

		co_await RequestMetadataAsync("Object/PowerUpMonitor"_s);

		switch (_weaponType) {
			default:
			case WeaponType::Blaster: {
				auto& players = _levelHandler->GetPlayers();
				PlayerType playerType = (!players.empty() ? players[0]->GetPlayerType() : PlayerType::Jazz);
				switch (playerType) {
					default:
					case PlayerType::Jazz: SetAnimation("BlasterJazz"_s); break;
					case PlayerType::Spaz: SetAnimation("BlasterSpaz"_s); break;
					case PlayerType::Lori: SetAnimation("BlasterLori"_s); break;
				}
				break;
			}
			case WeaponType::Bouncer: SetAnimation("Bouncer"_s); break;
			case WeaponType::Freezer: SetAnimation("Freezer"_s); break;
			case WeaponType::Seeker:SetAnimation("Seeker"_s); break;
			case WeaponType::RF: SetAnimation("RF"_s); break;
			case WeaponType::Toaster: SetAnimation("Toaster"_s); break;
			case WeaponType::TNT:SetAnimation("TNT"_s); break;
			case WeaponType::Pepper: SetAnimation("Pepper"_s); break;
			case WeaponType::Electro: SetAnimation("Electro"_s); break;
			case WeaponType::Thunderbolt: SetAnimation("Thunderbolt"_s); break;
		}

		co_return true;
	}

	bool PowerUpWeaponMonitor::OnHandleCollision(std::shared_ptr<ActorBase> other)
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

	bool PowerUpWeaponMonitor::OnPerish(ActorBase* collider)
	{
		CreateParticleDebris();

		return SolidObjectBase::OnPerish(collider);
	}

	void PowerUpWeaponMonitor::DestroyAndApplyToPlayer(Player* player)
	{
		player->AddWeaponUpgrade(_weaponType, 0x01);
		player->AddAmmo(_weaponType, 25);

		DecreaseHealth(INT32_MAX, player);
		PlaySfx("Break"_s);
	}
}