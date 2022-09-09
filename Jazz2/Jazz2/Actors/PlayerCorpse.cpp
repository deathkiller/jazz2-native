#include "PlayerCorpse.h"
#include "../LevelInitialization.h"

namespace Jazz2::Actors
{
	PlayerCorpse::PlayerCorpse()
	{
	}

	Task<bool> PlayerCorpse::OnActivatedAsync(const ActorActivationDetails& details)
	{
		PlayerType playerType = (PlayerType)details.Params[0];
		SetFacingLeft(details.Params[1] != 0);

		SetState(ActorState::ForceDisableCollisions, true);
		SetState(ActorState::CollideWithTileset | ActorState::CollideWithOtherActors | ActorState::ApplyGravitation, false);

		switch (playerType) {
			default:
			case PlayerType::Jazz:
				co_await RequestMetadataAsync("Interactive/PlayerJazz"_s);
				break;
			case PlayerType::Spaz:
				co_await RequestMetadataAsync("Interactive/PlayerSpaz"_s);
				break;
			case PlayerType::Lori:
				co_await RequestMetadataAsync("Interactive/PlayerLori"_s);
				break;
		}

		SetAnimation("Corpse"_s);

		co_return true;
	}
}