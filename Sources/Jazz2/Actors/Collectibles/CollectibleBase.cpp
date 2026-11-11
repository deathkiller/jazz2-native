#include "CollectibleBase.h"
#include "../Player.h"
#include "../Explosion.h"
#include "../Weapons/ShotBase.h"
#include "../Weapons/TNT.h"
#include "../Enemies/TurtleShell.h"
#include "../../ILevelHandler.h"
#include "../../Events/EventMap.h"

#include "../../../nCine/Base/FrameTimer.h"
#include "../../../nCine/Base/Random.h"
#include "../../../nCine/CommonConstants.h"

namespace Jazz2::Actors::Collectibles
{
	namespace
	{
		// A number from 0 to 1, the same for the same seed
		float GetSwarmHash(std::uint32_t seed)
		{
			std::uint32_t h = seed * 0x9E3779B1u;
			h ^= h >> 15;
			h *= 0x2C1B3C6Du;
			h ^= h >> 12;
			return float(h & 0xFFFF) / 65535.0f;
		}
	}

	void CollectibleBase::IlluminateLights::Create(float elapsedFrames)
	{
		// The same swarm for every object, as JJ2+ shows it
		_time = elapsedFrames;
		_lights.reserve(LightCount);
		for (std::int32_t i = 0; i < LightCount; i++) {
			auto& light = _lights.emplace_back();
			std::uint32_t seed = std::uint32_t(i) * 4u;
			light.Intensity = 0.22f + 0.2f * GetSwarmHash(seed);
			// Three in ten spots close to the object, the others farther out
			light.Distance = (i < LightCount * 3 / 10 ? 8.0f + 16.0f * GetSwarmHash(seed + 1u) : 24.0f + 18.0f * GetSwarmHash(seed + 1u));
			light.Phase = fTwoPi * GetSwarmHash(seed + 2u);
			light.Speed = -(0.04f + 0.08f * GetSwarmHash(seed + 3u));
		}
	}

	void CollectibleBase::IlluminateLights::OnUpdate(float timeMult)
	{
		_time += timeMult;
	}

	void CollectibleBase::IlluminateLights::OnEmitLights(SmallVectorImpl<LightEmitter>& lights, Vector2f pos) const
	{
		// Fitted to the illuminated pickups of JJ2+, averaged over time in darkness and in full light
		for (const auto& current : _lights) {
			// Both waves repeat after 200 pi, which keeps the angle small
			float phase = std::fmod(current.Phase + current.Speed * _time, 200.0f * fPi);
			auto& light = lights.emplace_back();
			light.Pos = Vector2f(pos.X + cosApprox(phase + cosApprox(phase * 0.33f) * 0.33f) * current.Distance,
				pos.Y + sinApprox(phase + sinApprox(phase) * 0.33f) * current.Distance);
			light.Intensity = current.Intensity;
			light.Brightness = current.Intensity * 0.8f;
			light.RadiusNear = 0.0f;
			light.RadiusFar = 32.0f;
		}
	}

	CollectibleBase::CollectibleBase()
		: _scoreValue(0), _timeLeft(0.0f), _phase(0.0f), _startingY(0.0f), _untouched(true)
	{
	}

	Task<bool> CollectibleBase::OnActivatedAsync(const ActorActivationDetails& details)
	{
		_elasticity = 0.5f;

		// Collected by the pixels of both frames, as measured in the original
		SetState(ActorState::SkipPerPixelCollisions, false);

		Vector2f pos = _pos;
		_phase = ((pos.X / 32) + (pos.Y / 32)) * 2.0f;

		if ((GetState() & (ActorState::IsCreatedFromEventMap | ActorState::IsFromGenerator)) != ActorState::None) {
			_untouched = true;
			SetState(ActorState::ApplyGravitation, false);

			_startingY = pos.Y;
		} else {
			_untouched = false;
			SetState(ActorState::ApplyGravitation, true);

			if (_levelHandler->CanEventDisappear(details.Type)) {
				_timeLeft = 90.0f * FrameTimer::FramesPerSecond;
			}
		}

		if ((details.State & ActorState::Illuminated) == ActorState::Illuminated) {
			_illuminateLights = std::make_unique<IlluminateLights>();
			_illuminateLights->Create(_levelHandler->GetElapsedFrames());
		}

		async_return true;
	}

