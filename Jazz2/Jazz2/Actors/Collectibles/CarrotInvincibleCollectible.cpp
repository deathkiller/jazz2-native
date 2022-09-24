#include "CarrotInvincibleCollectible.h"
#include "../../LevelInitialization.h"
#include "../Player.h"

#include "../../../nCine/Base/FrameTimer.h"

namespace Jazz2::Actors::Collectibles
{
	CarrotInvincibleCollectible::CarrotInvincibleCollectible()
	{
	}

	Task<bool> CarrotInvincibleCollectible::OnActivatedAsync(const ActorActivationDetails& details)
	{
		co_await CollectibleBase::OnActivatedAsync(details);

		_scoreValue = 500;

		co_await RequestMetadataAsync("Collectible/CarrotInvincible"_s);

		SetAnimation("Carrot"_s);
		SetFacingDirection();

		co_return true;
	}

	void CarrotInvincibleCollectible::OnCollect(Player* player)
	{
		player->SetInvulnerability(30.0f * FrameTimer::FramesPerSecond, true);

		CollectibleBase::OnCollect(player);
	}
}