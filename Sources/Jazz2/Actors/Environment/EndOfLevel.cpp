#include "EndOfLevel.h"
#include "../../ILevelHandler.h"
#include "../Player.h"
#include "../Weapons/ShotBase.h"

namespace Jazz2::Actors::Environment
{
	EndOfLevel::EndOfLevel()
		: _exitType(ExitType::None)
	{
	}

	void EndOfLevel::Preload(const ActorActivationDetails& details)
	{
		PreloadMetadataAsync("Object/SignEol"_s);
	}

	Task<bool> EndOfLevel::OnActivatedAsync(const ActorActivationDetails& details)
	{
		_exitType = (ExitType)details.Params[0];

		SetState(ActorState::CanBeFrozen, false);
		_renderer.SetLayer(_renderer.GetLayer() - 40);

		async_await RequestMetadataAsync("Object/SignEol"_s);

		SetAnimation(AnimState::Default);

		async_return true;
	}

	void EndOfLevel::OnUpdateHitbox()
	{
		UpdateHitbox(24, 24);
	}

	bool EndOfLevel::OnHandleCollision(ActorBase* other)
	{
		// The original ends the level when the sign is shot, it takes no notice of a player touching it
		if (auto* shotBase = runtime_cast<Weapons::ShotBase>(other)) {
			Player* owner = shotBase->GetOwner();
			if (owner != nullptr && shotBase->GetStrength() > 0 && owner->GetHealth() > 0 &&
				_levelHandler->CanActivateSugarRush() && (_levelHandler->IsLocalSession() || _levelHandler->IsServer()) &&
				_levelHandler->CanTakeLevelExit(_exitType, {})) {
				_levelHandler->BeginLevelChange(owner, _exitType);
				shotBase->DecreaseHealth(1);
				return true;
			}
		}

		return ActorBase::OnHandleCollision(other);
	}
}