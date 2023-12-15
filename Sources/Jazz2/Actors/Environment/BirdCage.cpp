#include "BirdCage.h"
#include "../../ILevelHandler.h"
#include "../../Events/EventMap.h"
#include "../Explosion.h"
#include "../Player.h"
#include "../Weapons/ShotBase.h"
#include "../Weapons/TNT.h"

namespace Jazz2::Actors::Environment
{
	BirdCage::BirdCage()
		:
		_activated(false)
	{
	}

	void BirdCage::Preload(const ActorActivationDetails& details)
	{
		uint8_t type = details.Params[0];
		switch (type) {
			case 0: // Chuck (red)
				PreloadMetadataAsync("Object/BirdCageChuck"_s);
				PreloadMetadataAsync("Object/BirdChuck"_s);
				break;
			case 1: // Birdy (yellow)
				PreloadMetadataAsync("Object/BirdCageBirdy"_s);
				PreloadMetadataAsync("Object/BirdBirdy"_s);
				break;
		}
	}

	Task<bool> BirdCage::OnActivatedAsync(const ActorActivationDetails& details)
	{
		_renderer.setLayer(_renderer.layer() - 16);

		_type = details.Params[0];
		_activated = (details.Params[1] != 0);

		SetState(ActorState::TriggersTNT, true);
		SetState(ActorState::CollideWithSolidObjects | ActorState::IsSolidObject, !_activated);

		switch (_type) {
			case 0: // Chuck (red)
				async_await RequestMetadataAsync("Object/BirdCageChuck"_s);
				break;
			case 1: // Birdy (yellow)
				async_await RequestMetadataAsync("Object/BirdCageBirdy"_s);
				break;
		}

		SetAnimation(_activated ? AnimState::Activated : AnimState::Idle);

		async_return true;
	}

	bool BirdCage::OnHandleCollision(std::shared_ptr<ActorBase> other)
	{
		if (!_activated) {
			if (auto* shotBase = runtime_cast<Weapons::ShotBase*>(other)) {
				if (shotBase->GetStrength() > 0) {
					auto owner = shotBase->GetOwner();
					if (owner != nullptr && TryApplyToPlayer(owner)) {
						shotBase->DecreaseHealth(1);
						return true;
					}
				}
			} else if (auto* tnt = runtime_cast<Weapons::TNT*>(other)) {
				auto owner = tnt->GetOwner();
				if (owner != nullptr && TryApplyToPlayer(owner)) {
					return true;
				}
			} else if (auto* player = runtime_cast<Player*>(other)) {
				if (player->CanBreakSolidObjects() && TryApplyToPlayer(player)) {
					return true;
				}
			}
		}

		return ActorBase::OnHandleCollision(other);
	}

	bool BirdCage::TryApplyToPlayer(Player* player)
	{
		if (!player->SpawnBird(_type, _pos)) {
			return false;
		}

		_activated = true;
		SetState(ActorState::CollideWithSolidObjects | ActorState::IsSolidObject, false);
		SetAnimation(AnimState::Activated);

		PlaySfx("Break"_s);

		Explosion::Create(_levelHandler, Vector3i((int)(_pos.X - 12.0f), (int)(_pos.Y - 6.0f), _renderer.layer() + 90), Explosion::Type::SmokeBrown);
		Explosion::Create(_levelHandler, Vector3i((int)(_pos.X - 8.0f), (int)(_pos.Y + 28.0f), _renderer.layer() + 90), Explosion::Type::SmokeBrown);
		Explosion::Create(_levelHandler, Vector3i((int)(_pos.X + 12.0f), (int)(_pos.Y + 10.0f), _renderer.layer() + 90), Explosion::Type::SmokeBrown);

		Explosion::Create(_levelHandler, Vector3i((int)_pos.X, (int)(_pos.Y + 12.0f), _renderer.layer() + 110), Explosion::Type::SmokePoof);

		// Deactivate event in map
		auto eventMap = _levelHandler->EventMap();
		if (eventMap != nullptr) {
			uint8_t eventParams[16] = { _type, 1 };
			eventMap->StoreTileEvent(_originTile.X, _originTile.Y, EventType::BirdCage, ActorState::None, eventParams);
		}
		return true;
	}
}