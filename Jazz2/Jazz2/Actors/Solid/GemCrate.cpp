#include "GemCrate.h"
#include "../../LevelInitialization.h"
#include "../../ILevelHandler.h"
#include "../../Tiles/TileMap.h"
#include "../Player.h"
#include "../Weapons/ShotBase.h"
#include "../Weapons/TNT.h"

namespace Jazz2::Actors::Solid
{
	GemCrate::GemCrate()
	{
	}

	void GemCrate::Preload(const ActorActivationDetails& details)
	{
		PreloadMetadataAsync("Object/CrateContainer"_s);
		PreloadMetadataAsync("Collectible/Gems"_s);
	}

	Task<bool> GemCrate::OnActivatedAsync(const ActorActivationDetails& details)
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

		co_await RequestMetadataAsync("Object/CrateContainer"_s);

		SetAnimation(AnimState::Idle);

		co_return true;
	}

	bool GemCrate::OnHandleCollision(std::shared_ptr<ActorBase> other)
	{
		if (_health == 0) {
			return GenericContainer::OnHandleCollision(other);
		}

		if (auto shotBase = dynamic_cast<Weapons::ShotBase*>(other.get())) {
			DecreaseHealth(shotBase->GetStrength(), shotBase);
			shotBase->DecreaseHealth(INT32_MAX);
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

	bool GemCrate::OnPerish(ActorBase* collider)
	{
		CollisionFlags = CollisionFlags::None;

		CreateParticleDebris();

		PlaySfx("Break"_s);

		CreateSpriteDebris("CrateShrapnel1"_s, 3);
		CreateSpriteDebris("CrateShrapnel2"_s, 2);

		SetTransition(AnimState::TransitionDeath, false, [this, collider]() {
			GenericContainer::OnPerish(collider);
		});
		SpawnContent();
		return true;
	}
}