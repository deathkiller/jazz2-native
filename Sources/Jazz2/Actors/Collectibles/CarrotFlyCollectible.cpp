#include "CarrotFlyCollectible.h"
#include "../Player.h"

namespace Jazz2::Actors::Collectibles
{
	CarrotFlyCollectible::CarrotFlyCollectible()
	{
	}

	Task<bool> CarrotFlyCollectible::OnActivatedAsync(const ActorActivationDetails& details)
	{
		async_await CollectibleBase::OnActivatedAsync(details);

		_scoreValue = 500;

		async_await RequestMetadataAsync("Collectible/CarrotFly"_s);

		SetAnimation(AnimState::Default);
		SetFacingDirection();

		async_return true;
	}

	void CarrotFlyCollectible::OnCollect(Player* player)
	{
		if (player->SetModifier(Player::Modifier::Copter)) {
			CollectibleBase::OnCollect(player);
		}
	}
}