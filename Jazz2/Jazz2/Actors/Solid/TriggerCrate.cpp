#include "TriggerCrate.h"
#include "../../LevelInitialization.h"
#include "../../ILevelHandler.h"
#include "../../Tiles/TileMap.h"

#include "../Player.h"
#include "../Weapons/ShotBase.h"

namespace Jazz2::Actors::Solid
{
	TriggerCrate::TriggerCrate()
	{
	}

	void TriggerCrate::Preload(const ActorActivationDetails& details)
	{
		PreloadMetadataAsync("Object/TriggerCrate");
	}

	Task<bool> TriggerCrate::OnActivatedAsync(const ActorActivationDetails& details)
	{
		_triggerId = *(uint16_t*)&details.Params[0];
		if (details.Params[4] != 0) {
			_newState = TriggerCrateState::Toggle;
		} else {
			_newState = (details.Params[2] != 0 ? TriggerCrateState::On : TriggerCrateState::Off);
		}

		Movable = true;
		CollisionFlags |= CollisionFlags::SkipPerPixelCollisions;

		co_await RequestMetadataAsync("Object/TriggerCrate");

		SetAnimation("Crate");

		co_return true;
	}

	bool TriggerCrate::OnHandleCollision(ActorBase* other)
	{
		if (_health == 0) {
			return SolidObjectBase::OnHandleCollision(other);
		}

		if (auto shotBase = dynamic_cast<Weapons::ShotBase*>(other)) {
			WeaponType weaponType = shotBase->GetWeaponType();
			if (weaponType == WeaponType::RF || weaponType == WeaponType::Seeker ||
				weaponType == WeaponType::Pepper || weaponType == WeaponType::Electro) {
				DecreaseHealth(shotBase->GetStrength(), other);
				shotBase->DecreaseHealth(INT32_MAX);
				return true;
			}
		} /*else if (auto shotTnt = dynamic_cast<Weapons::ShotTNT*>(other)) {
			// TODO: TNT
		}*/ else if (auto player = dynamic_cast<Player*>(other)) {
			if (player->CanBreakSolidObjects()) {
				DecreaseHealth(INT32_MAX, other);
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

		PlaySfx("Break");

		CreateParticleDebris();

		//Explosion.Create(levelHandler, Transform.Pos, Explosion.SmokeBrown);

		return SolidObjectBase::OnPerish(collider);
	}
}