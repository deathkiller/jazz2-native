#include "Eva.h"
#include "../../ILevelHandler.h"
#include "../Player.h"

#include "../../../nCine/Base/Random.h"

namespace Jazz2::Actors::Environment
{
	Eva::Eva()
		:
		_animationTime(0.0f)
	{
	}

	Task<bool> Eva::OnActivatedAsync(const ActorActivationDetails& details)
	{
		SetState(ActorState::ApplyGravitation, false);

		async_await RequestMetadataAsync("Object/Eva"_s);

		SetAnimation(AnimState::Idle);

		async_return true;
	}

	void Eva::OnUpdate(float timeMult)
	{
		ActorBase::OnUpdate(timeMult);

		if (_currentTransition == nullptr) {
			if (_animationTime <= 0.0f) {
				SetTransition(AnimState::TransitionIdleBored, true);
				_animationTime = Random().NextFloat(160.0f, 200.0f);
			} else {
				_animationTime -= timeMult;
			}
		}
	}

	bool Eva::OnHandleCollision(std::shared_ptr<ActorBase> other)
	{
		if (auto player = dynamic_cast<Player*>(other.get())) {
			if (player->GetPlayerType() == PlayerType::Frog && player->DisableControllable(160.0f)) {
				SetTransition(AnimState::TransitionAttack, false, [this, player]() {
					player->MorphRevert();

					PlaySfx("Kiss"_s, 0.8f);
					SetTransition(AnimState::TransitionAttackEnd, false);
				});
			}
			return true;
		}

		return ActorBase::OnHandleCollision(other);
	}
}