#include "GemBarrel.h"
#include "../../LevelInitialization.h"
#include "../../ILevelHandler.h"
#include "../../Tiles/TileMap.h"
#include "../Player.h"
#include "../Weapons/ShotBase.h"

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
		CollisionFlags |= CollisionFlags::SkipPerPixelCollisions;

		uint8_t eventParam = 0;
		AddContent(EventType::Gem, details.Params[0], &eventParam, sizeof(eventParam));
		eventParam = 1;
		AddContent(EventType::Gem, details.Params[1], &eventParam, sizeof(eventParam));
		eventParam = 2;
		AddContent(EventType::Gem, details.Params[2], &eventParam, sizeof(eventParam));
		eventParam = 3;
		AddContent(EventType::Gem, details.Params[3], &eventParam, sizeof(eventParam));

		co_await RequestMetadataAsync("Object/BarrelContainer"_s);

		SetAnimation(AnimState::Idle);

		co_return true;
	}

	bool GemBarrel::OnHandleCollision(std::shared_ptr<ActorBase> other)
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

	bool GemBarrel::OnPerish(ActorBase* collider)
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