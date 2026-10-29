#include "CustomShot.h"
#include "ElectroShot.h"
#include "Thunderbolt.h"
#include "../../ILevelHandler.h"
#include "../../Events/EventMap.h"
#include "../../Tiles/TileMap.h"
#include "../Player.h"
#include "../Explosion.h"
#include "../Enemies/EnemyBase.h"

#include "../../../nCine/Base/Random.h"
#include "../../../nCine/CommonConstants.h"

#include <cfloat>

using namespace Jazz2::Tiles;

namespace Jazz2::Actors::Weapons
{
	CustomShot::CustomShot()
		: _slot(WeaponType::Blaster), _basedOn(WeaponType::Blaster), _fired(0), _pierceLeft(0), _bouncesLeft(0),
			_baseStrength(0), _detonated(false), _bubbleInWater(false), _flipX(false), _lookStep(0.0f), _particleSpawnTime(0.0f),
			_farPoint(Vector2f::Zero)
	{
	}

	Task<bool> CustomShot::OnActivatedAsync(const ActorActivationDetails& details)
	{
		async_await ShotBase::OnActivatedAsync(details);

		_upgrades = details.Params[0];
		_slot = (details.Params[1] < std::uint8_t(WeaponType::Count) ? WeaponType(details.Params[1]) : WeaponType::Blaster);

		const WeaponDefinition& definition = _levelHandler->GetWeaponDefinition(_slot);
		_basedOn = (definition.BasedOn != WeaponType::Unknown ? definition.BasedOn : _slot);
		_options = ((_upgrades & 0x01) != 0 ? definition.ShotPowered : definition.Shot);
		if ((details.Params[2] & IsBubble) != 0) {
			_options = WeaponShotOptions();
			_options.Sprite.Metadata = "Common/AmbientBubbles"_s;
			_options.Speed = 1.5f;
			_options.InheritPlayerSpeed = false;
			_options.Gravity = -0.08f;
			_options.Lifetime = 50.0f;
			_options.Strength = 0;
			_options.RotatesWithSpeed = false;
		} else {
			_bubbleInWater = definition.Options.ReplacedByBubbles;
		}

		_timeLeft = _options.Lifetime;
		_strength = _baseStrength = _options.Strength;
		_pierceLeft = _options.Pierce;
		_bouncesLeft = _options.Bounces;

		SetState(ActorState::SkipPerPixelCollisions, true);
		SetState(ActorState::ApplyGravitation, false);

		// Either of the two sprites, and which way they're flipped, at random
		const WeaponSprite& sprite = (!_options.AltSprite.IsEmpty() && Random().NextBool() ? _options.AltSprite : _options.Sprite);
		_flipX = (_options.FlipX == ShotFlip::Flipped || (_options.FlipX == ShotFlip::Random && Random().NextBool()));
		bool flipY = (_options.FlipY == ShotFlip::Flipped || (_options.FlipY == ShotFlip::Random && Random().NextBool()));

		String path = _levelHandler->GetWeapons().GetSpriteMetadataPath(sprite);
		AnimState state = AnimState(sprite.AnimState);
		if (!path.empty()) {
			async_await RequestMetadataAsync(path);
		}
		if (path.empty() || !SetAnimation(state)) {
			WeaponDefinition stock = WeaponDefinition::MakeCustom(_slot, _basedOn);
			const WeaponSprite& stockSprite = ((_upgrades & 0x01) != 0 ? stock.ShotPowered.Sprite : stock.Shot.Sprite);
			async_await RequestMetadataAsync(stockSprite.Metadata);
			if (!SetAnimation(AnimState(stockSprite.AnimState))) {
				SetAnimation(AnimState::Idle);
			}
		}

		if (_options.Look == ShotLook::Glow) {
			_renderer.setBlendingPreset(DrawableNode::BlendingPreset::Additive);
		}
		if (flipY) {
			_renderer.setFlippedY(true);
			_renderer.UpdateAnchor();
		}
		if (_options.Look == ShotLook::Beam) {
			_health = INT32_MAX;
		}

		if ((details.Params[2] & PlaysSound) != 0) {
			PlaySfx(definition.Fire.Sound);
		}

		async_return true;
	}

