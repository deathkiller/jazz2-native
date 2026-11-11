#include "BlasterShot.h"
#include "../../ILevelHandler.h"
#include "../../Events/EventMap.h"
#include "../Player.h"
#include "../Explosion.h"

#include "../../../nCine/Base/FrameTimer.h"
#include "../../../nCine/Base/Random.h"
#include "../../../nCine/CommonConstants.h"

using namespace Jazz2::Tiles;

namespace Jazz2::Actors::Weapons
{
	BlasterShot::BlasterShot()
		: _fired(0)
	{
	}

	Task<bool> BlasterShot::OnActivatedAsync(const ActorActivationDetails& details)
	{
		async_await ShotBase::OnActivatedAsync(details);

		_upgrades = details.Params[0];

		SetState(ActorState::SkipPerPixelCollisions, true);
		SetState(ActorState::ApplyGravitation, false);

		async_await RequestMetadataAsync("Weapon/Blaster"_s);

		AnimState state = AnimState::Idle;
		_ricochetLimit = MaxTileRicochets;
		if ((_upgrades & 0x01) != 0) {
			_timeLeft = 30.0f * TicksToFrames;
			state |= (AnimState)1;
			_strength = 2;
		} else {
			_timeLeft = 35.0f * TicksToFrames;
			_strength = 1;
		}

		SetAnimation(state);

		_renderer.SetBlendingPreset(DrawableNode::BlendingPreset::Additive);

		async_return true;
	}

	void BlasterShot::OnFire(const std::shared_ptr<ActorBase>& owner, Vector2f gunspotPos, Vector2f speed, float angle, bool isFacingLeft)
	{
		_owner = owner;
		SetFacingLeft(isFacingLeft);

		_gunspotPos = gunspotPos;

		float angleRel = angle * (isFacingLeft ? -1 : 1);

		bool powered = ((_upgrades & 0x01) != 0);
		float baseSpeed = (powered ? 7.0f : 6.0f) * TickSpeedScale;
		float sinAngle, cosAngle;
		sincosApprox(angleRel, sinAngle, cosAngle);
		_speed.X = (isFacingLeft ? -cosAngle : cosAngle) * baseSpeed;
		_speed.Y = sinAngle * baseSpeed;
		SetAcceleration(_speed.Normalized(), (powered ? 0.1875f : 0.125f) * TickAccelScale, 10.0f * TickSpeedScale);
		SetCarriedSpeed(std::abs(sinAngle) > 0.99f ? 0.0f : speed.X, true);
		ShortenUpShotLifetime(sinAngle);

		_renderer.SetRotation(angle);
		_renderer.SetDrawEnabled(false);
	}

	void BlasterShot::OnUpdate(float timeMult)
	{
		std::int32_t n = GetMovementSubstepCount(timeMult);
		TileCollisionParams params = { TileDestructType::Weapon, false, WeaponType::Blaster, _strength };
		for (std::int32_t i = 0; i < n && params.WeaponStrength > 0; i++) {
			TryMovement(timeMult / n, params);
		}
		if (params.TilesDestroyed > 0) {
			if (auto* player = runtime_cast<Player>(_owner.get())) {
				player->AddScore(params.TilesDestroyed * 50);
			}
		}
		if (params.WeaponStrength <= 0) {
			DecreaseHealth(INT32_MAX);
			return;
		}

		ShotBase::OnUpdate(timeMult);

		if (_timeLeft <= 0.0f) {
			PlaySfx("WallPoof"_s);
		}

		if (_fired < 255) {
			_fired++;
		}
		if (_fired >= 2) {
			RevealPastMuzzle(_gunspotPos);
		}
	}

	void BlasterShot::OnUpdateHitbox()
	{
		AABBInner = AABBf(_pos.X - 3, _pos.Y - 2, _pos.X + 3, _pos.Y + 4);
	}

	void BlasterShot::OnEmitLights(SmallVectorImpl<LightEmitter>& lights)
	{
		if (_fired >= 2) {
			auto& light = lights.emplace_back();
			light.Pos = _pos;
			light.Intensity = 0.8f;
			light.Brightness = 0.6f;
			light.RadiusNear = 5.0f;
			light.RadiusFar = 20.0f;
		}
	}

	bool BlasterShot::OnPerish(ActorBase* collider)
	{
		Explosion::Create(_levelHandler, Vector3i((std::int32_t)(_pos.X + _speed.X), (std::int32_t)(_pos.Y + _speed.Y), _renderer.GetLayer() + 2), Explosion::Type::Small);

		return ShotBase::OnPerish(collider);
	}

	void BlasterShot::OnHitWall(float timeMult)
	{
		auto events = _levelHandler->EventMap();
		if (events != nullptr) {
			std::uint8_t* eventParams;
			switch (events->GetEventByPosition(_pos.X + _speed.X, _pos.Y + _speed.Y, &eventParams)) {
				case EventType::ModifierRicochet:
					TriggerRicochet(nullptr);
					return;
			}
		}

		DecreaseHealth(INT32_MAX);

		PlaySfx("WallPoof"_s);
	}

	void BlasterShot::OnRicochet()
	{
		OnRicochetExact();

		_renderer.SetRotation(atan2Approx(_speed.Y, _speed.X));

		PlaySfx("Ricochet"_s);
	}
}