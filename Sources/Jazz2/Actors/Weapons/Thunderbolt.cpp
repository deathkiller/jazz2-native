#include "Thunderbolt.h"
#include "../../ILevelHandler.h"
#include "../../Tiles/TileMap.h"
#include "../Player.h"
#include "../Enemies/EnemyBase.h"

#include "../../../nCine/Base/Random.h"

using namespace Jazz2::Tiles;

namespace Jazz2::Actors::Weapons
{
	Thunderbolt::Thunderbolt()
		: _hit(false), _lightProgress(0.0f), _firedUp(false), _initialShot(false)
	{
	}

	Task<bool> Thunderbolt::OnActivatedAsync(const ActorActivationDetails& details)
	{
		async_await ShotBase::OnActivatedAsync(details);

		_upgrades = details.Params[0];
		_initialLayer = _renderer.layer();
		_strength = 2;
		_health = INT32_MAX;
		SetState(ActorState::ApplyGravitation, false);

		async_await RequestMetadataAsync("Weapon/Thunderbolt"_s);

		SetAnimation((AnimState)(Random().NextBool() ? 1 : 0));

		async_return true;
	}

	void Thunderbolt::OnFire(const std::shared_ptr<ActorBase>& owner, Vector2f gunspotPos, Vector2f speed, float angle, bool isFacingLeft)
	{
		angle += Random().FastFloat(-0.16f, 0.16f);

		float distance = (isFacingLeft ? -BeamDistance : BeamDistance);
		_farPoint = Vector2f(gunspotPos.X + cosf(angle) * distance, gunspotPos.Y + sinf(angle) * distance);

		_owner = owner;
		SetFacingLeft(isFacingLeft);

		MoveInstantly(gunspotPos, MoveType::Absolute | MoveType::Force);
		OnUpdateHitbox();

		if (Random().NextBool()) {
			_renderer.setFlippedY(true);
		}

		_renderer.setRotation(angle);

		if (auto* player = runtime_cast<Player>(owner.get())) {
			_firedUp = player->_wasUpPressed;

			// _fireFramesLeft is refreshed on every shot and expires only after a real pause in firing, so it's
			// still zero here exactly for the first bolt of a burst - the only one that gets the muzzle flash
			if (player->_fireFramesLeft <= 0.0f) {
				_initialShot = true;
				// Aliased state carrying the flash to remote clients; keeps the current variant if the metadata
				// doesn't define the aliases
				SetAnimation((AnimState)((std::uint32_t)_currentAnimation->State + InitialShotAnimOffset));
			}
		}
	}

	void Thunderbolt::OnUpdate(float timeMult)
	{
		if (auto* player = runtime_cast<Player>(_owner.get())) {
			if (_firedUp != player->_wasUpPressed || IsFacingLeft() != player->IsFacingLeft()) {
				_hit = true;
				_strength = 0;
				DecreaseHealth(INT32_MAX);
				return;
			}

			if (!_firedUp) {
				Vector3i initialPos; Vector2f gunspotPos; float angle;
				player->GetFirePointAndAngle(initialPos, gunspotPos, angle);
				float scale = 1.0f - std::abs(gunspotPos.X - _pos.X) / _currentAnimation->Base->FrameDimensions.X;
				MoveInstantly(gunspotPos, MoveType::Absolute | MoveType::Force);
				_renderer.setScale(Vector2f(scale, 1.0f));

				float anglePrev = _renderer.rotation();
				if (IsFacingLeft()) {
					angle = atan2f(_pos.Y - _farPoint.Y, _pos.X - _farPoint.X);
				} else {
					angle = atan2f(_farPoint.Y - _pos.Y, _farPoint.X - _pos.X);
				}
				angle = lerp(anglePrev, angle, 0.4f * timeMult);
				if (std::abs(anglePrev - angle) > 0.06f) {
					_renderer.setRotation(angle);
				} else {
					angle = _renderer.rotation();
				}

				float distance = (IsFacingLeft() ? -BeamDistance : BeamDistance);
				_farPoint = Vector2f(gunspotPos.X + cosf(angle) * distance, gunspotPos.Y + sinf(angle) * distance);
			}
		}

		if (_hit) {
			_strength = 0;
		} else if (_strength > 0) {
			TileCollisionParams params = { TileDestructType::Weapon | TileDestructType::IgnoreSolidTiles, false, WeaponType::Thunderbolt, _strength };
			_levelHandler->IsPositionEmpty(this, AABBInner, params);
			if (params.TilesDestroyed > 0) {
				if (auto* player = runtime_cast<Player>(_owner.get())) {
					player->AddScore(params.TilesDestroyed * 50);
				}
			}
			if (params.WeaponStrength <= 0) {
				_hit = true;
				_strength = 0;
			}
		}

		_lightProgress += timeMult * LightProgressSpeed;
		_renderer.setLayer((uint16_t)(_initialLayer - _lightProgress * 10.0f));

		// Sparks discharge only while the beam is hot (the same window as its light)
		if (_lightProgress < fPi) {
			CreateSparks(_levelHandler, _metadata, _pos, _farPoint, _renderer.layer(), timeMult);
		}
	}

