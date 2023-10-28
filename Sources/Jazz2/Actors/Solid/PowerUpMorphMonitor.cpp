#include "PowerUpMorphMonitor.h"
#include "../../ILevelHandler.h"
#include "../../Tiles/TileMap.h"
#include "../Player.h"
#include "../Weapons/ShotBase.h"
#include "../Weapons/TNT.h"

#include "../../../nCine/Base/Random.h"

namespace Jazz2::Actors::Solid
{
	PowerUpMorphMonitor::PowerUpMorphMonitor()
	{
	}

	void PowerUpMorphMonitor::Preload(const ActorActivationDetails& details)
	{
		MorphType morphType = (MorphType)details.Params[0];
		switch (morphType) {
			case MorphType::Swap2: PreloadMetadataAsync("Object/PowerUp/Swap2"_s); break;
			case MorphType::Swap3: PreloadMetadataAsync("Object/PowerUp/Swap3"_s); break;
			case MorphType::ToBird: PreloadMetadataAsync("Object/PowerUp/Bird"_s); break;
			default: PreloadMetadataAsync("Object/PowerUp/Empty"_s); break;
		}
	}

	Task<bool> PowerUpMorphMonitor::OnActivatedAsync(const ActorActivationDetails& details)
	{
		_morphType = (MorphType)details.Params[0];

		SetState(ActorState::TriggersTNT, true);
		Movable = true;

		switch (_morphType) {
			case MorphType::Swap2: async_await RequestMetadataAsync("Object/PowerUp/Swap2"_s); break;
			case MorphType::Swap3: async_await RequestMetadataAsync("Object/PowerUp/Swap3"_s); break;
			case MorphType::ToBird: async_await RequestMetadataAsync("Object/PowerUp/Bird"_s); break;
			default: async_await RequestMetadataAsync("Object/PowerUp/Empty"_s); break;
		}

		SetAnimation(AnimState::Default);

		auto& players = _levelHandler->GetPlayers();
		for (auto& player : players) {
			std::optional<PlayerType> playerType = GetTargetType(player->GetPlayerType());
			if (playerType.has_value()) {
				switch (playerType.value()) {
					case PlayerType::Jazz: PreloadMetadataAsync("Interactive/PlayerJazz"_s); break;
					case PlayerType::Spaz: PreloadMetadataAsync("Interactive/PlayerSpaz"_s); break;
					case PlayerType::Lori: PreloadMetadataAsync("Interactive/PlayerLori"_s); break;
				}
			}
		}

		async_return true;
	}

	bool PowerUpMorphMonitor::OnHandleCollision(std::shared_ptr<ActorBase> other)
	{
		if (_health == 0) {
			return SolidObjectBase::OnHandleCollision(other);
		}

		if (auto shotBase = dynamic_cast<Weapons::ShotBase*>(other.get())) {
			Player* owner = shotBase->GetOwner();
			WeaponType weaponType = shotBase->GetWeaponType();
			if (owner != nullptr && (weaponType == WeaponType::Blaster ||
				weaponType == WeaponType::RF || weaponType == WeaponType::Seeker ||
				weaponType == WeaponType::Pepper || weaponType == WeaponType::Electro)) {
				DestroyAndApplyToPlayer(owner);
				shotBase->DecreaseHealth(INT32_MAX);
			} else {
				shotBase->TriggerRicochet(this);
			}
			return true;
		} else if (auto tnt = dynamic_cast<Weapons::TNT*>(other.get())) {
			Player* owner = tnt->GetOwner();
			if (owner != nullptr) {
				DestroyAndApplyToPlayer(owner);
			}
			return true;
		} else if (auto player = dynamic_cast<Player*>(other.get())) {
			if (player->CanBreakSolidObjects()) {
				DestroyAndApplyToPlayer(player);
				return true;
			}
		}

		return SolidObjectBase::OnHandleCollision(other);
	}

	bool PowerUpMorphMonitor::OnPerish(ActorBase* collider)
	{
		CreateParticleDebris();

		return SolidObjectBase::OnPerish(collider);
	}

	void PowerUpMorphMonitor::DestroyAndApplyToPlayer(Player* player)
	{
		std::optional<PlayerType> playerType = GetTargetType(player->GetPlayerType());
		if (playerType.has_value()) {
			player->MorphTo(playerType.value());

			DecreaseHealth(INT32_MAX, player);
			PlaySfx("Break"_s);
		}
	}

	std::optional<PlayerType> PowerUpMorphMonitor::GetTargetType(PlayerType currentType)
	{
		switch (_morphType) {
			case MorphType::Swap2:
				if (currentType != PlayerType::Jazz) {
					return PlayerType::Jazz;
				} else {
					return PlayerType::Spaz;
				}
			case MorphType::Swap3:
				if (currentType == PlayerType::Spaz) {
					return PlayerType::Lori;
				} else if (currentType == PlayerType::Lori) {
					return PlayerType::Jazz;
				} else {
					return PlayerType::Spaz;
				}

			case MorphType::ToBird:
				// TODO: Implement Birds
				return std::nullopt;

			default:
				return std::nullopt;
		}
	}
}