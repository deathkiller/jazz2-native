#include "TileMap.h"

#include "../LevelHandler.h"
#include "../PreferencesCache.h"

#include "../../nCine/tracy.h"
#include "../../nCine/Base/Random.h"
#include "../../nCine/Graphics/RenderQueue.h"
#include "../../nCine/Graphics/RenderResources.h"

namespace Jazz2::Tiles
{
	TileMap::TileMap(const StringView tileSetPath, std::uint16_t captionTileId, bool applyPalette)
		: _owner(nullptr), _sprLayerIndex(-1), _pitType(PitType::FallForever), _renderCommandsCount(0), _collapsingTimer(0.0f),
			_triggerState(TriggerCount), _texturedBackgroundLayer(-1), _texturedBackgroundPass(this)
	{
		auto& tileSetPart = _tileSets.emplace_back();
		tileSetPart.Data = ContentResolver::Get().RequestTileSet(tileSetPath, captionTileId, applyPalette);
		RETURN_ASSERT_MSG(tileSetPart.Data != nullptr, "Failed to load main tileset \"%s\"", tileSetPath.data());
		
		tileSetPart.Offset = 0;
		tileSetPart.Count = tileSetPart.Data->TileCount;

		_renderCommands.reserve(128);
	}

	TileMap::~TileMap()
	{
		TracyPlot("TileMap Render Commands", 0LL);
	}

	bool TileMap::IsValid() const
	{
		std::size_t count = _tileSets.size();
		if (count == 0) {
			return false;
		}

		for (std::size_t i = 0; i < count; i++) {
			if (_tileSets[i].Data == nullptr) {
				return false;
			}
		}

		return true;
	}

	void TileMap::SetOwner(ITileMapOwner* owner)
	{
		_owner = owner;
	}

	Vector2i TileMap::GetSize() const
	{
		if (_sprLayerIndex == -1) {
			return { };
		}

		return _layers[_sprLayerIndex].LayoutSize;
	}

	Vector2i TileMap::GetLevelBounds() const
	{
		if (_sprLayerIndex == -1) {
			return { };
		}

		Vector2i layoutSize = _layers[_sprLayerIndex].LayoutSize;
		return Vector2i(layoutSize.X * TileSet::DefaultTileSize, layoutSize.Y * TileSet::DefaultTileSize);
	}

	PitType TileMap::GetPitType() const
	{
		return _pitType;
	}

	void TileMap::SetPitType(PitType value)
	{
		_pitType = value;
	}

	void TileMap::OnUpdate(float timeMult)
	{
		ZoneScopedC(0xA09359);

		SceneNode::OnUpdate(timeMult);

		// Update animated tiles
		for (auto& animTile : _animatedTiles) {
			if (animTile.FrameDuration <= 0.0f || animTile.Tiles.size() < 2) {
				continue;
			}

			animTile.FramesLeft -= timeMult;
			while (animTile.FramesLeft <= 0.0f) {
				if (animTile.Forwards) {
					if (animTile.CurrentTileIdx == animTile.Tiles.size() - 1) {
						if (animTile.IsPingPong) {
							animTile.Forwards = false;
							animTile.FramesLeft += (animTile.FrameDuration * (1 + animTile.PingPongDelay));
						} else {
							animTile.CurrentTileIdx = 0;
							std::int32_t delayFrames = 1 + animTile.Delay;
							if (animTile.DelayJitter > 0) {
								delayFrames += Random().Next(0, animTile.DelayJitter + 1);
							}
							animTile.FramesLeft += animTile.FrameDuration * delayFrames;
						}
					} else {
						animTile.CurrentTileIdx++;
						animTile.FramesLeft += animTile.FrameDuration;
					}
				} else {
					if (animTile.CurrentTileIdx == 0) {
						// Reverse only occurs on ping pong mode so no need to check for that here
						animTile.Forwards = true;
						std::int32_t delayFrames = 1 + animTile.Delay;
						if (animTile.DelayJitter > 0) {
							delayFrames += Random().Next(0, animTile.DelayJitter + 1);
						}
						animTile.FramesLeft += animTile.FrameDuration * delayFrames;
					} else {
						animTile.CurrentTileIdx--;
						animTile.FramesLeft += animTile.FrameDuration;
					}
				}
			}
		}

		// Update layer scrolling
		for (auto& layer : _layers) {
			if (layer.Description.SpeedModelX != LayerSpeedModel::SpeedMultipliers && std::abs(layer.Description.AutoSpeedX) > 0) {
				layer.Description.OffsetX += layer.Description.AutoSpeedX * timeMult;
				if (layer.Description.RepeatX) {
					if (layer.Description.AutoSpeedX > 0) {
						while (layer.Description.OffsetX > (layer.LayoutSize.X * 32)) {
							layer.Description.OffsetX -= (layer.LayoutSize.X * 32);
						}
					} else {
						while (layer.Description.OffsetX < 0) {
							layer.Description.OffsetX += (layer.LayoutSize.X * 32);
						}
					}
				}
			}
			if (layer.Description.SpeedModelY != LayerSpeedModel::SpeedMultipliers && std::abs(layer.Description.AutoSpeedY) > 0) {
				layer.Description.OffsetY += layer.Description.AutoSpeedY * timeMult;
				if (layer.Description.RepeatY) {
					if (layer.Description.AutoSpeedY > 0) {
						while (layer.Description.OffsetY > (layer.LayoutSize.Y * 32)) {
							layer.Description.OffsetY -= (layer.LayoutSize.Y * 32);
						}
					} else {
						while (layer.Description.OffsetY < 0) {
							layer.Description.OffsetY += (layer.LayoutSize.Y * 32);
						}
					}
				}
			}
		}

		AdvanceCollapsingTileTimers(timeMult);
		UpdateDebris(timeMult);
	}

	bool TileMap::OnDraw(RenderQueue& renderQueue)
	{
		ZoneScopedC(0xA09359);

		SceneNode::OnDraw(renderQueue);

		_renderCommandsCount = 0;

		for (auto& layer : _layers) {
			DrawLayer(renderQueue, layer);
		}

		DrawDebris(renderQueue);

		TracyPlot("TileMap Render Commands", static_cast<int64_t>(_renderCommandsCount));

		return true;
	}

	bool TileMap::IsTileEmpty(std::int32_t tx, std::int32_t ty)
	{
		if (_sprLayerIndex == -1) {
			return true;
		}

		Vector2i layoutSize = _layers[_sprLayerIndex].LayoutSize;
		if (tx < 0 || tx >= layoutSize.X) {
			return false;
		}
		if (ty >= layoutSize.Y) {
			return (_pitType != PitType::StandOnPlatform);
		}
		if (ty < 0) {
			ty = 0;
		}

		LayerTile& tile = _layers[_sprLayerIndex].Layout[ty * layoutSize.X + tx];
		std::int32_t tileId = ResolveTileID(tile);
		TileSet* tileSet = ResolveTileSet(tileId);
		return (tileSet == nullptr || tileSet->IsTileMaskEmpty(tileId));
	}

