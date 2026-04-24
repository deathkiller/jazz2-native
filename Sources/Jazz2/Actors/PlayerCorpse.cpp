#include "PlayerCorpse.h"
#include "../PlayerType.h"

namespace Jazz2::Actors
{
	PlayerCorpse::PlayerCorpse()
	{
	}

	Task<bool> PlayerCorpse::OnActivatedAsync(const ActorActivationDetails& details)
	{
		PlayerType playerType = (PlayerType)details.Params[0];
		SetFacingLeft(details.Params[1] != 0);
		std::uint8_t birdColorVariant = details.Params[2];

		SetState(ActorState::PreserveOnRollback, true);
		SetState(ActorState::CanBeFrozen | ActorState::CollideWithOtherActors, false);

		switch (playerType) {
			default:
			case PlayerType::Jazz:
				async_await RequestMetadataAsync("Interactive/PlayerJazz"_s);
				break;
			case PlayerType::Spaz:
				async_await RequestMetadataAsync("Interactive/PlayerSpaz"_s);
				break;
			case PlayerType::Lori:
				async_await RequestMetadataAsync("Interactive/PlayerLori"_s);
				break;
			case PlayerType::Frog:
				async_await RequestMetadataAsync("Interactive/PlayerFrog"_s);
				break;
			case PlayerType::Bird:
				if (birdColorVariant == 0) {
					async_await RequestMetadataAsync("Interactive/PlayerBird"_s);
				} else {
					async_await RequestMetadataAsync("Interactive/PlayerBirdYellow"_s);
				}
				break;
		}

		SetAnimation((AnimState)536870912);

		async_return true;
	}
}