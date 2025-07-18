#include "Stopwatch.h"
#include "../Player.h"
#include "../../ILevelHandler.h"

#include "../../../nCine/Base/FrameTimer.h"

namespace Jazz2::Actors::Collectibles
{
	Stopwatch::Stopwatch()
	{
	}

	void Stopwatch::Preload(const ActorActivationDetails& details)
	{
		PreloadMetadataAsync("Collectible/Stopwatch"_s);
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
		bool timeIncreased = player->IncreaseShieldTime(10.0f * FrameTimer::FramesPerSecond);
		// Always collect if Reforged is disabled
		if (timeIncreased || !_levelHandler->IsReforged()) {
			CollectibleBase::OnCollect(player);
		}
	}
}