#include "CtfBase.h"

#if defined(WITH_MULTIPLAYER)

#include "../../Multiplayer/Teams.h"

namespace Jazz2::Actors::Multiplayer
{
	CtfBase::CtfBase()
		: _team(0)
	{
	}

	void CtfBase::Preload(const ActorActivationDetails& details)
	{
		PreloadMetadataAsync("Object/CtfBase"_s);
	}

	Task<bool> CtfBase::OnActivatedAsync(const ActorActivationDetails& details)
	{
		_team = details.Params[0];

		SetState(ActorState::ApplyGravitation | ActorState::CanBeFrozen, false);
		SetState(ActorState::ForceDisableCollisions, true);

		async_await RequestMetadataAsync("Object/CtfBase"_s);
		// CTF/base.aura has one frame per team color; each is exposed as a single-frame animation (state = team)
		SetAnimation((AnimState)(_team < Jazz2::Multiplayer::MaxTeamCount ? _team : 0));

		async_return true;
	}
}

#endif