	bool TileMap::IsTileEmpty(const AABBf& aabb, TileCollisionParams& params)
	{
		if (_sprLayerIndex == -1) {
			return true;
		}

		Vector2i layoutSize = _layers[_sprLayerIndex].LayoutSize;

		std::int32_t limitRightPx = layoutSize.X * TileSet::DefaultTileSize;
		std::int32_t limitBottomPx = layoutSize.Y * TileSet::DefaultTileSize;

		// Consider out-of-level coordinates as solid walls
		if (aabb.L < 0 || aabb.R >= limitRightPx) {
			return false;
		}
		if (aabb.B >= limitBottomPx) {
			return (_pitType != PitType::StandOnPlatform);
		}

		// Check all covered tiles for collisions; if all are empty, no need to do pixel collision checking
		std::int32_t hx1 = std::max((std::int32_t)aabb.L, 0);
		std::int32_t hx2 = std::min((std::int32_t)std::ceil(aabb.R), limitRightPx - 1);
		std::int32_t hy1 = std::max((std::int32_t)aabb.T, 0);
		std::int32_t hy2 = std::min((std::int32_t)std::ceil(aabb.B), limitBottomPx - 1);

		if (hy2 <= 0) {
			hy1 = 0;
			hy2 = 1;
		}

		std::int32_t hx1t = hx1 / TileSet::DefaultTileSize;
		std::int32_t hx2t = hx2 / TileSet::DefaultTileSize;
		std::int32_t hy1t = hy1 / TileSet::DefaultTileSize;
		std::int32_t hy2t = hy2 / TileSet::DefaultTileSize;

		auto* sprLayerLayout = _layers[_sprLayerIndex].Layout.get();

		for (std::int32_t y = hy1t; y <= hy2t; y++) {
			for (std::int32_t x = hx1t; x <= hx2t; x++) {
			RecheckTile:
				LayerTile& tile = sprLayerLayout[y * layoutSize.X + x];

				if (tile.DestructType == TileDestructType::Weapon && (params.DestructType & TileDestructType::Weapon) == TileDestructType::Weapon) {
					if ((tile.TileParams & (1 << (std::uint16_t)params.UsedWeaponType)) != 0) {
						if (AdvanceDestructibleTileAnimation(tile, x, y, params.WeaponStrength, "SceneryDestruct"_s)) {
							params.TilesDestroyed++;
							if (params.WeaponStrength <= 0) {
								return false;
							} else {
								goto RecheckTile;
							}
						}
					} else if (params.UsedWeaponType == WeaponType::Freezer && tile.DestructFrameIndex < (_animatedTiles[tile.DestructAnimation].Tiles.size() - 2)) {
						std::int32_t tx = x * TileSet::DefaultTileSize + TileSet::DefaultTileSize / 2;
						std::int32_t ty = y * TileSet::DefaultTileSize + TileSet::DefaultTileSize / 2;
						_owner->OnTileFrozen(tx, ty);
						return false;
					}
				} else if (tile.DestructType == TileDestructType::Special && (params.DestructType & TileDestructType::Special) == TileDestructType::Special) {
					std::int32_t amount = 1;
					if (AdvanceDestructibleTileAnimation(tile, x, y, amount, "SceneryDestruct"_s)) {
						params.TilesDestroyed++;
						goto RecheckTile;
					}
				} else if (tile.DestructType == TileDestructType::Speed && (params.DestructType & TileDestructType::Speed) == TileDestructType::Speed) {
					std::int32_t amount = 1;
					if (tile.TileParams <= params.Speed && AdvanceDestructibleTileAnimation(tile, x, y, amount, "SceneryDestruct"_s)) {
						params.TilesDestroyed++;
						goto RecheckTile;
					}
				} else if (tile.DestructType == TileDestructType::Collapse && (params.DestructType & TileDestructType::Collapse) == TileDestructType::Collapse) {
					bool found = false;
					for (auto& current : _activeCollapsingTiles) {
						if (current == Vector2i(x, y)) {
							found = true;
							break;
						}
					}

					if (!found) {
						_activeCollapsingTiles.emplace_back(x, y);
						params.TilesDestroyed++;
					}
				}

				if ((params.DestructType & TileDestructType::IgnoreSolidTiles) != TileDestructType::IgnoreSolidTiles &&
					tile.HasSuspendType == SuspendType::None && ((tile.Flags & LayerTileFlags::OneWay) != LayerTileFlags::OneWay || params.Downwards)) {
					std::int32_t tileId = ResolveTileID(tile);
					TileSet* tileSet = ResolveTileSet(tileId);
					if (tileSet == nullptr || tileSet->IsTileMaskEmpty(tileId)) {
						continue;
					}

					std::int32_t tx = x * TileSet::DefaultTileSize;
					std::int32_t ty = y * TileSet::DefaultTileSize;

					std::int32_t left = std::max(hx1 - tx, 0);
					std::int32_t right = std::min(hx2 - tx, TileSet::DefaultTileSize - 1);
					std::int32_t top = std::max(hy1 - ty, 0);
					std::int32_t bottom = std::min(hy2 - ty, TileSet::DefaultTileSize - 1);

					if ((tile.Flags & LayerTileFlags::FlipX) == LayerTileFlags::FlipX) {
						std::int32_t left2 = left;
						left = (TileSet::DefaultTileSize - 1 - right);
						right = (TileSet::DefaultTileSize - 1 - left2);
					}
					if ((tile.Flags & LayerTileFlags::FlipY) == LayerTileFlags::FlipY) {
						std::int32_t top2 = top;
						top = (TileSet::DefaultTileSize - 1 - bottom);
						bottom = (TileSet::DefaultTileSize - 1 - top2);
					}

					top *= TileSet::DefaultTileSize;
					bottom *= TileSet::DefaultTileSize;

					std::uint8_t* mask = tileSet->GetTileMask(tileId);
					for (std::int32_t ry = top; ry <= bottom; ry += TileSet::DefaultTileSize) {
						for (std::int32_t rx = left; rx <= right; rx++) {
							if (mask[ry | rx]) {
								return false;
							}
						}
					}
				}
			}
		}

		return true;
	}

	bool TileMap::CanBeDestroyed(const AABBf& aabb, TileCollisionParams& params)
	{
		if (_sprLayerIndex == -1) {
			return true;
		}

		Vector2i layoutSize = _layers[_sprLayerIndex].LayoutSize;

		std::int32_t limitRightPx = layoutSize.X * TileSet::DefaultTileSize;
		std::int32_t limitBottomPx = layoutSize.Y * TileSet::DefaultTileSize;

		// Consider out-of-level coordinates as solid walls
		if (aabb.L < 0 || aabb.R >= limitRightPx) {
			return false;
		}
		if (aabb.B >= limitBottomPx) {
			return (_pitType != PitType::StandOnPlatform);
		}

		// Check all covered tiles for collisions; if all are empty, no need to do pixel collision checking
		std::int32_t hx1 = std::max((std::int32_t)aabb.L, 0);
		std::int32_t hx2 = std::min((std::int32_t)std::ceil(aabb.R), limitRightPx - 1);
		std::int32_t hy1 = std::max((std::int32_t)aabb.T, 0);
		std::int32_t hy2 = std::min((std::int32_t)std::ceil(aabb.B), limitBottomPx - 1);

		if (hy2 <= 0) {
			hy1 = 0;
			hy2 = 1;
		}

		std::int32_t hx1t = hx1 / TileSet::DefaultTileSize;
		std::int32_t hx2t = hx2 / TileSet::DefaultTileSize;
		std::int32_t hy1t = hy1 / TileSet::DefaultTileSize;
		std::int32_t hy2t = hy2 / TileSet::DefaultTileSize;

		auto* sprLayerLayout = _layers[_sprLayerIndex].Layout.get();

		for (std::int32_t y = hy1t; y <= hy2t; y++) {
			for (std::int32_t x = hx1t; x <= hx2t; x++) {
				LayerTile& tile = sprLayerLayout[y * layoutSize.X + x];

				if ((tile.DestructType & TileDestructType::Weapon) == TileDestructType::Weapon && (params.DestructType & TileDestructType::Weapon) == TileDestructType::Weapon) {
					if (tile.DestructFrameIndex < (_animatedTiles[tile.DestructAnimation].Tiles.size() - 2) &&
						((tile.TileParams & (1 << (std::uint16_t)params.UsedWeaponType)) != 0 || params.UsedWeaponType == WeaponType::Freezer)) {
						return true;
					}
				} else if ((tile.DestructType & TileDestructType::Special) == TileDestructType::Special && (params.DestructType & TileDestructType::Special) == TileDestructType::Special) {
					if (tile.DestructFrameIndex < (_animatedTiles[tile.DestructAnimation].Tiles.size() - 2)) {
						return true;
					}
				} else if ((tile.DestructType & TileDestructType::Speed) == TileDestructType::Speed && (params.DestructType & TileDestructType::Speed) == TileDestructType::Speed) {
					if (tile.DestructFrameIndex < (_animatedTiles[tile.DestructAnimation].Tiles.size() - 2) && tile.TileParams <= params.Speed) {
						return true;
					}
				} else if ((tile.DestructType & TileDestructType::Collapse) == TileDestructType::Collapse && (params.DestructType & TileDestructType::Collapse) == TileDestructType::Collapse) {
					bool found = false;
					for (auto& current : _activeCollapsingTiles) {
						if (current == Vector2i(x, y)) {
							found = true;
							break;
						}
					}

					if (!found) {
						return true;
					}
				}

				if ((params.DestructType & TileDestructType::IgnoreSolidTiles) != TileDestructType::IgnoreSolidTiles &&
					tile.HasSuspendType == SuspendType::None && ((tile.Flags & LayerTileFlags::OneWay) != LayerTileFlags::OneWay || params.Downwards)) {
					std::int32_t tileId = ResolveTileID(tile);
					TileSet* tileSet = ResolveTileSet(tileId);
					if (tileSet == nullptr || tileSet->IsTileMaskEmpty(tileId)) {
						continue;
					}

					std::int32_t tx = x * TileSet::DefaultTileSize;
					std::int32_t ty = y * TileSet::DefaultTileSize;

					std::int32_t left = std::max(hx1 - tx, 0);
					std::int32_t right = std::min(hx2 - tx, TileSet::DefaultTileSize - 1);
					std::int32_t top = std::max(hy1 - ty, 0);
					std::int32_t bottom = std::min(hy2 - ty, TileSet::DefaultTileSize - 1);

					if ((tile.Flags & LayerTileFlags::FlipX) == LayerTileFlags::FlipX) {
						std::int32_t left2 = left;
						left = (TileSet::DefaultTileSize - 1 - right);
						right = (TileSet::DefaultTileSize - 1 - left2);
					}
					if ((tile.Flags & LayerTileFlags::FlipY) == LayerTileFlags::FlipY) {
						std::int32_t top2 = top;
						top = (TileSet::DefaultTileSize - 1 - bottom);
						bottom = (TileSet::DefaultTileSize - 1 - top2);
					}

					top *= TileSet::DefaultTileSize;
					bottom *= TileSet::DefaultTileSize;

					std::uint8_t* mask = tileSet->GetTileMask(tileId);
					for (std::int32_t ry = top; ry <= bottom; ry += TileSet::DefaultTileSize) {
						for (std::int32_t rx = left; rx <= right; rx++) {
							if (mask[ry | rx]) {
								return false;
							}
						}
					}
				}
			}
		}

		return false;
	}