	void CollectibleBase::OnUpdate(float timeMult)
	{
		ActorBase::OnUpdate(timeMult);

		if (_pickupDelay > 0.0f) {
			_pickupDelay -= timeMult;
		}

		if (_untouched) {
			_phase += timeMult * 0.15f;

			// Carried by a belt in the tile below as the player is, until it's off its end, as in the original
			float x = _pos.X;
			std::uint8_t* p;
			if (_levelHandler->EventMap()->GetEventByPosition(_pos.X, _startingY + 32.0f, &p) == EventType::AreaHForce && (p[0] != 0 || p[1] != 0)) {
				float factor = (_levelHandler->IsReforged() ? 0.7f : Player::LegacyBeltFactor);
				x += (float(p[1]) - float(p[0])) * factor * timeMult;
			}

			float waveOffset = 3.2f * cosApprox((_phase * 0.25f) * fPi) + 0.6f;
			MoveInstantly(Vector2f(x, _startingY + waveOffset), MoveType::Absolute);
		} else if (_timeLeft > 0.0f) {
			_timeLeft -= timeMult;
			if (_timeLeft <= 0.0f) {
				Explosion::Create(_levelHandler, Vector3i((std::int32_t)_pos.X, (std::int32_t)_pos.Y, _renderer.GetLayer()), Explosion::Type::Generator);
				DecreaseHealth(INT32_MAX);
			}
		}

		if (_illuminateLights != nullptr) {
			_illuminateLights->OnUpdate(timeMult);
		}
	}

	void CollectibleBase::OnSerializeState(Stream& dest)
	{
		ActorBase::OnSerializeState(dest);

		dest.WriteValue<std::uint8_t>(_untouched ? 1 : 0);
		dest.WriteVariableInt32(_scoreValue);
		dest.WriteValueAsLE<float>(_timeLeft);
		dest.WriteValueAsLE<float>(_phase);
		dest.WriteValueAsLE<float>(_startingY);
	}

	void CollectibleBase::OnDeserializeState(Stream& src)
	{
		ActorBase::OnDeserializeState(src);

		_untouched = (src.ReadValue<std::uint8_t>() != 0);
		_scoreValue = src.ReadVariableInt32();
		_timeLeft = src.ReadValueAsLE<float>();
		_phase = src.ReadValueAsLE<float>();
		_startingY = src.ReadValueAsLE<float>();
	}

	void CollectibleBase::OnEmitLights(SmallVectorImpl<LightEmitter>& lights)
	{
		if (_illuminateLights != nullptr) {
			_illuminateLights->OnEmitLights(lights, _pos);
		}
	}

	bool CollectibleBase::OnHandleCollision(ActorBase* other)
	{
		if (auto* player = runtime_cast<Player>(other)) {
			if (_pickupDelay > 0.0f) {
				return false;
			}
			OnCollect(player);
			return true;
		} else {
			bool shouldDrop = _untouched && (runtime_cast<Weapons::ShotBase>(other) ||
				runtime_cast<Weapons::TNT>(other) || runtime_cast<Enemies::TurtleShell>(other));
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
		Explosion::Create(_levelHandler, Vector3i((std::int32_t)_pos.X, (std::int32_t)_pos.Y, _renderer.GetLayer()), Explosion::Type::Generator);
		DecreaseHealth(INT32_MAX);
	}

	void CollectibleBase::SetFacingDirection(bool inverse)
	{
		SetFacingLeft(((((std::int32_t)(_pos.X + _pos.Y) / 32) & 1) == 0) ^ inverse);
	}
}