	void CustomShot::OnFire(const std::shared_ptr<ActorBase>& owner, Vector2f gunspotPos, Vector2f speed, float angle, bool isFacingLeft)
	{
		_owner = owner;
		TurnTo(isFacingLeft);

		_gunspotPos = gunspotPos;

		float angleRel = angle * (isFacingLeft ? -1 : 1);
		float sinAngle, cosAngle;
		sincosApprox(angleRel, sinAngle, cosAngle);
		float inherited = (_options.InheritPlayerSpeed ? speed.X : 0.0f);
		if (isFacingLeft) {
			_speed.X = std::min(0.0f, inherited) - cosAngle * _options.Speed;
		} else {
			_speed.X = std::max(0.0f, inherited) + cosAngle * _options.Speed;
		}
		_speed.Y = sinAngle * _options.Speed;

		if (_options.Look == ShotLook::Beam) {
			// It stays at the muzzle, so it's seen from the first frame
			_speed = Vector2f::Zero;
			_farPoint = GetBeamEnd(gunspotPos, angle);
			_renderer.setRotation(angle);
			MoveInstantly(gunspotPos, MoveType::Absolute | MoveType::Force);
			OnUpdateHitbox();
			return;
		}

		if (_options.RotatesWithSpeed) {
			_renderer.setRotation(angle);
		}
		_renderer.setDrawEnabled(false);
	}

	void CustomShot::TurnTo(bool left)
	{
		SetFacingLeft(left);
		// A shot flipped horizontally shows the other side to where it flies
		if (_flipX && _renderer.isFlippedX() == left) {
			_renderer.setFlippedX(!left);
			_renderer.UpdateAnchor();
		}
	}

	Vector2f CustomShot::GetBeamEnd(Vector2f from, float angle) const
	{
		float distance = std::abs(_options.Speed) * _options.Lifetime;
		if (IsFacingLeft()) {
			distance = -distance;
		}
		float sinAngle, cosAngle;
		sincosApprox(angle, sinAngle, cosAngle);
		return Vector2f(from.X + cosAngle * distance, from.Y + sinAngle * distance);
	}

	void CustomShot::UpdateBeam(float timeMult)
	{
		// It follows the muzzle while the player faces the same way
		if (auto* player = runtime_cast<Player>(_owner.get())) {
			if (player->IsFacingLeft() != IsFacingLeft()) {
				Detonate();
				return;
			}
			Vector3i initialPos; Vector2f gunspotPos; float angle;
			player->GetFirePointAndAngle(initialPos, gunspotPos, angle);
			MoveInstantly(gunspotPos, MoveType::Absolute | MoveType::Force);
			_farPoint = GetBeamEnd(gunspotPos, _renderer.rotation());
		}

		WeaponType blocksAs = (_options.DestroysBlocksAs != WeaponType::Unknown ? _options.DestroysBlocksAs : _slot);
		TileDestructType destructType = TileDestructType::Weapon;
		if (_options.PassesWalls) {
			destructType |= TileDestructType::IgnoreSolidTiles;
		}
		TileCollisionParams params = { destructType, false, blocksAs, std::max(_baseStrength, 1) };
		_levelHandler->IsPositionEmpty(this, AABBInner, params);
		if (params.TilesDestroyed > 0) {
			if (auto* player = runtime_cast<Player>(_owner.get())) {
				player->AddScore(params.TilesDestroyed * 50);
			}
		}

		if (_lookStep * Thunderbolt::LightProgressSpeed < fPi) {
			Thunderbolt::CreateSparks(_levelHandler, _metadata, _pos, _farPoint, _renderer.layer(), timeMult);
		}
	}

	WeaponType CustomShot::GetWeaponType()
	{
		return _basedOn;
	}

	WeaponType CustomShot::GetSourceWeapon()
	{
		return _slot;
	}

	float CustomShot::GetFrozenDuration() const
	{
		return _options.FreezeDuration;
	}

	bool CustomShot::ThawsFrozen() const
	{
		return _options.Thaws;
	}

