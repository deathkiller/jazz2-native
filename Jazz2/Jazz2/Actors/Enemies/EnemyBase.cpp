#include "EnemyBase.h"
#include "../../LevelInitialization.h"
#include "../../ILevelHandler.h"
#include "../../Events/EventMap.h"
#include "../Weapons/ShotBase.h"
#include "../Player.h"

namespace Jazz2::Actors::Enemies
{
	EnemyBase::EnemyBase()
		:
		_canHurtPlayer(true),
		_canCollideWithAmmo(true),
		_scoreValue(0),
		_lastHitDir(LastHitDirection::None)
	{
	}

	Task<bool> EnemyBase::OnActivatedAsync(const ActorActivationDetails& details)
	{
		// TODO: change these types to uint8_t
		PlayerType playerType = (PlayerType)details.Params[0];
		SetFacingLeft(details.Params[2] != 0);

		switch (playerType) {
			default:
			case PlayerType::Jazz:
				co_await RequestMetadataAsync("Interactive/PlayerJazz");
				break;
			case PlayerType::Spaz:
				co_await RequestMetadataAsync("Interactive/PlayerSpaz");
				break;
			case PlayerType::Lori:
				co_await RequestMetadataAsync("Interactive/PlayerLori");
				break;
		}

		SetAnimation("Corpse");

		CollisionFlags = CollisionFlags::ForceDisableCollisions;

		co_return true;
	}

	void EnemyBase::SetHealthByDifficulty(int health)
	{
		switch (_levelHandler->Difficulty()) {
			case GameDifficulty::Easy: health = (int)std::round(health * 0.6f); break;
			case GameDifficulty::Hard: health = (int)std::round(health * 1.4f); break;
		}

		_health = std::max(health, 1);
		_maxHealth = _health;
	}

	bool EnemyBase::CanMoveToPosition(float x, float y)
	{
		int direction = (IsFacingLeft() ? -1 : 1);
		AABBf aabbA = AABBInner + Vector2f(x, y - 3);
		AABBf aabbB = AABBInner + Vector2f(x, y + 3);
		if (!_levelHandler->IsPositionEmpty(this, aabbA, true) && !_levelHandler->IsPositionEmpty(this, aabbB, true)) {
			return false;
		}

		AABBf aabbDir = AABBInner + Vector2f(x + direction * (AABBInner.R - AABBInner.L) * 0.5f, y + 12);

		uint8_t* eventParams;
		auto events = _levelHandler->EventMap();
		return ((events == nullptr || events->GetEventByPosition(_pos.X + x, _pos.Y + y, &eventParams) != EventType::AreaStopEnemy) && !_levelHandler->IsPositionEmpty(this, aabbDir, true));
	}

	void EnemyBase::TryGenerateRandomDrop()
	{
		// TODO
		/*EventType drop = MathF.Rnd.OneOfWeighted(
			new KeyValuePair<EventType, float>(EventType.Empty, 10),
			new KeyValuePair<EventType, float>(EventType.Carrot, 2),
			new KeyValuePair<EventType, float>(EventType.FastFire, 2),
			new KeyValuePair<EventType, float>(EventType.Gem, 6)
		);

		if (drop != EventType.Empty) {
			ActorBase actor = levelHandler.EventSpawner.SpawnEvent(drop, new ushort[8], ActorInstantiationFlags.None, Transform.Pos);
			levelHandler.AddActor(actor);
		}*/
	}

	bool EnemyBase::OnHandleCollision(ActorBase* other)
	{
		if (!GetState(ActorFlags::IsInvulnerable)) {
			if (auto shotBase = dynamic_cast<Weapons::ShotBase*>(other)) {
				Vector2f ammoSpeed = shotBase->GetSpeed();
				if (std::abs(ammoSpeed.X) > 0.2f) {
					_lastHitDir = (ammoSpeed.X > 0.0f ? LastHitDirection::Right : LastHitDirection::Left);
				} else {
					_lastHitDir = (ammoSpeed.Y > 0.0f ? LastHitDirection::Down : LastHitDirection::Up);
				}
				DecreaseHealth(shotBase->GetStrength(), shotBase);
				// Collision must also be processed by the shot
				//return true;
			} /*else if (auto shotTnt = dynamic_cast<Weapons::ShotTNT*>(other)) {
				DecreaseHealth(5, shotTnt);
				return true;
			}*/
		}

		return false;
	}

	bool EnemyBase::OnPerish(ActorBase* collider)
	{
		if (auto player = dynamic_cast<Player*>(collider)) {
			player->AddScore(_scoreValue);
		} else if (auto shotBase = dynamic_cast<Weapons::ShotBase*>(collider)) {
			auto owner = shotBase->GetOwner();
			if (owner != nullptr) {
				owner->AddScore(_scoreValue);
			}
		} /*else if(auto shotTnt = dynamic_cast<Weapons::ShotTNT*>(collider)) {
			auto owner = shotTnt->GetOwner();
			if (owner != nullptr) {
				owner->AddScore(_scoreValue);
			}
		}*/

		return ActorBase::OnPerish(collider);
	}
}