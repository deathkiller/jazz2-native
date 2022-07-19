#include "EndOfLevel.h"
#include "../../ILevelHandler.h"

namespace Jazz2::Actors::Environment
{
	EndOfLevel::EndOfLevel()
		:
		_exitType(ExitType::None)
	{
	}

	Task<bool> EndOfLevel::OnActivatedAsync(const ActorActivationDetails& details)
	{
		_exitType = (ExitType)details.Params[0];

		SetState(ActorFlags::CanBeFrozen, false);

		co_await RequestMetadataAsync("Object/SignEol");

		SetAnimation("SignEol");

		co_return true;
	}

	void EndOfLevel::OnUpdateHitbox()
	{
		UpdateHitbox(24, 24);
	}
}