#include "CoinCollectible.h"
#include "../Player.h"
#include "../../ILevelHandler.h"

namespace Jazz2::Actors::Collectibles
{
	CoinCollectible::CoinCollectible()
		: _isGold(false), _transmuted(false)
	{
	}

	void CoinCollectible::Preload(const ActorActivationDetails& details)
	{
		PreloadMetadataAsync("Collectible/Coins"_s);
		PreloadMetadataAsync("Collectible/Gems"_s);
	}

	Task<bool> CoinCollectible::OnActivatedAsync(const ActorActivationDetails& details)
	{
		async_await CollectibleBase::OnActivatedAsync(details);

		uint8_t coinType = details.Params[0];
		switch (coinType) {
			default:
			case 0: // Silver
				_scoreValue = 500;
				break;
			case 1: // Gold
				_scoreValue = 1000;
				break;
		}

		async_await RequestMetadataAsync("Collectible/Coins"_s);
		SetAnimation((AnimState)coinType);
		SetFacingDirection();

		_isGold = (coinType == 1);
		if (_levelHandler->AreCoinsTransmuted()) {
			Transmute();
		}

		async_return true;
	}

	void CoinCollectible::OnUpdate(float timeMult)
	{
		if (!_transmuted && _levelHandler->AreCoinsTransmuted()) {
			Transmute();
		}

		CollectibleBase::OnUpdate(timeMult);
	}

	void CoinCollectible::OnUpdateHitbox()
	{
		if (_transmuted) {
			UpdateHitbox(20, 20);
		} else {
			CollectibleBase::OnUpdateHitbox();
		}
	}

	void CoinCollectible::Transmute()
	{
		_transmuted = true;
		// Gold becomes a green gem, silver a red one
		_scoreValue = (_isGold ? 500 : 100);

		RequestMetadata("Collectible/Gems"_s);
		SetAnimation((AnimState)(_isGold ? 1 : 0));
		_renderer.SetAlphaF(0.7f);
	}

	void CoinCollectible::OnCollect(Player* player)
	{
		if (_transmuted) {
			player->AddGems(_isGold ? 1 : 0, 1);
		} else {
			player->AddCoins(GetCoinValue());
		}

		CollectibleBase::OnCollect(player);
	}
}