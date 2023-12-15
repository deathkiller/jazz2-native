#include "GemCrate.h"
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
		PreloadMetadataAsync("Object/Crate/Generic"_s);
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

		async_await RequestMetadataAsync("Object/Crate/Generic"_s);

		SetAnimation(AnimState::Idle);

		async_return true;
	}

	bool GemCrate::OnHandleCollision(std::shared_ptr<ActorBase> other)
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

	bool GemCrate::OnPerish(ActorBase* collider)
	{
		SetState(ActorState::CollideWithTileset | ActorState::CollideWithOtherActors | ActorState::ApplyGravitation, false);

		CreateParticleDebris();

		PlaySfx("Break"_s);

		CreateSpriteDebris((AnimState)1, 3);
		CreateSpriteDebris((AnimState)2, 2);

		_frozenTimeLeft = std::min(1.0f, _frozenTimeLeft);
		SetTransition(AnimState::TransitionDeath, false, [this, collider]() {
			GenericContainer::OnPerish(collider);
		});
		SpawnContent();
		return true;
	}
}