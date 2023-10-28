#include "BarrelContainer.h"
#include "../../ILevelHandler.h"
#include "../../Tiles/TileMap.h"
#include "../Player.h"
#include "../Weapons/ShotBase.h"
#include "../Weapons/TNT.h"

namespace Jazz2::Actors::Solid
{
	BarrelContainer::BarrelContainer()
	{
	}

	void BarrelContainer::Preload(const ActorActivationDetails& details)
	{
		PreloadMetadataAsync("Object/BarrelContainer"_s);
	}

	Task<bool> BarrelContainer::OnActivatedAsync(const ActorActivationDetails& details)
	{
		Movable = true;

		EventType eventType = (EventType)*(uint16_t*)&details.Params[0];
		int count = details.Params[2];
		if (eventType != EventType::Empty && count > 0) {
			AddContent(eventType, count, &details.Params[3], 16 - 4);
		}

		async_await RequestMetadataAsync("Object/BarrelContainer"_s);

		SetAnimation(AnimState::Idle);

		async_return true;
	}

	bool BarrelContainer::OnHandleCollision(std::shared_ptr<ActorBase> other)
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

	bool BarrelContainer::OnPerish(ActorBase* collider)
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