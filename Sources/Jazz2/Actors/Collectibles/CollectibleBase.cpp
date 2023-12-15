#include "CollectibleBase.h"
#include "../Player.h"
#include "../Explosion.h"
#include "../Weapons/ShotBase.h"
#include "../Weapons/TNT.h"
#include "../Enemies/TurtleShell.h"

#include "../../../nCine/Base/FrameTimer.h"
#include "../../../nCine/Base/Random.h"
#include "../../../nCine/CommonConstants.h"

namespace Jazz2::Actors::Collectibles
{
	CollectibleBase::CollectibleBase()
		:
		_untouched(true),
		_scoreValue(0),
		_phase(0.0f),
		_timeLeft(0.0f),
		_startingY(0.0f)
	{
	}

	Task<bool> CollectibleBase::OnActivatedAsync(const ActorActivationDetails& details)
	{
		_elasticity = 0.6f;

		SetState(ActorState::SkipPerPixelCollisions, true);

		Vector2f pos = _pos;
		_phase = ((pos.X / 32) + (pos.Y / 32)) * 2.0f;

		if ((GetState() & (ActorState::IsCreatedFromEventMap | ActorState::IsFromGenerator)) != ActorState::None) {
			_untouched = true;
			SetState(ActorState::ApplyGravitation, false);

			_startingY = pos.Y;
		} else {
			_untouched = false;
			SetState(ActorState::ApplyGravitation, true);

			_timeLeft = 90.0f * FrameTimer::FramesPerSecond;
		}

		if ((details.State & ActorState::Illuminated) == ActorState::Illuminated) {
			_illuminateLights.reserve(IlluminateLightCount);
			for (int i = 0; i < IlluminateLightCount; i++) {
				auto& light = _illuminateLights.emplace_back();
				light.Intensity = Random().NextFloat(0.22f, 0.42f);
				light.Distance = Random().NextFloat(4.0f, 36.0f);
				light.Phase = Random().NextFloat(0.0f, fTwoPi);
				light.Speed = Random().NextFloat(-0.12f, -0.04f);
			}
		}

		async_return true;
	}

	void CollectibleBase::OnUpdate(float timeMult)
	{
		ActorBase::OnUpdate(timeMult);

		if (_untouched) {
			_phase += timeMult * 0.15f;

			float waveOffset = 3.2f * cosf((_phase * 0.25f) * fPi) + 0.6f;
			MoveInstantly(Vector2f(_pos.X, _startingY + waveOffset), MoveType::Absolute);
		} else if (_timeLeft > 0.0f) {
			_timeLeft -= timeMult;
			if (_timeLeft <= 0.0f) {
				Explosion::Create(_levelHandler, Vector3i((int)_pos.X, (int)_pos.Y, _renderer.layer()), Explosion::Type::Generator);
				DecreaseHealth(INT32_MAX);
			}
		}

		for (auto& current : _illuminateLights) {
			current.Phase += current.Speed * timeMult;
		}
	}

	void CollectibleBase::OnEmitLights(SmallVectorImpl<LightEmitter>& lights)
	{
		for (auto& current : _illuminateLights) {
			auto& light = lights.emplace_back();
			light.Pos = Vector2f(_pos.X + cosf(current.Phase + cosf(current.Phase * 0.33f) * 0.33f) * current.Distance,
				_pos.Y + sinf(current.Phase + sinf(current.Phase) * 0.33f) * current.Distance);
			light.Intensity = current.Intensity * 0.7f;
			light.Brightness = current.Intensity;
			light.RadiusNear = 0.0f;
			light.RadiusFar = current.Intensity * 86.0f;
		}
	}

	bool CollectibleBase::OnHandleCollision(std::shared_ptr<ActorBase> other)
	{
		if (auto* player = runtime_cast<Player*>(other)) {
			OnCollect(player);
			return true;
		} else {
			bool shouldDrop = _untouched && (runtime_cast<Weapons::ShotBase*>(other) || runtime_cast<Weapons::TNT*>(other) || runtime_cast<Enemies::TurtleShell*>(other));
			if (shouldDrop) {
				Vector2f speed = other->GetSpeed();
				_externalForce.X += speed.X / 2.0f * (0.9f + Random().NextFloat(0.0f, 0.2f));
				_externalForce.Y += speed.Y / 4.0f * (0.9f + Random().NextFloat(0.0f, 0.2f));

				_untouched = false;
				SetState(ActorState::ApplyGravitation, true);
			}
		}

		return false;
	}

	void CollectibleBase::OnCollect(Player* player)
	{
		player->AddScore(_scoreValue);
		Explosion::Create(_levelHandler, Vector3i((int)_pos.X, (int)_pos.Y, _renderer.layer()), Explosion::Type::Generator);
		DecreaseHealth(INT32_MAX);
	}

	void CollectibleBase::SetFacingDirection()
	{
		if ((((int)(_pos.X + _pos.Y) / 32) & 1) == 0) {
			SetFacingLeft(true);
		}
	}
}