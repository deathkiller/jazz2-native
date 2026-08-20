#include "CarrotFlyCollectible.h"
#include "../Player.h"

namespace Jazz2::Actors::Collectibles
{
	CarrotFlyCollectible::CarrotFlyCollectible()
	{
	}

	void CarrotFlyCollectible::Preload(const ActorActivationDetails& details)
	{
		PreloadMetadataAsync("Collectible/CarrotFly"_s);
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
		// Collect the item even if the player is already in Copter; otherwise switch to Copter first and collect only on success.
		if (player->GetModifier() == Player::Modifier::Copter || player->SetModifier(Player::Modifier::Copter)) {
			CollectibleBase::OnCollect(player);
		}
	}
}