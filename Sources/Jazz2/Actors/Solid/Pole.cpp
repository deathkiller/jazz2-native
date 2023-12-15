#include "Pole.h"
#include "../../ILevelHandler.h"
#include "../Weapons/ShotBase.h"
#include "../Weapons/FreezerShot.h"
#include "../Weapons/Thunderbolt.h"
#include "../Weapons/TNT.h"

using namespace Jazz2::Tiles;

namespace Jazz2::Actors::Solid
{
	Pole::Pole()
		:
		_fall(FallDirection::None),
		_angleVel(0.0f),
		_angleVelLast(0.0f),
		_fallTime(0.0f),
		_bouncesLeft(BouncesMax)
	{
	}

	void Pole::Preload(const ActorActivationDetails& details)
	{
		uint8_t theme = details.Params[0];
		switch (theme) {
			default:
			case 0: PreloadMetadataAsync("Pole/Carrotus"_s); break;
			case 1: PreloadMetadataAsync("Pole/Diamondus"_s); break;
			case 2: PreloadMetadataAsync("Pole/DiamondusTree"_s); break;
			case 3: PreloadMetadataAsync("Pole/Jungle"_s); break;
			case 4: PreloadMetadataAsync("Pole/Psych"_s); break;
		}
	}

	Task<bool> Pole::OnActivatedAsync(const ActorActivationDetails& details)
	{
		uint8_t theme = details.Params[0];
		int16_t x = *(int16_t*)&details.Params[1];
		int16_t y = *(int16_t*)&details.Params[3];

		_pos.X += x;
		_pos.Y += y;
		_renderer.setLayer(_renderer.layer() - 20);

		SetState(ActorState::TriggersTNT, true);
		SetState(ActorState::CanBeFrozen | ActorState::ApplyGravitation, false);

		bool isSolid = true;
		switch (theme) {
			default:
			case 0: async_await RequestMetadataAsync("Pole/Carrotus"_s); break;
			case 1: async_await RequestMetadataAsync("Pole/Diamondus"_s); break;
			case 2: async_await RequestMetadataAsync("Pole/DiamondusTree"_s); isSolid = false; break;
			case 3: async_await RequestMetadataAsync("Pole/Jungle"_s); break;
			case 4: async_await RequestMetadataAsync("Pole/Psych"_s); break;
		}

		if (isSolid) {
			SetState(ActorState::IsSolidObject, true);
		}

		SetAnimation(AnimState::Default);

		async_return true;
	}

	void Pole::OnUpdate(float timeMult)
	{
		constexpr float FallMultiplier = 0.0036f;
		constexpr float Bounce = -0.2f;

		ActorBase::OnUpdate(timeMult);

		if (_fall != FallDirection::Left && _fall != FallDirection::Right) {
			return;
		}

		_fallTime += timeMult;

		if (_fall == FallDirection::Right) {
			if (_angleVel > 0 && IsPositionBlocked()) {
				if (_bouncesLeft > 0) {
					if (_bouncesLeft == BouncesMax) {
						_angleVelLast = _angleVel;
						PlaySfx("FallEnd"_s, 0.8f);
					}

					_bouncesLeft--;
					_angleVel = Bounce * _bouncesLeft * _angleVelLast;
				} else {
					_fall = FallDirection::Fallen;
					if (_fallTime < 10.0f) {
						SetState(ActorState::IsSolidObject, false);
					}
				}
			} else {
				_angleVel += FallMultiplier * timeMult;
				_renderer.setRotation(_renderer.rotation() + _angleVel * timeMult);
				SetState(ActorState::IsDirty, true);
			}
		} else if (_fall == FallDirection::Left) {
			if (_angleVel < 0 && IsPositionBlocked()) {
				if (_bouncesLeft > 0) {
					if (_bouncesLeft == BouncesMax) {
						_angleVelLast = _angleVel;
						PlaySfx("FallEnd"_s, 0.8f);
					}

					_bouncesLeft--;
					_angleVel = Bounce * _bouncesLeft * _angleVelLast;
				} else {
					_fall = FallDirection::Fallen;
					if (_fallTime < 10.0f) {
						SetState(ActorState::IsSolidObject, false);
					}
				}
			} else {
				_angleVel -= FallMultiplier * timeMult;
				_renderer.setRotation(_renderer.rotation() + _angleVel * timeMult);
				SetState(ActorState::IsDirty, true);
			}
		}
	}

