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

		SetState(ActorState::CanBeFrozen, false);
		_renderer.setLayer(_renderer.layer() - 40);

		async_await RequestMetadataAsync("Object/SignEol"_s);

		SetAnimation(AnimState::Default);

		async_return true;
	}

	void EndOfLevel::OnUpdateHitbox()
	{
		UpdateHitbox(24, 24);
	}
}