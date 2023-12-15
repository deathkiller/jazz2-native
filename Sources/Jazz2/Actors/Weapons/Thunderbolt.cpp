#include "Thunderbolt.h"
#include "../../ILevelHandler.h"
#include "../Player.h"
#include "../Enemies/EnemyBase.h"

#include "../../../nCine/Base/Random.h"

using namespace Jazz2::Tiles;

namespace Jazz2::Actors::Weapons
{
	Thunderbolt::Thunderbolt()
		:
		_hit(false),
		_lightProgress(0.0f),
		_firedUp(false)
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

		float distance = (isFacingLeft ? -140.0f : 140.0f);
		_farPoint = Vector2f(gunspotPos.X + cosf(angle) * distance, gunspotPos.Y + sinf(angle) * distance);

		_owner = owner;
		SetFacingLeft(isFacingLeft);

		MoveInstantly(gunspotPos, MoveType::Absolute | MoveType::Force);
		OnUpdateHitbox();

		if (Random().NextBool()) {
			_renderer.setFlippedY(true);
		}

		_renderer.setRotation(angle);

		if (auto* player = runtime_cast<Player*>(owner)) {
			_firedUp = player->_wasUpPressed;
		}
	}

	void Thunderbolt::OnUpdate(float timeMult)
	{
		if (auto* player = runtime_cast<Player*>(_owner)) {
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

				float distance = (IsFacingLeft() ? -140.0f : 140.0f);
				_farPoint = Vector2f(gunspotPos.X + cosf(angle) * distance, gunspotPos.Y + sinf(angle) * distance);
			}
		}

		if (_hit) {
			_strength = 0;
		} else if (_strength > 0) {
			TileCollisionParams params = { TileDestructType::Weapon | TileDestructType::IgnoreSolidTiles, false, WeaponType::Thunderbolt, _strength };
			_levelHandler->IsPositionEmpty(this, AABBInner, params);
			if (params.WeaponStrength <= 0) {
				_hit = true;
				_strength = 0;
			}
		}

		_lightProgress += timeMult * 0.123f;
		_renderer.setLayer((uint16_t)(_initialLayer - _lightProgress * 10.0f));
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
		constexpr int LightCount = 4;

		if (_lightProgress < fPi) {
			float lightIntensity = sinf(_lightProgress) * 0.2f;
			for (int i = -1; i <= LightCount; i++) {
				float dist = (float)i / LightCount;
				auto& light = lights.emplace_back();
				light.Pos = Vector2f(lerp(_pos.X, _farPoint.X, dist), lerp(_pos.Y, _farPoint.Y, dist));
				light.Intensity = lightIntensity;
				light.Brightness = lightIntensity * (0.1f + (1.0f - dist) * 0.4f);
				light.RadiusNear = 20.0f;
				light.RadiusFar = 100.0f;
			}
		}
	}

	void Thunderbolt::OnAnimationFinished()
	{
		ShotBase::OnAnimationFinished();

		DecreaseHealth(INT32_MAX);
	}

	bool Thunderbolt::OnHandleCollision(std::shared_ptr<ActorBase> other)
	{
		if (auto* enemyBase = runtime_cast<Enemies::EnemyBase*>(other)) {
			if (enemyBase->CanCollideWithAmmo) {
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