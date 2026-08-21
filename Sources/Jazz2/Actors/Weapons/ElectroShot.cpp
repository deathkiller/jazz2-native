#include "ElectroShot.h"
#include "../../ILevelHandler.h"
#include "../../Events/EventMap.h"
#include "../../Tiles/TileMap.h"
#include "../Enemies/EnemyBase.h"
#include "../Explosion.h"
#include "../Player.h"

#include "../../../nCine/Base/Random.h"

using namespace Jazz2::Tiles;

namespace Jazz2::Actors::Weapons
{
	ElectroShot::ElectroShot()
		: _fired(0), _currentStep(0.0f), _particleSpawnTime(0.0f)
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
		// The animation state carries the variant to remote clients (the sprite itself is never drawn); fall back
		// to the base state if the metadata doesn't define the powered-up one
		if ((_upgrades & 0x1) == 0 || !SetAnimation(PoweredUpAnimState)) {
			SetAnimation(AnimState::Idle);
		}
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
		std::int32_t n = (timeMult > 0.9f ? 2 : 1);
		TileCollisionParams params = { TileDestructType::Weapon | TileDestructType::IgnoreSolidTiles, false, WeaponType::Electro, _strength };
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

		_fired++;
		if (_fired == 2) {
			MoveInstantly(_gunspotPos, MoveType::Absolute | MoveType::Force);
		} else if (_fired > 2) {
			_particleSpawnTime -= timeMult;
			if (_particleSpawnTime <= 0.0f) {
				_particleSpawnTime += 1.0f;
				CreateParticles(_levelHandler, _metadata, _pos, _renderer.layer(), _currentStep, IsFacingLeft(), (_upgrades & 0x1) != 0);
			}

			_currentStep += timeMult;
		}
	}

	void ElectroShot::CreateParticles(ILevelHandler* levelHandler, Metadata* metadata, Vector2f pos, std::uint16_t layer, float currentStep, bool facingLeft, bool poweredUp)
	{
		auto tilemap = levelHandler->TileMap();
		if (tilemap == nullptr || metadata == nullptr) {
			return;
		}

		auto* res = metadata->FindAnimation((AnimState)1); // Particle
		if (res == nullptr || res->Base->TextureDiffuse == nullptr) {
			return;
		}

		auto& resBase = res->Base;
		Vector2i texSize = resBase->TextureDiffuse->GetSize();

		for (int i = 0; i < 6; i++) {
			float angle = (currentStep * 0.3f + i * 0.6f);
			if (facingLeft) {
				angle = -angle;
			}

			float size = (8.0f + currentStep * 0.2f);
			float dist = (2.0f + currentStep * 0.01f);
			float dx = dist * cosf(angle);
			float dy = dist * sinf(angle);

			Tiles::TileMap::DestructibleDebris debris = {};
			debris.Pos = Vector2f(pos.X + dx, pos.Y + dy);
			debris.Depth = layer;
			debris.Size = Vector2f(size, size);

			debris.Scale = 1.0f;
			debris.ScaleSpeed = -0.1f;
			debris.Alpha = 1.0f;
			debris.AlphaSpeed = -0.1f;
			debris.Angle = angle;

			debris.Time = 60.0f;

			std::int32_t curAnimFrame = (poweredUp ? 2 : 0) + Random().Fast(0, 2);
			Recti frameRect = resBase->GetFrameRect(curAnimFrame);
			debris.TexScaleX = (float(frameRect.W) / float(texSize.X));
			debris.TexBiasX = (float(frameRect.X) / float(texSize.X));
			debris.TexScaleY = (float(frameRect.H) / float(texSize.Y));
			debris.TexBiasY = (float(frameRect.Y) / float(texSize.Y));

			debris.DiffuseTexture = resBase->TextureDiffuse.get();
			// Recolor through the palette when the sprite is indexed (-1 = baked/RGBA, behavior unchanged)
			debris.PaletteOffset = (((resBase->Flags & GenericGraphicResourceFlags::Indexed) == GenericGraphicResourceFlags::Indexed) ? (std::int32_t)res->PaletteOffset : -1);

			tilemap->CreateDebris(debris);
		}

		// A few tiny additive sparks crackle off the edge of the swirl in random directions, decelerating
		// as they fade out
		for (int i = 0; i < 2; i++) {
			float sparkAngle = Random().FastFloat(0.0f, fRadAngle360);
			float sparkSpeed = Random().FastFloat(0.6f, 2.4f);
			Vector2f sparkDir = Vector2f(cosf(sparkAngle), sinf(sparkAngle));
			float sparkDist = (3.0f + currentStep * 0.01f);
			float sparkSize = Random().FastFloat(2.0f, 3.5f);

			Tiles::TileMap::DestructibleDebris spark = {};
			spark.Pos = Vector2f(pos.X + sparkDir.X * sparkDist, pos.Y + sparkDir.Y * sparkDist);
			spark.Depth = layer;
			spark.Size = Vector2f(sparkSize, sparkSize);
			spark.Speed = sparkDir * sparkSpeed;
			spark.Acceleration = sparkDir * -sparkSpeed * 0.03f;

			spark.Scale = 1.0f;
			spark.ScaleSpeed = -0.03f;
			spark.Alpha = 1.0f;
			spark.AlphaSpeed = Random().FastFloat(-0.07f, -0.04f);
			spark.Angle = sparkAngle;
			spark.AngleSpeed = Random().FastFloat(-0.3f, 0.3f);

			spark.Time = 24.0f;

			std::int32_t sparkAnimFrame = (poweredUp ? 2 : 0) + Random().Fast(0, 2);
			Recti sparkFrameRect = resBase->GetFrameRect(sparkAnimFrame);
			spark.TexScaleX = (float(sparkFrameRect.W) / float(texSize.X));
			spark.TexBiasX = (float(sparkFrameRect.X) / float(texSize.X));
			spark.TexScaleY = (float(sparkFrameRect.H) / float(texSize.Y));
			spark.TexBiasY = (float(sparkFrameRect.Y) / float(texSize.Y));

			spark.DiffuseTexture = resBase->TextureDiffuse.get();
			spark.PaletteOffset = (((resBase->Flags & GenericGraphicResourceFlags::Indexed) == GenericGraphicResourceFlags::Indexed) ? (std::int32_t)res->PaletteOffset : -1);
			spark.Flags = Tiles::TileMap::DebrisFlags::AdditiveBlending;

			tilemap->CreateDebris(spark);
		}
	}

	void ElectroShot::EmitParticleLight(SmallVectorImpl<LightEmitter>& lights, Vector2f pos, float currentStep)
	{
		auto& light = lights.emplace_back();
		light.Pos = pos;
		light.Intensity = 0.4f + 0.016f * currentStep;
		light.Brightness = 0.2f + 0.02f * currentStep;
		light.RadiusNear = 0.0f;
		light.RadiusFar = 12.0f + 0.4f * currentStep;
	}

	void ElectroShot::OnUpdateHitbox()
	{
		UpdateHitbox(4, 4);
	}

	void ElectroShot::OnEmitLights(SmallVectorImpl<LightEmitter>& lights)
	{
		if (_fired >= 2) {
			EmitParticleLight(lights, _pos, _currentStep);
		}
	}

	bool ElectroShot::OnHandleCollision(ActorBase* other)
	{
		if (auto* enemyBase = runtime_cast<Enemies::EnemyBase>(other)) {
			if (enemyBase->IsInvulnerable() || !enemyBase->CanCollideWithShots) {
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