	bool TileMap::IsTileHurting(float x, float y)
	{
		// TODO: Implement all JJ2+ parameters (directional hurt events)
		std::int32_t tx = (std::int32_t)x / TileSet::DefaultTileSize;
		std::int32_t ty = (std::int32_t)y / TileSet::DefaultTileSize;

		if (tx < 0 || ty < 0 || _sprLayerIndex == -1) {
			return false;
		}

		Vector2i layoutSize = _layers[_sprLayerIndex].LayoutSize;
		if (tx >= layoutSize.X || ty >= layoutSize.Y) {
			return false;
		}

		LayerTile& tile = _layers[_sprLayerIndex].Layout[ty * layoutSize.X + tx];
		if ((tile.Flags & LayerTileFlags::Hurt) != LayerTileFlags::Hurt) {
			return false;
		}
		// TODO: Some tiles have Hurt event with empty mask
		//std::int32_t tileId = ResolveTileID(tile);
		//TileSet* tileSet = ResolveTileSet(tileId);
		//return (tileSet == nullptr || tileSet->IsTileMaskEmpty(tileId));
		return true;
	}

	SuspendType TileMap::GetTileSuspendState(float x, float y)
	{
		constexpr std::int32_t Tolerance = 4;

		if (x < 0 || y < 0 || _sprLayerIndex == -1) {
			return SuspendType::None;
		}

		std::int32_t tx = (std::int32_t)x / TileSet::DefaultTileSize;
		std::int32_t ty = (std::int32_t)y / TileSet::DefaultTileSize;

		Vector2i layoutSize = _layers[_sprLayerIndex].LayoutSize;
		if (tx >= layoutSize.X || ty >= layoutSize.Y) {
			return SuspendType::None;
		}

		TileMapLayer& layer = _layers[_sprLayerIndex];
		LayerTile& tile = layer.Layout[tx + ty * layer.LayoutSize.X];
		if (tile.HasSuspendType == SuspendType::None) {
			return SuspendType::None;
		}

		std::int32_t tileId = ResolveTileID(tile);
		TileSet* tileSet = ResolveTileSet(tileId);
		if (tileSet == nullptr) {
			return SuspendType::None;
		}

		std::uint8_t* mask = tileSet->GetTileMask(tileId);

		std::int32_t rx = (std::int32_t)x & 31;
		std::int32_t ry = (std::int32_t)y & 31;

		if ((tile.Flags & LayerTileFlags::FlipX) == LayerTileFlags::FlipX) {
			rx = (TileSet::DefaultTileSize - 1 - rx);
		}
		if ((tile.Flags & LayerTileFlags::FlipY) == LayerTileFlags::FlipY) {
			ry = (TileSet::DefaultTileSize - 1 - ry);
		}

		std::int32_t top = std::max(ry - Tolerance, 0) << 5;
		std::int32_t bottom = std::min(ry + Tolerance, TileSet::DefaultTileSize - 1) << 5;

		for (std::int32_t ti = bottom | rx; ti >= top; ti -= TileSet::DefaultTileSize) {
			if (mask[ti]) {
				return tile.HasSuspendType;
			}
		}

		return SuspendType::None;
	}

	bool TileMap::AdvanceDestructibleTileAnimation(std::int32_t tx, std::int32_t ty, std::int32_t amount)
	{
		Vector2i layoutSize = _layers[_sprLayerIndex].LayoutSize;
		LayerTile& tile = _layers[_sprLayerIndex].Layout[tx + ty * layoutSize.X];
		return AdvanceDestructibleTileAnimation(tile, tx, ty, amount, {});
	}

	bool TileMap::AdvanceDestructibleTileAnimation(LayerTile& tile, std::int32_t tx, std::int32_t ty, std::int32_t& amount, const StringView soundName)
	{
		AnimatedTile& anim = _animatedTiles[tile.DestructAnimation];
		std::int32_t max = (std::int32_t)(anim.Tiles.size() - 2);
		if (amount > 0 && tile.DestructFrameIndex < max) {
			// Tile not destroyed yet, advance counter by one
			std::int32_t current = std::min(amount, max - tile.DestructFrameIndex);

			tile.DestructFrameIndex += current;
			tile.TileID = anim.Tiles[tile.DestructFrameIndex].TileID;
			if (tile.DestructFrameIndex >= max) {
				if (!soundName.empty()) {
					_owner->PlayCommonSfx(soundName, Vector3f(tx * TileSet::DefaultTileSize + (TileSet::DefaultTileSize / 2),
						ty * TileSet::DefaultTileSize + (TileSet::DefaultTileSize / 2), 0.0f));
				}
				CreateTileDebris(anim.Tiles[anim.Tiles.size() - 1].TileID, tx, ty);
			}

			amount -= current;

			_owner->OnAdvanceDestructibleTileAnimation(tx, ty, current);
			return true;
		}
		return false;
	}

	void TileMap::AdvanceCollapsingTileTimers(float timeMult)
	{
		ZoneScopedC(0xA09359);

		_collapsingTimer -= timeMult;
		if (_collapsingTimer > 0.0f) {
			return;
		}

		_collapsingTimer = 1.0f;

		const Vector2i& layoutSize = _layers[_sprLayerIndex].LayoutSize;

		for (std::int32_t i = 0; i < _activeCollapsingTiles.size(); i++) {
			Vector2i tilePos = _activeCollapsingTiles[i];
			auto& tile = _layers[_sprLayerIndex].Layout[tilePos.X + tilePos.Y * layoutSize.X];
			if (tile.TileParams == 0) {
				std::int32_t amount = 1;
				if (!AdvanceDestructibleTileAnimation(tile, tilePos.X, tilePos.Y, amount, "SceneryCollapse"_s)) {
					tile.DestructType = TileDestructType::None;
					_activeCollapsingTiles.erase(_activeCollapsingTiles.begin() + i);
					i--;
				} else {
					tile.TileParams = 4;
				}
			} else {
				tile.TileParams--;
			}
		}
	}

