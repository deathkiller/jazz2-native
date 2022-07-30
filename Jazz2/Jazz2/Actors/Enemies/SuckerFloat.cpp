#include "SuckerFloat.h"
#include "../../LevelInitialization.h"
#include "../../ILevelHandler.h"
#include "../../Tiles/TileMap.h"
#include "Sucker.h"

#include "../../../nCine/Base/Random.h"

namespace Jazz2::Actors::Enemies
{
	SuckerFloat::SuckerFloat()
		:
		_phase(0.0f)
	{
	}

	void SuckerFloat::Preload(const ActorActivationDetails& details)
	{
		PreloadMetadataAsync("Enemy/SuckerFloat"_s);
		PreloadMetadataAsync("Enemy/Sucker"_s);
	}

	Task<bool> SuckerFloat::OnActivatedAsync(const ActorActivationDetails& details)
	{
		_originPos = Vector2i(details.Pos.X, details.Pos.Y);

		CollisionFlags &= ~CollisionFlags::ApplyGravitation;

		SetHealthByDifficulty(1);
		_scoreValue = 200;

		co_await RequestMetadataAsync("Enemy/SuckerFloat"_s);

		SetAnimation(AnimState::Idle);

		co_return true;
	}

	void SuckerFloat::OnUpdate(float timeMult)
	{
		if (_frozenTimeLeft <= 0) {
			_phase = fmodf(_phase + 0.05f * timeMult, fTwoPi);
			MoveInstantly(Vector2f(_originPos.X + 10 * std::cosf(_phase), _originPos.Y + 10 * std::sinf(_phase)), MoveType::Absolute, true);

			SetFacingLeft(_phase < fPiOver2 || _phase > 3 * fPiOver2);
		}

		EnemyBase::OnUpdate(timeMult);
	}

	bool SuckerFloat::OnPerish(ActorBase* collider)
	{
		std::shared_ptr<Sucker> sucker = std::make_shared<Sucker>();
		uint8_t suckerParams[1] = { (uint8_t)_lastHitDir };
		sucker->OnActivated({
			.LevelHandler = _levelHandler,
			.Pos = Vector3i((int)_pos.X, (int)_pos.Y, _renderer.layer()),
			.Params = suckerParams
		});
		_levelHandler->AddActor(sucker);

		return EnemyBase::OnPerish(collider);
	}
}