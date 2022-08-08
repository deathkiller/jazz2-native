#include "FastFireCollectible.h"
#include "../../ILevelHandler.h"
#include "../../LevelInitialization.h"
#include "../Player.h"

namespace Jazz2::Actors::Collectibles
{
	FastFireCollectible::FastFireCollectible()
	{
	}

	void FastFireCollectible::Preload(const ActorActivationDetails& details)
	{
		// TODO
	}

	Task<bool> FastFireCollectible::OnActivatedAsync(const ActorActivationDetails& details)
	{
		co_await CollectibleBase::OnActivatedAsync(details);

		_scoreValue = 200;

		auto& players = _levelHandler->GetPlayers();
		PlayerType playerType = (!players.empty() ? players[0]->GetPlayerType() : PlayerType::Jazz);
		switch (playerType) {
			default:
			case PlayerType::Jazz: co_await RequestMetadataAsync("Collectible/FastFireJazz"_s); break;
			case PlayerType::Spaz: co_await RequestMetadataAsync("Collectible/FastFireSpaz"_s); break;
			case PlayerType::Lori: co_await RequestMetadataAsync("Collectible/FastFireLori"_s); break;
		}

		SetAnimation("FastFire"_s);

		SetFacingDirection();

		co_return true;
	}

	void FastFireCollectible::OnCollect(Player* player)
	{
		if (player->AddFastFire(1)) {
			CollectibleBase::OnCollect(player);
		}
	}
}