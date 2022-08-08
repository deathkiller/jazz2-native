#include "ToasterShot.h"
#include "../../ILevelHandler.h"
#include "../../LevelInitialization.h"
#include "../../Tiles/TileMap.h"

#include "../../../nCine/Base/Random.h"

namespace Jazz2::Actors::Weapons
{
	ToasterShot::ToasterShot()
		:
		_fired(false)
	{
	}

	Task<bool> ToasterShot::OnActivatedAsync(const ActorActivationDetails& details)
	{
		co_await ShotBase::OnActivatedAsync(details);

		_strength = 1;
		_upgrades = details.Params[0];

		CollisionFlags &= ~CollisionFlags::ApplyGravitation;

		co_await RequestMetadataAsync("Weapon/Toaster"_s);

		AnimState state = AnimState::Idle;
		if ((_upgrades & 0x01) != 0) {
			_timeLeft = 80.0f;
			state |= (AnimState)1;
		} else {
			_timeLeft = 60.0f;
		}

		SetAnimation(state);

		co_return true;
	}

	void ToasterShot::OnFire(const std::shared_ptr<ActorBase>& owner, Vector2f gunspotPos, Vector2f speed, float angle, bool isFacingLeft)
	{
		_owner = owner;
		SetFacingLeft(isFacingLeft);

		_gunspotPos = gunspotPos;

		float ax = std::cosf(angle);
		float ay = std::sinf(angle);

		constexpr float baseSpeed = 1.2f;
		if (isFacingLeft) {
			_speed.X = std::min(0.0f, speed.X) - ax * (baseSpeed + Random().NextFloat(0.0f, 0.2f));
		} else {
			_speed.X = std::max(0.0f, speed.X) + ax * (baseSpeed + Random().NextFloat(0.0f, 0.2f));
		}
		_speed.X += ay * Random().NextFloat(-0.5f, 0.5f);

		if (isFacingLeft) {
			_speed.Y = -ay * (baseSpeed + Random().NextFloat(0.0f, 0.2f));
		} else {
			_speed.Y = ay * (baseSpeed + Random().NextFloat(0.0f, 0.2f));
		}
		_speed.Y += ax * Random().NextFloat(-0.5f, 0.5f);

		_renderer.setDrawEnabled(false);
	}

	void ToasterShot::OnUpdate(float timeMult)
	{
		OnUpdateHitbox();

		if (_pos.Y >= _levelHandler->WaterLevel()) {
			DecreaseHealth(INT32_MAX);
			return;
		}

		auto tiles = _levelHandler->TileMap();
		TileCollisionParams params = { TileDestructType::Weapon, _speed.Y >= 0.0f, WeaponType::Toaster, _strength };
		if (tiles == nullptr || tiles->IsTileEmpty(AABBInner, params)) {
			MoveInstantly(Vector2f(_speed.X * timeMult, _speed.Y * timeMult), MoveType::Relative | MoveType::Force, params);
		} else {
			MoveInstantly(Vector2f(_speed.X * timeMult, _speed.Y * timeMult), MoveType::Relative | MoveType::Force, params);
			MoveInstantly(Vector2f(-_speed.X * timeMult, -_speed.Y * timeMult), MoveType::Relative | MoveType::Force, params);

			if ((_upgrades & 0x1) == 0) {
				DecreaseHealth(INT32_MAX);
			}
		}

		if (!_fired) {
			_fired = true;
			MoveInstantly(_gunspotPos, MoveType::Absolute | MoveType::Force);
			_renderer.setDrawEnabled(true);
		}

		ShotBase::OnUpdate(timeMult);
	}

	void ToasterShot::OnEmitLights(SmallVectorImpl<LightEmitter>& lights)
	{
		auto& light = lights.emplace_back();
		light.Pos = _pos;
		light.Intensity = 0.85f;
		light.Brightness = 0.4f;
		light.RadiusNear = 0.0f;
		light.RadiusFar = 30.0f;
	}

	void ToasterShot::OnRicochet()
	{
		_speed.Y = _speed.Y * -0.2f * (Random().Next() % 100 - 50);
		_speed.X = _speed.X * -0.2f + (Random().Next() % 100 - 50) * 0.02f;
	}
}