	bool Pole::OnHandleCollision(std::shared_ptr<ActorBase> other)
	{
		if (auto* shotBase = runtime_cast<Weapons::ShotBase*>(other)) {
			if (shotBase->GetStrength() > 0) {
				FallDirection fallDirection;
				if (auto* thunderbolt = runtime_cast<Weapons::Thunderbolt*>(shotBase)) {
					fallDirection = (_pos.X < shotBase->GetPos().X ? FallDirection::Left : FallDirection::Right);
				} else {
					fallDirection = (shotBase->GetSpeed().X < 0.0f ? FallDirection::Left : FallDirection::Right);
				}
				Fall(fallDirection);
				shotBase->DecreaseHealth(1);
				return true;
			} else if (auto* freezerShot = runtime_cast<Weapons::FreezerShot*>(shotBase)) {
				shotBase->DecreaseHealth(INT32_MAX);
				return true;
			}
		} else if (auto* tnt = runtime_cast<Weapons::TNT*>(other)) {
			Fall(tnt->GetPos().X > _pos.X ? FallDirection::Left : FallDirection::Right);
			return true;
		}

		return false;
	}

	void Pole::Fall(FallDirection dir)
	{
		if (_fall != FallDirection::None) {
			return;
		}

		_fall = dir;
		SetState(ActorState::IsInvulnerable | ActorState::IsSolidObject, true);
		PlaySfx("FallStart"_s, 0.6f);
	}

	bool Pole::IsPositionBlocked()
	{
		constexpr float Ratio1 = 0.96f;
		constexpr float Ratio2 = 0.8f;
		constexpr float Ratio3 = 0.6f;
		constexpr float Ratio4 = 0.3f;

		float angle = _renderer.rotation() - fPiOver2;
		float rx = cosf(angle);
		float ry = sinf(angle);
		float radius = (float)_currentAnimation->Base->FrameDimensions.Y;
		TileCollisionParams params = { TileDestructType::None, true };

		if (_fallTime > 20) {
			// Check radius 1
			{
				float x = _pos.X + (rx * Ratio1 * radius);
				float y = _pos.Y + (ry * Ratio1 * radius);
				AABBf aabb = AABBf(x - 3, y - 3, x + 7, y + 7);
				if (!_levelHandler->IsPositionEmpty(this, aabb, params)) {
					return true;
				}
			}
			// Check radius 2
			{
				float x = _pos.X + (rx * Ratio2 * radius);
				float y = _pos.Y + (ry * Ratio2 * radius);
				AABBf aabb = AABBf(x - 3, y - 3, x + 7, y + 7);
				if (!_levelHandler->IsPositionEmpty(this, aabb, params)) {
					return true;
				}
			}
		}
		// Check radius 3
		{
			float x = _pos.X + (rx * Ratio3 * radius);
			float y = _pos.Y + (ry * Ratio3 * radius);
			AABBf aabb = AABBf(x - 3, y - 3, x + 7, y + 7);
			if (!_levelHandler->IsPositionEmpty(this, aabb, params)) {
				return true;
			}
		}
		// Check radius 4
		{
			float x = _pos.X + (rx * Ratio4 * radius);
			float y = _pos.Y + (ry * Ratio4 * radius);
			AABBf aabb = AABBf(x - 3, y - 3, x + 7, y + 7);
			if (!_levelHandler->IsPositionEmpty(this, aabb, params)) {
				return true;
			}
		}

		return false;
	}
}