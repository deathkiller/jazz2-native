#include "CarrotFlyCollectible.h"
#include "../../LevelInitialization.h"
#include "../Player.h"

namespace Jazz2::Actors::Collectibles
{
	CarrotFlyCollectible::CarrotFlyCollectible()
	{
	}

	Task<bool> CarrotFlyCollectible::OnActivatedAsync(const ActorActivationDetails& details)
	{
		co_await CollectibleBase::OnActivatedAsync(details);

		_scoreValue = 500;

		co_await RequestMetadataAsync("Collectible/CarrotFly"_s);

		SetAnimation("Carrot"_s);
		SetFacingDirection();

		co_return true;
	}

	void CarrotFlyCollectible::OnCollect(Player* player)
	{
		if (player->SetModifier(Player::Modifier::Copter)) {
			CollectibleBase::OnCollect(player);
		}
	}
}