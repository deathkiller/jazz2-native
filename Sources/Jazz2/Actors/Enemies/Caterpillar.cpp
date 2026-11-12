#include "Caterpillar.h"
#include "../../ILevelHandler.h"
#include "../../Tiles/TileMap.h"
#include "../Player.h"
#include "../Weapons/ShotBase.h"

#include "../../../nCine/Base/Random.h"

namespace Jazz2::Actors::Enemies
{
	Caterpillar::Caterpillar()
		: _state(StateIdle), _count(0), _time(0.0f)
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
		_count = Random().Next(0, 32);

		async_await RequestMetadataAsync("Enemy/Caterpillar"_s);
		SetAnimation(AnimState::Idle);

		async_return true;
	}

	void Caterpillar::OnUpdate(float timeMult)
	{
		// Caterpillar doesn't move and we also need to suppress unstucking logic, so we don't call the base class OnUpdate() here
		//EnemyBase::OnUpdate(timeMult);

		if (_frozenTimeLeft > 0.0f) {
			return;
		}

		// The cycle runs in ticks of the original, each step shows its frames at a fixed rate
		_time += timeMult * TicksPerFrame;
		std::int32_t step = (std::int32_t)(_time / FrameTicks);

		switch (_state) {
			case StateIdle: {
				ShowFrame(AnimState::Idle, 0);
				if (_time > (float)_count) {
					StartStep(StateInhaleStart);
				}
				break;
			}
			case StateInhaleStart: {
				if (ShowFrame(AnimStateInhaleStart, step) >= 0) {
					StartStep(StateInhale);
					_count = Random().Next(1, 5);
				}
				break;
			}
			case StateInhale: {
				// The last frame is held for a few ticks before it breathes out
				std::int32_t over = ShowFrame(AnimStateInhale, step);
				if (over >= 0 && _time - (float)((step - over) * FrameTicks) >= (float)_count) {
					StartStep(StateExhaleStart);
				}
				break;
			}
			case StateExhaleStart: {
				if (ShowFrame(AnimStateExhaleStart, step) >= 0) {
					StartStep(StateExhale);
					_count = Random().Next(4, 12);
				}
				break;
			}
			case StateExhale: {
				// Each puff plays the frames, holds the last one for another frame and releases a smoke ring
				std::int32_t frame = (std::int32_t)(_time / ExhaleFrameTicks);
				ShowFrame(AnimStateExhale, frame);
				if (frame > _renderer.FrameCount) {
					_time = 0.0f;
					SpawnSmoke();
					_count--;
					if (_count <= 0) {
						StartStep(StateExhaleEnd);
					}
				}
				break;
			}
			case StateExhaleEnd: {
				// The mouth closes by playing the exhale start backwards
				SetAnimation(AnimStateExhaleStart);
				std::int32_t frame = _renderer.FrameCount - 1 - step;
				if (frame < 0) {
					StartStep(StateIdle);
					_count = Random().Next(0, 32);
					ShowFrame(AnimState::Idle, 0);
				} else {
					SetAnimationFrame(frame);
				}
				break;
			}
			case StateDisoriented: {
				SetAnimation(AnimStateDisoriented);
				if (_renderer.FrameCount > 0) {
					SetAnimationFrame(step % _renderer.FrameCount);
				}
				if (_time >= DisorientedTicks) {
					StartStep(StateIdle);
					_count = Random().Next(70, 198);
					ShowFrame(AnimState::Idle, 0);
				}
				break;
			}
		}
	}

	void Caterpillar::OnSerializeState(Stream& dest)
	{
		EnemyBase::OnSerializeState(dest);

		dest.WriteVariableInt32(_state);
		dest.WriteVariableInt32(_count);
		dest.WriteValueAsLE<float>(_time);
	}

	void Caterpillar::OnDeserializeState(Stream& src)
	{
		EnemyBase::OnDeserializeState(src);

		_state = src.ReadVariableInt32();
		_count = src.ReadVariableInt32();
		_time = src.ReadValueAsLE<float>();

		if (_state < StateIdle || _state > StateDisoriented) {
			_state = StateIdle;
		}
	}

	bool Caterpillar::OnHandleCollision(ActorBase* other)
	{
		if (auto* shotBase = runtime_cast<Weapons::ShotBase>(other)) {
			// A shot leaves it dizzy for a while, another one meanwhile doesn't make it longer
			if (_state != StateDisoriented) {
				StartStep(StateDisoriented);
			}
		}

		return false;
	}

	void Caterpillar::StartStep(std::int32_t state)
	{
		_state = state;
		_time = 0.0f;
	}

	std::int32_t Caterpillar::ShowFrame(AnimState state, std::int32_t frame)
	{
		SetAnimation(state);
		std::int32_t frameCount = std::max(_renderer.FrameCount, 1);
		SetAnimationFrame(std::min(frame, frameCount - 1));
		return (frame >= frameCount ? frame - frameCount : -1);
	}

	void Caterpillar::SpawnSmoke()
	{
		std::shared_ptr<Smoke> smoke = std::make_shared<Smoke>();
		smoke->OnActivated(ActorActivationDetails(
			_levelHandler,
			Vector3i((std::int32_t)_pos.X - 26, (std::int32_t)_pos.Y - 18, _renderer.GetLayer() + 20)
		));
		_levelHandler->AddActor(smoke);
	}

	Task<bool> Caterpillar::Smoke::OnActivatedAsync(const ActorActivationDetails& details)
	{
		SetState(ActorState::CanBeFrozen | ActorState::CollideWithTileset | ActorState::ApplyGravitation, false);
		SetState(ActorState::IsInvulnerable | ActorState::SkipPerPixelCollisions, true);
		CanCollideWithShots = false;
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
		_speed.X = _baseSpeed.X + cosApprox((500.0f - _time) * 0.09f) * 0.5f;
		_speed.Y = _baseSpeed.Y + sinApprox((500.0f - _time) * 0.05f) * 0.5f;

		MoveInstantly(Vector2f(_speed.X * timeMult, _speed.Y * timeMult), MoveType::Relative | MoveType::Force);
		_renderer.SetScale(_renderer.GetScale() - 0.0011f * timeMult);

		if (_time <= 0.0f) {
			DecreaseHealth(INT32_MAX);
		} else {
			_time -= timeMult;
		}
	}

	bool Caterpillar::Smoke::OnHandleCollision(ActorBase* other)
	{
		if (auto* player = runtime_cast<Player>(other)) {
			if (player->SetDizzy(180.0f)) {
				// TODO: Add fade-out
				PlaySfx("Dizzy"_s);
			}
		}

		return false;
	}
}
