#include "Stopwatch.h"
#include "../../LevelInitialization.h"
#include "../Player.h"

namespace Jazz2::Actors::Collectibles
{
	Stopwatch::Stopwatch()
	{
	}

	Task<bool> Stopwatch::OnActivatedAsync(const ActorActivationDetails& details)
	{
		co_await CollectibleBase::OnActivatedAsync(details);

		co_await RequestMetadataAsync("Collectible/Stopwatch"_s);

		SetAnimation("Stopwatch"_s);
		SetFacingDirection();

		co_return true;
	}

	void Stopwatch::OnCollect(Player* player)
	{
		// TODO
		//if (player->IncreaseShieldTime(10.0f)) {
			CollectibleBase::OnCollect(player);
		//}
	}
}