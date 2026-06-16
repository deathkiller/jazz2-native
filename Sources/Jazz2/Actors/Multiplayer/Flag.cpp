#include "Flag.h"

#if defined(WITH_MULTIPLAYER)

namespace Jazz2::Actors::Multiplayer
{
	Flag::Flag()
		: _team(0)
	{
	}

	void Flag::Preload(const ActorActivationDetails& details)
	{
		PreloadMetadataAsync("Object/CtfFlag"_s);
	}

	Task<bool> Flag::OnActivatedAsync(const ActorActivationDetails& details)
	{
		_team = details.Params[0];

		SetState(ActorState::ApplyGravitation | ActorState::CanBeFrozen, false);
		SetState(ActorState::ForceDisableCollisions, true);

		async_await RequestMetadataAsync("Object/CtfFlag"_s);
		// Only blue (state 0) and red (state 1) flag sprites exist in the original game, so teams beyond the
		// first two reuse them by parity
		SetAnimation((AnimState)(_team % 2));

		async_return true;
	}
}

#endif
