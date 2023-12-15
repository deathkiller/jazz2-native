#include "GemBarrel.h"
#include "../../ILevelHandler.h"
#include "../../Tiles/TileMap.h"
#include "../Player.h"
#include "../Weapons/ShotBase.h"
#include "../Weapons/TNT.h"

namespace Jazz2::Actors::Solid
{
	GemBarrel::GemBarrel()
	{
	}

	void GemBarrel::Preload(const ActorActivationDetails& details)
	{
		PreloadMetadataAsync("Object/BarrelContainer"_s);
		PreloadMetadataAsync("Collectible/Gems"_s);
	}

	Task<bool> GemBarrel::OnActivatedAsync(const ActorActivationDetails& details)
	{
		Movable = true;

		uint8_t eventParam = 0;
		AddContent(EventType::Gem, details.Params[0], &eventParam, sizeof(eventParam));
		eventParam = 1;
		AddContent(EventType::Gem, details.Params[1], &eventParam, sizeof(eventParam));
		eventParam = 2;
		AddContent(EventType::Gem, details.Params[2], &eventParam, sizeof(eventParam));
		eventParam = 3;
		AddContent(EventType::Gem, details.Params[3], &eventParam, sizeof(eventParam));

		async_await RequestMetadataAsync("Object/BarrelContainer"_s);

		SetAnimation(AnimState::Idle);

		async_return true;
	}

	bool GemBarrel::OnHandleCollision(std::shared_ptr<ActorBase> other)
	{
		if (_health == 0) {
			return GenericContainer::OnHandleCollision(other);
		}

		if (auto* shotBase = runtime_cast<Weapons::ShotBase*>(other)) {
			WeaponType weaponType = shotBase->GetWeaponType();
			if (weaponType == WeaponType::RF || weaponType == WeaponType::Seeker ||
				weaponType == WeaponType::Pepper || weaponType == WeaponType::Electro) {
				DecreaseHealth(shotBase->GetStrength(), shotBase);
				shotBase->DecreaseHealth(INT32_MAX);
			} else {
				shotBase->TriggerRicochet(this);
			}
			return true;
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

	bool GemBarrel::OnPerish(ActorBase* collider)
	{
		PlaySfx("Break"_s);

		CreateParticleDebris();

		CreateSpriteDebris((AnimState)1, 3);
		CreateSpriteDebris((AnimState)2, 3);
		CreateSpriteDebris((AnimState)3, 2);
		CreateSpriteDebris((AnimState)4, 1);

		return GenericContainer::OnPerish(collider);
	}
}