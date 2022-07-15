#include "BonusWarp.h"
#include "../../ILevelHandler.h"
#include "../../Events/EventMap.h"
#include "../Player.h"

namespace Jazz2::Actors::Environment
{
	BonusWarp::BonusWarp()
		:
		_warpTarget(UINT16_MAX),
		_cost(INT16_MAX),
		_setLaps(false),
		_fast(false)
	{
	}

	Task<bool> BonusWarp::OnActivatedAsync(const ActorActivationDetails& details)
	{
		_warpTarget = *(uint16_t*)&details.Params[0];
		_fast = (details.Params[2] != 0);
		_setLaps = details.Params[4] != 0;
		_cost = *(uint16_t*)&details.Params[6];
		// ToDo: Show rabbit for non-listed number of coins (use JJ2+ anim set 8)
		//_showAnim = details.Params[4] != 0;

		SetState(ActorFlags::CanBeFrozen, false);

		co_await RequestMetadataAsync("Object/BonusWarp");

		switch (_cost) {
			case 10:
				SetAnimation("Bonus10");
				break;
			case 20:
				SetAnimation("Bonus20");
				break;
			case 50:
				SetAnimation("Bonus50");
				break;
			case 100:
				SetAnimation("Bonus100");
				break;
			default:
				// ToDo: Show rabbit + coins needed, if (showAnim)
				SetAnimation("BonusGeneric");
				break;
		}

		co_return true;
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