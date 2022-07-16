#include "CollectibleBase.h"
#include "../../LevelInitialization.h"
#include "../Player.h"
#include "../Weapons/ShotBase.h"
#include "../Enemies/TurtleShell.h"

#include "../../../nCine/Base/FrameTimer.h"
#include "../../../nCine/Base/Random.h"
#include "../../../nCine/CommonConstants.h"

namespace Jazz2::Actors::Collectibles
{
	CollectibleBase::CollectibleBase()
		:
		_untouched(true),
		_scoreValue(0),
		_phase(0.0f),
		_timeLeft(0.0f),
		_startingY(0.0f)
	{
	}

	Task<bool> CollectibleBase::OnActivatedAsync(const ActorActivationDetails& details)
	{
		_elasticity = 0.6f;

		CollisionFlags |= CollisionFlags::SkipPerPixelCollisions;

		Vector2f pos = _pos;
		_phase = ((pos.X / 32) + (pos.Y / 32)) * 2.0f;

		if ((_flags & (ActorFlags::IsCreatedFromEventMap | ActorFlags::IsFromGenerator)) != ActorFlags::None) {
			_untouched = true;
			CollisionFlags &= ~CollisionFlags::ApplyGravitation;

			_startingY = pos.Y;
		} else {
			_untouched = false;
			CollisionFlags |= CollisionFlags::ApplyGravitation;

			_timeLeft = 90.0f * FrameTimer::FramesPerSecond;
		}

		// TODO: lights
		/*if ((details.Flags & ActorInstantiationFlags::Illuminated) != 0) {
			Illuminate();
		}*/

		co_return true;
	}

	void CollectibleBase::OnUpdate(float timeMult)
	{
		ActorBase::OnUpdate(timeMult);

		if (_untouched) {
			_phase += timeMult * 0.15f;

			float waveOffset = 3.2f * std::cosf((_phase * 0.25f) * fPi) + 0.6f;
			MoveInstantly(Vector2f(_pos.X, _startingY + waveOffset), MoveType::Absolute);
		} else if (_timeLeft > 0.0f) {
			_timeLeft -= timeMult;
			if (_timeLeft <= 0.0f) {
				// TODO: exp
				//Explosion.Create(_levelHandler, Transform.Pos, Explosion.Generator);

				DecreaseHealth(INT32_MAX);
			}
		}
	}

	bool CollectibleBase::OnHandleCollision(ActorBase* other)
	{
		if (auto player = dynamic_cast<Player*>(other)) {
			OnCollect(player);
			return true;
		} else {
			// TODO: Add TNT
			bool shouldDrop = _untouched && (dynamic_cast<Weapons::ShotBase*>(other) != nullptr || dynamic_cast<Enemies::TurtleShell*>(other) != nullptr);
			if (shouldDrop) {
				Vector2f speed = other->GetSpeed();
				_externalForce.X += speed.X / 2.0f * (0.9f + nCine::Random().NextFloat(0.0f, 0.2f));
				_externalForce.Y += -speed.Y / 4.0f * (0.9f + nCine::Random().NextFloat(0.0f, 0.2f));

				_untouched = false;
				CollisionFlags |= CollisionFlags::ApplyGravitation;
			}
		}

		return false;
	}

	void CollectibleBase::OnCollect(Player* player)
	{
		player->AddScore(_scoreValue);

		// TODO: explosions
		//Explosion.Create(_levelHandler, _pos, Explosion.Generator);

		DecreaseHealth(INT32_MAX);
	}

	void CollectibleBase::SetFacingDirection()
	{
		if ((((int)(_pos.X + _pos.Y) / 32) & 1) == 0) {
			SetFacingLeft(true);
		}
	}
}