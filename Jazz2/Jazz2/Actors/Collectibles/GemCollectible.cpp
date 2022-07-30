#include "GemCollectible.h"
#include "../../LevelInitialization.h"
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
		co_await CollectibleBase::OnActivatedAsync(details);

		_gemType = (uint16_t)(details.Params[0] & 0x3);

		co_await RequestMetadataAsync("Collectible/Gems"_s);

		switch (_gemType) {
			default:
			case 0: // Red (+1)
				_scoreValue = 100;
				SetAnimation("GemRed"_s);
				break;
			case 1: // Green (+5)
				_scoreValue = 500;
				SetAnimation("GemGreen"_s);
				break;
			case 2: // Blue (+10)
				_scoreValue = 1000;
				SetAnimation("GemBlue"_s);
				break;
			case 3: // Purple
				_scoreValue = 100;
				SetAnimation("GemPurple"_s);
				break;
		}

		SetFacingDirection();

		co_return true;
	}

	void GemCollectible::OnUpdateHitbox()
	{
		UpdateHitbox(18, 18);
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