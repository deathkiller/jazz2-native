#include "FastFireCollectible.h"
#include "../../ILevelHandler.h"
#include "../Player.h"

namespace Jazz2::Actors::Collectibles
{
	FastFireCollectible::FastFireCollectible()
	{
	}

	void FastFireCollectible::Preload(const ActorActivationDetails& details)
	{
		// Preloading is not supported here
	}

	Task<bool> FastFireCollectible::OnActivatedAsync(const ActorActivationDetails& details)
	{
		async_await CollectibleBase::OnActivatedAsync(details);

		_scoreValue = 200;

		auto players = _levelHandler->GetPlayers();
		PlayerType playerType = (!players.empty() ? players[0]->GetPlayerType() : PlayerType::Jazz);
		switch (playerType) {
			default:
			case PlayerType::Jazz: async_await RequestMetadataAsync("Collectible/FastFireJazz"_s); break;
			case PlayerType::Spaz: async_await RequestMetadataAsync("Collectible/FastFireSpaz"_s); break;
			case PlayerType::Lori: async_await RequestMetadataAsync("Collectible/FastFireLori"_s); break;
		}

		SetAnimation(AnimState::Default);

		SetFacingDirection();

		async_return true;
	}

	void FastFireCollectible::OnCollect(Player* player)
	{
		if (player->AddFastFire(1)) {
			CollectibleBase::OnCollect(player);
		}
	}
}