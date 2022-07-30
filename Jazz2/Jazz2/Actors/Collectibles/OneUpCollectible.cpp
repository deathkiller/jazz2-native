#include "OneUpCollectible.h"
#include "../../LevelInitialization.h"
#include "../Player.h"

namespace Jazz2::Actors::Collectibles
{
	OneUpCollectible::OneUpCollectible()
	{
	}

	Task<bool> OneUpCollectible::OnActivatedAsync(const ActorActivationDetails& details)
	{
		co_await CollectibleBase::OnActivatedAsync(details);

		_scoreValue = 1000;

		co_await RequestMetadataAsync("Collectible/OneUp"_s);

		SetAnimation("OneUp"_s);

		co_return true;
	}

	void OneUpCollectible::OnCollect(Player* player)
	{
		player->AddLives(1);

		CollectibleBase::OnCollect(player);
	}
}