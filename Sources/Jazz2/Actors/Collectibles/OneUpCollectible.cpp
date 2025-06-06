﻿#include "OneUpCollectible.h"
#include "../Player.h"

namespace Jazz2::Actors::Collectibles
{
	OneUpCollectible::OneUpCollectible()
	{
	}

	void OneUpCollectible::Preload(const ActorActivationDetails& details)
	{
		PreloadMetadataAsync("Collectible/OneUp"_s);
	}

	Task<bool> OneUpCollectible::OnActivatedAsync(const ActorActivationDetails& details)
	{
		async_await CollectibleBase::OnActivatedAsync(details);

		_scoreValue = 2000;

		async_await RequestMetadataAsync("Collectible/OneUp"_s);

		SetAnimation(AnimState::Default);

		async_return true;
	}

	void OneUpCollectible::OnCollect(Player* player)
	{
		if (player->AddLives(1)) {
			CollectibleBase::OnCollect(player);
		}
	}
}