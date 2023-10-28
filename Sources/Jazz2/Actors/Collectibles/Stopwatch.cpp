#include "Stopwatch.h"
#include "../Player.h"

#include "../../../nCine/Base/FrameTimer.h"

namespace Jazz2::Actors::Collectibles
{
	Stopwatch::Stopwatch()
	{
	}

	Task<bool> Stopwatch::OnActivatedAsync(const ActorActivationDetails& details)
	{
		async_await CollectibleBase::OnActivatedAsync(details);

		async_await RequestMetadataAsync("Collectible/Stopwatch"_s);

		SetAnimation(AnimState::Default);
		SetFacingDirection();

		async_return true;
	}

	void Stopwatch::OnCollect(Player* player)
	{
		if (player->IncreaseShieldTime(10.0f * FrameTimer::FramesPerSecond)) {
			CollectibleBase::OnCollect(player);
		}
	}
}