	void Thunderbolt::CreateSparks(ILevelHandler* levelHandler, Metadata* metadata, Vector2f pos, Vector2f farPoint, std::uint16_t layer, float timeMult)
	{
		// Sparks are rare and irregular, which looks more natural than a steady stream
		if (Random().FastFloat(0.0f, 1.0f) > 0.16f * timeMult) {
			return;
		}

		auto tilemap = levelHandler->TileMap();
		if (tilemap == nullptr || metadata == nullptr) {
			return;
		}

		auto* res = metadata->FindAnimation((AnimState)2); // Particle
		if (res == nullptr || res->Base->TextureDiffuse == nullptr) {
			return;
		}

		Vector2f beam = farPoint - pos;
		float beamLength = beam.Length();
		if (beamLength < 1.0f) {
			return;
		}

		auto& resBase = res->Base;
		Vector2i texSize = resBase->TextureDiffuse->GetSize();

		// The discharge leaves a random point of the beam mostly sideways, with a slight drift along it;
		// the visible bolt ends before the logical far point, so sparks spawn only along ~80% of it
		Vector2f beamDir = beam / beamLength;
		Vector2f perpDir = (Random().NextBool() ? Vector2f(-beamDir.Y, beamDir.X) : Vector2f(beamDir.Y, -beamDir.X));
		Vector2f sparkSpeed = perpDir * Random().FastFloat(2.4f, 9.6f) + beamDir * Random().FastFloat(-1.8f, 3.0f);
		// Sparks shouldn't soar - damp the upward part so they arc down as soon as possible
		if (sparkSpeed.Y < 0.0f) {
			sparkSpeed.Y *= 0.3f;
		}
		float sparkSize = Random().FastFloat(1.5f, 3.0f);
		Vector2f sparkPos = pos + beamDir * (beamLength * Random().FastFloat(0.05f, 0.8f)) + perpDir * Random().FastFloat(0.0f, 3.0f);

		// The beam passes through solid tiles, but a spark born inside one would just sit there
		if (!tilemap->IsTilePointEmpty((std::int32_t)sparkPos.X, (std::int32_t)sparkPos.Y, false)) {
			return;
		}

		Tiles::TileMap::DestructibleDebris spark = {};
		spark.Pos = sparkPos;
		spark.Depth = layer;
		spark.Size = Vector2f(sparkSize, sparkSize);
		spark.Speed = sparkSpeed;
		spark.Acceleration = Vector2f(0.0f, 0.2f);
		spark.Elasticity = 0.4f;

		spark.Scale = 1.0f;
		spark.ScaleSpeed = -0.004f;
		spark.Alpha = 1.0f;
		spark.AlphaSpeed = Random().FastFloat(-0.018f, -0.010f);
		spark.Angle = atan2f(sparkSpeed.Y, sparkSpeed.X);
		spark.AngleSpeed = Random().FastFloat(-0.2f, 0.2f);

		spark.Time = 70.0f;

		// Frames 2/3 of the shared electro particle, matching the blue of the upgraded electro shot
		std::int32_t sparkAnimFrame = 2 + Random().Fast(0, 2);
		Recti sparkFrameRect = resBase->GetFrameRect(sparkAnimFrame);
		spark.TexScaleX = (float(sparkFrameRect.W) / float(texSize.X));
		spark.TexBiasX = (float(sparkFrameRect.X) / float(texSize.X));
		spark.TexScaleY = (float(sparkFrameRect.H) / float(texSize.Y));
		spark.TexBiasY = (float(sparkFrameRect.Y) / float(texSize.Y));

		spark.DiffuseTexture = resBase->TextureDiffuse.get();
		// Recolor through the palette when the sprite is indexed (-1 = baked/RGBA, behavior unchanged)
		spark.PaletteOffset = (((resBase->Flags & GenericGraphicResourceFlags::Indexed) == GenericGraphicResourceFlags::Indexed) ? (std::int32_t)res->PaletteOffset : -1);
		spark.Flags = Tiles::TileMap::DebrisFlags::Bounce | Tiles::TileMap::DebrisFlags::AdditiveBlending;

		tilemap->CreateDebris(spark);
	}

