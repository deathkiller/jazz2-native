#include "GemGiant.h"
#include "../../ILevelHandler.h"
#include "../../Events/EventSpawner.h"
#include "../Player.h"
#include "../Weapons/ShotBase.h"
#include "../Weapons/TNT.h"

#include "../../../nCine/Base/Random.h"

namespace Jazz2::Actors::Collectibles
{
	GemGiant::GemGiant()
	{
	}

	void GemGiant::Preload(const ActorActivationDetails& details)
	{
		PreloadMetadataAsync("Object/GemGiant"_s);
		PreloadMetadataAsync("Collectible/Gems"_s);
	}

	Task<bool> GemGiant::OnActivatedAsync(const ActorActivationDetails& details)
	{
		SetState(ActorState::ApplyGravitation, false);

		async_await RequestMetadataAsync("Object/GemGiant"_s);

		SetAnimation("GemGiant"_s);

		_renderer.setAlphaF(0.7f);

		async_return true;
	}

	bool GemGiant::OnHandleCollision(std::shared_ptr<ActorBase> other)
	{
		if (auto shotBase = dynamic_cast<Weapons::ShotBase*>(other.get())) {
			if (shotBase->GetStrength() > 0) {
				DecreaseHealth(shotBase->GetStrength(), shotBase);
				shotBase->DecreaseHealth(1);
				return true;
			}
		} else if (auto tnt = dynamic_cast<Weapons::TNT*>(other.get())) {
			DecreaseHealth(INT32_MAX, tnt);
			return true;
		} else if (auto player = dynamic_cast<Player*>(other.get())) {
			if (player->CanBreakSolidObjects()) {
				DecreaseHealth(INT32_MAX, player);
				return true;
			}
		}

		return ActorBase::OnHandleCollision(other);
	}

	bool GemGiant::OnPerish(ActorBase* collider)
	{
		CreateParticleDebris();

		PlaySfx("Break"_s);

		for (int i = 0; i < 10; i++) {
			float fx = Random().NextFloat(-16.0f, 16.0f);
			float fy = Random().NextFloat(-2.0f, 2.0f);
			uint8_t eventParams[1] = { 0 };
			std::shared_ptr<ActorBase> actor = _levelHandler->EventSpawner()->SpawnEvent(EventType::Gem, eventParams, ActorState::None, Vector3i((int)(_pos.X + fx * 2.0f), (int)(_pos.Y + fy * 4.0f), _renderer.layer() - 10));
			if (actor != nullptr) {
				actor->AddExternalForce(fx, fy);
				_levelHandler->AddActor(actor);
			}
		}

		return ActorBase::OnPerish(collider);
	}
}