	void CustomShot::OnUpdate(float timeMult)
	{
		if (_detonated) {
			return;
		}

		if (_bubbleInWater && _fired >= 2 && _pos.Y >= _levelHandler->GetWaterLevel()) {
			TurnIntoBubble();
			return;
		}

		// An enemy it already hit isn't hit again while the shot passes through it
		_strength = (IsOverlappingHitActor() ? 0 : _baseStrength);

		if (_options.Look == ShotLook::Beam) {
			UpdateBeam(timeMult);
			_lookStep += timeMult;
			_timeLeft -= timeMult;
			if (_timeLeft <= 0.0f && !_detonated) {
				Detonate();
			}
			return;
		}

		if (_options.Trigger == ShotTrigger::Proximity && _fired >= 2) {
			bool triggered = false;
			_levelHandler->FindCollisionActorsByRadius(_pos.X, _pos.Y, _options.TriggerRadius, [this, &triggered](ActorBase* actor) {
				if (runtime_cast<Enemies::EnemyBase>(actor) != nullptr && actor->CanCauseDamage(this)) {
					triggered = true;
					return false;
				}
				return true;
			});
			if (triggered) {
				Detonate();
				return;
			}
		}

		if (_options.HomingTurnRate > 0.0f) {
			FollowNearestEnemy(timeMult);
		}

		std::int32_t n = GetMovementSubstepCount(timeMult);
		WeaponType blocksAs = (_options.DestroysBlocksAs != WeaponType::Unknown ? _options.DestroysBlocksAs : _slot);
		TileDestructType destructType = TileDestructType::Weapon;
		if (_options.PassesWalls) {
			destructType |= TileDestructType::IgnoreSolidTiles;
		}
		TileCollisionParams params = { destructType, _speed.Y >= 0.0f, blocksAs, std::max(_baseStrength, 1) };
		for (std::int32_t i = 0; i < n && params.WeaponStrength > 0 && !_detonated; i++) {
			Move(timeMult / n, params);
		}
		if (params.TilesDestroyed > 0) {
			if (auto* player = runtime_cast<Player>(_owner.get())) {
				player->AddScore(params.TilesDestroyed * 50);
			}
		}
		if (_detonated) {
			return;
		}
		if (params.WeaponStrength <= 0) {
			Detonate();
			return;
		}

		if (_options.RotatesWithSpeed && _fired >= 2 && (_options.Gravity != 0.0f || _options.HomingTurnRate > 0.0f || _options.Bounces > 0)) {
			_renderer.setRotation(IsFacingLeft() ? atan2Approx(-_speed.Y, -_speed.X) : atan2Approx(_speed.Y, _speed.X));
		}

		_timeLeft -= timeMult;
		if (_timeLeft <= 0.0f) {
			Detonate();
			return;
		}

		_fired++;
		if (_fired == 2) {
			MoveInstantly(_gunspotPos, MoveType::Absolute | MoveType::Force);
			// Only the sparks of the electro are seen, not the shot
			_renderer.setDrawEnabled(_options.Look != ShotLook::Electro);
		} else if (_fired > 2 && _options.Look == ShotLook::Electro) {
			_particleSpawnTime -= timeMult;
			if (_particleSpawnTime <= 0.0f) {
				_particleSpawnTime += 1.0f;
				ElectroShot::CreateParticles(_levelHandler, _currentAnimation, _pos, _renderer.layer(), _lookStep, IsFacingLeft(), (_upgrades & 0x01) != 0);
			}
			_lookStep += timeMult;
		}
	}

	void CustomShot::Move(float timeMult, TileCollisionParams& params)
	{
		_speed.X = std::clamp(_speed.X + _externalForce.X * timeMult, -16.0f, 16.0f);
		_speed.Y = std::clamp(_speed.Y + (_options.Gravity + _externalForce.Y) * timeMult, -16.0f, 16.0f);

		Vector2f step = Vector2f(_speed.X * timeMult, _speed.Y * timeMult);
		if (MoveInstantly(step, MoveType::Relative, params) || params.WeaponStrength <= 0) {
			return;
		}

		if (_bouncesLeft <= 0) {
			OnHitWall(timeMult);
			return;
		}

		// Off the side it hit, a corner reverses both
		bool freeX = (step.X != 0.0f && MoveInstantly(Vector2f(step.X, 0.0f), MoveType::Relative, params));
		bool freeY = (step.Y != 0.0f && MoveInstantly(Vector2f(0.0f, step.Y), MoveType::Relative, params));
		if (!freeX && step.X != 0.0f) {
			_speed.X = -_speed.X * _options.Elasticity;
			TurnTo(_speed.X < 0.0f);
		}
		if (!freeY && step.Y != 0.0f) {
			_speed.Y = -_speed.Y * _options.Elasticity;
		}
		_bouncesLeft--;
		PlaySfx("Bounce"_s, 0.5f);
	}

