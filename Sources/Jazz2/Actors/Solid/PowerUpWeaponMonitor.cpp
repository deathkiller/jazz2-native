#include "PowerUpWeaponMonitor.h"
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
		WeaponType weaponType = (WeaponType)details.Params[0];
		switch (weaponType) {
			case WeaponType::Blaster: PreloadMetadataAsync("Object/PowerUp/Blaster"_s); break;
			case WeaponType::Bouncer: PreloadMetadataAsync("Object/PowerUp/Bouncer"_s); break;
			case WeaponType::Freezer: PreloadMetadataAsync("Object/PowerUp/Freezer"_s); break;
			case WeaponType::Seeker: PreloadMetadataAsync("Object/PowerUp/Seeker"_s); break;
			case WeaponType::RF: PreloadMetadataAsync("Object/PowerUp/RF"_s); break;
			case WeaponType::Toaster: PreloadMetadataAsync("Object/PowerUp/Toaster"_s); break;
			//case WeaponType::TNT: TODO
			case WeaponType::Pepper: PreloadMetadataAsync("Object/PowerUp/Pepper"_s); break;
			case WeaponType::Electro: PreloadMetadataAsync("Object/PowerUp/Electro"_s); break;
			//case WeaponType::Thunderbolt: TODO
			default: PreloadMetadataAsync("Object/PowerUp/Empty"_s); break;
		}
	}

	Task<bool> PowerUpWeaponMonitor::OnActivatedAsync(const ActorActivationDetails& details)
	{
		_weaponType = (WeaponType)details.Params[0];

		SetState(ActorState::TriggersTNT, true);
		Movable = true;

		switch (_weaponType) {
			case WeaponType::Blaster: async_await RequestMetadataAsync("Object/PowerUp/Blaster"_s); break;
			case WeaponType::Bouncer: async_await RequestMetadataAsync("Object/PowerUp/Bouncer"_s); break;
			case WeaponType::Freezer: async_await RequestMetadataAsync("Object/PowerUp/Freezer"_s); break;
			case WeaponType::Seeker: async_await RequestMetadataAsync("Object/PowerUp/Seeker"_s); break;
			case WeaponType::RF: async_await RequestMetadataAsync("Object/PowerUp/RF"_s); break;
			case WeaponType::Toaster: async_await RequestMetadataAsync("Object/PowerUp/Toaster"_s); break;
			//case WeaponType::TNT: TODO
			case WeaponType::Pepper: async_await RequestMetadataAsync("Object/PowerUp/Pepper"_s); break;
			case WeaponType::Electro: async_await RequestMetadataAsync("Object/PowerUp/Electro"_s); break;
			//case WeaponType::Thunderbolt: TODO
			default: async_await RequestMetadataAsync("Object/PowerUp/Empty"_s); break;
		}

		if (_weaponType == WeaponType::Blaster) {
			auto& players = _levelHandler->GetPlayers();
			PlayerType playerType = (!players.empty() ? players[0]->GetPlayerType() : PlayerType::Jazz);
			switch (playerType) {
				case PlayerType::Jazz: 
				case PlayerType::Spaz:
				case PlayerType::Lori: SetAnimation((AnimState)playerType); break;
				default: SetAnimation((AnimState)PlayerType::Jazz); break;
			}
		} else {
			SetAnimation(AnimState::Default);
		}

		async_return true;
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