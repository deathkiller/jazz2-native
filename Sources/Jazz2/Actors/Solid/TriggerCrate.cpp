#include "TriggerCrate.h"
#include "../../ILevelHandler.h"
#include "../../Tiles/TileMap.h"
#include "../Explosion.h"
#include "../Player.h"
#include "../Weapons/ShotBase.h"
#include "../Weapons/TNT.h"

namespace Jazz2::Actors::Solid
{
	TriggerCrate::TriggerCrate()
	{
	}

	void TriggerCrate::Preload(const ActorActivationDetails& details)
	{
		PreloadMetadataAsync("Object/TriggerCrate"_s);
	}

	Task<bool> TriggerCrate::OnActivatedAsync(const ActorActivationDetails& details)
	{
		_triggerId = details.Params[0];
		if (details.Params[2] != 0) {
			_newState = TriggerCrateState::Toggle;
		} else {
			_newState = (details.Params[1] != 0 ? TriggerCrateState::On : TriggerCrateState::Off);
		}

		SetState(ActorState::TriggersTNT, true);
		Movable = true;

		async_await RequestMetadataAsync("Object/TriggerCrate"_s);

		SetAnimation("Crate"_s);

		async_return true;
	}

	bool TriggerCrate::OnHandleCollision(std::shared_ptr<ActorBase> other)
	{
		if (_health == 0) {
			return SolidObjectBase::OnHandleCollision(other);
		}

		if (auto shotBase = dynamic_cast<Weapons::ShotBase*>(other.get())) {
			WeaponType weaponType = shotBase->GetWeaponType();
			if (weaponType == WeaponType::RF || weaponType == WeaponType::Seeker ||
				weaponType == WeaponType::Pepper || weaponType == WeaponType::Electro) {
				DecreaseHealth(shotBase->GetStrength(), shotBase);
				shotBase->DecreaseHealth(INT32_MAX);
			} else {
				shotBase->TriggerRicochet(this);
			}
			return true;
		} else if (auto tnt = dynamic_cast<Weapons::TNT*>(other.get())) {
			DecreaseHealth(INT32_MAX, tnt);
			return true;
		} else if (auto player = dynamic_cast<Player*>(other.get())) {
			if (player->CanBreakSolidObjects()) {
				DecreaseHealth(INT32_MAX, player);
				return true;
			}
		}

		return SolidObjectBase::OnHandleCollision(other);
	}

	bool TriggerCrate::OnPerish(ActorBase* collider)
	{
		auto tiles = _levelHandler->TileMap();
		if (tiles != nullptr) {
			if (_newState == TriggerCrateState::Toggle) {
				// Toggle
				tiles->SetTrigger(_triggerId, !tiles->GetTrigger(_triggerId));
			} else {
				// Turn off/on
				tiles->SetTrigger(_triggerId, _newState == TriggerCrateState::On);
			}
		}

		PlaySfx("Break"_s);

		CreateParticleDebris();

		Explosion::Create(_levelHandler, Vector3i((int)_pos.X, (int)_pos.Y, _renderer.layer() + 2), Explosion::Type::SmokeBrown);

		return SolidObjectBase::OnPerish(collider);
	}
}