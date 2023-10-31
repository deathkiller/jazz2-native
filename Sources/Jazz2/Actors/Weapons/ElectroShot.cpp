#include "ElectroShot.h"
#include "../../ILevelHandler.h"
#include "../../Events/EventMap.h"
#include "../../Tiles/TileMap.h"
#include "../Enemies/EnemyBase.h"
#include "../Explosion.h"

#include "../../../nCine/Base/Random.h"

using namespace Jazz2::Tiles;

namespace Jazz2::Actors::Weapons
{
	ElectroShot::ElectroShot()
		:
		_fired(0),
		_currentStep(0.0f),
		_particleSpawnTime(0.0f)
	{
	}

	Task<bool> ElectroShot::OnActivatedAsync(const ActorActivationDetails& details)
	{
		async_await ShotBase::OnActivatedAsync(details);

		_upgrades = details.Params[0];
		_strength = 4;
		_timeLeft = 55;

		SetState(ActorState::SkipPerPixelCollisions, true);
		SetState(ActorState::ApplyGravitation, false);

		async_await RequestMetadataAsync("Weapon/Electro"_s);
		SetAnimation(AnimState::Idle);
		PlaySfx("Fire"_s);

		_renderer.setDrawEnabled(false);

		async_return true;
	}

	void ElectroShot::OnFire(const std::shared_ptr<ActorBase>& owner, Vector2f gunspotPos, Vector2f speed, float angle, bool isFacingLeft)
	{
		_owner = owner;
		SetFacingLeft(isFacingLeft);

		_gunspotPos = gunspotPos;

		float angleRel = angle * (isFacingLeft ? -1 : 1);

		float baseSpeed = ((_upgrades & 0x1) != 0 ? 5.0f : 4.0f);
		if (isFacingLeft) {
			_speed.X = std::min(0.0f, speed.X) - cosf(angleRel) * baseSpeed;
		} else {
			_speed.X = std::max(0.0f, speed.X) + cosf(angleRel) * baseSpeed;
		}
		_speed.Y = sinf(angleRel) * baseSpeed;
	}

	void ElectroShot::OnUpdate(float timeMult)
	{
		int n = (timeMult > 0.9f ? 2 : 1);
		TileCollisionParams params = { TileDestructType::Weapon | TileDestructType::IgnoreSolidTiles, false, WeaponType::Electro, _strength };
		for (int i = 0; i < n && params.WeaponStrength > 0; i++) {
			TryMovement(timeMult / n, params);
		}
		if (params.WeaponStrength <= 0) {
			DecreaseHealth(INT32_MAX);
			return;
		}

		ShotBase::OnUpdate(timeMult);

		_fired++;
		if (_fired == 2) {
			MoveInstantly(_gunspotPos, MoveType::Absolute | MoveType::Force);
		} else if (_fired > 2) {
			_particleSpawnTime -= timeMult;
			if (_particleSpawnTime <= 0.0f) {
				_particleSpawnTime += 1.0f;

				auto tilemap = _levelHandler->TileMap();
				if (tilemap != nullptr) {
					auto* res = _metadata->FindAnimation((AnimState)1); // Particle
					if (res != nullptr) {
						auto& resBase = res->Base;
						Vector2i texSize = resBase->TextureDiffuse->size();

						for (int i = 0; i < 6; i++) {
							float angle = (_currentStep * 0.3f + i * 0.6f);
							if (IsFacingLeft()) {
								angle = -angle;
							}

							float size = (8.0f + _currentStep * 0.2f);
							float dist = (2.0f + _currentStep * 0.01f);
							float dx = dist * cosf(angle);
							float dy = dist * sinf(angle);

							Tiles::TileMap::DestructibleDebris debris = { };
							debris.Pos = Vector2f(_pos.X + dx, _pos.Y + dy);
							debris.Depth = _renderer.layer();
							debris.Size = Vector2f(size, size);

							debris.Scale = 1.0f;
							debris.ScaleSpeed = -0.1f;
							debris.Alpha = 1.0f;
							debris.AlphaSpeed = -0.1f;
							debris.Angle = angle;

							debris.Time = 60.0f;

							int curAnimFrame = ((_upgrades & 0x1) != 0 ? 2 : 0) + Random().Fast(0, 2);
							int col = curAnimFrame % resBase->FrameConfiguration.X;
							int row = curAnimFrame / resBase->FrameConfiguration.X;
							debris.TexScaleX = (float(resBase->FrameDimensions.X) / float(texSize.X));
							debris.TexBiasX = (float(resBase->FrameDimensions.X * col) / float(texSize.X));
							debris.TexScaleY = (float(resBase->FrameDimensions.Y) / float(texSize.Y));
							debris.TexBiasY = (float(resBase->FrameDimensions.Y * row) / float(texSize.Y));

							debris.DiffuseTexture = resBase->TextureDiffuse.get();

							tilemap->CreateDebris(debris);
						}
					}
				}
			}

			_currentStep += timeMult;
		}
	}

	void ElectroShot::OnUpdateHitbox()
	{
		UpdateHitbox(4, 4);
	}

	void ElectroShot::OnEmitLights(SmallVectorImpl<LightEmitter>& lights)
	{
		if (_fired >= 2) {
			auto& light = lights.emplace_back();
			light.Pos = _pos;
			light.Intensity = 0.4f + 0.016f * _currentStep;
			light.Brightness = 0.2f + 0.02f * _currentStep;
			light.RadiusNear = 0.0f;
			light.RadiusFar = 12.0f + 0.4f * _currentStep;
		}
	}

	bool ElectroShot::OnHandleCollision(std::shared_ptr<ActorBase> other)
	{
		if (auto enemyBase = dynamic_cast<Enemies::EnemyBase*>(other.get())) {
			if (enemyBase->IsInvulnerable() || !enemyBase->CanCollideWithAmmo) {
				return false;
			}
		}

		return ShotBase::OnHandleCollision(other);
	}

	bool ElectroShot::OnPerish(ActorBase* collider)
	{
		return ShotBase::OnPerish(collider);
	}

	void ElectroShot::OnHitWall(float timeMult)
	{
	}

	void ElectroShot::OnRicochet()
	{
	}
}