	void CustomShot::FollowNearestEnemy(float timeMult)
	{
		Vector2f targetPos;
		float targetDistance = FLT_MAX;
		_levelHandler->FindCollisionActorsByRadius(_pos.X, _pos.Y, _options.HomingRange, [this, &targetPos, &targetDistance](ActorBase* actor) {
			if (runtime_cast<Enemies::EnemyBase>(actor) != nullptr && actor->CanCauseDamage(this)) {
				float distance = (_pos - actor->GetPos()).Length();
				if (distance < targetDistance) {
					targetPos = actor->GetPos();
					targetDistance = distance;
				}
			}
			return true;
		});
		if (targetDistance >= _options.HomingRange) {
			return;
		}

		float speed = std::max(Vector2f(_speed.X, _speed.Y).Length(), _options.Speed);
		float current = atan2Approx(_speed.Y, _speed.X);
		float wanted = atan2Approx(targetPos.Y - _pos.Y, targetPos.X - _pos.X);
		float delta = wanted - current;
		while (delta > fPi) {
			delta -= fTwoPi;
		}
		while (delta < -fPi) {
			delta += fTwoPi;
		}
		float maxTurn = _options.HomingTurnRate * timeMult;
		float angle = current + std::clamp(delta, -maxTurn, maxTurn);
		float sinAngle, cosAngle;
		sincosApprox(angle, sinAngle, cosAngle);
		_speed.X = cosAngle * speed;
		_speed.Y = sinAngle * speed;
		TurnTo(_speed.X < 0.0f);
	}

	bool CustomShot::IsOverlappingHitActor()
	{
		if (_hitActors.empty()) {
			return false;
		}
		bool overlapping = false;
		_levelHandler->FindCollisionActorsByAABB(this, AABBInner, [this, &overlapping](ActorBase* actor) {
			for (const ActorBase* hit : _hitActors) {
				if (hit == actor) {
					overlapping = true;
					return false;
				}
			}
			return true;
		});
		return overlapping;
	}

	bool CustomShot::OnHandleCollision(ActorBase* other)
	{
		auto* enemy = runtime_cast<Enemies::EnemyBase>(other);
		if (enemy == nullptr || !enemy->CanCollideWithShots || _detonated || _strength <= 0) {
			return false;
		}

		for (const ActorBase* hit : _hitActors) {
			if (hit == other) {
				return false;
			}
		}
		if (_hitActors.size() < MaxRememberedHits) {
			_hitActors.push_back(other);
		}

		if (_options.Trigger != ShotTrigger::Contact || _pierceLeft <= 0) {
			Detonate();
		} else {
			_pierceLeft--;
		}
		return false;
	}

	void CustomShot::OnUpdateHitbox()
	{
		if (_options.Look == ShotLook::Beam && _farPoint != Vector2f::Zero) {
			constexpr float Size = 10.0f;
			AABBInner = AABBf(std::min(_pos.X, _farPoint.X) - Size, std::min(_pos.Y, _farPoint.Y) - Size,
				std::max(_pos.X, _farPoint.X) + Size, std::max(_pos.Y, _farPoint.Y) + Size);
			return;
		}
		AABBInner = AABBf(_pos.X - 4.0f, _pos.Y - 4.0f, _pos.X + 4.0f, _pos.Y + 4.0f);
	}

	void CustomShot::OnEmitLights(SmallVectorImpl<LightEmitter>& lights)
	{
		if (_options.LightRadius <= 0.0f) {
			return;
		}
		if (_options.Look == ShotLook::Beam) {
			Thunderbolt::EmitBeamLights(lights, _pos, _farPoint, _lookStep * Thunderbolt::LightProgressSpeed, false);
			return;
		}
		if (_options.Look == ShotLook::Electro) {
			if (_fired >= 2) {
				ElectroShot::EmitParticleLight(lights, _pos, _lookStep);
			}
			return;
		}
		if (_fired >= 2) {
			auto& light = lights.emplace_back();
			light.Pos = _pos;
			light.Intensity = _options.LightIntensity;
			light.Brightness = 0.6f;
			light.RadiusNear = _options.LightRadius * 0.25f;
			light.RadiusFar = _options.LightRadius;
		}
	}

