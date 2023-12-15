#include "AmmoCollectible.h"
#include "../../ILevelHandler.h"
#include "../../WeaponType.h"
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
			case WeaponType::Bouncer: PreloadMetadataAsync("Collectible/AmmoBouncer"_s); PreloadMetadataAsync("Weapon/Bouncer"_s); break;
			case WeaponType::Freezer: PreloadMetadataAsync("Collectible/AmmoFreezer"_s); PreloadMetadataAsync("Weapon/Freezer"_s); break;
			case WeaponType::Seeker: PreloadMetadataAsync("Collectible/AmmoSeeker"_s); PreloadMetadataAsync("Weapon/Seeker"_s); break;
			case WeaponType::RF: PreloadMetadataAsync("Collectible/AmmoRF"_s); PreloadMetadataAsync("Weapon/RF"_s); break;
			case WeaponType::Toaster: PreloadMetadataAsync("Collectible/AmmoToaster"_s); PreloadMetadataAsync("Weapon/Toaster"_s); break;
			case WeaponType::TNT: PreloadMetadataAsync("Collectible/AmmoTNT"_s); PreloadMetadataAsync("Weapon/TNT"_s); break;
			case WeaponType::Pepper: PreloadMetadataAsync("Collectible/AmmoPepper"_s); PreloadMetadataAsync("Weapon/Pepper"_s); break;
			case WeaponType::Electro: PreloadMetadataAsync("Collectible/AmmoElectro"_s); PreloadMetadataAsync("Weapon/Electro"_s); break;
			case WeaponType::Thunderbolt: PreloadMetadataAsync("Collectible/AmmoThunderbolt"_s); PreloadMetadataAsync("Weapon/Thunderbolt"_s); break;
		}
	}

	Task<bool> AmmoCollectible::OnActivatedAsync(const ActorActivationDetails& details)
	{
		async_await CollectibleBase::OnActivatedAsync(details);

		_scoreValue = 100;

		_weaponType = (WeaponType)details.Params[0];
		switch (_weaponType) {
			case WeaponType::Bouncer: async_await RequestMetadataAsync("Collectible/AmmoBouncer"_s); break;
			case WeaponType::Freezer: async_await RequestMetadataAsync("Collectible/AmmoFreezer"_s); break;
			case WeaponType::Seeker: async_await RequestMetadataAsync("Collectible/AmmoSeeker"_s); break;
			case WeaponType::RF: async_await RequestMetadataAsync("Collectible/AmmoRF"_s); break;
			case WeaponType::Toaster: async_await RequestMetadataAsync("Collectible/AmmoToaster"_s); break;
			case WeaponType::TNT: async_await RequestMetadataAsync("Collectible/AmmoTNT"_s); break;
			case WeaponType::Pepper: async_await RequestMetadataAsync("Collectible/AmmoPepper"_s); break;
			case WeaponType::Electro: async_await RequestMetadataAsync("Collectible/AmmoElectro"_s); break;
			case WeaponType::Thunderbolt: async_await RequestMetadataAsync("Collectible/AmmoThunderbolt"_s); break;
		}

		// Show upgraded ammo if player has upgraded weapon
		const auto& players = _levelHandler->GetPlayers();
		bool upgraded = (!players.empty() && (players[0]->GetWeaponUpgrades()[(std::uint8_t)_weaponType] & 0x01) != 0);
		SetAnimation((AnimState)(upgraded ? 1 : 0));

		SetFacingDirection();

		async_return true;
	}

	void AmmoCollectible::OnCollect(Player* player)
	{
		if (player->AddAmmo(_weaponType, 3)) {
			CollectibleBase::OnCollect(player);
		}
	}
}