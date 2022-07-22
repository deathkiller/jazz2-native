#include "Bridge.h"
#include "../../LevelInitialization.h"
#include "../../ILevelHandler.h"
#include "../../Tiles/TileMap.h"

namespace Jazz2::Actors::Solid
{
	Bridge::Bridge()
	{
	}

	void Bridge::Preload(const ActorActivationDetails& details)
	{
		PreloadMetadataAsync("Bridge/Stone");
	}

	Task<bool> Bridge::OnActivatedAsync(const ActorActivationDetails& details)
	{
		_bridgeWidth = *(uint16_t*)&details.Params[0];
		_bridgeType = (BridgeType)details.Params[2];
		if (_bridgeType > BridgeType::Last) {
			_bridgeType = BridgeType::Rope;
		}

		int toughness = details.Params[2];
		_heightFactor = std::sqrtf((16 - toughness) * _bridgeWidth) * 4.0f;

		co_await RequestMetadataAsync("Bridge/Stone");

		SetAnimation(AnimState::Idle);

		_originalY = _pos.Y - 6;

		/*if (bridgePieces.Count == 0) {
			int[] widthList = PieceWidths[(int)bridgeType];

			int widthCovered = widthList[0] / 2;
			for (int i = 0; (widthCovered <= bridgeWidth * 16 + 6) || (i * 16 < bridgeWidth); i++) {
				Piece piece = new Piece();
				piece.OnActivated(new ActorActivationDetails {
					LevelHandler = levelHandler,
					Pos = new Vector3(pos.X + widthCovered - 16, pos.Y - 20, LevelHandler.MainPlaneZ + 30),
					Params = new[] { (ushort)bridgeType, (ushort)i }
				});
				levelHandler.AddActor(piece);

				bridgePieces.Add(piece);

				widthCovered += (widthList[i % widthList.Length] + widthList[(i + 1) % widthList.Length]) / 2;
			}
		}*/

		CollisionFlags = CollisionFlags::CollideWithOtherActors | CollisionFlags::SkipPerPixelCollisions | CollisionFlags::IsSolidObject;

		co_return true;
	}

	void Bridge::OnUpdate(float timeMult)
	{
		// TODO
	}

	void Bridge::OnUpdateHitbox()
	{
		AABBInner = AABBf(_pos.X - 16, _pos.Y - 10, _pos.X - 16 + _bridgeWidth * 16, _pos.Y + 16);
	}
}