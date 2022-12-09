#include "IceBlock.h"
#include "../../ILevelHandler.h"
#include "../../Events/EventMap.h"
#include "../Explosion.h"
#include "../Player.h"
#include "../Weapons/ShotBase.h"
#include "../Weapons/TNT.h"

namespace Jazz2::Actors::Environment
{
	IceBlock::IceBlock()
		:
		_timeLeft(200.0f)
	{
	}

	void IceBlock::Preload(const ActorActivationDetails& details)
	{
		PreloadMetadataAsync("Object/IceBlock"_s);
	}

	Task<bool> IceBlock::OnActivatedAsync(const ActorActivationDetails& details)
	{
		SetState(ActorState::CanBeFrozen, false);

		async_await RequestMetadataAsync("Object/IceBlock"_s);
		SetAnimation("IceBlock"_s);

		async_return true;
	}

	void IceBlock::OnUpdate(float timeMult)
	{
		SolidObjectBase::OnUpdate(timeMult);

		_timeLeft -= timeMult;
		if (_timeLeft <= 0.0f) {
			float newAlpha = _renderer.alpha() - (0.02f * timeMult);
			if (newAlpha > 0.0f) {
				_renderer.setAlphaF(newAlpha);
			} else {
				DecreaseHealth(INT32_MAX);
			}
		}
	}
}