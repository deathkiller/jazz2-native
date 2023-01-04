#include "PushableBox.h"
#include "../../ILevelHandler.h"
#include "../../Tiles/TileMap.h"

#include "../Explosion.h"
#include "../Player.h"
#include "../Weapons/ShotBase.h"

namespace Jazz2::Actors::Solid
{
	PushableBox::PushableBox()
	{
	}

	void PushableBox::Preload(const ActorActivationDetails& details)
	{
		uint8_t theme = details.Params[0];
		switch (theme) {
			default:
			case 0: PreloadMetadataAsync("Object/PushBoxRock"_s); break;
			case 1: PreloadMetadataAsync("Object/PushBoxCrate"_s); break;
		}
	}

	Task<bool> PushableBox::OnActivatedAsync(const ActorActivationDetails& details)
	{
		uint8_t theme = details.Params[0];

		Movable = true;

		switch (theme) {
			default:
			case 0: async_await RequestMetadataAsync("Object/PushBoxRock"); break;
			case 1: async_await RequestMetadataAsync("Object/PushBoxCrate"); break;
		}

		SetAnimation("PushBox"_s);

		async_return true;
	}

	bool PushableBox::OnHandleCollision(std::shared_ptr<ActorBase> other)
	{
		if (auto shotBase = dynamic_cast<Weapons::ShotBase*>(other.get())) {
			WeaponType weaponType = shotBase->GetWeaponType();
			if (weaponType == WeaponType::Blaster || weaponType == WeaponType::RF ||
				weaponType == WeaponType::Seeker || weaponType == WeaponType::Pepper) {
				shotBase->DecreaseHealth(INT32_MAX);
			} else if (weaponType != WeaponType::Electro) {
				shotBase->TriggerRicochet(this);
			}
			return true;
		}

		return SolidObjectBase::OnHandleCollision(other);
	}
}