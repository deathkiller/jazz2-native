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
		WeaponType weaponType = (WeaponType)details.Params[0];
		switch (weaponType) {
			case WeaponType::Bouncer: PreloadMetadataAsync("Object/Crate/AmmoBouncer"_s); break;
			case WeaponType::Freezer: PreloadMetadataAsync("Object/Crate/AmmoFreezer"_s); break;
			case WeaponType::Seeker: PreloadMetadataAsync("Object/Crate/AmmoSeeker"_s); break;
			case WeaponType::RF: PreloadMetadataAsync("Object/Crate/AmmoRF"_s); break;
			case WeaponType::Toaster: PreloadMetadataAsync("Object/Crate/AmmoToaster"_s); break;
			case WeaponType::TNT: PreloadMetadataAsync("Object/Crate/AmmoTNT"_s); break;
			case WeaponType::Pepper: PreloadMetadataAsync("Object/Crate/AmmoPepper"_s); break;
			case WeaponType::Electro: PreloadMetadataAsync("Object/Crate/AmmoElectro"_s); break;
			//case WeaponType::Thunderbolt: TODO
			default: PreloadMetadataAsync("Object/Crate/Generic"_s); break;
		}
	}

	Task<bool> AmmoCrate::OnActivatedAsync(const ActorActivationDetails& details)
	{
		Movable = true;

		WeaponType weaponType = (WeaponType)details.Params[0];
		if (weaponType != WeaponType::Blaster) {
			AddContent(EventType::Ammo, 5, &details.Params[0], 1);
		}

		switch (weaponType) {
			case WeaponType::Bouncer: async_await RequestMetadataAsync("Object/Crate/AmmoBouncer"_s); break;
			case WeaponType::Freezer: async_await RequestMetadataAsync("Object/Crate/AmmoFreezer"_s); break;
			case WeaponType::Seeker: async_await RequestMetadataAsync("Object/Crate/AmmoSeeker"_s); break;
			case WeaponType::RF: async_await RequestMetadataAsync("Object/Crate/AmmoRF"_s); break;
			case WeaponType::Toaster: async_await RequestMetadataAsync("Object/Crate/AmmoToaster"_s); break;
			case WeaponType::TNT: async_await RequestMetadataAsync("Object/Crate/AmmoTNT"_s); break;
			case WeaponType::Pepper: async_await RequestMetadataAsync("Object/Crate/AmmoPepper"_s); break;
			case WeaponType::Electro: async_await RequestMetadataAsync("Object/Crate/AmmoElectro"_s); break;
			//case WeaponType::Thunderbolt: TODO
			default: async_await RequestMetadataAsync("Object/Crate/Generic"_s); break;
		}

		SetAnimation(AnimState::Idle);

		async_return true;
	}

	bool AmmoCrate::OnHandleCollision(std::shared_ptr<ActorBase> other)
	{
		if (_health == 0) {
			return GenericContainer::OnHandleCollision(other);
		}

		if (auto* shotBase = runtime_cast<Weapons::ShotBase*>(other)) {
			if (shotBase->GetStrength() > 0) {
				DecreaseHealth(shotBase->GetStrength(), shotBase);
				shotBase->DecreaseHealth(1);
				return true;
			}
		} else if (auto* tnt = runtime_cast<Weapons::TNT*>(other)) {
			DecreaseHealth(INT32_MAX, tnt);
			return true;
		} else if (auto* player = runtime_cast<Player*>(other)) {
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

			CreateSpriteDebris((AnimState)1, 3);
			CreateSpriteDebris((AnimState)2, 2);

			_frozenTimeLeft = std::min(1.0f, _frozenTimeLeft);
			SetTransition(AnimState::TransitionDeath, false, [this, collider]() {
				GenericContainer::OnPerish(collider);
			});
			SpawnContent();
			return true;
		} else {
			CreateSpriteDebris((AnimState)1, 3);
			CreateSpriteDebris((AnimState)2, 2);

			return GenericContainer::OnPerish(collider);
		}
	}
}