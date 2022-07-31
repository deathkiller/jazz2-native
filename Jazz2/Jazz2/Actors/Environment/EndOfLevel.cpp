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
		_renderer.setLayer(_renderer.layer() - 40);

		co_await RequestMetadataAsync("Object/SignEol"_s);

		SetAnimation("SignEol"_s);

		co_return true;
	}

	void EndOfLevel::OnUpdateHitbox()
	{
		UpdateHitbox(24, 24);
	}
}