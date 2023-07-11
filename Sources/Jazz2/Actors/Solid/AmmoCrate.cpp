#include "AmmoCrate.h"
#include "../../ILevelHandler.h"
#include "../../Tiles/TileMap.h"
#include "../Player.h"
#include "../Weapons/ShotBase.h"
#include "../Weapons/TNT.h"

#include "../../../nCine/Base/Random.h"

namespace Jazz2::Actors::Solid
{
	AmmoCrate::AmmoCrate()
	{
	}

	void AmmoCrate::Preload(const ActorActivationDetails& details)
	{
		PreloadMetadataAsync("Object/CrateContainer"_s);
	}

	Task<bool> AmmoCrate::OnActivatedAsync(const ActorActivationDetails& details)
	{
		Movable = true;

		WeaponType weaponType = (WeaponType)details.Params[0];
		if (weaponType != WeaponType::Blaster) {
			AddContent(EventType::Ammo, 5, &details.Params[0], 1);
		}

		async_await RequestMetadataAsync("Object/CrateContainer"_s);

		switch (weaponType) {
			case WeaponType::Bouncer: SetAnimation("CrateAmmoBouncer"_s); break;
			case WeaponType::Freezer: SetAnimation("CrateAmmoFreezer"_s); break;
			case WeaponType::Seeker:SetAnimation("CrateAmmoSeeker"_s); break;
			case WeaponType::RF: SetAnimation("CrateAmmoRF"_s); break;
			case WeaponType::Toaster: SetAnimation("CrateAmmoToaster"_s); break;
			case WeaponType::TNT:SetAnimation("CrateAmmoTNT"_s); break;
			case WeaponType::Pepper: SetAnimation("CrateAmmoPepper"_s); break;
			case WeaponType::Electro: SetAnimation("CrateAmmoElectro"_s); break;
			case WeaponType::Thunderbolt: SetAnimation("CrateAmmoThunderbolt"_s); break;
			default: SetAnimation(AnimState::Idle); break;
		}

		async_return true;
	}

	bool AmmoCrate::OnHandleCollision(std::shared_ptr<ActorBase> other)
	{
		if (_health == 0) {
			return GenericContainer::OnHandleCollision(other);
		}

		if (auto shotBase = dynamic_cast<Weapons::ShotBase*>(other.get())) {
			if (shotBase->GetStrength() > 0) {
				DecreaseHealth(shotBase->GetStrength(), shotBase);
				shotBase->DecreaseHealth(1);
				return true;
			}
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

	bool AmmoCrate::OnPerish(ActorBase* collider)
	{
		SetState(ActorState::CollideWithTileset | ActorState::CollideWithOtherActors | ActorState::ApplyGravitation, false);

		CreateParticleDebris();

		PlaySfx("Break"_s);

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

			CreateSpriteDebris("CrateShrapnel1"_s, 3);
			CreateSpriteDebris("CrateShrapnel2"_s, 2);

			_frozenTimeLeft = std::min(1.0f, _frozenTimeLeft);
			SetTransition(AnimState::TransitionDeath, false, [this, collider]() {
				GenericContainer::OnPerish(collider);
			});
			SpawnContent();
			return true;
		} else {
			CreateSpriteDebris("CrateAmmoShrapnel1"_s, 3);
			CreateSpriteDebris("CrateAmmoShrapnel2"_s, 2);

			return GenericContainer::OnPerish(collider);
		}
	}
}