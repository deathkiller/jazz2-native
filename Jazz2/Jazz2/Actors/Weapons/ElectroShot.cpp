#include "ElectroShot.h"
#include "../../ILevelHandler.h"
#include "../../LevelInitialization.h"
#include "../../Events/EventMap.h"
#include "../Enemies/EnemyBase.h"
#include "../Explosion.h"

#include "../../../nCine/Base/Random.h"

namespace Jazz2::Actors::Weapons
{
	ElectroShot::ElectroShot()
		:
		_fired(false)
	{
	}

	Task<bool> ElectroShot::OnActivatedAsync(const ActorActivationDetails& details)
	{
		co_await ShotBase::OnActivatedAsync(details);

		_upgrades = details.Params[0];
		_strength = 4;
		_timeLeft = 55;

		SetState(ActorState::SkipPerPixelCollisions, true);
		SetState(ActorState::ApplyGravitation | ActorState::CollideWithTileset, false);

		co_await RequestMetadataAsync("Weapon/Electro"_s);

		AnimState state = AnimState::Idle;
		// TODO: Electro shot rendering

		SetAnimation(state);

		PlaySfx("Fire"_s);

		_renderer.setBlendingPreset(DrawableNode::BlendingPreset::ADDITIVE);

		co_return true;
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

		_renderer.setDrawEnabled(false);
	}

	void ElectroShot::OnUpdate(float timeMult)
	{
		int n = (timeMult > 0.9f ? 2 : 1);
		TileCollisionParams params = { TileDestructType::Weapon, false, WeaponType::Electro, _strength };
		for (int i = 0; i < n && params.WeaponStrength > 0; i++) {
			TryMovement(timeMult / n, params);
		}
		if (params.WeaponStrength <= 0) {
			DecreaseHealth(INT32_MAX);
			return;
		}

		ShotBase::OnUpdate(timeMult);

		if (!_fired) {
			_fired = true;
			MoveInstantly(_gunspotPos, MoveType::Absolute | MoveType::Force);
			_renderer.setDrawEnabled(true);
		}
	}

	void ElectroShot::OnUpdateHitbox()
	{
		UpdateHitbox(4, 4);
	}

	void ElectroShot::OnEmitLights(SmallVectorImpl<LightEmitter>& lights)
	{
		auto& light = lights.emplace_back();
		light.Pos = _pos;
		light.Intensity = 0.4f;
		light.Brightness = 0.2f;
		light.RadiusNear = 0.0f;
		light.RadiusFar = 12.0f;
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