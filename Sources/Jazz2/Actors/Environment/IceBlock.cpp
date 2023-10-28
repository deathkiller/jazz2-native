#include "IceBlock.h"
#include "../../ILevelHandler.h"
#include "../../Tiles/TileMap.h"
#include "../Explosion.h"
#include "../Weapons/ShotBase.h"
#include "../Weapons/TNT.h"

#include "../../../nCine/Base/Random.h"

namespace Jazz2::Actors::Environment
{
	IceBlock::IceBlock()
		:
		_timeLeft(200.0f)
	{
	}

	void IceBlock::Preload(const ActorActivationDetails& details)
	{
		PreloadMetadataAsync("Object/IceBlock"_s);
	}

	Task<bool> IceBlock::OnActivatedAsync(const ActorActivationDetails& details)
	{
		SetState(ActorState::SkipPerPixelCollisions, true);
		SetState(ActorState::CanBeFrozen | ActorState::CollideWithTileset | ActorState::ApplyGravitation, false);

		async_await RequestMetadataAsync("Object/IceBlock"_s);
		SetAnimation(AnimState::Default);

		_renderer.Initialize(ActorRendererType::FrozenMask);

		async_return true;
	}

	void IceBlock::OnUpdate(float timeMult)
	{
		ActorBase::OnUpdate(timeMult);

		auto tileMap = _levelHandler->TileMap();
		if (tileMap != nullptr) {
			if (tileMap->IsTileEmpty(_originTile.X, _originTile.Y)) {
				DecreaseHealth(INT32_MAX);
				return;
			}
		}

		_timeLeft -= timeMult;
		if (_timeLeft <= 0.0f) {
			DecreaseHealth(INT32_MAX);

			if (tileMap != nullptr) {
				if (tileMap->IsTileEmpty(_originTile.X - 1, _originTile.Y)) {
					for (int i = 0; i < 5; i++) {
						Explosion::Create(_levelHandler, Vector3i((int)_pos.X - 24, (int)_pos.Y - 16 + Random().Fast(0, 32), _renderer.layer() + 10), Explosion::Type::IceShrapnel);
					}
				}
				if (tileMap->IsTileEmpty(_originTile.X + 1, _originTile.Y)) {
					for (int i = 0; i < 5; i++) {
						Explosion::Create(_levelHandler, Vector3i((int)_pos.X + 24, (int)_pos.Y - 16 + Random().Fast(0, 32), _renderer.layer() + 10), Explosion::Type::IceShrapnel);
					}
				}
				if (tileMap->IsTileEmpty(_originTile.X, _originTile.Y + 1)) {
					for (int i = 0; i < 5; i++) {
						Explosion::Create(_levelHandler, Vector3i((int)_pos.X - 16 + Random().Fast(0, 32), (int)_pos.Y + 24, _renderer.layer() + 10), Explosion::Type::IceShrapnel);
					}
				}
			}

			Explosion::Create(_levelHandler, Vector3i((int)_pos.X, (int)_pos.Y, _renderer.layer() + 90), Explosion::Type::SmokeWhite);

			_levelHandler->PlayCommonSfx("IceBreak"_s, Vector3f(_pos.X, _pos.Y, 0.0f));
		}
	}

	void IceBlock::ResetTimeLeft()
	{
		if (_timeLeft > 0.0f) {
			_timeLeft = 200.0f;
		}
	}
}