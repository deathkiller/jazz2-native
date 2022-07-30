#include "Spring.h"
#include "../../ILevelHandler.h"
#include "../../Tiles/TileMap.h"

namespace Jazz2::Actors::Environment
{
	Spring::Spring()
		:
		_cooldown(0.0f)
	{
	}

	Vector2f Spring::Activate()
	{
		if (_cooldown > 0.0f || _frozenTimeLeft > 0.0f) {
			return Vector2f::Zero;
		}

		_cooldown = 6.0f;

		SetTransition(_currentAnimationState | (AnimState)0x200, false);
		switch (_orientation) {
			case 0: // Bottom
				PlaySfx("Vertical"_s);
				return Vector2f(0, -_strength);
			case 2: // Top
				PlaySfx("VerticalReversed"_s);
				return Vector2f(0, _strength);
			case 1: // Right
			case 3: // Left
				PlaySfx("Horizontal"_s);
				return Vector2f(_strength * (_orientation == 1 ? 1 : -1), 0);
			default:
				return Vector2f::Zero;
		}
	}

	Task<bool> Spring::OnActivatedAsync(const ActorActivationDetails& details)
	{
		// TODO: change these types to uint8_t
		_type = *(uint16_t*)&details.Params[0];
		_orientation = *(uint16_t*)&details.Params[2];
		KeepSpeedX = (details.Params[4] != 0);
		KeepSpeedY = (details.Params[6] != 0);
		//delay = details.Params[4];
		//frozen = (details.Params[5] != 0);

		CollisionFlags |= CollisionFlags::SkipPerPixelCollisions;

		co_await RequestMetadataAsync("Object/Spring"_s);

		Vector2f tileCorner = Vector2f((int)(_pos.X / Tiles::TileSet::DefaultTileSize) * Tiles::TileSet::DefaultTileSize,
			(int)(_pos.Y / Tiles::TileSet::DefaultTileSize) * Tiles::TileSet::DefaultTileSize);
		if (_orientation > 3) {
			// JJ2 horizontal springs held no data about which way they were facing.
			// For compatibility, the level converter sets their orientation to 5, which is interpreted here.
			AABBf aabb = AABBf(_pos.X + 6, _pos.Y - 2, _pos.X + 22, _pos.Y + 2);
			_orientation = (uint16_t)(_levelHandler->TileMap()->IsTileEmpty(aabb, false) != (_orientation == 5) ? 1 : 3);
		}

		int orientationBit = 0;
		switch (_orientation) {
			case 0: // Bottom
				MoveInstantly(Vector2f(tileCorner.X + 16, tileCorner.Y + 8), MoveType::Absolute, true);
				break;
			case 1: // Right
				MoveInstantly(Vector2f(tileCorner.X + 16, tileCorner.Y + 16), MoveType::Absolute, true);
				orientationBit = 1;
				CollisionFlags &= ~CollisionFlags::ApplyGravitation;
				break;
			case 2: // Top
				MoveInstantly(Vector2f(tileCorner.X + 16, tileCorner.Y + 8), MoveType::Absolute, true);
				orientationBit = 2;
				CollisionFlags &= ~CollisionFlags::ApplyGravitation;
				break;
			case 3: // Left
				MoveInstantly(Vector2f(tileCorner.X + 16, tileCorner.Y + 16), MoveType::Absolute, true);
				orientationBit = 1;
				CollisionFlags &= ~CollisionFlags::ApplyGravitation;
				SetFacingLeft(true);
				break;
		}

		// Red starts at 1 in "Object/Spring"
		SetAnimation((AnimState)(((_type + 1) << 10) | (orientationBit << 12)));

		if ((_orientation % 2) == 1) {
			// Horizontal springs all seem to have the same strength.
			// This constant strength gives about the correct amount of horizontal push.
			_strength = 9.5f;
		} else {
			// Vertical springs should work as follows:
			// Red spring lifts the player 9 tiles, green 14, and blue 19.
			// Vertical strength currently works differently from horizontal, that explains
			// the otherwise inexplicable difference of scale between the two types.
			switch (_type) {
				case 0: // Red
					_strength = 1.25f;
					break;
				case 1: // Green
					_strength = 1.50f;
					break;
				case 2: // Blue
					_strength = 1.65f;
					break;
			}
		}

		if ((CollisionFlags & CollisionFlags::ApplyGravitation) == CollisionFlags::ApplyGravitation) {
			OnUpdateHitbox();

			// Apply instant gravitation
			int i = 10;
			while (i-- > 0 && MoveInstantly(Vector2f(0.0f, 4.0f), MoveType::Relative)) {
				// Nothing to do...
			}
			while (i-- > 0 && MoveInstantly(Vector2f(0.0f, 1.0f), MoveType::Relative)) {
				// Nothing to do...
			}
		}

		co_return true;
	}

	void Spring::OnUpdate(float timeMult)
	{
		ActorBase::OnUpdate(timeMult);

		if (_cooldown > 0.0f) {
			_cooldown -= timeMult;
		}
	}

	void Spring::OnUpdateHitbox()
	{
		switch (_orientation) {
			case 1: // Right
				AABBInner = AABBf(_pos.X - 8, _pos.Y - 10, _pos.X, _pos.Y + 10);
				break;
			case 3: // Left
				AABBInner = AABBf(_pos.X, _pos.Y - 10, _pos.X + 8, _pos.Y + 10);
				break;
			default:
			case 0: // Bottom
			case 2: // Top
				AABBInner = AABBf(_pos.X - 10, _pos.Y, _pos.X + 10, _pos.Y + 8);
				break;
		}
	}
}