#include "DevanRemote.h"
#include "../../../ILevelHandler.h"
#include "../../Player.h"
#include "../../Explosion.h"

#include "../../../../nCine/Base/Random.h"

#include <float.h>

namespace Jazz2::Actors::Bosses
{
	DevanRemote::DevanRemote()
		:
		_introText(0),
		_endText(0)
	{
	}

	DevanRemote::~DevanRemote()
	{
		if (_robot != nullptr) {
			_robot->Deactivate();
			_robot = nullptr;
		}
	}

	void DevanRemote::Preload(const ActorActivationDetails& details)
	{
		PreloadMetadataAsync("Boss/DevanRemote"_s);
	}

	Task<bool> DevanRemote::OnActivatedAsync(const ActorActivationDetails& details)
	{
		_introText = details.Params[1];
		_endText = details.Params[2];

		SetState(ActorState::CollideWithOtherActors | ActorState::CanBeFrozen, false);

		async_await RequestMetadataAsync("Boss/DevanRemote"_s);
		SetAnimation(AnimState::Idle);

		SetFacingLeft(true);

		async_return true;
	}

	bool DevanRemote::OnActivatedBoss()
	{
		StringView text = _levelHandler->GetLevelText(_introText);
		_levelHandler->ShowLevelText(text);

		auto actors = _levelHandler->GetActors();
		for (auto& actor : actors) {
			if (auto* robot = runtime_cast<Robot*>(actor)) {
				_robot = std::shared_ptr<Robot>(actor, robot);
				_robot->Activate();

				// Copy health to Devan to enable HealthBar
				_health = _robot->GetHealth();
				_maxHealth = _robot->GetMaxHealth();
				break;
			}
		}

		return true;
	}

	bool DevanRemote::OnPlayerDied()
	{
		if (_robot != nullptr) {
			_robot->Deactivate();
			_robot = nullptr;
		}

		return BossBase::OnPlayerDied();
	}

	void DevanRemote::OnUpdate(float timeMult)
	{
		BossBase::OnUpdate(timeMult);

		if (_robot != nullptr) {
			int robotHealth = _robot->GetHealth();
			if (robotHealth <= 0) {
				_robot = nullptr;

				_health = 1;

				StringView text = _levelHandler->GetLevelText(_endText, -1, '|');
				_levelHandler->ShowLevelText(text);

				PlaySfx("WarpOut"_s);
				SetTransition(AnimState::TransitionWarpOut, false, [this]() {
					_renderer.setDrawEnabled(false);
					DecreaseHealth(INT32_MAX);
				});
			} else {
				_health = robotHealth;
			}
		}
	}
}