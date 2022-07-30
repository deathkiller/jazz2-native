#include "CoinCollectible.h"
#include "../../LevelInitialization.h"
#include "../Player.h"

namespace Jazz2::Actors::Collectibles
{
	CoinCollectible::CoinCollectible()
		:
		_coinValue(0)
	{
	}

	Task<bool> CoinCollectible::OnActivatedAsync(const ActorActivationDetails& details)
	{
		co_await CollectibleBase::OnActivatedAsync(details);

		uint8_t coinType = details.Params[0];

		co_await RequestMetadataAsync("Collectible/Coins"_s);

		switch (coinType) {
			default:
			case 0: // Silver
				_coinValue = 1;
				_scoreValue = 500;
				SetAnimation("CoinSilver"_s);
				break;
			case 1: // Gold
				_coinValue = 5;
				_scoreValue = 1000;
				SetAnimation("CoinGold"_s);
				break;
		}

		SetFacingDirection();

		co_return true;
	}

	void CoinCollectible::OnCollect(Player* player)
	{
		player->AddCoins(_coinValue);

		CollectibleBase::OnCollect(player);
	}
}