#include "TurtleShell.h"
#include "../../ILevelHandler.h"
#include "../../Tiles/TileMap.h"
#include "../Solid/AmmoCrate.h"
#include "../Solid/CrateContainer.h"
#include "../Solid/GemCrate.h"
#include "../Weapons/ShotBase.h"
#include "../Weapons/FreezerShot.h"
#include "../Weapons/ShieldFireShot.h"
#include "../Weapons/Thunderbolt.h"
#include "../Weapons/ToasterShot.h"

#include "../../../nCine/Base/Random.h"

using namespace Jazz2::Tiles;

namespace Jazz2::Actors::Enemies
{
	TurtleShell::TurtleShell()
		: _lastAngle(0.0f)
	{
	}

	void TurtleShell::Preload(const ActorActivationDetails& details)
	{
		uint8_t theme = details.Params[8];
		switch (theme) {
			case 0:
			default:
				PreloadMetadataAsync("Enemy/TurtleShell"_s);
				break;
			case 1: // Xmas
				PreloadMetadataAsync("Enemy/TurtleShellXmas"_s);
				break;
			case 2: // Tough (Boss)
				PreloadMetadataAsync("Boss/TurtleBossShell"_s);
				break;
		}
	}

	Task<bool> TurtleShell::OnActivatedAsync(const ActorActivationDetails& details)
	{
		SetHealthByDifficulty(1);
		_scoreValue = 100;

		SetState(ActorState::CollideWithTilesetReduced | ActorState::SkipPerPixelCollisions, true);
		if (_levelHandler->IsReforged()) {
			SetState(ActorState::CollideWithSolidObjects | ActorState::CollideWithSolidObjectsBelow, true);
		}

		_speed.X = *(float*)&details.Params[0];
		_externalForce.Y = *(float*)&details.Params[4];
		uint8_t theme = details.Params[8];
		switch (theme) {
			case 0:
			default:
				async_await RequestMetadataAsync("Enemy/TurtleShell"_s);
				break;
			case 1: // Xmas
				async_await RequestMetadataAsync("Enemy/TurtleShellXmas"_s);
				break;
			case 2: // Tough (Boss)
				async_await RequestMetadataAsync("Boss/TurtleBossShell"_s);
				break;
		}

		SetAnimation(AnimState::Default);

		_canHurtPlayer = false;
		_friction = _levelHandler->Gravity() * 0.04f;
		_elasticity = 0.5f;
		_health = 8;

		if (std::abs(_speed.X) > 0.0f || std::abs(_externalForce.Y) > 0.0f) {
			PlaySfx("Fly"_s);
			StartBlinking();
		}

		async_return true;
	}

	void TurtleShell::OnUpdate(float timeMult)
	{
		_speed.X = lerpByTime(_speed.X, 0.0f, _friction, timeMult);

		double posYBefore = _pos.Y;

		TileCollisionParams params = { TileDestructType::Weapon | TileDestructType::Special, _speed.Y >= 0.0f, WeaponType::Blaster, 1 };
		TryStandardMovement(timeMult, params);
		OnUpdateHitbox();
		UpdateFrozenState(timeMult);

		HandleBlinking(timeMult);

		if (posYBefore - _pos.Y > 0.5 && std::abs(_speed.Y) < 1) {
			_speed.X = std::max(std::abs(_speed.X) - 10.0f * _friction, 0.0f) * (_speed.X < 0.0f ? -1.0f : 1.0f);
		}

		if (_levelHandler->IsReforged()) {
			// Enable tilting only if Reforged
			_lastAngle = lerp(_lastAngle, _speed.X * 0.06f, timeMult * 0.2f);
			_renderer.setRotation(_lastAngle);
		}
	}

	void TurtleShell::OnUpdateHitbox()
	{
		UpdateHitbox(30, 16);
	}

	bool TurtleShell::OnPerish(ActorBase* collider)
	{
		CreateDeathDebris(collider);
		_levelHandler->PlayCommonSfx("Splat"_s, Vector3f(_pos.X, _pos.Y, 0.0f));

		TryGenerateRandomDrop();

		return EnemyBase::OnPerish(collider);
	}