	void CustomShot::TurnIntoBubble()
	{
		std::uint8_t shotParams[3] = { 0, std::uint8_t(_slot), IsBubble };
		std::shared_ptr<CustomShot> bubble = std::make_shared<CustomShot>();
		bubble->OnActivated(ActorActivationDetails(_levelHandler, Vector3i((std::int32_t)_pos.X, (std::int32_t)_pos.Y, _renderer.layer()), shotParams));
		bubble->OnFire(_owner, _pos, Vector2f::Zero, 0.0f, IsFacingLeft());
		_levelHandler->AddActor(bubble);

		_options.Explosion = -1;
		_options.AreaRadius = 0.0f;
		Detonate();
	}

	void CustomShot::Detonate()
	{
		if (_detonated) {
			return;
		}
		_detonated = true;
		DecreaseHealth(INT32_MAX);
	}

	bool CustomShot::OnPerish(ActorBase* collider)
	{
		if (_options.AreaRadius > 0.0f) {
			_levelHandler->FindCollisionActorsByRadius(_pos.X, _pos.Y, _options.AreaRadius, [this](ActorBase* actor) {
				if (actor == this) {
					return true;
				}
				if (auto* player = runtime_cast<Player>(actor)) {
					if (_options.Knockback != 0.0f) {
						bool pushLeft = (_pos.X > player->GetPos().X);
						player->AddExternalForce(pushLeft ? -_options.Knockback : _options.Knockback, 0.0f);
						_levelHandler->HandlePlayerPushed(player);
					}
				} else if (runtime_cast<Enemies::EnemyBase>(actor) != nullptr) {
					if (_options.AreaStrength > 0 && actor->CanCauseDamage(this)) {
						actor->DecreaseHealth(_options.AreaStrength, this);
					}
				} else if (_options.AreaStrength > 0) {
					// Crates and the like break as by a shot
					actor->OnHandleCollision(this);
				}
				return true;
			});

			auto* tiles = _levelHandler->TileMap();
			if (tiles != nullptr && _options.AreaStrength > 0) {
				WeaponType blocksAs = (_options.DestroysBlocksAs != WeaponType::Unknown ? _options.DestroysBlocksAs : _slot);
				float r = _options.AreaRadius;
				AABBf aabb = AABBf(_pos.X - r, _pos.Y - r, _pos.X + r, _pos.Y + r);
				TileCollisionParams params = { TileDestructType::Weapon | TileDestructType::IgnoreSolidTiles, false, blocksAs, _options.AreaStrength };
				tiles->IsTileEmpty(aabb, params);
				if (params.TilesDestroyed > 0) {
					if (auto* player = runtime_cast<Player>(_owner.get())) {
						player->AddScore(params.TilesDestroyed * 50);
					}
				}
			}
		}

		if (_options.Explosion >= 0 && _options.Explosion <= std::int32_t(Explosion::Type::IceShrapnel)) {
			Explosion::Create(_levelHandler, Vector3i((std::int32_t)(_pos.X + _speed.X), (std::int32_t)(_pos.Y + _speed.Y), _renderer.layer() + 2),
				Explosion::Type(_options.Explosion));
		}

		return ShotBase::OnPerish(collider);
	}

	void CustomShot::OnHitWall(float timeMult)
	{
		auto events = _levelHandler->EventMap();
		if (events != nullptr) {
			std::uint8_t* eventParams;
			if (events->GetEventByPosition(_pos.X + _speed.X, _pos.Y + _speed.Y, &eventParams) == EventType::ModifierRicochet) {
				TriggerRicochet(nullptr);
				return;
			}
		}

		if (_options.Trigger == ShotTrigger::Contact) {
			Detonate();
			PlaySfx("WallPoof"_s);
		} else {
			_speed.X = 0.0f;
			_speed.Y = 0.0f;
		}
	}

	void CustomShot::OnRicochet()
	{
		ShotBase::OnRicochet();

		if (_options.RotatesWithSpeed) {
			_renderer.setRotation(atan2Approx(_speed.Y, _speed.X));
		}
		PlaySfx("Ricochet"_s);
	}
}
