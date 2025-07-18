#include "CarrotCollectible.h"
#include "../Player.h"
#include "../../ILevelHandler.h"

#include "../../../nCine/Base/FrameTimer.h"

namespace Jazz2::Actors::Collectibles
{
	CarrotCollectible::CarrotCollectible()
		: _maxCarrot(false)
	{
	}

	void CarrotCollectible::Preload(const ActorActivationDetails& details)
	{
		bool maxCarrot = (details.Params[0] != 0);
		if (maxCarrot) {
			PreloadMetadataAsync("Collectible/CarrotFull"_s);
		} else {
			PreloadMetadataAsync("Collectible/Carrot"_s);
		}
	}

	Task<bool> CarrotCollectible::OnActivatedAsync(const ActorActivationDetails& details)
	{
		async_await CollectibleBase::OnActivatedAsync(details);

		_maxCarrot = (details.Params[0] != 0);

		if (_maxCarrot) {
			_scoreValue = 500;
			async_await RequestMetadataAsync("Collectible/CarrotFull"_s);
		} else {
			_scoreValue = 200;
			async_await RequestMetadataAsync("Collectible/Carrot"_s);
		}
		SetAnimation(AnimState::Default);
		SetFacingDirection();

		async_return true;
	}

	void CarrotCollectible::OnCollect(Player* player)
	{
		if (_maxCarrot) {
			bool healthNotFull = player->AddHealth(-1);
			// Always collect if Reforged is enabled
			if (healthNotFull || _levelHandler->IsReforged()) {
				player->SetInvulnerability(5.0f * FrameTimer::FramesPerSecond, Player::InvulnerableType::Shielded);
				CollectibleBase::OnCollect(player);
			}
		} else {
			if (player->AddHealth(1)) {
				player->SetInvulnerability(0.8f * FrameTimer::FramesPerSecond, Player::InvulnerableType::Shielded);
				CollectibleBase::OnCollect(player);
			}
		}
	}
}