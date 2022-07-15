#include "TurtleShell.h"
#include "../../LevelInitialization.h"
#include "../../ILevelHandler.h"
#include "../../Tiles/TileMap.h"
#include "../Weapons//ShotBase.h"

#include "../../../nCine/Base/Random.h"

namespace Jazz2::Actors::Enemies
{
	TurtleShell::TurtleShell()
		:
		_lastAngle(0.0f)
	{
	}

	void TurtleShell::Preload(const ActorActivationDetails& details)
	{
		uint8_t theme = details.Params[0];
		switch (theme) {
			case 0:
			default:
				PreloadMetadataAsync("Enemy/TurtleShell");
				break;
			case 1: // Xmas
				PreloadMetadataAsync("Enemy/TurtleShellXmas");
				break;
			case 2: // Tough (Boss)
				PreloadMetadataAsync("Boss/TurtleShellTough");
				break;
		}
	}

	Task<bool> TurtleShell::OnActivatedAsync(const ActorActivationDetails& details)
	{
		SetHealthByDifficulty(1);
		CollisionFlags |= CollisionFlags::SkipPerPixelCollisions;

		_scoreValue = 100;

		uint8_t theme = details.Params[0];
		switch (theme) {
			case 0:
			default:
				co_await RequestMetadataAsync("Enemy/TurtleShell");
				break;
			case 1: // Xmas
				co_await RequestMetadataAsync("Enemy/TurtleShellXmas");
				break;
			case 2: // Tough (Boss)
				co_await RequestMetadataAsync("Boss/TurtleShellTough");
				break;
		}

		SetAnimation(AnimState::Idle);

		_canHurtPlayer = false;
		_friction = _levelHandler->Gravity * 0.05f;
		_elasticity = 0.5f;
		_health = 8;

		//PlaySound("Fly");

		co_return true;
	}

	void TurtleShell::OnUpdate(float timeMult)
	{
		_speed.X = std::max(std::abs(_speed.X) - _friction, 0.0f) * (_speed.X < 0.0f ? -1.0f : 1.0f);

		double posYBefore = _pos.Y;
		EnemyBase::OnUpdate(timeMult);

		if (posYBefore - _pos.Y > 0.5 && std::abs(_speed.Y) < 1) {
			_speed.X = std::max(std::abs(_speed.X) - 10.0f * _friction, 0.0f) * (_speed.X < 0.0f ? -1.0f : 1.0f);
		}

		auto tiles = _levelHandler->TileMap();
		if (tiles != nullptr) {
			tiles->CheckSpecialDestructible(AABBInner);
			tiles->CheckCollapseDestructible(AABBInner);
			tiles->CheckWeaponDestructible(AABBInner, WeaponType::Blaster, 1);
		}

		_lastAngle = lerp(_lastAngle, _speed.X * 0.06f, timeMult * 0.2f);
		if (std::abs(_angle - /*MathF.NormalizeAngle*/(_lastAngle)) > 0.01f) {
			_angle = _lastAngle;
		}
	}

	void TurtleShell::OnUpdateHitbox()
	{
		UpdateHitbox(30, 16);
	}

	bool TurtleShell::OnPerish(ActorBase* collider)
	{
		CreateDeathDebris(collider);
		//_levelHandler->PlayCommonSound("Splat", _pos);

		TryGenerateRandomDrop();

		return EnemyBase::OnPerish(collider);
	}

	bool TurtleShell::OnHandleCollision(ActorBase* other)
	{
		EnemyBase::OnHandleCollision(other);

		if (auto shotBase = dynamic_cast<Weapons::ShotBase*>(other)) {
			// TODO
			/*if (ammo is AmmoFreezer) {
				break;
			}

			if (other is AmmoToaster) {
				DecreaseHealth(INT32_MAX, other);
				return;
			}*/

			float otherSpeed = other->GetSpeed().X;
			_speed.X = std::max(4.0f, std::abs(otherSpeed)) * (otherSpeed < 0.0f ? -0.5f : 0.5f);

			//PlaySound("Fly");
			return true;
		} else if (auto shell = dynamic_cast<TurtleShell*>(other)) {
			auto otherSpeed = shell->GetSpeed();
			if (std::abs(otherSpeed.Y - _speed.Y) > 1.0f && otherSpeed.Y > 0.0f) {
				DecreaseHealth(10, this);
			} else if (std::abs(_speed.X) > std::abs(otherSpeed.X)) {
				// Handle this only in the faster of the two
				//pos.X = collider.Transform.Pos.X + (speedX >= 0 ? -1f : 1f) * (currentAnimation.Base.FrameDimensions.X + 1);
				float totalSpeed = std::abs(_speed.X) + std::abs(otherSpeed.X);

				shell->_speed.X = totalSpeed / 2 * (_speed.X >= 0.0f ? 1.0f : -1.0f);
				_speed.X = totalSpeed / 2.0f * (_speed.X >= 0.0f ? -1.0f : 1.0f);

				shell->DecreaseHealth(1, this);
				//PlaySound(Transform.Pos, "ImpactShell", 0.8f);
			}
			return true;
		} else if (auto enemyBase = dynamic_cast<EnemyBase*>(other)) {
			if (enemyBase->CanCollideWithAmmo) {
				if (!enemyBase->GetState(ActorFlags::IsInvulnerable)) {
					enemyBase->DecreaseHealth(1, this);
				}

				_speed.X = std::max(std::abs(_speed.X), 2.0f) * (_speed.X >= 0.0f ? -1.0f : 1.0f);
			}
		}

		return false;
	}

	void TurtleShell::OnHitFloor()
	{
		// TODO
		/*if (std::abs(_speed.Y) > 1.0f) {
			PlaySound(Transform.Pos, "ImpactGround");
		}*/
	}
}