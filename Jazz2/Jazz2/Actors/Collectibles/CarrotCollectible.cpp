#include "CarrotCollectible.h"
#include "../../LevelInitialization.h"
#include "../Player.h"

#include "../../../nCine/Base/FrameTimer.h"

namespace Jazz2::Actors::Collectibles
{
	CarrotCollectible::CarrotCollectible()
		:
		_maxCarrot(false)
	{
	}

	Task<bool> CarrotCollectible::OnActivatedAsync(const ActorActivationDetails& details)
	{
		co_await CollectibleBase::OnActivatedAsync(details);

		_maxCarrot = (details.Params[0] != 0);

		if (_maxCarrot) {
			_scoreValue = 500;
			co_await RequestMetadataAsync("Collectible/CarrotFull"_s);
		} else {
			_scoreValue = 200;
			co_await RequestMetadataAsync("Collectible/Carrot"_s);
		}
		SetAnimation("Carrot"_s);
		SetFacingDirection();

		co_return true;
	}

	void CarrotCollectible::OnCollect(Player* player)
	{
		if (_maxCarrot) {
			player->AddHealth(-1);
			player->SetInvulnerability(5.0f * FrameTimer::FramesPerSecond, true);
			CollectibleBase::OnCollect(player);
		} else {
			if (player->AddHealth(1)) {
				CollectibleBase::OnCollect(player);
			}
		}
	}
}