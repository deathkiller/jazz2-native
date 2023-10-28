#include "CarrotInvincibleCollectible.h"
#include "../Player.h"

#include "../../../nCine/Base/FrameTimer.h"

namespace Jazz2::Actors::Collectibles
{
	CarrotInvincibleCollectible::CarrotInvincibleCollectible()
	{
	}

	Task<bool> CarrotInvincibleCollectible::OnActivatedAsync(const ActorActivationDetails& details)
	{
		async_await CollectibleBase::OnActivatedAsync(details);

		_scoreValue = 500;

		async_await RequestMetadataAsync("Collectible/CarrotInvincible"_s);

		SetAnimation(AnimState::Default);
		SetFacingDirection();

		async_return true;
	}

	void CarrotInvincibleCollectible::OnCollect(Player* player)
	{
		player->SetInvulnerability(30.0f * FrameTimer::FramesPerSecond, true);

		CollectibleBase::OnCollect(player);
	}
}