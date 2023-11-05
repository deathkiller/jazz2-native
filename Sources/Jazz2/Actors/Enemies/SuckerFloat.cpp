#include "SuckerFloat.h"
#include "../../ILevelHandler.h"
#include "../../Tiles/TileMap.h"
#include "Sucker.h"
#include "../Player.h"
#include "../Solid/PushableBox.h"
#include "../Weapons/TNT.h"

#include "../../../nCine/Base/Random.h"

namespace Jazz2::Actors::Enemies
{
	SuckerFloat::SuckerFloat()
		: _phase(0.0f)
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

		SetState(ActorState::ApplyGravitation, false);

		SetHealthByDifficulty(1);
		_scoreValue = 200;

		async_await RequestMetadataAsync("Enemy/SuckerFloat"_s);

		SetAnimation(AnimState::Idle);

		async_return true;
	}

	void SuckerFloat::OnUpdate(float timeMult)
	{
		if (_frozenTimeLeft <= 0.0f) {
			_phase = fmodf(_phase + 0.05f * timeMult, fTwoPi);
			MoveInstantly(Vector2f(_originPos.X + 10 * cosf(_phase), _originPos.Y + 10 * sinf(_phase)), MoveType::Absolute | MoveType::Force);

			SetFacingLeft(_phase < fPiOver2 || _phase > 3 * fPiOver2);
		}

		EnemyBase::OnUpdate(timeMult);
	}

	bool SuckerFloat::OnPerish(ActorBase* collider)
	{
		bool shouldDestroy = (_frozenTimeLeft > 0.0f);
		if (auto player = dynamic_cast<Player*>(collider)) {
			if (player->GetSpecialMove() != Player::SpecialMoveType::None) {
				shouldDestroy = true;
			}
		} else if (dynamic_cast<Weapons::TNT*>(collider) != nullptr ||
					dynamic_cast<Solid::PushableBox*>(collider) != nullptr) {
			shouldDestroy = true;
		}

		if (shouldDestroy) {
			CreateDeathDebris(collider);
			_levelHandler->PlayCommonSfx("Splat"_s, Vector3f(_pos.X, _pos.Y, 0.0f));

			TryGenerateRandomDrop();
		} else {
			std::shared_ptr<Sucker> sucker = std::make_shared<Sucker>();
			uint8_t suckerParams[1] = { (uint8_t)_lastHitDir };
			sucker->OnActivated(ActorActivationDetails(
				_levelHandler,
				Vector3i((std::int32_t)_pos.X, (std::int32_t)_pos.Y, _renderer.layer()),
				suckerParams
			));
			_levelHandler->AddActor(sucker);
		}

		return EnemyBase::OnPerish(collider);
	}
}