	void TileMap::DrawLayer(RenderQueue& renderQueue, TileMapLayer& layer)
	{
		ZoneScopedNC("Layer", 0xA09359);

		if (!layer.Visible) {
			return;
		}

		Vector2i viewSize = _owner->GetViewSize();
		Vector2f viewCenter = _owner->GetCameraPos();

		Vector2i tileCount = layer.LayoutSize;

		// Get current layer offsets and speeds
		float loX = layer.Description.OffsetX;
		float loY = layer.Description.OffsetY - (layer.Description.UseInherentOffset ? (viewSize.Y - 200) / 2 : 0) + 1;

		// Find out coordinates for a tile from outside the boundaries from topleft corner of the screen 
		float x1 = viewCenter.X - HardcodedOffset - (viewSize.X * 0.5f);
		float y1 = viewCenter.Y - HardcodedOffset - (viewSize.Y * 0.5f);

		if (layer.Description.RendererType >= LayerRendererType::Sky && layer.Description.RendererType <= LayerRendererType::Circle && tileCount.Y == 8 && tileCount.X == 8) {
			constexpr float PerspectiveSpeedX = 0.4f;
			constexpr float PerspectiveSpeedY = 0.16f;
			RenderTexturedBackground(renderQueue, layer, x1 * PerspectiveSpeedX + loX, y1 * PerspectiveSpeedY + loY);
		} else {
			float xt, yt;
			switch (layer.Description.SpeedModelX) {
				case LayerSpeedModel::AlwaysOnTop:
					xt = -HardcodedOffset;
					break;
				case LayerSpeedModel::FitLevel: {
					float progress = (float)viewCenter.X / (_layers[_sprLayerIndex].LayoutSize.X * TileSet::DefaultTileSize);
					xt = std::clamp(progress, 0.0f, 1.0f)
						* ((layer.LayoutSize.X * TileSet::DefaultTileSize) - viewSize.X + HardcodedOffset)
						+ loX;
					break;
				}
				case LayerSpeedModel::SpeedMultipliers: {
					float progress = (float)viewCenter.X / (_layers[_sprLayerIndex].LayoutSize.X * TileSet::DefaultTileSize);
					progress = (layer.Description.SpeedX < layer.Description.AutoSpeedX
						? std::clamp(progress, layer.Description.SpeedX, layer.Description.AutoSpeedX)
						: (layer.Description.SpeedX + layer.Description.AutoSpeedX) * 0.5f);
					xt = progress
						* ((layer.LayoutSize.X * TileSet::DefaultTileSize) - HardcodedOffset)
						+ loX;
					break;
				}
				default:
					xt = TranslateCoordinate(x1, layer.Description.SpeedX, loX, viewSize.X, false);
					break;
			}
			switch (layer.Description.SpeedModelY) {
				case LayerSpeedModel::AlwaysOnTop:
					yt = -HardcodedOffset;
					break;
				case LayerSpeedModel::FitLevel: {
					float progress = (float)viewCenter.Y / (_layers[_sprLayerIndex].LayoutSize.Y * TileSet::DefaultTileSize);
					yt = std::clamp(progress, 0.0f, 1.0f)
						* ((layer.LayoutSize.Y * TileSet::DefaultTileSize) - viewSize.Y + HardcodedOffset)
						+ loY;
					break;
				}
				case LayerSpeedModel::SpeedMultipliers: {
					float progress = (float)viewCenter.Y / (_layers[_sprLayerIndex].LayoutSize.Y * TileSet::DefaultTileSize);
					progress = (layer.Description.SpeedY < layer.Description.AutoSpeedY
						? std::clamp(progress, layer.Description.SpeedY, layer.Description.AutoSpeedY)
						: (layer.Description.SpeedY + layer.Description.AutoSpeedY) * 0.5f);
					yt = progress
						* ((layer.LayoutSize.Y * TileSet::DefaultTileSize) - HardcodedOffset)
						+ loY;
					break;
				}
				default:
					// TODO: Some levels looks better with these adjustments
					/*if (speedY < 1.0f) {
						speedY = powf(speedY, 1.06f);
					} else if (speedY > 1.0f) {
						speedY = powf(speedY, 0.996f);
					}*/

					yt = TranslateCoordinate(y1, layer.Description.SpeedY, loY, viewSize.Y, true);
					break;
			}

			// Calculate the index (on the layer map) of the first tile that needs to be drawn to the position determined earlier
			std::int32_t tileX, tileY, tileAbsX, tileAbsY;

			// Get the actual tile coords on the layer layout
			if (xt > 0) {
				tileAbsX = (std::int32_t)std::floor(xt / (float)TileSet::DefaultTileSize);
				tileX = tileAbsX % tileCount.X;
			} else {
				tileAbsX = (std::int32_t)std::ceil(xt / (float)TileSet::DefaultTileSize);
				tileX = tileAbsX % tileCount.X;
				while (tileX < 0) {
					tileX += tileCount.X;
				}
			}

			if (yt > 0) {
				tileAbsY = (std::int32_t)std::floor(yt / (float)TileSet::DefaultTileSize);
				tileY = tileAbsY % tileCount.Y;
			} else {
				tileAbsY = (std::int32_t)std::ceil(yt / (float)TileSet::DefaultTileSize);
				tileY = tileAbsY % tileCount.Y;
				while (tileY < 0) {
					tileY += tileCount.Y;
				}
			}

			// Update x1 and y1 with the remainder, so that we start at the tile boundary
			// minus 1, because indices are updated in the beginning of the loops
			float remX = fmodf(xt, (float)TileSet::DefaultTileSize);
			float remY = fmodf(yt, (float)TileSet::DefaultTileSize);
			x1 -= remX - (float)TileSet::DefaultTileSize;
			y1 -= remY - (float)TileSet::DefaultTileSize;
			
			// Save the tile Y at the left border so that we can roll back to it at the start of every row iteration
			std::int32_t tileYs = tileY;

			// Calculate the last coordinates we want to draw to
			float x3 = x1 + (TileSet::DefaultTileSize * 2) + viewSize.X;
			float y3 = y1 + (TileSet::DefaultTileSize * 2) + viewSize.Y;

			std::int32_t tile_xo = -1;
			for (float x2 = x1; x2 < x3; x2 += TileSet::DefaultTileSize) {
				tileX = (tileX + 1) % tileCount.X;
				tile_xo++;
				if (!layer.Description.RepeatX) {
					// If the current tile isn't in the first iteration of the layer horizontally, don't draw this column
					if (tileAbsX + tile_xo + 1 < 0 || tileAbsX + tile_xo + 1 >= tileCount.X) {
						continue;
					}
				}
				tileY = tileYs;
				std::int32_t tile_yo = -1;
				for (float y2 = y1; y2 < y3; y2 += TileSet::DefaultTileSize) {
					tileY = (tileY + 1) % tileCount.Y;
					tile_yo++;

					LayerTile tile = layer.Layout[tileX + tileY * layer.LayoutSize.X];

					if (!layer.Description.RepeatY) {
						// If the current tile isn't in the first iteration of the layer vertically, don't draw it
						if (tileAbsY + tile_yo + 1 < 0 || tileAbsY + tile_yo + 1 >= tileCount.Y) {
							continue;
						}
					}

					std::int32_t tileId = ResolveTileID(tile);
					if (tileId == 0 || tile.Alpha == 0) {
						continue;
					}
					TileSet* tileSet = ResolveTileSet(tileId);
					if (tileSet == nullptr) {
						continue;
					}

					auto command = RentRenderCommand(layer.Description.RendererType);
					command->setType(RenderCommand::CommandTypes::TileMap);
					command->material().setBlendingFactors(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

					Vector2i texSize = tileSet->TextureDiffuse->size();
					float texScaleX = TileSet::DefaultTileSize / float(texSize.X);
					float texBiasX = ((tileId % tileSet->TilesPerRow) * (TileSet::DefaultTileSize + 2.0f) + 1.0f) / float(texSize.X);
					float texScaleY = TileSet::DefaultTileSize / float(texSize.Y);
					float texBiasY = ((tileId / tileSet->TilesPerRow) * (TileSet::DefaultTileSize + 2.0f) + 1.0f) / float(texSize.Y);

					// ToDo: Flip normal map somehow
					if ((tile.Flags & LayerTileFlags::FlipX) == LayerTileFlags::FlipX) {
						texBiasX += texScaleX;
						texScaleX *= -1;
					}
					if ((tile.Flags & LayerTileFlags::FlipY) == LayerTileFlags::FlipY) {
						texBiasY += texScaleY;
						texScaleY *= -1;
					}

					auto instanceBlock = command->material().uniformBlock(Material::InstanceBlockName);
					instanceBlock->uniform(Material::TexRectUniformName)->setFloatValue(texScaleX, texBiasX, texScaleY, texBiasY);
					instanceBlock->uniform(Material::SpriteSizeUniformName)->setFloatValue(TileSet::DefaultTileSize, TileSet::DefaultTileSize);

					Vector4f color = layer.Description.Color;
					color.W *= tile.Alpha / 255.0f;
					instanceBlock->uniform(Material::ColorUniformName)->setFloatVector(color.Data());

					float x2r = x2, y2r = y2;
					if (!PreferencesCache::UnalignedViewport) {
						x2r = std::floor(x2r); y2r = std::floor(y2r);
					}

					command->setTransformation(Matrix4x4f::Translation(x2r, y2r, 0.0f));
					command->setLayer(layer.Description.Depth);
					command->material().setTexture(*tileSet->TextureDiffuse);

					renderQueue.addCommand(command);
				}
			}
		}
	}

	float TileMap::TranslateCoordinate(float coordinate, float speed, float offset, std::int32_t viewSize, bool isY)
	{
		// Coordinate: the "vanilla" coordinate of the tile on the layer if the layer was fixed to the sprite layer with same
		// speed and no other options. Think of its position in JCS.
		// Speed: the set layer speed; 1 for anything that moves the same speed as the sprite layer (where the objects live),
		// less than 1 for backgrounds that move slower, more than 1 for foregrounds that move faster
		// Offset: any difference to starting coordinates caused by an inherent automatic speed a layer has

		// `HardcodedOffsetY` (literal 70) is the same as in `DrawLayer`, it's the offscreen offset of the first tile to draw.
		// Don't touch unless absolutely necessary.
		std::int32_t alignment = ((isY ? (viewSize - 200) : (viewSize - 320)) / 2) + HardcodedOffset;
		return (coordinate * speed + offset + alignment * (speed - 1.0f));
	}

	RenderCommand* TileMap::RentRenderCommand(LayerRendererType type)
	{
		RenderCommand* command;
		if (_renderCommandsCount < _renderCommands.size()) {
			command = _renderCommands[_renderCommandsCount].get();
			_renderCommandsCount++;
		} else {
			command = _renderCommands.emplace_back(std::make_unique<RenderCommand>()).get();
			_renderCommandsCount++;
			command->material().setBlendingEnabled(true);
		}

		bool shaderChanged;
		switch (type) {
			case LayerRendererType::Tinted: shaderChanged = command->material().setShader(ContentResolver::Get().GetShader(PrecompiledShader::Tinted)); break;
			default: shaderChanged = command->material().setShaderProgramType(Material::ShaderProgramType::SPRITE); break;
		}
		if (shaderChanged) {
			command->material().reserveUniformsDataMemory();
			command->geometry().setDrawParameters(GL_TRIANGLE_STRIP, 0, 4);

			GLUniformCache* textureUniform = command->material().uniform(Material::TextureUniformName);
			if (textureUniform && textureUniform->intValue(0) != 0) {
				textureUniform->setIntValue(0); // GL_TEXTURE0
			}
		}

		return command;
	}

	void TileMap::AddTileSet(const StringView tileSetPath, std::uint16_t offset, std::uint16_t count, const std::uint8_t* paletteRemapping)
	{
		auto& tileSetPart = _tileSets.emplace_back();
		tileSetPart.Data = ContentResolver::Get().RequestTileSet(tileSetPath, 0, false, paletteRemapping);
		tileSetPart.Offset = offset;
		tileSetPart.Count = count;

		if (tileSetPart.Data == nullptr) {
			LOGE("Cannot load extra tileset \"%s\"", tileSetPath.data());
		}
	}

	void TileMap::ReadLayerConfiguration(Stream& s)
	{
		LayerType layerType = (LayerType)s.ReadValue<std::uint8_t>();
		std::uint16_t layerFlags = s.ReadValue<std::uint16_t>();

		if (layerType == LayerType::Sprite) {
			_sprLayerIndex = (std::int32_t)_layers.size();
		}

		TileMapLayer& newLayer = _layers.emplace_back();

		int32_t width = s.ReadValue<int32_t>();
		int32_t height = s.ReadValue<int32_t>();
		newLayer.LayoutSize = Vector2i(width, height);
		newLayer.Visible = ((layerFlags & 0x08) == 0x08);

		if (layerType != LayerType::Sprite) {
			std::uint8_t combinedSpeedModels = s.ReadValue<std::uint8_t>();
			newLayer.Description.SpeedModelX = (LayerSpeedModel)(combinedSpeedModels & 0x0f);
			newLayer.Description.SpeedModelY = (LayerSpeedModel)((combinedSpeedModels >> 4) & 0x0f);

			newLayer.Description.OffsetX = s.ReadValue<float>();
			newLayer.Description.OffsetY = s.ReadValue<float>();
			newLayer.Description.SpeedX = s.ReadValue<float>();
			newLayer.Description.SpeedY = s.ReadValue<float>();
			newLayer.Description.AutoSpeedX = s.ReadValue<float>();
			newLayer.Description.AutoSpeedY = s.ReadValue<float>();
			newLayer.Description.RepeatX = ((layerFlags & 0x01) == 0x01);
			newLayer.Description.RepeatY = ((layerFlags & 0x02) == 0x02);
			int16_t depth = s.ReadValue<int16_t>();
			newLayer.Description.Depth = (std::uint16_t)(ILevelHandler::MainPlaneZ - depth);
			newLayer.Description.UseInherentOffset = ((layerFlags & 0x04) == 0x04);

			newLayer.Description.RendererType = (LayerRendererType)s.ReadValue<std::uint8_t>();
			std::uint8_t r = s.ReadValue<std::uint8_t>();
			std::uint8_t g = s.ReadValue<std::uint8_t>();
			std::uint8_t b = s.ReadValue<std::uint8_t>();
			std::uint8_t a = s.ReadValue<std::uint8_t>();

			if (newLayer.Description.RendererType == LayerRendererType::Tinted) {
				// TODO: Tinted color is precomputed from palette here
				const std::uint32_t* palettes = ContentResolver::Get().GetPalettes();
				std::uint32_t color = palettes[r];
				newLayer.Description.Color = Vector4f((color & 0x000000ff) / 255.0f, ((color >> 8) & 0x000000ff) / 255.0f, ((color >> 16) & 0x000000ff) / 255.0f, a * ((color >> 24) & 0x000000ff) / (255.0f * 255.0f));
			} else {
				newLayer.Description.Color = Vector4f(r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f);

				if (newLayer.Description.RendererType >= LayerRendererType::Sky) {
					_texturedBackgroundLayer = (std::int32_t)_layers.size() - 1;
				}
			}
		} else {
			newLayer.Description.OffsetX = 0.0f;
			newLayer.Description.OffsetY = 0.0f;
			newLayer.Description.SpeedX = 1.0f;
			newLayer.Description.SpeedY = 1.0f;
			newLayer.Description.AutoSpeedX = 0.0f;
			newLayer.Description.AutoSpeedY = 0.0f;
			newLayer.Description.RepeatX = false;
			newLayer.Description.RepeatY = false;
			newLayer.Description.Depth = (std::uint16_t)(ILevelHandler::MainPlaneZ - 50);
			newLayer.Description.UseInherentOffset = false;
			newLayer.Description.SpeedModelX = LayerSpeedModel::Default;
			newLayer.Description.SpeedModelY = LayerSpeedModel::Default;

			newLayer.Description.RendererType = LayerRendererType::Default;
			newLayer.Description.Color = Vector4f(1.0f, 1.0f, 1.0f, 1.0f);
		}

		newLayer.Layout = std::make_unique<LayerTile[]>(width * height);

		for (std::int32_t i = 0; i < (width * height); i++) {
			std::uint8_t tileFlags = s.ReadValue<std::uint8_t>();
			std::uint16_t tileIdx = s.ReadValue<std::uint16_t>();

			std::uint8_t tileModifier = (std::uint8_t)(tileFlags >> 4);

			LayerTile& tile = newLayer.Layout[i];
			tile.TileID = tileIdx;

			tile.Flags = (LayerTileFlags)(tileFlags & 0x0f);

			if (tileModifier == 1 /*Translucent*/) {
				tile.Alpha = 192;
			} else if (tileModifier == 2 /*Invisible*/) {
				tile.Alpha = 0;
			} else {
				tile.Alpha = 255;
			}
		}
	}

	void TileMap::ReadAnimatedTiles(Stream& s)
	{
		int16_t count = s.ReadValue<int16_t>();

		_animatedTiles.reserve(count);

		for (std::int32_t i = 0; i < count; i++) {
			std::uint8_t frameCount = s.ReadValue<std::uint8_t>();
			if (frameCount == 0) {
				continue;
			}

			AnimatedTile& animTile = _animatedTiles.emplace_back();

			// FrameDuration is multiplied by 16 before saving, so divide it here back
			animTile.FrameDuration = s.ReadValue<std::uint16_t>() / 16.0f;
			animTile.Delay = s.ReadValue<std::uint16_t>();
			animTile.DelayJitter = s.ReadValue<std::uint16_t>();

			animTile.IsPingPong = s.ReadValue<std::uint8_t>();
			animTile.PingPongDelay = s.ReadValue<std::uint16_t>();

			for (std::int32_t j = 0; j < frameCount; j++) {
				auto& frame = animTile.Tiles.emplace_back();
				// TODO: flags
				/*std::uint8_t flag =*/ s.ReadValue<std::uint8_t>();
				frame.TileID = s.ReadValue<std::uint16_t>();
			}
		}
	}

	void TileMap::SetTileEventFlags(std::int32_t x, std::int32_t y, EventType tileEvent, std::uint8_t* tileParams)
	{
		auto& tile = _layers[_sprLayerIndex].Layout[x + y * _layers[_sprLayerIndex].LayoutSize.X];

		switch (tileEvent) {
			case EventType::ModifierOneWay:
				tile.Flags |= LayerTileFlags::OneWay;
				break;
			case EventType::ModifierVine:
				tile.HasSuspendType = SuspendType::Vine;
				break;
			case EventType::ModifierHook:
				tile.HasSuspendType = SuspendType::Hook;
				break;
			case EventType::ModifierHurt:
				tile.Flags |= LayerTileFlags::Hurt;
				break;
			case EventType::SceneryDestruct:
				SetTileDestructibleEventParams(tile, TileDestructType::Weapon, tileParams[0] | (tileParams[1] << 8));
				break;
			case EventType::SceneryDestructButtstomp:
				SetTileDestructibleEventParams(tile, TileDestructType::Special, tileParams[0]);
				break;
			case EventType::TriggerArea:
				SetTileDestructibleEventParams(tile, TileDestructType::Trigger, tileParams[0]);
				break;
			case EventType::SceneryDestructSpeed:
				SetTileDestructibleEventParams(tile, TileDestructType::Speed, tileParams[0]);
				break;
			case EventType::SceneryCollapse:
				// TODO: Framerate (tileParams[1]) not used
				SetTileDestructibleEventParams(tile, TileDestructType::Collapse, tileParams[0]);
				break;
		}
	}

	void TileMap::SetTileDestructibleEventParams(LayerTile& tile, TileDestructType type, std::uint16_t tileParams)
	{
		if ((tile.Flags & LayerTileFlags::Animated) != LayerTileFlags::Animated) {
			return;
		}

		tile.DestructType = type;
		tile.Flags &= ~LayerTileFlags::Animated;
		tile.DestructAnimation = tile.TileID;
		tile.TileID = _animatedTiles[tile.DestructAnimation].Tiles[0].TileID;
		tile.TileParams = tileParams;
		tile.DestructFrameIndex = 0;
	}

	void TileMap::CreateDebris(const DestructibleDebris& debris)
	{
		auto& spriteLayer = _layers[_sprLayerIndex];
		if ((debris.Flags & DebrisFlags::Disappear) == DebrisFlags::Disappear && debris.Depth <= spriteLayer.Description.Depth) {
			std::int32_t x = (std::int32_t)debris.Pos.X / TileSet::DefaultTileSize;
			std::int32_t y = (std::int32_t)debris.Pos.Y / TileSet::DefaultTileSize;
			if (x < 0 || y < 0 || x >= spriteLayer.LayoutSize.X || y >= spriteLayer.LayoutSize.Y) {
				return;
			}

			std::int32_t tileId = ResolveTileID(spriteLayer.Layout[x + y * spriteLayer.LayoutSize.X]);
			TileSet* tileSet = ResolveTileSet(tileId);
			if (tileSet != nullptr) {
				if (tileSet->IsTileFilled(tileId)) {
					return;
				}

				if (_sprLayerIndex + 1 < _layers.size() && _layers[_sprLayerIndex + 1].Description.SpeedX == 1.0f && _layers[_sprLayerIndex + 1].Description.SpeedY == 1.0f) {
					tileId = ResolveTileID(_layers[_sprLayerIndex + 1].Layout[x + y * spriteLayer.LayoutSize.X]);
					if (tileSet->IsTileFilled(tileId)) {
						return;
					}
				}
			}
		}

		_debrisList.push_back(debris);
	}

	void TileMap::CreateTileDebris(std::int32_t tileId, std::int32_t x, std::int32_t y)
	{
		static const float SpeedMultiplier[] = { -2, 2, -1, 1 };
		constexpr std::int32_t QuarterSize = TileSet::DefaultTileSize / 2;

		// Tile #0 is always empty
		if (tileId == 0) {
			return;
		}

		TileSet* tileSet = ResolveTileSet(tileId);
		if (tileSet == nullptr) {
			return;
		}

		std::uint16_t z = _layers[_sprLayerIndex].Description.Depth + 80;

		Vector2i texSize = tileSet->TextureDiffuse->size();
		float texScaleX = float(QuarterSize) / float(texSize.X);
		float texBiasX = ((tileId % tileSet->TilesPerRow) * (TileSet::DefaultTileSize + 2.0f) + 1.0f) / float(texSize.X);
		float texScaleY = float(QuarterSize) / float(texSize.Y);
		float texBiasY = ((tileId / tileSet->TilesPerRow) * (TileSet::DefaultTileSize + 2.0f) + 1.0f) / float(texSize.Y);

		// TODO: Implement flip here
		/*if (isFlippedX) {
			texBiasX += texScaleX;
			texScaleX *= -1;
		}
		if (isFlippedY) {
			texBiasY += texScaleY;
			texScaleY *= -1;
		}*/

		for (std::int32_t i = 0; i < 4; i++) {
			DestructibleDebris& debris = _debrisList.emplace_back();
			debris.Pos = Vector2f(x * TileSet::DefaultTileSize + (i % 2) * QuarterSize, y * TileSet::DefaultTileSize + (i / 2) * QuarterSize);
			debris.Depth = z;
			debris.Size = Vector2f(QuarterSize, QuarterSize);
			debris.Speed = Vector2f(SpeedMultiplier[i] * Random().FastFloat(0.8f, 1.2f), -4.0f * Random().FastFloat(0.8f, 1.2f));
			debris.Acceleration = Vector2f(0.0f, 0.3f);

			debris.Scale = 1.0f;
			debris.ScaleSpeed = Random().FastFloat(-0.01f, -0.002f);
			debris.Angle = 0.0f;
			debris.AngleSpeed = SpeedMultiplier[i] * Random().FastFloat(0.0f, 0.014f);

			debris.Alpha = 1.0f;
			debris.AlphaSpeed = -0.01f;

			debris.Time = 120.0f;

			debris.TexScaleX = texScaleX;
			debris.TexBiasX = texBiasX + ((i % 2) * QuarterSize / float(texSize.X));
			debris.TexScaleY = texScaleY;
			debris.TexBiasY = texBiasY + ((i / 2) * QuarterSize / float(texSize.Y));

			debris.DiffuseTexture = tileSet->TextureDiffuse.get();
			debris.Flags = DebrisFlags::None;
		}
	}

	void TileMap::CreateParticleDebris(const GraphicResource* res, Vector3f pos, Vector2f force, std::int32_t currentFrame, bool isFacingLeft)
	{
		constexpr std::int32_t DebrisSize = 3;

		float x = pos.X - res->Base->Hotspot.X;
		float y = pos.Y - res->Base->Hotspot.Y;
		Vector2i texSize = res->Base->TextureDiffuse->size();

		for (std::int32_t fy = 0; fy < res->Base->FrameDimensions.Y; fy += DebrisSize + 1) {
			for (std::int32_t fx = 0; fx < res->Base->FrameDimensions.X; fx += DebrisSize + 1) {
				float currentSize = DebrisSize * Random().FastFloat(0.2f, 1.1f);

				DestructibleDebris& debris = _debrisList.emplace_back();
				debris.Pos = Vector2f(x + (isFacingLeft ? res->Base->FrameDimensions.X - fx : fx), y + fy);
				debris.Depth = (std::uint16_t)pos.Z;
				debris.Size = Vector2f(currentSize, currentSize);
				debris.Speed = Vector2f(force.X + ((fx - res->Base->FrameDimensions.X / 2) + Random().FastFloat(-2.0f, 2.0f)) * (isFacingLeft ? -1.0f : 1.0f) * Random().FastFloat(2.0f, 8.0f) / res->Base->FrameDimensions.X,
						force.Y - 1.0f * Random().FastFloat(2.2f, 4.0f));
				debris.Acceleration = Vector2f(0.0f, 0.2f);

				debris.Scale = 1.0f;
				debris.ScaleSpeed = 0.0f;
				debris.Angle = 0.0f;
				debris.AngleSpeed = 0.0f;

				debris.Alpha = 1.0f;
				debris.AlphaSpeed = -0.002f;

				debris.Time = 320.0f;

				debris.TexScaleX = (currentSize / float(texSize.X));
				debris.TexBiasX = (((float)(currentFrame % res->Base->FrameConfiguration.X) / res->Base->FrameConfiguration.X) + ((float)fx / float(texSize.X)));
				debris.TexScaleY = (currentSize / float(texSize.Y));
				debris.TexBiasY = (((float)(currentFrame / res->Base->FrameConfiguration.X) / res->Base->FrameConfiguration.Y) + ((float)fy / float(texSize.Y)));

				debris.DiffuseTexture = res->Base->TextureDiffuse.get();
				debris.Flags = DebrisFlags::Bounce;
			}
		}
	}

	void TileMap::CreateSpriteDebris(const GraphicResource* res, Vector3f pos, std::int32_t count)
	{
		float x = pos.X - res->Base->Hotspot.X;
		float y = pos.Y - res->Base->Hotspot.Y;
		Vector2i texSize = res->Base->TextureDiffuse->size();

		for (std::int32_t i = 0; i < count; i++) {
			float speedX = Random().FastFloat(-1.0f, 1.0f) * Random().FastFloat(0.2f, 0.8f) * count;

			DestructibleDebris& debris = _debrisList.emplace_back();
			debris.Pos = Vector2f(x, y);
			debris.Depth = (std::uint16_t)pos.Z;
			debris.Size = Vector2f((float)res->Base->FrameDimensions.X, (float)res->Base->FrameDimensions.Y);
			debris.Speed = Vector2f(speedX, -1.0f * Random().FastFloat(2.2f, 4.0f));
			debris.Acceleration = Vector2f(0.0f, 0.2f);

			debris.Scale = 1.0f;
			debris.ScaleSpeed = -0.002f;
			debris.Angle = Random().FastFloat(0.0f, fTwoPi);
			debris.AngleSpeed = speedX * 0.02f;

			debris.Alpha = 1.0f;
			debris.AlphaSpeed = -0.002f;

			debris.Time = 560.0f;

			std::int32_t curAnimFrame = res->FrameOffset + Random().Next(0, res->FrameCount);
			std::int32_t col = curAnimFrame % res->Base->FrameConfiguration.X;
			std::int32_t row = curAnimFrame / res->Base->FrameConfiguration.X;
			debris.TexScaleX = (float(res->Base->FrameDimensions.X) / float(texSize.X));
			debris.TexBiasX = (float(res->Base->FrameDimensions.X * col) / float(texSize.X));
			debris.TexScaleY = (float(res->Base->FrameDimensions.Y) / float(texSize.Y));
			debris.TexBiasY = (float(res->Base->FrameDimensions.Y * row) / float(texSize.Y));

			debris.DiffuseTexture = res->Base->TextureDiffuse.get();
			debris.Flags = DebrisFlags::Bounce;
		}
	}

	void TileMap::UpdateDebris(float timeMult)
	{
		ZoneScopedC(0xA09359);

		std::int32_t size = (std::int32_t)_debrisList.size();
		for (std::int32_t i = 0; i < size; i++) {
			DestructibleDebris& debris = _debrisList[i];

			if (debris.Scale <= 0.0f || debris.Alpha <= 0.0f) {
				std::swap(debris, _debrisList[size - 1]);
				_debrisList.pop_back();
				i--;
				size--;
				continue;
			}

			debris.Time -= timeMult;
			if (debris.Time <= 0.0f) {
				debris.AlphaSpeed = -std::min(0.02f, debris.Alpha);
			}

			if ((debris.Flags & (DebrisFlags::Disappear | DebrisFlags::Bounce)) != DebrisFlags::None) {
				// Debris should collide with tilemap
				float nx = debris.Pos.X + debris.Speed.X * timeMult;
				float ny = debris.Pos.Y + debris.Speed.Y * timeMult;
				AABB aabb = AABBf(nx - 1, ny - 1, nx + 1, ny + 1);
				TileCollisionParams params = { TileDestructType::None, true };
				if (IsTileEmpty(aabb, params)) {
					// Nothing...
				} else if ((debris.Flags & DebrisFlags::Disappear) == DebrisFlags::Disappear) {
					debris.ScaleSpeed = -0.02f;
					debris.AlphaSpeed = -0.006f;
					debris.Speed = Vector2f::Zero;
					debris.Acceleration = Vector2f::Zero;
				} else {
					// Place us to the ground only if no horizontal movement was
					// involved (this prevents speeds resetting if the actor
					// collides with a wall from the side while in the air)
					aabb.T = debris.Pos.Y - 1;
					aabb.B = debris.Pos.Y + 1;

					if (IsTileEmpty(aabb, params)) {
						if (debris.Speed.Y > 0.0f) {
							debris.Speed.Y = -(0.8f/*elasticity*/ * debris.Speed.Y);
							//OnHitFloorHook();
						} else {
							debris.Speed.Y = 0;
							//OnHitCeilingHook();
						}
					}

					// If the actor didn't move all the way horizontally,
					// it hit a wall (or was already touching it)
					aabb = AABBf(debris.Pos.X - 1, ny - 1, debris.Pos.X + 1, ny + 1);
					if (IsTileEmpty(aabb, params)) {
						debris.Speed.X = -(0.8f/*elasticity*/ * debris.Speed.X);
						debris.AngleSpeed = -(0.8f/*elasticity*/ * debris.AngleSpeed);
						//OnHitWallHook();
					}
				}
			}

			debris.Pos.X += debris.Speed.X * timeMult + 0.5f * debris.Acceleration.X * timeMult * timeMult;
			debris.Pos.Y += debris.Speed.Y * timeMult + 0.5f * debris.Acceleration.Y * timeMult * timeMult;

			if (debris.Acceleration.X != 0.0f) {
				debris.Speed.X = std::min(debris.Speed.X + debris.Acceleration.X * timeMult, 10.0f);
			}
			if (debris.Acceleration.Y != 0.0f) {
				debris.Speed.Y = std::min(debris.Speed.Y + debris.Acceleration.Y * timeMult, 10.0f);
			}

			debris.Scale += debris.ScaleSpeed * timeMult;
			debris.Angle += debris.AngleSpeed * timeMult;
			debris.Alpha += debris.AlphaSpeed * timeMult;
		}
	}

	void TileMap::DrawDebris(RenderQueue& renderQueue)
	{
		ZoneScopedNC("Debris", 0xA09359);

		constexpr float MaxDebrisSize = 128.0f;

		Rectf viewportRect = RenderResources::currentViewport()->cullingRect();
		viewportRect.X -= MaxDebrisSize;
		viewportRect.Y -= MaxDebrisSize;
		viewportRect.W += MaxDebrisSize * 2.0f;
		viewportRect.H += MaxDebrisSize * 2.0f;

		for (const auto& debris : _debrisList) {
			if (!viewportRect.Contains(debris.Pos)) {
				continue;
			}

			auto command = RentRenderCommand(LayerRendererType::Default);
			command->setType(RenderCommand::CommandTypes::Particle);

			if ((debris.Flags & DebrisFlags::AdditiveBlending) == DebrisFlags::AdditiveBlending) {
				command->material().setBlendingFactors(GL_SRC_ALPHA, GL_ONE);
			} else {
				command->material().setBlendingFactors(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			}

			auto instanceBlock = command->material().uniformBlock(Material::InstanceBlockName);
			instanceBlock->uniform(Material::TexRectUniformName)->setFloatValue(debris.TexScaleX, debris.TexBiasX, debris.TexScaleY, debris.TexBiasY);
			instanceBlock->uniform(Material::SpriteSizeUniformName)->setFloatValue(debris.Size.X, debris.Size.Y);
			instanceBlock->uniform(Material::ColorUniformName)->setFloatVector(Colorf(1.0f, 1.0f, 1.0f, debris.Alpha).Data());

			Matrix4x4f worldMatrix = Matrix4x4f::Translation(debris.Pos.X, debris.Pos.Y, 0.0f);
			worldMatrix.RotateZ(debris.Angle);
			worldMatrix.Scale(debris.Scale, debris.Scale, 1.0f);
			worldMatrix.Translate(debris.Size.X  * -0.5f, debris.Size.Y * -0.5f, 0.0f);
			command->setTransformation(worldMatrix);
			command->setLayer(debris.Depth);
			command->material().setTexture(*debris.DiffuseTexture);

			renderQueue.addCommand(command);
		}
	}

	bool TileMap::GetTrigger(std::uint8_t triggerId)
	{
		return _triggerState[triggerId];
	}

	void TileMap::SetTrigger(std::uint8_t triggerId, bool newState)
	{
		if (_triggerState[triggerId] == newState) {
			return;
		}

		_triggerState.Set(triggerId, newState);

		// Go through all tiles and update any that are influenced by this trigger
		Vector2i layoutSize = _layers[_sprLayerIndex].LayoutSize;
		std::int32_t n = layoutSize.X * layoutSize.Y;
		for (std::int32_t i = 0; i < n; i++) {
			LayerTile& tile = _layers[_sprLayerIndex].Layout[i];
			if (tile.DestructType == TileDestructType::Trigger && tile.TileParams == triggerId) {
				if (_animatedTiles[tile.DestructAnimation].Tiles.size() > 1) {
					tile.DestructFrameIndex = (newState ? 1 : 0);
					tile.TileID = _animatedTiles[tile.DestructAnimation].Tiles[tile.DestructFrameIndex].TileID;
				}
			}
		}
	}

	void TileMap::InitializeFromStream(Stream& src)
	{
		std::int32_t layoutSize = src.ReadVariableInt32();
		if (layoutSize == -1) {
			return;
		}

		RETURN_ASSERT_MSG(_sprLayerIndex != -1, "Sprite layer not defined");
		
		auto& spriteLayer = _layers[_sprLayerIndex];
		std::int32_t realLayoutSize = spriteLayer.LayoutSize.X * spriteLayer.LayoutSize.Y;
		RETURN_ASSERT_MSG(layoutSize == realLayoutSize, "Layout size mismatch");

		for (std::int32_t i = 0; i < layoutSize; i++) {
			auto& tile = spriteLayer.Layout[i];
			tile.DestructFrameIndex = src.ReadVariableInt32();
			if (tile.DestructFrameIndex > 0) {
				auto& anim = _animatedTiles[tile.DestructAnimation];
				std::int32_t max = (std::int32_t)(anim.Tiles.size() - 2);
				if (tile.DestructFrameIndex > max) {
					LOGW("Serialized tile %i with animation frame %i is out of range", i, tile.DestructFrameIndex);
					tile.DestructFrameIndex = max;
				}
				tile.TileID = anim.Tiles[tile.DestructFrameIndex].TileID;
			}
		}

		src.Read(_triggerState.RawData(), _triggerState.SizeInBytes());
	}

	void TileMap::SerializeResumableToStream(Stream& dest)
	{
		if (_sprLayerIndex == -1) {
			dest.WriteValue<std::int32_t>(-1);
			return;
		}

		auto& spriteLayer = _layers[_sprLayerIndex];
		std::int32_t layoutSize = spriteLayer.LayoutSize.X * spriteLayer.LayoutSize.Y;
		dest.WriteVariableInt32(layoutSize);
		for (std::int32_t i = 0; i < layoutSize; i++) {
			dest.WriteVariableInt32(spriteLayer.Layout[i].DestructFrameIndex);
		}

		dest.Write(_triggerState.RawData(), _triggerState.SizeInBytes());
	}

	void TileMap::RenderTexturedBackground(RenderQueue& renderQueue, TileMapLayer& layer, float x, float y)
	{
		auto target = _texturedBackgroundPass._target.get();
		if (target == nullptr) {
			return;
		}

		Vector2i viewSize = _owner->GetViewSize();
		Vector2f viewCenter = _owner->GetCameraPos();

		auto* command = &_texturedBackgroundPass._outputRenderCommand;

		auto* instanceBlock = command->material().uniformBlock(Material::InstanceBlockName);
		instanceBlock->uniform(Material::TexRectUniformName)->setFloatValue(1.0f, 0.0f, 1.0f, 0.0f);
		instanceBlock->uniform(Material::SpriteSizeUniformName)->setFloatValue((float)viewSize.X, (float)viewSize.Y);
		instanceBlock->uniform(Material::ColorUniformName)->setFloatVector(Colorf(1.0f, 1.0f, 1.0f, 1.0f).Data());

		command->material().uniform("uViewSize")->setFloatValue((float)viewSize.X, (float)viewSize.Y);
		command->material().uniform("uCameraPos")->setFloatVector(viewCenter.Data());
		command->material().uniform("uShift")->setFloatValue(x, y);
		command->material().uniform("uHorizonColor")->setFloatVector(layer.Description.Color.Data());

		command->setTransformation(Matrix4x4f::Translation(viewCenter.X - viewSize.X * 0.5f, viewCenter.Y - viewSize.Y * 0.5f, 0.0f));
		command->setLayer(layer.Description.Depth);
		command->material().setTexture(*target);

		renderQueue.addCommand(command);
	}

	void TileMap::OnInitializeViewport()
	{
		if (_texturedBackgroundLayer != -1) {
			_texturedBackgroundPass.Initialize();
		}
	}

	TileSet* TileMap::ResolveTileSet(std::int32_t& tileId)
	{
		for (auto& tileSetPart : _tileSets) {
			if (tileId < tileSetPart.Count) {
				tileId += tileSetPart.Offset;
				return tileSetPart.Data.get();
			}

			tileId -= tileSetPart.Count;
		}

		return nullptr;
	}

	std::int32_t TileMap::ResolveTileID(LayerTile& tile)
	{
		std::int32_t tileId = tile.TileID;
		if ((tile.Flags & LayerTileFlags::Animated) == LayerTileFlags::Animated) {
			if (tileId >= (std::int32_t)_animatedTiles.size()) {
				return 0;
			}
			auto& animTile = _animatedTiles[tileId];
			tileId = animTile.Tiles[animTile.CurrentTileIdx].TileID;
		}

		return tileId;
	}

	void TileMap::TexturedBackgroundPass::Initialize()
	{
		bool notInitialized = (_view == nullptr);

		if (notInitialized) {
			Vector2i layoutSize = _owner->_layers[_owner->_texturedBackgroundLayer].LayoutSize;
			std::int32_t width = layoutSize.X * TileSet::DefaultTileSize;
			std::int32_t height = layoutSize.Y * TileSet::DefaultTileSize;

			_camera = std::make_unique<Camera>();
			_camera->setOrthoProjection(0.0f, (float)width, 0.0f, (float)height);
			_camera->setView(0, 0, 0, 1);
			_target = std::make_unique<Texture>(nullptr, Texture::Format::RGB8, width, height);
			_view = std::make_unique<Viewport>(_target.get(), Viewport::DepthStencilFormat::None);
			_view->setRootNode(this);
			_view->setCamera(_camera.get());
			//_view->setClearMode(Viewport::ClearMode::Never);
			_target->setMagFiltering(SamplerFilter::Linear);
			_target->setWrap(SamplerWrapping::Repeat);

			// Prepare render commands
			std::int32_t renderCommandCount = (width * height) / (TileSet::DefaultTileSize * TileSet::DefaultTileSize);
			_renderCommands.reserve(renderCommandCount);
			for (std::int32_t i = 0; i < renderCommandCount; i++) {
				std::unique_ptr<RenderCommand>& command = _renderCommands.emplace_back(std::make_unique<RenderCommand>());
				command->material().setShaderProgramType(Material::ShaderProgramType::SPRITE);
				command->material().reserveUniformsDataMemory();
				command->geometry().setDrawParameters(GL_TRIANGLE_STRIP, 0, 4);

				GLUniformCache* textureUniform = command->material().uniform(Material::TextureUniformName);
				if (textureUniform && textureUniform->intValue(0) != 0) {
					textureUniform->setIntValue(0); // GL_TEXTURE0
				}
			}

			// Prepare output render command
			_outputRenderCommand.material().setShader(ContentResolver::Get().GetShader(_owner->_layers[_owner->_texturedBackgroundLayer].Description.RendererType == LayerRendererType::Circle
				? PrecompiledShader::TexturedBackgroundCircle
				: PrecompiledShader::TexturedBackground));
			_outputRenderCommand.material().reserveUniformsDataMemory();
			_outputRenderCommand.geometry().setDrawParameters(GL_TRIANGLE_STRIP, 0, 4);

			GLUniformCache* textureUniform = _outputRenderCommand.material().uniform(Material::TextureUniformName);
			if (textureUniform && textureUniform->intValue(0) != 0) {
				textureUniform->setIntValue(0); // GL_TEXTURE0
			}
		}

		Viewport::chain().push_back(_view.get());
	}

	bool TileMap::TexturedBackgroundPass::OnDraw(RenderQueue& renderQueue)
	{
		TileMapLayer& layer = _owner->_layers[_owner->_texturedBackgroundLayer];
		Vector2i layoutSize = layer.LayoutSize;

		std::int32_t renderCommandIndex = 0;
		bool isAnimated = false;

		for (std::int32_t y = 0; y < layoutSize.Y; y++) {
			for (std::int32_t x = 0; x < layoutSize.X; x++) {
				LayerTile& tile = layer.Layout[x + y * layer.LayoutSize.X];

				std::int32_t tileId = _owner->ResolveTileID(tile);
				if (tileId == 0) {
					continue;
				}
				TileSet* tileSet = _owner->ResolveTileSet(tileId);
				if (tileSet == nullptr) {
					continue;
				}

				auto command = _renderCommands[renderCommandIndex++].get();

				Vector2i texSize = tileSet->TextureDiffuse->size();
				float texScaleX = TileSet::DefaultTileSize / float(texSize.X);
				float texBiasX = ((tileId % tileSet->TilesPerRow) * (TileSet::DefaultTileSize + 2.0f) + 1.0f) / float(texSize.X);
				float texScaleY = TileSet::DefaultTileSize / float(texSize.Y);
				float texBiasY = ((tileId / tileSet->TilesPerRow) * (TileSet::DefaultTileSize + 2.0f) + 1.0f) / float(texSize.Y);

				// TODO: Flip normal map somehow
				if ((tile.Flags & LayerTileFlags::FlipX) == LayerTileFlags::FlipX) {
					texBiasX += texScaleX;
					texScaleX *= -1;
				}
				if ((tile.Flags & LayerTileFlags::FlipY) == LayerTileFlags::FlipY) {
					texBiasY += texScaleY;
					texScaleY *= -1;
				}

				auto instanceBlock = command->material().uniformBlock(Material::InstanceBlockName);
				instanceBlock->uniform(Material::TexRectUniformName)->setFloatValue(texScaleX, texBiasX, texScaleY, texBiasY);
				instanceBlock->uniform(Material::SpriteSizeUniformName)->setFloatValue(TileSet::DefaultTileSize, TileSet::DefaultTileSize);
				instanceBlock->uniform(Material::ColorUniformName)->setFloatVector(Colorf::White.Data());

				command->setTransformation(Matrix4x4f::Translation(x * TileSet::DefaultTileSize, y * TileSet::DefaultTileSize, 0.0f));
				command->material().setTexture(*tileSet->TextureDiffuse);

				renderQueue.addCommand(command);
			}
		}

		if (!isAnimated && _alreadyRendered) {
			// If it's not animated, it can be rendered only once
			for (std::int32_t i = Viewport::chain().size() - 1; i >= 0; i--) {
				auto& item = Viewport::chain()[i];
				if (item == _view.get()) {
					Viewport::chain().erase(&item);
					break;
				}
			}
		}

		_alreadyRendered = true;
		return true;
	}
}