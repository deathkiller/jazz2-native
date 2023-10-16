#include "Bird.h"
#include "../../ILevelHandler.h"
#include "../Player.h"
#include "../Enemies/EnemyBase.h"
#include "../Weapons/BlasterShot.h"

namespace Jazz2::Actors::Environment
{
	Bird::Bird()
		:
		_owner(nullptr),
		_fireCooldown(60.0f),
		_attackTime(0.0f)
	{
	}

	void Bird::Preload(const ActorActivationDetails& details)
	{
		uint8_t type = details.Params[0];
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
		SetState(ActorState::SkipPerPixelCollisions, true);
		SetState(ActorState::CollideWithTileset | ActorState::CollideWithSolidObjects | ActorState::CollideWithOtherActors | ActorState::ApplyGravitation, false);

		_type = details.Params[0];
		uint8_t playerIndex = details.Params[1];
		auto& players = _levelHandler->GetPlayers();
		if (playerIndex < players.size()) {
			_owner = players[playerIndex];
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

	bool Bird::OnHandleCollision(std::shared_ptr<ActorBase> other)
	{
		if (_attackTime > 0.0f && !other->IsInvulnerable()) {
			if (auto enemy = dynamic_cast<Enemies::EnemyBase*>(other.get())) {
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
			if (auto enemy = dynamic_cast<Enemies::EnemyBase*>(actor)) {
				Vector2f newPos = enemy->GetPos();
				if (IsFacingLeft() ? (newPos.X > _pos.X) : (newPos.X < _pos.X)) {
					return true;
				}

				switch (_type) {
					case 0: { // Chuck (red)
						SetState(ActorState::CollideWithTileset, true);
						TileCollisionParams params = { TileDestructType::None, true };
						if (_levelHandler->IsPositionEmpty(this, AABBf(_pos.X - 2.0f, _pos.Y - 2.0f, _pos.X + 2.0f, _pos.Y + 2.0f), params)) {
							uint8_t shotParams[1] = { 0 };
							std::shared_ptr<ActorBase> sharedOwner = _owner->shared_from_this();

							std::shared_ptr<Weapons::BlasterShot> shot1 = std::make_shared<Weapons::BlasterShot>();
							shot1->OnActivated(ActorActivationDetails(
								_levelHandler,
								Vector3i((std::int32_t)_pos.X, (std::int32_t)_pos.Y, _renderer.layer() - 2),
								shotParams
							));
							shot1->OnFire(sharedOwner, _pos, _speed, 0.0f, IsFacingLeft());
							_levelHandler->AddActor(shot1);

							std::shared_ptr<Weapons::BlasterShot> shot2 = std::make_shared<Weapons::BlasterShot>();
							shot2->OnActivated(ActorActivationDetails(
								_levelHandler,
								Vector3i((std::int32_t)_pos.X, (std::int32_t)_pos.Y, _renderer.layer() - 2),
								shotParams
							));
							shot2->OnFire(sharedOwner, _pos, _speed, IsFacingLeft() ? -0.18f : 0.18f, IsFacingLeft());
							_levelHandler->AddActor(shot2);

							PlaySfx("Fire"_s, 0.5f);
							_fireCooldown = 48.0f;
						}
						SetState(ActorState::CollideWithTileset, false);
						break;
					}

					case 1: { // Birdy (yellow)
						SetAnimation(AnimState::Shoot);

						Vector2f attackSpeed = (newPos - _pos).Normalize();
						_speed.X = attackSpeed.X * 6.0f;
						_speed.Y = attackSpeed.Y * 6.0f;

						float angle = atan2f(_speed.Y, _speed.X);
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