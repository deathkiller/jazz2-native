#include "Caterpillar.h"
#include "../../ILevelHandler.h"
#include "../../Tiles/TileMap.h"
#include "../Player.h"
#include "../Weapons/ShotBase.h"

#include "../../../nCine/Base/Random.h"

namespace Jazz2::Actors::Enemies
{
	Caterpillar::Caterpillar()
		:
		_state(StateIdle),
		_smokesLeft(0),
		_attackTime(0.0f)
	{
	}

	void Caterpillar::Preload(const ActorActivationDetails& details)
	{
		PreloadMetadataAsync("Enemy/Caterpillar"_s);
	}

	Task<bool> Caterpillar::OnActivatedAsync(const ActorActivationDetails& details)
	{
		SetState(ActorState::CanBeFrozen, false);
		SetState(ActorState::IsInvulnerable, true);
		SetFacingLeft(true);

		_canHurtPlayer = false;
		_health = INT32_MAX;

		async_await RequestMetadataAsync("Enemy/Caterpillar"_s);
		SetAnimation(AnimState::Idle);

		async_return true;
	}

	void Caterpillar::OnUpdate(float timeMult)
	{
		EnemyBase::OnUpdate(timeMult);

		if (_frozenTimeLeft > 0.0f) {
			return;
		}

		switch (_state) {
			case StateIdle: {
				if (_currentTransition == nullptr) {
					// InhaleStart
					SetTransition((AnimState)1, true, [this]() {
						// Inhale
						SetTransition((AnimState)2, true, [this]() {
							// ExhaleStart
							SetTransition((AnimState)3, true, [this]() {
								_state = StateAttacking;
								_smokesLeft = Random().Next(6, 12);
							});
						});
					});
				}

				break;
			}
			case StateAttacking: {
				if (_attackTime <= 0.0f) {
					_attackTime = 60.0f;

					SetAnimation((AnimState)5);
					SetTransition((AnimState)4, true, [this]() {
						std::shared_ptr<Smoke> smoke = std::make_shared<Smoke>();
						smoke->OnActivated(ActorActivationDetails(
							_levelHandler,
							Vector3i((std::int32_t)_pos.X - 26, (std::int32_t)_pos.Y - 18, _renderer.layer() + 20)
						));
						_levelHandler->AddActor(smoke);

						_smokesLeft--;
						if (_smokesLeft <= 0) {
							_state = StateIdle;
							SetAnimation(AnimState::Idle);
						}
					});
				} else {
					_attackTime -= timeMult;
				}
				break;
			}
		}
	}

	bool Caterpillar::OnHandleCollision(std::shared_ptr<ActorBase> other)
	{
		if (auto* shotBase = runtime_cast<Weapons::ShotBase*>(other)) {
			if (_state != StateDisoriented) {
				Disoriented(Random().Next(8, 13));
			}
		}

		return false;
	}

	void Caterpillar::Disoriented(int count)
	{
		_attackTime = 0.0f;
		_smokesLeft = 0;
		_state = StateDisoriented;

		SetTransition((AnimState)6, false, [this, count]() {
			if (count > 1) {
				Disoriented(count - 1);
			} else {
				_state = StateIdle;
				SetAnimation(AnimState::Idle);
			}
		});
	}

	Task<bool> Caterpillar::Smoke::OnActivatedAsync(const ActorActivationDetails& details)
	{
		SetState(ActorState::CanBeFrozen | ActorState::CollideWithTileset | ActorState::ApplyGravitation, false);
		SetState(ActorState::IsInvulnerable | ActorState::SkipPerPixelCollisions, true);
		CanCollideWithAmmo = false;
		_canHurtPlayer = false;

		_health = INT32_MAX;
		_baseSpeed.X = Random().NextFloat(-1.4f, -0.8f);
		_baseSpeed.Y = Random().NextFloat(-1.6f, -0.8f);
		_time = 500.0f;

		async_await RequestMetadataAsync("Enemy/Caterpillar"_s);
		SetAnimation((AnimState)7);

		async_return true;
	}

	void Caterpillar::Smoke::OnUpdate(float timeMult)
	{
		_speed.X = _baseSpeed.X + cosf((500.0f - _time) * 0.09f) * 0.5f;
		_speed.Y = _baseSpeed.Y + sinf((500.0f - _time) * 0.05f) * 0.5f;

		MoveInstantly(Vector2f(_speed.X * timeMult, _speed.Y * timeMult), MoveType::Relative | MoveType::Force);
		_renderer.setScale(_renderer.scale() - 0.0011f * timeMult);

		if (_time <= 0.0f) {
			DecreaseHealth(INT32_MAX);
		} else {
			_time -= timeMult;
		}
	}

	bool Caterpillar::Smoke::OnHandleCollision(std::shared_ptr<ActorBase> other)
	{
		if (auto* player = runtime_cast<Player*>(other)) {
			if (player->SetDizzyTime(180.0f)) {
				// TODO: Add fade-out
				PlaySfx("Dizzy"_s);
			}
		}

		return false;
	}
}