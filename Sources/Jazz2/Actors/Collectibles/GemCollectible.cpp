#include "GemCollectible.h"
#include "../Player.h"
#include "../../ILevelHandler.h"

namespace Jazz2::Actors::Collectibles
{
	GemCollectible::GemCollectible()
		: _ignoreTime(0.0f), _gemType(0)
	{
	}

	void GemCollectible::Preload(const ActorActivationDetails& details)
	{
		PreloadMetadataAsync("Collectible/Gems"_s);
	}

	Task<bool> GemCollectible::OnActivatedAsync(const ActorActivationDetails& details)
	{
		async_await CollectibleBase::OnActivatedAsync(details);

		_gemType = (std::uint8_t)(details.Params[0] & 0x03);
		bool hasLimitedLifetime = (details.Params[1] & 0x01) == 0x01;
		bool isFlipped = (details.Params[1] & 0x02) == 0x02;
		bool isDelayed = (details.Params[1] & 0x04) == 0x04;

		if (hasLimitedLifetime && _levelHandler->CanEventDisappear(EventType::Gem)) {
			_timeLeft = 8.0f * FrameTimer::FramesPerSecond;
		}
		if (isDelayed) {
			_ignoreTime = 40.0f;
		}

		async_await RequestMetadataAsync("Collectible/Gems"_s);

		std::int32_t weightedCount;
		switch (_gemType) {
			default:
			case 0: // Red (+1)
				weightedCount = 1;
				break;
			case 1: // Green (+5)
				weightedCount = 5;
				break;
			case 2: // Blue (+10)
				weightedCount = 10;
				break;
			case 3: // Purple
				weightedCount = 1;
				break;
		}
		_scoreValue = weightedCount * 100;

		SetAnimation((AnimState)_gemType);
		SetFacingDirection(isFlipped);

		_renderer.setAlphaF(0.7f);

		async_return true;
	}

	void GemCollectible::OnUpdate(float timeMult)
	{
		CollectibleBase::OnUpdate(timeMult);

		if (_ignoreTime > 0.0f) {
			_ignoreTime -= timeMult;
		}
	}

	void GemCollectible::OnUpdateHitbox()
	{
		UpdateHitbox(20, 20);
	}

	void GemCollectible::OnCollect(Player* player)
	{
		if (_ignoreTime > 0.0f) {
			return;
		}

		player->AddGems(_gemType, 1);

		CollectibleBase::OnCollect(player);
	}
}