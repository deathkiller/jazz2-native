#include "PlayerCorpse.h"
#include "../LevelInitialization.h"

namespace Jazz2::Actors
{
	PlayerCorpse::PlayerCorpse()
	{
	}

	Task<bool> PlayerCorpse::OnActivatedAsync(const ActorActivationDetails& details)
	{
		// TODO: change these types to uint8_t
		PlayerType playerType = (PlayerType)details.Params[0];
		SetFacingLeft(details.Params[1] != 0);

		switch (playerType) {
			default:
			case PlayerType::Jazz:
				co_await RequestMetadataAsync("Interactive/PlayerJazz");
				break;
			case PlayerType::Spaz:
				co_await RequestMetadataAsync("Interactive/PlayerSpaz");
				break;
			case PlayerType::Lori:
				co_await RequestMetadataAsync("Interactive/PlayerLori");
				break;
		}

		SetAnimation("Corpse");

		CollisionFlags = CollisionFlags::ForceDisableCollisions;

		co_return true;
	}
}