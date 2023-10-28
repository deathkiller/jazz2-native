#include "BonusWarp.h"
#include "../../ILevelHandler.h"
#include "../../Events/EventMap.h"
#include "../Player.h"

namespace Jazz2::Actors::Environment
{
	BonusWarp::BonusWarp()
		:
		_warpTarget(UINT8_MAX),
		_cost(UINT8_MAX),
		_setLaps(false),
		_fast(false)
	{
	}

	Task<bool> BonusWarp::OnActivatedAsync(const ActorActivationDetails& details)
	{
		_warpTarget = details.Params[0];
		_fast = (details.Params[1] != 0);
		_setLaps = details.Params[2] != 0;
		_cost = details.Params[3];
		// TODO: Show rabbit for non-listed number of coins (use JJ2+ anim set 8)
		//_showAnim = details.Params[4] != 0;

		SetState(ActorState::CanBeFrozen, false);
		_renderer.setLayer(_renderer.layer() - 20);

		async_await RequestMetadataAsync("Object/BonusWarp"_s);

		switch (_cost) {
			case 10:
			case 20:
			case 50:
			case 100:
				SetAnimation((AnimState)_cost);
				break;
			default:
				// TODO: Show rabbit + coins needed, if (showAnim)
				SetAnimation(AnimState::Default);
				break;
		}

		async_return true;
	}

	void BonusWarp::OnUpdateHitbox()
	{
		UpdateHitbox(28, 28);
	}

	void BonusWarp::Activate(Player* player)
	{
		auto events = _levelHandler->EventMap();
		if (events == nullptr) {
			return;
		}

		Vector2f targetPos = events->GetWarpTarget(_warpTarget);
		if (targetPos.X < 0.0f || targetPos.Y < 0.0f) {
			// Warp target not found
			return;
		}

		player->WarpToPosition(targetPos, _fast);
	}
}