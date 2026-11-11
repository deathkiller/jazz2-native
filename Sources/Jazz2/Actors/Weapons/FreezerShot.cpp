#include "FreezerShot.h"
#include "../../ILevelHandler.h"
#include "../../Tiles/TileMap.h"
#include "../../Events/EventMap.h"
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
		: _particlesTime(1.0f), _wavePhase(0.0f), _fired(0), _waves(false)
	{
	}

	Task<bool> FreezerShot::OnActivatedAsync(const ActorActivationDetails& details)
	{
		async_await ShotBase::OnActivatedAsync(details);

		_upgrades = details.Params[0];
		SetFrozenDuration(((_upgrades & 0x1) != 0 ? 254.0f : 200.0f) * TicksToFrames);
		_ricochetLimit = MaxTileRicochets;

		SetState(ActorState::SkipPerPixelCollisions, true);
		SetState(ActorState::ApplyGravitation, false);
		_strength = 0;

		async_await RequestMetadataAsync("Weapon/Freezer"_s);

		AnimState state = AnimState::Idle;
		if ((_upgrades & 0x01) != 0) {
			_timeLeft = 30.0f * TicksToFrames;
			state |= (AnimState)1;
			PlaySfx("FireUpgraded"_s);

			// TODO: Add better upgraded effect
			_renderer.SetScale(1.2f);
		} else {
			_timeLeft = 35.0f * TicksToFrames;
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

		bool poweredUp = ((_upgrades & 0x01) != 0);
		float baseSpeed = (poweredUp ? 7.0f : 6.0f) * TickSpeedScale;
		float sinAngle, cosAngle;
		sincosApprox(angleRel, sinAngle, cosAngle);
		_speed.X = (isFacingLeft ? -cosAngle : cosAngle) * baseSpeed;
		_speed.Y = sinAngle * baseSpeed;
		// The powered-up shot isn't capped, the plain one is
		SetAcceleration(_speed.Normalized(), (poweredUp ? 0.1875f : 0.125f) * TickAccelScale, poweredUp ? 0.0f : 10.0f * TickSpeedScale);
		SetCarriedSpeed(std::abs(sinAngle) > 0.99f ? 0.0f : speed.X, true);
		ShortenUpShotLifetime(sinAngle);

		_renderer.SetRotation(angle);
		_renderer.SetDrawEnabled(false);
	}

	void FreezerShot::OnUpdate(float timeMult)
	{
		std::int32_t n = GetMovementSubstepCount(timeMult);
		TileCollisionParams params = { TileDestructType::Weapon, _speed.Y >= 0.0f, WeaponType::Freezer, _strength };
		for (std::int32_t i = 0; i < n; i++) {
			TryMovement(timeMult / n, params);
		}

		ShotBase::OnUpdate(timeMult);

		// TODO: Add particles

		if (_timeLeft <= 0.0f) {
			PlaySfx("WallPoof"_s);
		}

		if (_waves) {
			float sinWave, cosWave;
			sincosApprox(_wavePhase * (fRadAngle360 / 1024.0f), sinWave, cosWave);
			Vector2f wave = (std::abs(_speed.X) < 0.01f ? Vector2f(sinWave, 0.0f) : Vector2f(0.0f, sinWave)) * (TickSpeedScale * timeMult);
			TileCollisionParams waveParams = { TileDestructType::Weapon, false, WeaponType::Freezer, _strength };
			MoveInstantly(wave, MoveType::Relative, waveParams);
			_wavePhase += WaveStep * TickSpeedScale * timeMult;
		}

		if (_fired < 255) {
			_fired++;
		}
		if (_fired >= ((_upgrades & 0x01) != 0 ? HiddenUpdatesPoweredUp : HiddenUpdates)) {
			RevealPastMuzzle(_gunspotPos);
		}

		_particlesTime -= timeMult;
		if (_particlesTime <= 0.0f) {
			_particlesTime += 1.0f;

			auto tileMap = _levelHandler->TileMap();
			auto resBase = _currentAnimation->Base;
			if (tileMap != nullptr && _pos.Y < _levelHandler->GetWaterLevel() && resBase->TextureDiffuse != nullptr) {
				Vector2i texSize = resBase->TextureDiffuse->GetSize();
				float dx = Random().FastFloat(-8.0f, 8.0f);
				float dy = Random().FastFloat(-3.0f, 3.0f);

				constexpr float currentSize = 1.0f;

				Tiles::TileMap::DestructibleDebris debris = { };
				debris.Pos = Vector2f(_pos.X + dx, _pos.Y + dy);
				debris.Depth = _renderer.GetLayer();
				debris.Size = Vector2f(currentSize, currentSize);
				debris.Acceleration = Vector2f(0.0f, _levelHandler->GetGravity());

				debris.Scale = 1.2f,
					debris.Alpha = 1.0f;

				debris.Time = 300.0f;

				debris.TexScaleX = (currentSize / float(texSize.X));
				const Recti frameRect = resBase->GetFrameRect(_renderer.CurrentFrame);
				debris.TexBiasX = ((float(frameRect.X) + (frameRect.W * 0.5f) + dx) / float(texSize.X));
				debris.TexScaleY = (currentSize / float(texSize.Y));
				debris.TexBiasY = ((float(frameRect.Y) + (frameRect.H * 0.5f) + dy) / float(texSize.Y));

				debris.DiffuseTexture = resBase->TextureDiffuse.get();
				// Recolor through the palette when the sprite is indexed (-1 = baked/RGBA, behavior unchanged)
				debris.PaletteOffset = (((resBase->Flags & GenericGraphicResourceFlags::Indexed) == GenericGraphicResourceFlags::Indexed) ? (std::int32_t)_currentAnimation->PaletteOffset : -1);
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
		Explosion::Create(_levelHandler, Vector3i((std::int32_t)(_pos.X + _speed.X), (std::int32_t)(_pos.Y + _speed.Y), _renderer.GetLayer() + 2), Explosion::Type::SmokeWhite);

		return ShotBase::OnPerish(collider);
	}

	void FreezerShot::OnHitWall(float timeMult)
	{
		auto events = _levelHandler->EventMap();
		if (events != nullptr) {
			std::uint8_t* eventParams;
			if (events->GetEventByPosition(_pos.X + _speed.X, _pos.Y + _speed.Y, &eventParams) == EventType::ModifierRicochet) {
				TriggerRicochet(nullptr);
				return;
			}
		}

		DecreaseHealth(INT32_MAX);

		PlaySfx("WallPoof"_s);
	}

	void FreezerShot::OnRicochet()
	{
		OnRicochetExact();

		_renderer.SetRotation(atan2Approx(_speed.Y, _speed.X));

		PlaySfx("Ricochet"_s);
	}
}