#include "AmmoCollectible.h"
#include "../../LevelInitialization.h"
#include "../Player.h"

namespace Jazz2::Actors::Collectibles
{
	AmmoCollectible::AmmoCollectible()
		:
		_weaponType(WeaponType::Unknown)
	{
	}

	void AmmoCollectible::Preload(const ActorActivationDetails& details)
	{
		WeaponType weaponType = (WeaponType)details.Params[0];
		switch (weaponType) {
			case WeaponType::Blaster: PreloadMetadataAsync("Collectible/AmmoBlaster"); break;
			case WeaponType::Bouncer: PreloadMetadataAsync("Collectible/AmmoBouncer"); break;
			case WeaponType::Freezer: PreloadMetadataAsync("Collectible/AmmoFreezer"); break;
			case WeaponType::Seeker: PreloadMetadataAsync("Collectible/AmmoSeeker"); break;
			case WeaponType::RF: PreloadMetadataAsync("Collectible/AmmoRF"); break;
			case WeaponType::Toaster: PreloadMetadataAsync("Collectible/AmmoToaster"); break;
			case WeaponType::TNT: PreloadMetadataAsync("Collectible/AmmoTNT"); break;
			case WeaponType::Pepper: PreloadMetadataAsync("Collectible/AmmoPepper"); break;
			case WeaponType::Electro: PreloadMetadataAsync("Collectible/AmmoElectro"); break;
			case WeaponType::Thunderbolt: PreloadMetadataAsync("Collectible/AmmoThunderbolt"); break;
		}
	}

	Task<bool> AmmoCollectible::OnActivatedAsync(const ActorActivationDetails& details)
	{
		co_await CollectibleBase::OnActivatedAsync(details);

		_scoreValue = 100;

		_weaponType = (WeaponType)details.Params[0];
		switch (_weaponType) {
			case WeaponType::Blaster: co_await RequestMetadataAsync("Collectible/AmmoBlaster"); break;
			case WeaponType::Bouncer: co_await RequestMetadataAsync("Collectible/AmmoBouncer"); break;
			case WeaponType::Freezer: co_await RequestMetadataAsync("Collectible/AmmoFreezer"); break;
			case WeaponType::Seeker: co_await RequestMetadataAsync("Collectible/AmmoSeeker"); break;
			case WeaponType::RF: co_await RequestMetadataAsync("Collectible/AmmoRF"); break;
			case WeaponType::Toaster: co_await RequestMetadataAsync("Collectible/AmmoToaster"); break;
			case WeaponType::TNT: co_await RequestMetadataAsync("Collectible/AmmoTNT"); break;
			case WeaponType::Pepper: co_await RequestMetadataAsync("Collectible/AmmoPepper"); break;
			case WeaponType::Electro: co_await RequestMetadataAsync("Collectible/AmmoElectro"); break;
			case WeaponType::Thunderbolt: co_await RequestMetadataAsync("Collectible/AmmoThunderbolt"); break;
		}

		SetAnimation("Ammo");

		SetFacingDirection();

		co_return true;
	}

	void AmmoCollectible::OnCollect(Player* player)
	{
		if (player->AddAmmo(_weaponType, 3)) {
			CollectibleBase::OnCollect(player);
		}
	}
}