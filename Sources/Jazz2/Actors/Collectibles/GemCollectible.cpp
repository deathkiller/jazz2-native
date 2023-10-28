#include "GemCollectible.h"
#include "../Player.h"

namespace Jazz2::Actors::Collectibles
{
	GemCollectible::GemCollectible()
		:
		_gemType(0)
	{
	}

	Task<bool> GemCollectible::OnActivatedAsync(const ActorActivationDetails& details)
	{
		async_await CollectibleBase::OnActivatedAsync(details);

		_gemType = (uint8_t)(details.Params[0] & 0x03);

		async_await RequestMetadataAsync("Collectible/Gems"_s);

		switch (_gemType) {
			default:
			case 0: // Red (+1)
				_scoreValue = 100;
				break;
			case 1: // Green (+5)
				_scoreValue = 500;
				break;
			case 2: // Blue (+10)
				_scoreValue = 1000;
				break;
			case 3: // Purple
				_scoreValue = 100;
				break;
		}

		SetAnimation((AnimState)_gemType);
		SetFacingDirection();

		_renderer.setAlphaF(0.7f);

		async_return true;
	}

	void GemCollectible::OnUpdateHitbox()
	{
		UpdateHitbox(20, 20);
	}

	void GemCollectible::OnCollect(Player* player)
	{
		int value;
		switch (_gemType) {
			default:
			case 0: // Red (+1)
				value = 1;
				break;
			case 1: // Green (+5)
				value = 5;
				break;
			case 2: // Blue (+10)
				value = 10;
				break;
			case 3: // Purple
				value = 1;
				break;
		}

		player->AddGems(value);

		CollectibleBase::OnCollect(player);
	}
}