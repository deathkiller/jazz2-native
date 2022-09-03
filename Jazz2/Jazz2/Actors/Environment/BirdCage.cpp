#include "BirdCage.h"
#include "../../ILevelHandler.h"
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

		CollisionFlags |= CollisionFlags::CollideWithSolidObjects;

		_type = details.Params[0];
		_activated = (details.Params[1] != 0);

		switch (_type) {
			case 0: // Chuck (red)
				co_await RequestMetadataAsync("Object/BirdCageChuck"_s);
				PreloadMetadataAsync("Object/BirdChuck"_s);
				break;
			case 1: // Birdy (yellow)
				co_await RequestMetadataAsync("Object/BirdCageBirdy"_s);
				PreloadMetadataAsync("Object/BirdBirdy"_s);
				break;
		}

		SetAnimation(_activated ? AnimState::Activated : AnimState::Idle);

		co_return true;
	}

	bool BirdCage::OnHandleCollision(std::shared_ptr<ActorBase> other)
	{
		if (!_activated) {
			if (auto shotBase = dynamic_cast<Weapons::ShotBase*>(other.get())) {
				auto owner = shotBase->GetOwner();
				if (owner != nullptr) {
					ApplyToPlayer(owner);
					shotBase->DecreaseHealth(INT32_MAX);
				}
				return true;
			} else if (auto tnt = dynamic_cast<Weapons::TNT*>(other.get())) {
				auto owner = tnt->GetOwner();
				if (owner != nullptr) {
					ApplyToPlayer(owner);
				}
				return true;
			} else if (auto player = dynamic_cast<Player*>(other.get())) {
				if (player->CanBreakSolidObjects()) {
					ApplyToPlayer(player);
				}
				return true;
			}
		}

		return ActorBase::OnHandleCollision(other);
	}

	void BirdCage::ApplyToPlayer(Player* player)
	{
		// TODO: Birds
	}
}