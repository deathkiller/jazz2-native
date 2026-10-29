#include "Bird.h"
#include "../../ILevelHandler.h"
#include "../Player.h"
#include "../Enemies/EnemyBase.h"
#include "../Weapons/BlasterShot.h"
#include "../Weapons/BouncerShot.h"
#include "../Weapons/CustomShot.h"
#include "../Weapons/ElectroShot.h"
#include "../Weapons/FreezerShot.h"
#include "../Weapons/SeekerShot.h"

using namespace Jazz2::Tiles;

namespace Jazz2::Actors::Environment
{
	Bird::Bird()
		: _owner(nullptr), _fireCooldown(60.0f), _attackTime(0.0f)
	{
	}

	void Bird::Preload(const ActorActivationDetails& details)
	{
		std::uint8_t type = details.Params[0];
		switch (type) {
			case 0: // Chuck (red)
				PreloadMetadataAsync("Object/BirdChuck"_s);
				break;
			case 1: // Birdy (yellow)
				PreloadMetadataAsync("Object/BirdBirdy"_s);
				break;
		}
	}

	Task<bool> Bird::OnActivatedAsync(const ActorActivationDetails& details)
	{
		// The bird belongs to its player, so it stays when the level is rolled back to a checkpoint
		SetState(ActorState::SkipPerPixelCollisions | ActorState::PreserveOnRollback, true);
		SetState(ActorState::CollideWithTileset | ActorState::CollideWithSolidObjects | ActorState::CollideWithOtherActors | ActorState::ApplyGravitation, false);

		_type = details.Params[0];

		std::uint8_t playerIndex = details.Params[1];
		for (auto* player : _levelHandler->GetPlayers()) {
			if (player->GetPlayerIndex() == playerIndex) {
				_owner = player;
				break;
			}
		}

		switch (_type) {
			case 0: // Chuck (red)
				async_await RequestMetadataAsync("Object/BirdChuck"_s);
				break;
			case 1: // Birdy (yellow)
				async_await RequestMetadataAsync("Object/BirdBirdy"_s);
				break;
		}

		SetAnimation(AnimState::Idle);

		async_return true;
	}

	void Bird::OnUpdate(float timeMult)
	{
		if (_owner == nullptr) {
			// Fly away
			if (_fireCooldown <= 0.0f) {
				DecreaseHealth(INT32_MAX);
			} else {
				_fireCooldown -= timeMult;
				MoveInstantly(Vector2f((IsFacingLeft() ? -8.0f : 8.0f) * timeMult, -1.0f * timeMult), MoveType::Relative | MoveType::Force);
				_renderer.setScale(_renderer.scale() + 0.014f * timeMult);
			}
			return;
		}

		if (_type == 1 && _attackTime > 0.0f) {
			// Attack
			MoveInstantly(Vector2f(_speed.X * timeMult, _speed.Y * timeMult), MoveType::Relative | MoveType::Force);

			_attackTime -= timeMult;

			if (_attackTime <= 0.0f) {
				SetAnimation(AnimState::Idle);
				SetState(ActorState::CollideWithOtherActors, false);
				_renderer.setRotation(0.0f);
			}
		} else {
			// Follow player
			Vector2f targetPos = _owner->GetPos();
			bool playerFacingLeft = _owner->IsFacingLeft();

			if (playerFacingLeft) {
				targetPos.X += 55.0f;
			} else {
				targetPos.X -= 55.0f;
			}
			targetPos.Y -= 50.0f;

			if (IsFacingLeft() != playerFacingLeft) {
				SetFacingLeft(playerFacingLeft);
				_fireCooldown = 40.0f;
			}

			targetPos.X = lerp(_pos.X, targetPos.X, 0.02f * timeMult);
			targetPos.Y = lerp(_pos.Y, targetPos.Y, 0.02f * timeMult);
			MoveInstantly(targetPos, MoveType::Absolute | MoveType::Force);
		}

		// Fire
		if (_fireCooldown <= 0.0f) {
			TryFire();
		} else {
			_fireCooldown -= timeMult;
		}
	}

	void Bird::OnAnimationFinished()
	{
		ActorBase::OnAnimationFinished();

		PlaySfx("Fly"_s, 0.3f);
	}

	bool Bird::OnHandleCollision(ActorBase* other)
	{
		if (_attackTime > 0.0f && !other->IsInvulnerable()) {
			if (auto* enemy = runtime_cast<Enemies::EnemyBase>(other)) {
				enemy->DecreaseHealth(1, this);

				SetAnimation(AnimState::Idle);
				_attackTime = 0.0f;
				SetState(ActorState::CollideWithOtherActors, false);
				_renderer.setRotation(0.0f);
				return true;
			}
		}

		return ActorBase::OnHandleCollision(other);
	}

