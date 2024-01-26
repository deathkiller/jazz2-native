#include "Witch.h"
#include "../../ILevelHandler.h"
#include "../../Tiles/TileMap.h"
#include "../Player.h"
#include "../Explosion.h"

#include "../../../nCine/Base/Random.h"

namespace Jazz2::Actors::Enemies
{
	Witch::Witch()
		:
		_attackTime(0.0f),
		_playerHit(false)
	{
	}

	void Witch::Preload(const ActorActivationDetails& details)
	{
		PreloadMetadataAsync("Enemy/Witch"_s);
		PreloadMetadataAsync("Interactive/PlayerFrog"_s);
	}

	Task<bool> Witch::OnActivatedAsync(const ActorActivationDetails& details)
	{
		SetState(ActorState::ApplyGravitation, false);

		SetHealthByDifficulty(30);
		_scoreValue = 1000;

		async_await RequestMetadataAsync("Enemy/Witch"_s);
		SetAnimation(AnimState::Idle);

		async_return true;
	}

	void Witch::OnUpdate(float timeMult)
	{
		OnUpdateHitbox();
		HandleBlinking(timeMult);
		UpdateFrozenState(timeMult);

		if (_frozenTimeLeft > 0.0f) {
			return;
		}

		MoveInstantly(Vector2f(_speed.X * timeMult, _speed.Y * timeMult), MoveType::Relative | MoveType::Force);

		if (_playerHit) {
			if (_attackTime > 0.0f) {
				_attackTime -= timeMult;
			} else {
				EnemyBase::OnPerish(nullptr);
			}
			return;
		}

		if (_attackTime > 0.0f) {
			_attackTime -= timeMult;
		}

		Vector2f targetPos;

		auto& players = _levelHandler->GetPlayers();
		for (auto& player : players) {
			targetPos = player->GetPos();
			// Fly above the player
			targetPos.Y -= 100.0f;

			Vector2f direction = (_pos - targetPos);
			float length = direction.Length();

			if (_attackTime <= 0.0f && length < 260.0f) {
				_attackTime = 450.0f;

				PlaySfx("MagicFire"_s);

				SetTransition(AnimState::TransitionAttack, true, [this]() {
					Vector2f bulletPos = Vector2f(_pos.X + (IsFacingLeft() ? -24.0f : 24.0f), _pos.Y);

					std::shared_ptr<MagicBullet> magicBullet = std::make_shared<MagicBullet>(this);
					magicBullet->OnActivated(ActorActivationDetails(
						_levelHandler,
						Vector3i((std::int32_t)bulletPos.X, (std::int32_t)bulletPos.Y, _renderer.layer() + 1)
					));
					_levelHandler->AddActor(magicBullet);

					Explosion::Create(_levelHandler, Vector3i((int)bulletPos.X, (int)bulletPos.Y, _renderer.layer() + 2), Explosion::Type::TinyDark);
				});
			} else if (length > 20.0f && length < 500.0f) {
				direction.Normalize();
				_speed.X = (direction.X * DefaultSpeed + _speed.X) * 0.5f;
				_speed.Y = (direction.Y * DefaultSpeed + _speed.Y) * 0.5f;

				SetFacingLeft(_speed.X < 0.0f);
				return;
			}
		}

		_speed.X = 0.0f;
		_speed.Y = 0.0f;
	}

	void Witch::OnUpdateHitbox()
	{
		UpdateHitbox(30, 30);
	}

	bool Witch::OnPerish(ActorBase* collider)
	{
		// It must be done here, because the player may not exist after animation callback 
		AddScoreToCollider(collider);

		_levelHandler->PlayCommonSfx("Splat"_s, Vector3f(_pos.X, _pos.Y, 0.0f));

		SetTransition(AnimState::TransitionDeath, false, [this, collider]() {
			EnemyBase::OnPerish(collider);
		});

		CreateParticleDebris();

		return false;
	}

	bool Witch::OnTileDeactivated()
	{
		// Witch cannot be deactivated
		return false;
	}

	void Witch::OnPlayerHit()
	{
		_playerHit = true;
		_attackTime = 400.0f;

		_speed.X = (IsFacingLeft() ? -9.0f : 9.0f);
		_speed.Y = -0.8f;

		PlaySfx("Laugh"_s);
	}

	Task<bool> Witch::MagicBullet::OnActivatedAsync(const ActorActivationDetails& details)
	{
		SetState(ActorState::IsInvulnerable | ActorState::SkipPerPixelCollisions, true);
		SetState(ActorState::CanBeFrozen | ActorState::CollideWithTileset | ActorState::ApplyGravitation, false);

		_health = INT32_MAX;

		async_await RequestMetadataAsync("Enemy/Witch"_s);
		SetAnimation((AnimState)1073741828);

		async_return true;
	}

	void Witch::MagicBullet::OnUpdate(float timeMult)
	{
		MoveInstantly(Vector2f(_speed.X * timeMult, _speed.Y * timeMult), MoveType::Relative | MoveType::Force);
		OnUpdateHitbox();

		if (_time <= 0.0f) {
			DecreaseHealth(INT32_MAX);
		} else {
			_time -= timeMult;
			FollowNearestPlayer();
		}
	}

	void Witch::MagicBullet::OnUpdateHitbox()
	{
		UpdateHitbox(10, 10);
	}

	bool Witch::MagicBullet::OnHandleCollision(std::shared_ptr<ActorBase> other)
	{
		if (auto* player = runtime_cast<Player*>(other)) {
			DecreaseHealth(INT32_MAX);
			_owner->OnPlayerHit();

			player->MorphTo(PlayerType::Frog);
			return true;
		}

		return false;
	}

	void Witch::MagicBullet::OnEmitLights(SmallVectorImpl<LightEmitter>& lights)
	{
		auto& light = lights.emplace_back();
		light.Pos = _pos;
		light.Intensity = 0.7f;
		light.Brightness = 0.4f;
		light.RadiusNear = 0.0f;
		light.RadiusFar = 30.0f;
	}

	void Witch::MagicBullet::FollowNearestPlayer()
	{
		bool found = false;
		Vector2f targetPos;

		auto& players = _levelHandler->GetPlayers();
		for (auto& player : players) {
			Vector2f newPos = player->GetPos();
			if (!found || (_pos - newPos).SqrLength() < (_pos - targetPos).SqrLength()) {
				targetPos = newPos;
				found = true;
			}
		}

		if (found) {
			Vector2f diff = (targetPos - _pos).Normalized();
			Vector2f speed = (_speed + diff * 0.8f).Normalized();
			_speed = speed * 5.0f;
			_renderer.setRotation(atan2f(_speed.Y, _speed.X));
		}
	}
}