#include "FreezerShot.h"
#include "../../ILevelHandler.h"
#include "../../Tiles/TileMap.h"
#include "../Player.h"
#include "../Explosion.h"
#include "../Solid/TriggerCrate.h"

#include "../../../nCine/Base/FrameTimer.h"
#include "../../../nCine/Base/Random.h"
#include "../../../nCine/CommonConstants.h"

using namespace Jazz2::Tiles;

namespace Jazz2::Actors::Weapons
{
	FreezerShot::FreezerShot()
		: _fired(0), _particlesTime(1.0f)
	{
	}

	Task<bool> FreezerShot::OnActivatedAsync(const ActorActivationDetails& details)
	{
		async_await ShotBase::OnActivatedAsync(details);

		_upgrades = details.Params[0];

		SetState(ActorState::SkipPerPixelCollisions, true);
		SetState(ActorState::ApplyGravitation, false);
		_strength = 0;

		async_await RequestMetadataAsync("Weapon/Freezer"_s);

		AnimState state = AnimState::Idle;
		if ((_upgrades & 0x01) != 0) {
			_timeLeft = 38;
			state |= (AnimState)1;
			PlaySfx("FireUpgraded"_s);

			// TODO: Add better upgraded effect
			_renderer.setScale(1.2f);
		} else {
			_timeLeft = 44;
			PlaySfx("Fire"_s);
		}

		SetAnimation(state);

		async_return true;
	}

	void FreezerShot::OnFire(const std::shared_ptr<ActorBase>& owner, Vector2f gunspotPos, Vector2f speed, float angle, bool isFacingLeft)
	{
		_owner = owner;
		SetFacingLeft(isFacingLeft);

		_gunspotPos = gunspotPos;

		float angleRel = angle * (isFacingLeft ? -1 : 1);

		float baseSpeed = ((_upgrades & 0x01) != 0 ? 8.0f : 6.0f);
		if (isFacingLeft) {
			_speed.X = std::min(0.0f, speed.X) - cosf(angleRel) * baseSpeed;
		} else {
			_speed.X = std::max(0.0f, speed.X) + cosf(angleRel) * baseSpeed;
		}
		_speed.Y = sinf(angleRel) * baseSpeed;

		_renderer.setRotation(angle);
		_renderer.setDrawEnabled(false);
	}

	void FreezerShot::OnUpdate(float timeMult)
	{
		int n = (timeMult > 0.9f ? 2 : 1);
		TileCollisionParams params = { TileDestructType::Weapon, _speed.Y >= 0.0f, WeaponType::Freezer, _strength };
		for (int i = 0; i < n; i++) {
			TryMovement(timeMult / n, params);
		}

		ShotBase::OnUpdate(timeMult);

		// TODO: Add particles

		if (_timeLeft <= 0.0f) {
			PlaySfx("WallPoof"_s);
		}

		_fired++;
		if (_fired == 2) {
			MoveInstantly(_gunspotPos, MoveType::Absolute | MoveType::Force);
			_renderer.setDrawEnabled(true);
		}

		_particlesTime -= timeMult;
		if (_particlesTime <= 0.0f) {
			_particlesTime += 1.0f;

			auto tileMap = _levelHandler->TileMap();
			auto resBase = _currentAnimation->Base;
			if (tileMap != nullptr && _pos.Y < _levelHandler->WaterLevel() && resBase->TextureDiffuse != nullptr) {
				Vector2i texSize = resBase->TextureDiffuse->size();
				float dx = Random().FastFloat(-8.0f, 8.0f);
				float dy = Random().FastFloat(-3.0f, 3.0f);

				constexpr float currentSize = 1.0f;

				Tiles::TileMap::DestructibleDebris debris = { };
				debris.Pos = Vector2f(_pos.X + dx, _pos.Y + dy);
				debris.Depth = _renderer.layer();
				debris.Size = Vector2f(currentSize, currentSize);
				debris.Acceleration = Vector2f(0.0f, _levelHandler->Gravity());

				debris.Scale = 1.2f,
					debris.Alpha = 1.0f;

				debris.Time = 300.0f;

				debris.TexScaleX = (currentSize / float(texSize.X));
				debris.TexBiasX = (((float)(_renderer.CurrentFrame % resBase->FrameConfiguration.X) / resBase->FrameConfiguration.X) + (((resBase->FrameDimensions.X * 0.5f) + dx) / float(texSize.X)));
				debris.TexScaleY = (currentSize / float(texSize.Y));
				debris.TexBiasY = (((float)(_renderer.CurrentFrame / resBase->FrameConfiguration.X) / resBase->FrameConfiguration.Y) + (((resBase->FrameDimensions.Y * 0.5f) + dy) / float(texSize.Y)));

				debris.DiffuseTexture = resBase->TextureDiffuse.get();
				debris.Flags = Tiles::TileMap::DebrisFlags::Disappear;

				tileMap->CreateDebris(debris);
			}
		}
	}

	void FreezerShot::OnUpdateHitbox()
	{
		// TODO: This is a quick fix for player cannot freeze springs
		AABBInner = AABBf(
			_pos.X - 4,
			_pos.Y - 2,
			_pos.X + 4,
			_pos.Y + 6
		);
	}

	void FreezerShot::OnEmitLights(SmallVectorImpl<LightEmitter>& lights)
	{
		if (_fired >= 2) {
			auto& light = lights.emplace_back();
			light.Pos = _pos;
			light.Intensity = 0.8f;
			light.Brightness = 0.2f;
			light.RadiusNear = 0.0f;
			light.RadiusFar = 20.0f;
		}
	}

	bool FreezerShot::OnPerish(ActorBase* collider)
	{
		Explosion::Create(_levelHandler, Vector3i((int)(_pos.X + _speed.X), (int)(_pos.Y + _speed.Y), _renderer.layer() + 2), Explosion::Type::SmokeWhite);

		return ShotBase::OnPerish(collider);
	}

	void FreezerShot::OnHitWall(float timeMult)
	{
		DecreaseHealth(INT32_MAX);

		PlaySfx("WallPoof"_s);
	}

	void FreezerShot::OnRicochet()
	{
		DecreaseHealth(INT32_MAX);

		PlaySfx("WallPoof"_s);
	}
}