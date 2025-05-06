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

		SetAnimation(AnimState::Default);

		_renderer.setAlphaF(0.7f);

		async_return true;
	}

	bool GemGiant::OnHandleCollision(std::shared_ptr<ActorBase> other)
	{
		if (auto* shotBase = runtime_cast<Weapons::ShotBase>(other.get())) {
			if (shotBase->GetStrength() > 0) {
				DecreaseHealth(shotBase->GetStrength(), shotBase);
				shotBase->DecreaseHealth(1);
				return true;
			}
		} else if (auto* tnt = runtime_cast<Weapons::TNT>(other.get())) {
			DecreaseHealth(INT32_MAX, tnt);
			return true;
		} else if (auto* player = runtime_cast<Player>(other.get())) {
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

		std::int32_t count = Random().Next(5, 16);
		for (int i = 0; i < count; i++) {
			float fx = Random().NextFloat(-16.0f, 16.0f);
			float fy = Random().NextFloat(-2.0f, 2.0f);
			std::uint8_t eventParams[Events::EventSpawner::SpawnParamsSize] = { 0, 0x01 };
			std::shared_ptr<ActorBase> actor = _levelHandler->EventSpawner()->SpawnEvent(EventType::Gem, eventParams, ActorState::None, Vector3i((std::int32_t)(_pos.X + fx * 2.0f), (std::int32_t)(_pos.Y + fy * 4.0f), _renderer.layer() - 10));
			if (actor != nullptr) {
				actor->AddExternalForce(fx, fy);
				_levelHandler->AddActor(actor);
			}
		}

		return ActorBase::OnPerish(collider);
	}
}