	bool TurtleShell::OnHandleCollision(std::shared_ptr<ActorBase> other)
	{
		EnemyBase::OnHandleCollision(other);

		if (auto* shotBase = runtime_cast<Weapons::ShotBase*>(other)) {
			if (shotBase->GetStrength() > 0) {
				if (runtime_cast<Weapons::FreezerShot*>(shotBase)) {
					return false;
				}

				if (auto* toasterShot = runtime_cast<Weapons::ToasterShot*>(shotBase)) {
					DecreaseHealth(INT32_MAX, toasterShot);
					return true;
				}
				if (auto* shieldFireShot = runtime_cast<Weapons::ShieldFireShot*>(shotBase)) {
					DecreaseHealth(INT32_MAX, shieldFireShot);
					return true;
				}

				float shotSpeed;
				if (runtime_cast<Weapons::Thunderbolt*>(shotBase)) {
					shotSpeed = (_pos.X < shotBase->GetPos().X ? -8.0f : 8.0f);
				} else {
					shotSpeed = shotBase->GetSpeed().X;
				}

				_speed.X = std::max(4.0f, std::abs(shotSpeed)) * (shotSpeed < 0.0f ? -0.6f : 0.6f);

				PlaySfx("Fly"_s);
			}
		} else if (auto* shell = runtime_cast<TurtleShell*>(other)) {
			auto otherSpeed = shell->GetSpeed();
			if (std::abs(otherSpeed.Y - _speed.Y) > 1.0f && otherSpeed.Y > 0.0f) {
				DecreaseHealth(10, this);
			} else if (std::abs(_speed.X) > std::abs(otherSpeed.X)) {
				// Handle this only in the faster of the two
				//pos.X = collider.Transform.Pos.X + (speedX >= 0 ? -1f : 1f) * (currentAnimation.Base.FrameDimensions.X + 1);
				float totalSpeed = std::abs(_speed.X) + std::abs(otherSpeed.X);

				shell->_speed.X = totalSpeed / 2 * (_speed.X < 0.0f ? -1.0f : 1.0f);
				_speed.X = totalSpeed / 2.0f * (_speed.X < 0.0f ? 1.0f : -1.0f);

				shell->DecreaseHealth(1, this);
				PlaySfx("ImpactShell"_s, 0.8f);
				return true;
			}
		} else if (auto* enemyBase = runtime_cast<EnemyBase*>(other)) {
			if (enemyBase->CanCollideWithAmmo) {
				float absSpeed = std::abs(_speed.X);
				if (absSpeed > 2.0f) {
					_speed.X = std::max(absSpeed, 2.0f) * (_speed.X < 0.0f ? 1.0f : -1.0f);
					if (!enemyBase->GetState(ActorState::IsInvulnerable)) {
						enemyBase->DecreaseHealth(1, this);
						return true;
					}
				}
			}
		} else if (auto* crateContainer = runtime_cast<Solid::CrateContainer*>(other)) {
			float absSpeed = std::abs(_speed.X);
			if (absSpeed > 2.0f) {
				_speed.X = std::max(absSpeed, 2.0f) * (_speed.X >= 0.0f ? -1.0f : 1.0f);
				crateContainer->DecreaseHealth(1, this);
				return true;
			}
		} else if (auto* ammoCrate = runtime_cast<Solid::AmmoCrate*>(other)) {
			float absSpeed = std::abs(_speed.X);
			if (absSpeed > 2.0f) {
				_speed.X = std::max(absSpeed, 2.0f) * (_speed.X >= 0.0f ? -1.0f : 1.0f);
				ammoCrate->DecreaseHealth(1, this);
				return true;
			}
		} else if (auto* gemCrate = runtime_cast<Solid::GemCrate*>(other)) {
			float absSpeed = std::abs(_speed.X);
			if (absSpeed > 2.0f) {
				_speed.X = std::max(absSpeed, 2.0f) * (_speed.X >= 0.0f ? -1.0f : 1.0f);
				gemCrate->DecreaseHealth(1, this);
				return true;
			}
		}

		return false;
	}

	void TurtleShell::OnHitFloor(float timeMult)
	{
		if (std::abs(_speed.Y) > 1.0f) {
			PlaySfx("ImpactGround"_s);
		}
	}
}