#include "AmmoBarrel.h"
#include "../../ILevelHandler.h"
#include "../../Tiles/TileMap.h"
#include "../Player.h"
#include "../Weapons/ShotBase.h"
#include "../Weapons/TNT.h"

#include "../../../nCine/Base/Random.h"

namespace Jazz2::Actors::Solid
{
	AmmoBarrel::AmmoBarrel()
	{
	}

	void AmmoBarrel::Preload(const ActorActivationDetails& details)
	{
		PreloadMetadataAsync("Object/BarrelContainer"_s);
	}

	Task<bool> AmmoBarrel::OnActivatedAsync(const ActorActivationDetails& details)
	{
		Movable = true;

		WeaponType weaponType = (WeaponType)details.Params[0];
		if (weaponType != WeaponType::Blaster) {
			AddContent(EventType::Ammo, 5, &details.Params[0], 1);
		}

		async_await RequestMetadataAsync("Object/BarrelContainer"_s);

		SetAnimation(AnimState::Idle);

		async_return true;
	}

	bool AmmoBarrel::OnHandleCollision(std::shared_ptr<ActorBase> other)
	{
		if (_health == 0) {
			return GenericContainer::OnHandleCollision(other);
		}

		if (auto shotBase = dynamic_cast<Weapons::ShotBase*>(other.get())) {
			WeaponType weaponType = shotBase->GetWeaponType();
			if (weaponType == WeaponType::RF || weaponType == WeaponType::Seeker ||
				weaponType == WeaponType::Pepper || weaponType == WeaponType::Electro) {
				DecreaseHealth(shotBase->GetStrength(), shotBase);
				shotBase->DecreaseHealth(INT32_MAX);
			} else {
				shotBase->TriggerRicochet(this);
			}
			return true;
		} else if (auto tnt = dynamic_cast<Weapons::TNT*>(other.get())) {
			DecreaseHealth(INT32_MAX, tnt);
			return true;
		} else if (auto player = dynamic_cast<Player*>(other.get())) {
			if (player->CanBreakSolidObjects()) {
				DecreaseHealth(INT32_MAX, player);
				return true;
			}
		}

		return GenericContainer::OnHandleCollision(other);
	}

	bool AmmoBarrel::OnPerish(ActorBase* collider)
	{
		if (_content.empty()) {
			// Random Ammo create
			SmallVector<WeaponType, (int)WeaponType::Count> weaponTypes;
			auto& players = _levelHandler->GetPlayers();
			for (auto& player : players) {
				const auto playerAmmo = player->GetWeaponAmmo();
				for (int i = 1; i < (int)WeaponType::Count; i++) {
					if (playerAmmo[i] > 0) {
						weaponTypes.push_back((WeaponType)i);
					}
				}
			}

			if (weaponTypes.empty()) {
				weaponTypes.push_back(WeaponType::Bouncer);
			}

			int n = Random().Next(4, 7);
			for (int i = 0; i < n; i++) {
				uint8_t weaponType = (uint8_t)weaponTypes[Random().Next(0, (uint32_t)weaponTypes.size())];
				AddContent(EventType::Ammo, 1, &weaponType, sizeof(weaponType));
			}
		}

		PlaySfx("Break"_s);

		CreateParticleDebris();

		CreateSpriteDebris("BarrelShrapnel1"_s, 3);
		CreateSpriteDebris("BarrelShrapnel2"_s, 3);
		CreateSpriteDebris("BarrelShrapnel3"_s, 2);
		CreateSpriteDebris("BarrelShrapnel4"_s, 1);

		return GenericContainer::OnPerish(collider);
	}
}