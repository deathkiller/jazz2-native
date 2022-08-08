#include "BarrelContainer.h"
#include "../../LevelInitialization.h"
#include "../../ILevelHandler.h"
#include "../../Tiles/TileMap.h"

#include "../Player.h"
#include "../Weapons/ShotBase.h"

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
		CollisionFlags |= CollisionFlags::SkipPerPixelCollisions;

		EventType eventType = (EventType)*(uint16_t*)&details.Params[0];
		int count = (int)*(uint16_t*)&details.Params[2];
		if (eventType != EventType::Empty && count > 0) {
			AddContent(eventType, count, &details.Params[4], 16 - 4);
		}

		co_await RequestMetadataAsync("Object/BarrelContainer"_s);

		SetAnimation(AnimState::Idle);

		co_return true;
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
		} /*else if (auto shotTnt = dynamic_cast<Weapons::ShotTNT*>(other)) {
			// TODO: TNT
		}*/ else if (auto player = dynamic_cast<Player*>(other.get())) {
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

		CreateSpriteDebris("BarrelShrapnel1"_s, 3);
		CreateSpriteDebris("BarrelShrapnel2"_s, 3);
		CreateSpriteDebris("BarrelShrapnel3"_s, 2);
		CreateSpriteDebris("BarrelShrapnel4"_s, 1);

		return GenericContainer::OnPerish(collider);
	}
}