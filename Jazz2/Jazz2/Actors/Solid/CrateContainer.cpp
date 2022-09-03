#include "CrateContainer.h"
#include "../../LevelInitialization.h"
#include "../../ILevelHandler.h"
#include "../../Tiles/TileMap.h"
#include "../Player.h"
#include "../Weapons/ShotBase.h"
#include "../Weapons/TNT.h"

namespace Jazz2::Actors::Solid
{
	CrateContainer::CrateContainer()
	{
	}

	void CrateContainer::Preload(const ActorActivationDetails& details)
	{
		PreloadMetadataAsync("Object/CrateContainer"_s);
	}

	Task<bool> CrateContainer::OnActivatedAsync(const ActorActivationDetails& details)
	{
		Movable = true;

		EventType eventType = (EventType)*(uint16_t*)&details.Params[0];
		int count = details.Params[2];
		if (eventType != EventType::Empty && count > 0) {
			AddContent(eventType, count, &details.Params[3], 16 - 4);
		}

		co_await RequestMetadataAsync("Object/CrateContainer"_s);

		SetAnimation(AnimState::Idle);

		co_return true;
	}

	bool CrateContainer::OnHandleCollision(std::shared_ptr<ActorBase> other)
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

	bool CrateContainer::OnPerish(ActorBase* collider)
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