	void Bird::FlyAway()
	{
		_owner = nullptr;
		_fireCooldown = 300.0f;

		if (_attackTime > 0.0f) {
			SetAnimation(AnimState::Idle);
			_attackTime = 0.0f;
			SetState(ActorState::CollideWithOtherActors, false);
			_renderer.setRotation(0.0f);
		}
	}

	void Bird::TryFire()
	{
		_levelHandler->FindCollisionActorsByRadius(_pos.X, _pos.Y, 260.0f, [this](ActorBase* actor) {
			if (auto* enemy = runtime_cast<Enemies::EnemyBase>(actor)) {
				Vector2f newPos = enemy->GetPos();
				if (IsFacingLeft() ? (newPos.X > _pos.X) : (newPos.X < _pos.X)) {
					return true;
				}

				switch (_type) {
					case 0: { // Chuck (red)
						SetState(ActorState::CollideWithTileset, true);
						TileCollisionParams params = { TileDestructType::None, true };
						if (_levelHandler->IsPositionEmpty(this, AABBf(_pos.X - 2.0f, _pos.Y - 2.0f, _pos.X + 2.0f, _pos.Y + 2.0f), params)) {
							// The weapon the player holds if the level lets birds fire it (see WeaponOptions::ComesFromBirds), the blaster
							// otherwise, and it doesn't take any ammo
							WeaponType weaponType = _owner->GetCurrentWeapon();
							const WeaponDefinition& definition = _levelHandler->GetWeaponDefinition(weaponType);
							bool fromBirds = (definition.Options.ComesFromBirds && weaponType < WeaponType::Count);
							bool custom = (fromBirds && definition.Kind == WeaponKind::Custom);
							if (!fromBirds) {
								weaponType = WeaponType::Blaster;
							}
							std::uint8_t upgrades = (fromBirds && definition.Options.ComesFromBirdsPowerup ? (_owner->GetWeaponUpgrades()[(std::int32_t)weaponType] & 0x01) : 0);
							std::uint8_t shotParams[3] = { upgrades, std::uint8_t(weaponType), 0 };
							std::shared_ptr<ActorBase> sharedOwner = _owner->shared_from_this();

							auto fire = [&](auto shot, float angle) {
								shot->OnActivated(ActorActivationDetails(
									_levelHandler,
									Vector3i((std::int32_t)_pos.X, (std::int32_t)_pos.Y, _renderer.layer() - 2),
									shotParams
								));
								shot->OnFire(sharedOwner, _pos, _speed, angle, IsFacingLeft());
								_levelHandler->AddActor(shot);
							};
							for (float angle : { 0.0f, IsFacingLeft() ? -0.18f : 0.18f }) {
								if (custom) {
									fire(std::make_shared<Weapons::CustomShot>(), angle);
								} else switch (weaponType) {
									case WeaponType::Bouncer: fire(std::make_shared<Weapons::BouncerShot>(), angle); break;
									case WeaponType::Freezer: fire(std::make_shared<Weapons::FreezerShot>(), angle); break;
									case WeaponType::Seeker: fire(std::make_shared<Weapons::SeekerShot>(), angle); break;
									case WeaponType::Electro: fire(std::make_shared<Weapons::ElectroShot>(), angle); break;
									case WeaponType::Blaster: fire(std::make_shared<Weapons::BlasterShot>(), angle); break;
									default: shotParams[0] = 0; fire(std::make_shared<Weapons::BlasterShot>(), angle); break;
								}
							}

							PlaySfx("Fire"_s, 0.5f);
							_fireCooldown = 32.0f;
						}
						SetState(ActorState::CollideWithTileset, false);
						break;
					}

					case 1: { // Birdy (yellow)
						SetAnimation(AnimState::Shoot);

						Vector2f attackSpeed = (newPos - _pos).Normalize();
						_speed.X = attackSpeed.X * 6.0f;
						_speed.Y = attackSpeed.Y * 6.0f;

						float angle = atan2Approx(_speed.Y, _speed.X);
						if (IsFacingLeft()) {
							angle += fPi;
						}
						_renderer.setRotation(angle);

						float distance = (newPos - _pos).Length();
						_attackTime = distance * 0.2f;
						_fireCooldown = 140.0f;

						SetState(ActorState::CollideWithOtherActors, true);
						break;
					}
				}
			}
			return true;
		});

	}
}