	void Thunderbolt::OnUpdateHitbox()
	{
		constexpr float Size = 10.0f;

		if (_farPoint.X != 0.0f && _farPoint.Y != 0.0f) {
			AABBInner = AABBf(_pos, _farPoint);
			AABBInner.L -= Size;
			AABBInner.T -= Size;
			AABBInner.R += Size;
			AABBInner.B += Size;
		}
	}

	void Thunderbolt::OnEmitLights(SmallVectorImpl<LightEmitter>& lights)
	{
		EmitBeamLights(lights, _pos, _farPoint, _lightProgress, _initialShot);
	}

	void Thunderbolt::EmitBeamLights(SmallVectorImpl<LightEmitter>& lights, Vector2f pos, Vector2f farPoint, float lightProgress, bool muzzleFlash)
	{
		constexpr std::int32_t LightCount = 4;
		// The flash lasts about 4 frames of the beam lifetime progress
		constexpr float FlashDuration = 0.5f;

		if (lightProgress < fPi) {
			float lightIntensity = sinApprox(lightProgress) * 0.2f;
			for (std::int32_t i = -1; i <= LightCount; i++) {
				float dist = (float)i / LightCount;
				auto& light = lights.emplace_back();
				light.Pos = Vector2f(lerp(pos.X, farPoint.X, dist), lerp(pos.Y, farPoint.Y, dist));
				light.Intensity = lightIntensity;
				light.Brightness = lightIntensity * (0.1f + (1.0f - dist) * 0.4f);
				light.RadiusNear = 20.0f;
				light.RadiusFar = 100.0f;
			}
		}

		// Brief intense flash at the muzzle, only on the first bolt of a burst (the bolt respawns every few
		// frames while the trigger is held). Two stacked emitters: a core saturated to full white around the
		// muzzle and a wide halo lighting up the whole surroundings.
		if (muzzleFlash && lightProgress < FlashDuration) {
			float flash = 1.0f - (lightProgress / FlashDuration);

			auto& core = lights.emplace_back();
			core.Pos = pos;
			core.Intensity = 0.9f * flash;
			core.Brightness = 0.5f * flash;
			core.RadiusNear = 250.0f;
			core.RadiusFar = 550.0f;

			auto& halo = lights.emplace_back();
			halo.Pos = pos;
			halo.Intensity = 0.5f * flash;
			halo.Brightness = 0.2f * flash;
			halo.RadiusNear = 200.0f;
			halo.RadiusFar = 1200.0f;
		}
	}

	void Thunderbolt::OnAnimationFinished()
	{
		ShotBase::OnAnimationFinished();

		DecreaseHealth(INT32_MAX);
	}

	bool Thunderbolt::OnHandleCollision(ActorBase* other)
	{
		if (auto* enemyBase = runtime_cast<Enemies::EnemyBase>(other)) {
			if (enemyBase->CanCollideWithShots) {
				_hit = true;
			}
		}

		return false;
	}

	void Thunderbolt::OnHitWall(float timeMult)
	{
	}

	void Thunderbolt::OnRicochet()
	{
	}
}