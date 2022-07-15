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

		co_await RequestMetadataAsync("Collectible/Coins");

		if (_maxCarrot) {
			_scoreValue = 500;
			co_await RequestMetadataAsync("Collectible/CarrotFull");
		} else {
			_scoreValue = 200;
			co_await RequestMetadataAsync("Collectible/Carrot");
		}
		SetAnimation("Carrot");
		SetFacingDirection();

		co_return true;
	}

	void CarrotCollectible::OnCollect(Player* player)
	{
		if (_maxCarrot) {
			player->AddHealth(-1);
			player->SetInvulnerability(5 * FrameTimer::FramesPerSecond, true);
			CollectibleBase::OnCollect(player);
		} else {
			if (player->AddHealth(1)) {
				CollectibleBase::OnCollect(player);
			}
		}
	}
}