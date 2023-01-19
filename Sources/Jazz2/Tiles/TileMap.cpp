#include "TileMap.h"

#include "../LevelHandler.h"
#include "../Actors/Environment/IceBlock.h"

#include "../../nCine/Graphics/RenderQueue.h"
#include "../../nCine/IO/IFileStream.h"
#include "../../nCine/Base/Random.h"

namespace Jazz2::Tiles
{
	TileMap::TileMap(LevelHandler* levelHandler, const StringView& tileSetPath, uint16_t captionTileId, PitType pitType, bool applyPalette)
		:
		_levelHandler(levelHandler),
		_sprLayerIndex(-1),
		_pitType(pitType),
		_renderCommandsCount(0),
		_collapsingTimer(0.0f),
		_triggerState(TriggerCount),
		_texturedBackgroundLayer(-1),
		_texturedBackgroundPass(this)
	{
		auto& tileSetPart = _tileSets.emplace_back();
		tileSetPart.Data = ContentResolver::Get().RequestTileSet(tileSetPath, captionTileId, applyPalette);
		tileSetPart.Offset = 0;
		tileSetPart.Count = tileSetPart.Data->TileCount;

		_renderCommands.reserve(128);

		if (tileSetPart.Data == nullptr) {
			LOGE_X("Cannot load main tileset \"%s\"", tileSetPath.data());
		}
	}

	Vector2i TileMap::Size()
	{
		if (_sprLayerIndex == -1) {
			return Vector2i();
		}

		return _layers[_sprLayerIndex].LayoutSize;
	}

	Vector2i TileMap::LevelBounds()
	{
		if (_sprLayerIndex == -1) {
			return Vector2i();
		}

		Vector2i layoutSize = _layers[_sprLayerIndex].LayoutSize;
		return Vector2i(layoutSize.X * TileSet::DefaultTileSize, layoutSize.Y * TileSet::DefaultTileSize);
	}

	void TileMap::OnUpdate(float timeMult)
	{
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
						if (animTile.PingPong) {
							animTile.Forwards = false;
							animTile.FramesLeft += (animTile.FrameDuration * (1 + animTile.PingPongDelay));
						} else {
							animTile.CurrentTileIdx = 0;
							animTile.FramesLeft += (animTile.FrameDuration * (1 + animTile.Delay));
						}
					} else {
						animTile.CurrentTileIdx++;
						animTile.FramesLeft += animTile.FrameDuration;
					}
				} else {
					if (animTile.CurrentTileIdx == 0) {
						// Reverse only occurs on ping pong mode so no need to check for that here
						animTile.Forwards = true;
						animTile.FramesLeft += (animTile.FrameDuration * (1 + animTile.Delay));
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
		SceneNode::OnDraw(renderQueue);

		_renderCommandsCount = 0;

		for (auto& layer : _layers) {
			DrawLayer(renderQueue, layer);
		}

		DrawDebris(renderQueue);

		return true;
	}

	bool TileMap::IsTileEmpty(int tx, int ty)
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
		int tileId = ResolveTileID(tile);
		TileSet* tileSet = ResolveTileSet(tileId);
		return (tileSet == nullptr || tileSet->IsTileMaskEmpty(tileId));
	}

	bool TileMap::IsTileEmpty(const AABBf& aabb, TileCollisionParams& params)
	{
		if (_sprLayerIndex == -1) {
			return true;
		}

		Vector2i layoutSize = _layers[_sprLayerIndex].LayoutSize;

		int limitRightPx = layoutSize.X * TileSet::DefaultTileSize;
		int limitBottomPx = layoutSize.Y * TileSet::DefaultTileSize;

		// Consider out-of-level coordinates as solid walls
		if (aabb.L < 0 || aabb.R >= limitRightPx) {
			return false;
		}
		if (aabb.B >= limitBottomPx) {
			return (_pitType != PitType::StandOnPlatform);
		}

		// Check all covered tiles for collisions; if all are empty, no need to do pixel collision checking
		int hx1 = std::max((int)aabb.L, 0);
		int hx2 = std::min((int)std::ceil(aabb.R), limitRightPx - 1);
		int hy1 = std::max((int)aabb.T, 0);
		int hy2 = std::min((int)std::ceil(aabb.B), limitBottomPx - 1);

		if (hy2 <= 0) {
			hy1 = 0;
			hy2 = 1;
		}

		int hx1t = hx1 / TileSet::DefaultTileSize;
		int hx2t = hx2 / TileSet::DefaultTileSize;
		int hy1t = hy1 / TileSet::DefaultTileSize;
		int hy2t = hy2 / TileSet::DefaultTileSize;

		auto sprLayerLayout = _layers[_sprLayerIndex].Layout.get();

		for (int y = hy1t; y <= hy2t; y++) {
			for (int x = hx1t; x <= hx2t; x++) {
			RecheckTile:
				LayerTile& tile = sprLayerLayout[y * layoutSize.X + x];

				if (tile.DestructType == TileDestructType::Weapon && (params.DestructType & TileDestructType::Weapon) == TileDestructType::Weapon) {
					if ((tile.TileParams & (1 << (uint16_t)params.UsedWeaponType)) != 0) {
						if (AdvanceDestructibleTileAnimation(tile, x, y, params.WeaponStrength, "SceneryDestruct"_s)) {
							params.TilesDestroyed++;
							if (params.WeaponStrength <= 0) {
								return false;
							} else {
								goto RecheckTile;
							}
						}
					} else if (params.UsedWeaponType == WeaponType::Freezer && tile.DestructFrameIndex < (_animatedTiles[tile.DestructAnimation].Tiles.size() - 2)) {
						int tx = x * TileSet::DefaultTileSize + TileSet::DefaultTileSize / 2;
						int ty = y * TileSet::DefaultTileSize + TileSet::DefaultTileSize / 2;

						bool iceBlockFound = false;
						_levelHandler->FindCollisionActorsByAABB(nullptr, AABBf(tx - 1.0f, ty - 1.0f, tx + 1.0f, ty + 1.0f), [&iceBlockFound](Actors::ActorBase* actor) -> bool {
							if ((actor->GetState() & Actors::ActorState::IsDestroyed) != Actors::ActorState::None) {
								return true;
							}

							Actors::Environment::IceBlock* iceBlock = dynamic_cast<Actors::Environment::IceBlock*>(actor);
							if (iceBlock != nullptr) {
								iceBlock->ResetTimeLeft();
								iceBlockFound = true;
								return false;
							}

							return true;
						});

						if (!iceBlockFound) {
							std::shared_ptr<Actors::Environment::IceBlock> iceBlock = std::make_shared<Actors::Environment::IceBlock>();
							iceBlock->OnActivated({
								.LevelHandler = _levelHandler,
								.Pos = Vector3i(tx - 1, ty - 2, ILevelHandler::MainPlaneZ)
							});
							_levelHandler->AddActor(iceBlock);
						}
						return false;
					}
				} else if (tile.DestructType == TileDestructType::Special && (params.DestructType & TileDestructType::Special) == TileDestructType::Special) {
					int amount = 1;
					if (AdvanceDestructibleTileAnimation(tile, x, y, amount, "SceneryDestruct"_s)) {
						params.TilesDestroyed++;
						goto RecheckTile;
					}
				} else if (tile.DestructType == TileDestructType::Speed && (params.DestructType & TileDestructType::Speed) == TileDestructType::Speed) {
					int amount = 1;
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
					int tileId = ResolveTileID(tile);
					TileSet* tileSet = ResolveTileSet(tileId);
					if (tileSet == nullptr || tileSet->IsTileMaskEmpty(tileId)) {
						continue;
					}

					int tx = x * TileSet::DefaultTileSize;
					int ty = y * TileSet::DefaultTileSize;

					int left = std::max(hx1 - tx, 0);
					int right = std::min(hx2 - tx, TileSet::DefaultTileSize - 1);
					int top = std::max(hy1 - ty, 0);
					int bottom = std::min(hy2 - ty, TileSet::DefaultTileSize - 1);

					if ((tile.Flags & LayerTileFlags::FlipX) == LayerTileFlags::FlipX) {
						int left2 = left;
						left = (TileSet::DefaultTileSize - 1 - right);
						right = (TileSet::DefaultTileSize - 1 - left2);
					}
					if ((tile.Flags & LayerTileFlags::FlipY) == LayerTileFlags::FlipY) {
						int top2 = top;
						top = (TileSet::DefaultTileSize - 1 - bottom);
						bottom = (TileSet::DefaultTileSize - 1 - top2);
					}

					top *= TileSet::DefaultTileSize;
					bottom *= TileSet::DefaultTileSize;

					uint8_t* mask = tileSet->GetTileMask(tileId);
					for (int ry = top; ry <= bottom; ry += TileSet::DefaultTileSize) {
						for (int rx = left; rx <= right; rx++) {
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

		int limitRightPx = layoutSize.X * TileSet::DefaultTileSize;
		int limitBottomPx = layoutSize.Y * TileSet::DefaultTileSize;

		// Consider out-of-level coordinates as solid walls
		if (aabb.L < 0 || aabb.R >= limitRightPx) {
			return false;
		}
		if (aabb.B >= limitBottomPx) {
			return (_pitType != PitType::StandOnPlatform);
		}

		// Check all covered tiles for collisions; if all are empty, no need to do pixel collision checking
		int hx1 = std::max((int)aabb.L, 0);
		int hx2 = std::min((int)std::ceil(aabb.R), limitRightPx - 1);
		int hy1 = std::max((int)aabb.T, 0);
		int hy2 = std::min((int)std::ceil(aabb.B), limitBottomPx - 1);

		if (hy2 <= 0) {
			hy1 = 0;
			hy2 = 1;
		}

		int hx1t = hx1 / TileSet::DefaultTileSize;
		int hx2t = hx2 / TileSet::DefaultTileSize;
		int hy1t = hy1 / TileSet::DefaultTileSize;
		int hy2t = hy2 / TileSet::DefaultTileSize;

		auto sprLayerLayout = _layers[_sprLayerIndex].Layout.get();

		for (int y = hy1t; y <= hy2t; y++) {
			for (int x = hx1t; x <= hx2t; x++) {
			RecheckTile:
				LayerTile& tile = sprLayerLayout[y * layoutSize.X + x];

				if (tile.DestructType == TileDestructType::Weapon && (params.DestructType & TileDestructType::Weapon) == TileDestructType::Weapon) {
					if (tile.DestructFrameIndex < (_animatedTiles[tile.DestructAnimation].Tiles.size() - 2) &&
						((tile.TileParams & (1 << (uint16_t)params.UsedWeaponType)) != 0 || params.UsedWeaponType == WeaponType::Freezer)) {
						return true;
					}
				} else if (tile.DestructType == TileDestructType::Special && (params.DestructType & TileDestructType::Special) == TileDestructType::Special) {
					if (tile.DestructFrameIndex < (_animatedTiles[tile.DestructAnimation].Tiles.size() - 2)) {
						return true;
					}
				} else if (tile.DestructType == TileDestructType::Speed && (params.DestructType & TileDestructType::Speed) == TileDestructType::Speed) {
					if (tile.DestructFrameIndex < (_animatedTiles[tile.DestructAnimation].Tiles.size() - 2) && tile.TileParams <= params.Speed) {
						return true;
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
						return true;
					}
				}

				if ((params.DestructType & TileDestructType::IgnoreSolidTiles) != TileDestructType::IgnoreSolidTiles &&
					tile.HasSuspendType == SuspendType::None && ((tile.Flags & LayerTileFlags::OneWay) != LayerTileFlags::OneWay || params.Downwards)) {
					int tileId = ResolveTileID(tile);
					TileSet* tileSet = ResolveTileSet(tileId);
					if (tileSet == nullptr || tileSet->IsTileMaskEmpty(tileId)) {
						continue;
					}

					int tx = x * TileSet::DefaultTileSize;
					int ty = y * TileSet::DefaultTileSize;

					int left = std::max(hx1 - tx, 0);
					int right = std::min(hx2 - tx, TileSet::DefaultTileSize - 1);
					int top = std::max(hy1 - ty, 0);
					int bottom = std::min(hy2 - ty, TileSet::DefaultTileSize - 1);

					if ((tile.Flags & LayerTileFlags::FlipX) == LayerTileFlags::FlipX) {
						int left2 = left;
						left = (TileSet::DefaultTileSize - 1 - right);
						right = (TileSet::DefaultTileSize - 1 - left2);
					}
					if ((tile.Flags & LayerTileFlags::FlipY) == LayerTileFlags::FlipY) {
						int top2 = top;
						top = (TileSet::DefaultTileSize - 1 - bottom);
						bottom = (TileSet::DefaultTileSize - 1 - top2);
					}

					top *= TileSet::DefaultTileSize;
					bottom *= TileSet::DefaultTileSize;

					uint8_t* mask = tileSet->GetTileMask(tileId);
					for (int ry = top; ry <= bottom; ry += TileSet::DefaultTileSize) {
						for (int rx = left; rx <= right; rx++) {
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
		int tx = (int)x / TileSet::DefaultTileSize;
		int ty = (int)y / TileSet::DefaultTileSize;

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
		//int tileId = ResolveTileID(tile);
		//TileSet* tileSet = ResolveTileSet(tileId);
		//return (tileSet == nullptr || tileSet->IsTileMaskEmpty(tileId));
		return true;
	}

	SuspendType TileMap::GetTileSuspendState(float x, float y)
	{
		constexpr int Tolerance = 4;

		if (x < 0 || y < 0 || _sprLayerIndex == -1) {
			return SuspendType::None;
		}

		int tx = (int)x / TileSet::DefaultTileSize;
		int ty = (int)y / TileSet::DefaultTileSize;

		Vector2i layoutSize = _layers[_sprLayerIndex].LayoutSize;
		if (tx >= layoutSize.X || ty >= layoutSize.Y) {
			return SuspendType::None;
		}

		TileMapLayer& layer = _layers[_sprLayerIndex];
		LayerTile& tile = layer.Layout[tx + ty * layer.LayoutSize.X];
		if (tile.HasSuspendType == SuspendType::None) {
			return SuspendType::None;
		}

		int tileId = ResolveTileID(tile);
		TileSet* tileSet = ResolveTileSet(tileId);
		if (tileSet == nullptr) {
			return SuspendType::None;
		}

		uint8_t* mask = tileSet->GetTileMask(tileId);

		int rx = (int)x & 31;
		int ry = (int)y & 31;

		if ((tile.Flags & LayerTileFlags::FlipX) == LayerTileFlags::FlipX) {
			rx = (TileSet::DefaultTileSize - 1 - rx);
		}
		if ((tile.Flags & LayerTileFlags::FlipY) == LayerTileFlags::FlipY) {
			ry = (TileSet::DefaultTileSize - 1 - ry);
		}

		int top = std::max(ry - Tolerance, 0) << 5;
		int bottom = std::min(ry + Tolerance, TileSet::DefaultTileSize - 1) << 5;

		for (int ti = bottom | rx; ti >= top; ti -= TileSet::DefaultTileSize) {
			if (mask[ti]) {
				return tile.HasSuspendType;
			}
		}

		return SuspendType::None;
	}

	bool TileMap::AdvanceDestructibleTileAnimation(LayerTile& tile, int tx, int ty, int& amount, const StringView& soundName)
	{
		AnimatedTile& anim = _animatedTiles[tile.DestructAnimation];
		int max = (int)(anim.Tiles.size() - 2);
		if (amount > 0 && tile.DestructFrameIndex < max) {
			// Tile not destroyed yet, advance counter by one
			int current = std::min(amount, max - tile.DestructFrameIndex);

			tile.DestructFrameIndex += current;
			tile.TileID = anim.Tiles[tile.DestructFrameIndex].TileID;
			if (tile.DestructFrameIndex >= max) {
				if (!soundName.empty()) {
					_levelHandler->PlayCommonSfx(soundName, Vector3f(tx * TileSet::DefaultTileSize + (TileSet::DefaultTileSize / 2),
						ty * TileSet::DefaultTileSize + (TileSet::DefaultTileSize / 2), 0.0f));
				}
				CreateTileDebris(anim.Tiles[anim.Tiles.size() - 1].TileID, tx, ty);
			}

			amount -= current;

#if MULTIPLAYER && SERVER
			((LevelHandler)levelHandler).OnAdvanceDestructibleTileAnimation(tx, ty, current);
#endif
			return true;
		}
		return false;
	}

	void TileMap::AdvanceCollapsingTileTimers(float timeMult)
	{
		_collapsingTimer -= timeMult;
		if (_collapsingTimer > 0.0f) {
			return;
		}

		_collapsingTimer = 1.0f;

		const Vector2i& layoutSize = _layers[_sprLayerIndex].LayoutSize;

		for (int i = 0; i < _activeCollapsingTiles.size(); i++) {
			Vector2i tilePos = _activeCollapsingTiles[i];
			auto& tile = _layers[_sprLayerIndex].Layout[tilePos.X + tilePos.Y * layoutSize.X];
			if (tile.TileParams == 0) {
				int amount = 1;
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
		if (!layer.Visible) {
			return;
		}

		Vector2i viewSize = _levelHandler->GetViewSize();
		Vector2f viewCenter = _levelHandler->GetCameraPos();

		Vector2i tileCount = layer.LayoutSize;
		Vector2i tileSize = Vector2i(TileSet::DefaultTileSize, TileSet::DefaultTileSize);

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

			float remX = fmodf(xt, (float)TileSet::DefaultTileSize);
			float remY = fmodf(yt, (float)TileSet::DefaultTileSize);

			// Calculate the index (on the layer map) of the first tile that needs to be drawn to the position determined earlier
			int tileX, tileY, tileAbsX, tileAbsY;

			// Get the actual tile coords on the layer layout
			if (xt > 0) {
				tileAbsX = (int)std::floor(xt / (float)TileSet::DefaultTileSize);
				tileX = tileAbsX % tileCount.X;
			} else {
				tileAbsX = (int)std::ceil(xt / (float)TileSet::DefaultTileSize);
				tileX = tileAbsX % tileCount.X;
				while (tileX < 0) {
					tileX += tileCount.X;
				}
			}

			if (yt > 0) {
				tileAbsY = (int)std::floor(yt / (float)TileSet::DefaultTileSize);
				tileY = tileAbsY % tileCount.Y;
			} else {
				tileAbsY = (int)std::ceil(yt / (float)TileSet::DefaultTileSize);
				tileY = tileAbsY % tileCount.Y;
				while (tileY < 0) {
					tileY += tileCount.Y;
				}
			}

			// update x1 and y1 with the remainder so that we start at the tile boundary
			// minus 1 because indices are updated in the beginning of the loops
			x1 -= remX - (float)TileSet::DefaultTileSize;
			y1 -= remY - (float)TileSet::DefaultTileSize;

			// Save the tile Y at the left border so that we can roll back to it at the start of every row iteration
			int tileYs = tileY;

			// Calculate the last coordinates we want to draw to
			float x3 = x1 + (TileSet::DefaultTileSize * 2) + viewSize.X;
			float y3 = y1 + (TileSet::DefaultTileSize * 2) + viewSize.Y;

			int tile_xo = -1;
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
				int tile_yo = -1;
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

					int tileId = ResolveTileID(tile);
					if (tileId == 0 || tile.Alpha == 0) {
						continue;
					}
					TileSet* tileSet = ResolveTileSet(tileId);
					if (tileSet == nullptr) {
						continue;
					}

					auto command = RentRenderCommand(layer.Description.RendererType);
					command->material().setBlendingFactors(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

					Vector2i texSize = tileSet->TextureDiffuse->size();
					float texScaleX = TileSet::DefaultTileSize / float(texSize.X);
					float texBiasX = (tileId % tileSet->TilesPerRow) * TileSet::DefaultTileSize / float(texSize.X);
					float texScaleY = TileSet::DefaultTileSize / float(texSize.Y);
					float texBiasY = (tileId / tileSet->TilesPerRow) * TileSet::DefaultTileSize / float(texSize.Y);

					// ToDo: Flip normal map somehow
					if ((tile.Flags & LayerTileFlags::FlipX) == LayerTileFlags::FlipX) {
						texBiasX += texScaleX;
						texScaleX *= -1;
					}
					if ((tile.Flags & LayerTileFlags::FlipY) == LayerTileFlags::FlipY) {
						texBiasY += texScaleY;
						texScaleY *= -1;
					}

					if ((viewSize.X & 1) == 1) {
						texBiasX += 0.5f / float(texSize.X);
					}
					if ((viewSize.Y & 1) == 1) {
						texBiasY -= 0.5f / float(texSize.Y);
					}

					auto instanceBlock = command->material().uniformBlock(Material::InstanceBlockName);
					instanceBlock->uniform(Material::TexRectUniformName)->setFloatValue(texScaleX, texBiasX, texScaleY, texBiasY);
					instanceBlock->uniform(Material::SpriteSizeUniformName)->setFloatValue(TileSet::DefaultTileSize, TileSet::DefaultTileSize);

					Vector4f color = layer.Description.Color;
					color.W *= tile.Alpha / 255.0f;
					instanceBlock->uniform(Material::ColorUniformName)->setFloatVector(color.Data());

					command->setTransformation(Matrix4x4f::Translation(std::floor(x2 + (TileSet::DefaultTileSize / 2)), std::floor(y2 + (TileSet::DefaultTileSize / 2)), 0.0f));
					command->setLayer(layer.Description.Depth);
					command->material().setTexture(*tileSet->TextureDiffuse);

					renderQueue.addCommand(command);
				}
			}
		}
	}

	float TileMap::TranslateCoordinate(float coordinate, float speed, float offset, int viewSize, bool isY)
	{
		// Coordinate: the "vanilla" coordinate of the tile on the layer if the layer was fixed to the sprite layer with same
		// speed and no other options. Think of its position in JCS.
		// Speed: the set layer speed; 1 for anything that moves the same speed as the sprite layer (where the objects live),
		// less than 1 for backgrounds that move slower, more than 1 for foregrounds that move faster
		// Offset: any difference to starting coordinates caused by an inherent automatic speed a layer has

		// `HardcodedOffsetY` (literal 70) is the same as in `DrawLayer`, it's the offscreen offset of the first tile to draw.
		// Don't touch unless absolutely necessary.
		int alignment = ((isY ? (viewSize - 200) : (viewSize - 320)) / 2) + HardcodedOffset;
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

	void TileMap::AddTileSet(const StringView& tileSetPath, uint16_t offset, uint16_t count, const uint8_t* paletteRemapping)
	{
		auto& tileSetPart = _tileSets.emplace_back();
		tileSetPart.Data = ContentResolver::Get().RequestTileSet(tileSetPath, 0, false, paletteRemapping);
		tileSetPart.Offset = offset;
		tileSetPart.Count = count;

		if (tileSetPart.Data == nullptr) {
			LOGE_X("Cannot load extra tileset \"%s\"", tileSetPath.data());
		}
	}

	void TileMap::ReadLayerConfiguration(IFileStream& s)
	{
		LayerType layerType = (LayerType)s.ReadValue<uint8_t>();
		uint16_t layerFlags = s.ReadValue<uint16_t>();

		if (layerType == LayerType::Sprite) {
			_sprLayerIndex = (int)_layers.size();
		}

		TileMapLayer& newLayer = _layers.emplace_back();

		int32_t width = s.ReadValue<int32_t>();
		int32_t height = s.ReadValue<int32_t>();
		newLayer.LayoutSize = Vector2i(width, height);
		newLayer.Visible = ((layerFlags & 0x08) == 0x08);

		if (layerType != LayerType::Sprite) {
			uint8_t combinedSpeedModels = s.ReadValue<uint8_t>();
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
			newLayer.Description.Depth = (uint16_t)(ILevelHandler::MainPlaneZ - depth);
			newLayer.Description.UseInherentOffset = ((layerFlags & 0x04) == 0x04);

			newLayer.Description.RendererType = (LayerRendererType)s.ReadValue<uint8_t>();
			uint8_t r = s.ReadValue<uint8_t>();
			uint8_t g = s.ReadValue<uint8_t>();
			uint8_t b = s.ReadValue<uint8_t>();
			uint8_t a = s.ReadValue<uint8_t>();

			if (newLayer.Description.RendererType == LayerRendererType::Tinted) {
				// TODO: Tinted color is precomputed from palette here
				const uint32_t* palettes = ContentResolver::Get().GetPalettes();
				uint32_t color = palettes[r];
				newLayer.Description.Color = Vector4f((color & 0x000000ff) / 255.0f, ((color >> 8) & 0x000000ff) / 255.0f, ((color >> 16) & 0x000000ff) / 255.0f, a * ((color >> 24) & 0x000000ff) / (255.0f * 255.0f));
			} else {
				newLayer.Description.Color = Vector4f(r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f);

				if (newLayer.Description.RendererType >= LayerRendererType::Sky) {
					_texturedBackgroundLayer = (int)_layers.size() - 1;
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
			newLayer.Description.Depth = (uint16_t)(ILevelHandler::MainPlaneZ - 50);
			newLayer.Description.UseInherentOffset = false;
			newLayer.Description.SpeedModelX = LayerSpeedModel::Default;
			newLayer.Description.SpeedModelY = LayerSpeedModel::Default;

			newLayer.Description.RendererType = LayerRendererType::Default;
			newLayer.Description.Color = Vector4f(1.0f, 1.0f, 1.0f, 1.0f);
		}

		newLayer.Layout = std::make_unique<LayerTile[]>(width * height);

		for (int i = 0; i < (width * height); i++) {
			uint8_t tileFlags = s.ReadValue<uint8_t>();
			uint16_t tileIdx = s.ReadValue<uint16_t>();

			uint8_t tileModifier = (uint8_t)(tileFlags >> 4);

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

	void TileMap::ReadAnimatedTiles(IFileStream& s)
	{
		int16_t count = s.ReadValue<int16_t>();

		_animatedTiles.reserve(count);

		for (int i = 0; i < count; i++) {
			uint8_t frameCount = s.ReadValue<uint8_t>();
			if (frameCount == 0) {
				continue;
			}

			AnimatedTile& animTile = _animatedTiles.emplace_back();

			// FrameDuration is multiplied by 16 before saving, so divide it here back
			animTile.FrameDuration = s.ReadValue<uint16_t>() / 16.0f;
			animTile.Delay = s.ReadValue<uint16_t>();

			// TODO: delayJitter
			/*uint16_t delayJitter =*/ s.ReadValue<uint16_t>();

			animTile.PingPong = s.ReadValue<uint8_t>();
			animTile.PingPongDelay = s.ReadValue<uint16_t>();

			for (int j = 0; j < frameCount; j++) {
				auto& frame = animTile.Tiles.emplace_back();
				// TODO: flags
				/*uint8_t flag =*/ s.ReadValue<uint8_t>();
				frame.TileID = s.ReadValue<uint16_t>();
			}
		}
	}

	void TileMap::SetTileEventFlags(int x, int y, EventType tileEvent, uint8_t* tileParams)
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

	void TileMap::SetTileDestructibleEventParams(LayerTile& tile, TileDestructType type, uint16_t tileParams)
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
			int x = (int)debris.Pos.X / TileSet::DefaultTileSize;
			int y = (int)debris.Pos.Y / TileSet::DefaultTileSize;
			if (x < 0 || y < 0 || x >= spriteLayer.LayoutSize.X || y >= spriteLayer.LayoutSize.Y) {
				return;
			}

			int tileId = ResolveTileID(spriteLayer.Layout[x + y * spriteLayer.LayoutSize.X]);
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

	void TileMap::CreateTileDebris(int tileId, int x, int y)
	{
		constexpr float SpeedMultiplier[] = { -2, 2, -1, 1 };
		constexpr int QuarterSize = TileSet::DefaultTileSize / 2;

		// Tile #0 is always empty
		if (tileId == 0) {
			return;
		}

		TileSet* tileSet = ResolveTileSet(tileId);
		if (tileSet == nullptr) {
			return;
		}

		uint16_t z = _layers[_sprLayerIndex].Description.Depth + 80;

		Vector2i texSize = tileSet->TextureDiffuse->size();
		float texScaleX = float(QuarterSize) / float(texSize.X);
		float texBiasX = ((tileId % tileSet->TilesPerRow) * TileSet::DefaultTileSize) / float(texSize.X);
		float texScaleY = float(QuarterSize) / float(texSize.Y);
		float texBiasY = ((tileId / tileSet->TilesPerRow) * TileSet::DefaultTileSize) / float(texSize.Y);

		// TODO: Implement flip here
		/*if (isFlippedX) {
			texBiasX += texScaleX;
			texScaleX *= -1;
		}
		if (isFlippedY) {
			texBiasY += texScaleY;
			texScaleY *= -1;
		}*/

		for (int i = 0; i < 4; i++) {
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

	void TileMap::CreateParticleDebris(const GraphicResource* res, Vector3f pos, Vector2f force, int currentFrame, bool isFacingLeft)
	{
		constexpr int DebrisSize = 3;

		float x = pos.X - res->Base->Hotspot.X;
		float y = pos.Y - res->Base->Hotspot.Y;
		Vector2i texSize = res->Base->TextureDiffuse->size();

		for (int fy = 0; fy < res->Base->FrameDimensions.Y; fy += DebrisSize + 1) {
			for (int fx = 0; fx < res->Base->FrameDimensions.X; fx += DebrisSize + 1) {
				float currentSize = DebrisSize * Random().FastFloat(0.2f, 1.1f);

				DestructibleDebris& debris = _debrisList.emplace_back();
				debris.Pos = Vector2f(x + (isFacingLeft ? res->Base->FrameDimensions.X - fx : fx), y + fy);
				debris.Depth = (uint16_t)pos.Z;
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

	void TileMap::CreateSpriteDebris(const GraphicResource* res, Vector3f pos, int count)
	{
		float x = pos.X - res->Base->Hotspot.X;
		float y = pos.Y - res->Base->Hotspot.Y;
		Vector2i texSize = res->Base->TextureDiffuse->size();

		for (int i = 0; i < count; i++) {
			float speedX = Random().FastFloat(-1.0f, 1.0f) * Random().FastFloat(0.2f, 0.8f) * count;

			DestructibleDebris& debris = _debrisList.emplace_back();
			debris.Pos = Vector2f(x, y);
			debris.Depth = (uint16_t)pos.Z;
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

			int curAnimFrame = res->FrameOffset + Random().Next(0, res->FrameCount);
			int col = curAnimFrame % res->Base->FrameConfiguration.X;
			int row = curAnimFrame / res->Base->FrameConfiguration.X;
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
		int size = (int)_debrisList.size();
		for (int i = 0; i < size; i++) {
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
		for (auto& debris : _debrisList) {
			auto command = RentRenderCommand(LayerRendererType::Default);

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
			command->setTransformation(worldMatrix);
			command->setLayer(debris.Depth);
			command->material().setTexture(*debris.DiffuseTexture);

			renderQueue.addCommand(command);
		}
	}

	bool TileMap::GetTrigger(uint8_t triggerId)
	{
		return _triggerState[triggerId];
	}

	void TileMap::SetTrigger(uint8_t triggerId, bool newState)
	{
		if (_triggerState[triggerId] == newState) {
			return;
		}

		_triggerState.Set(triggerId, newState);

		// Go through all tiles and update any that are influenced by this trigger
		Vector2i layoutSize = _layers[_sprLayerIndex].LayoutSize;
		int n = layoutSize.X * layoutSize.Y;
		for (int i = 0; i < n; i++) {
			LayerTile& tile = _layers[_sprLayerIndex].Layout[i];
			if (tile.DestructType == TileDestructType::Trigger && tile.TileParams == triggerId) {
				if (_animatedTiles[tile.DestructAnimation].Tiles.size() > 1) {
					tile.DestructFrameIndex = (newState ? 1 : 0);
					tile.TileID = _animatedTiles[tile.DestructAnimation].Tiles[tile.DestructFrameIndex].TileID;
				}
			}
		}
	}

	void TileMap::RenderTexturedBackground(RenderQueue& renderQueue, TileMapLayer& layer, float x, float y)
	{
		auto target = _texturedBackgroundPass._target.get();
		if (target == nullptr) {
			return;
		}

		Vector2i viewSize = _levelHandler->GetViewSize();
		Vector2f viewCenter = _levelHandler->GetCameraPos();

		auto command = &_texturedBackgroundPass._outputRenderCommand;

		auto instanceBlock = command->material().uniformBlock(Material::InstanceBlockName);
		instanceBlock->uniform(Material::TexRectUniformName)->setFloatValue(1.0f, 0.0f, 1.0f, 0.0f);
		instanceBlock->uniform(Material::SpriteSizeUniformName)->setFloatValue(viewSize.X, viewSize.Y);
		instanceBlock->uniform(Material::ColorUniformName)->setFloatVector(Colorf(1.0f, 1.0f, 1.0f, 1.0f).Data());

		command->material().uniform("uViewSize")->setFloatValue(viewSize.X, viewSize.Y);
		command->material().uniform("uCameraPos")->setFloatVector(viewCenter.Data());
		command->material().uniform("uShift")->setFloatValue(x, y);
		command->material().uniform("uHorizonColor")->setFloatVector(layer.Description.Color.Data());

		command->setTransformation(Matrix4x4f::Translation(viewCenter.X, viewCenter.Y, 0.0f));
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

	TileSet* TileMap::ResolveTileSet(int& tileId)
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

	void TileMap::TexturedBackgroundPass::Initialize()
	{
		bool notInitialized = (_view == nullptr);

		if (notInitialized) {
			Vector2i layoutSize = _owner->_layers[_owner->_texturedBackgroundLayer].LayoutSize;
			int width = layoutSize.X * TileSet::DefaultTileSize;
			int height = layoutSize.Y * TileSet::DefaultTileSize;

			_camera = std::make_unique<Camera>();
			_camera->setOrthoProjection(0, width, 0, height);
			_camera->setView(0, 0, 0, 1);
			_target = std::make_unique<Texture>(nullptr, Texture::Format::RGB8, width, height);
			_view = std::make_unique<Viewport>(_target.get(), Viewport::DepthStencilFormat::None);
			_view->setRootNode(this);
			_view->setCamera(_camera.get());
			//_view->setClearMode(Viewport::ClearMode::Never);
			_target->setMagFiltering(SamplerFilter::Linear);
			_target->setWrap(SamplerWrapping::Repeat);

			// Prepare render commands
			int renderCommandCount = (width * height) / (TileSet::DefaultTileSize * TileSet::DefaultTileSize);
			_renderCommands.reserve(renderCommandCount);
			for (int i = 0; i < renderCommandCount; i++) {
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
		Vector2i targetSize = _target->size();

		int renderCommandIndex = 0;
		bool isAnimated = false;

		for (int y = 0; y < layoutSize.Y; y++) {
			for (int x = 0; x < layoutSize.X; x++) {
				LayerTile& tile = layer.Layout[x + y * layer.LayoutSize.X];

				int tileId = _owner->ResolveTileID(tile);
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
				float texBiasX = (tileId % tileSet->TilesPerRow) * TileSet::DefaultTileSize / float(texSize.X);
				float texScaleY = TileSet::DefaultTileSize / float(texSize.Y);
				float texBiasY = (tileId / tileSet->TilesPerRow) * TileSet::DefaultTileSize / float(texSize.Y);

				// TODO: Flip normal map somehow
				if ((tile.Flags & LayerTileFlags::FlipX) == LayerTileFlags::FlipX) {
					texBiasX += texScaleX;
					texScaleX *= -1;
				}
				if ((tile.Flags & LayerTileFlags::FlipY) == LayerTileFlags::FlipY) {
					texBiasY += texScaleY;
					texScaleY *= -1;
				}

				if ((targetSize.X & 1) == 1) {
					texBiasX += 0.5f / float(texSize.X);
				}
				if ((targetSize.Y & 1) == 1) {
					texBiasY -= 0.5f / float(texSize.Y);
				}

				auto instanceBlock = command->material().uniformBlock(Material::InstanceBlockName);
				instanceBlock->uniform(Material::TexRectUniformName)->setFloatValue(texScaleX, texBiasX, texScaleY, texBiasY);
				instanceBlock->uniform(Material::SpriteSizeUniformName)->setFloatValue(TileSet::DefaultTileSize, TileSet::DefaultTileSize);
				instanceBlock->uniform(Material::ColorUniformName)->setFloatVector(Colorf::White.Data());

				command->setTransformation(Matrix4x4f::Translation(x * TileSet::DefaultTileSize + (TileSet::DefaultTileSize / 2), y * TileSet::DefaultTileSize + (TileSet::DefaultTileSize / 2), 0.0f));
				command->material().setTexture(*tileSet->TextureDiffuse);

				renderQueue.addCommand(command);
			}
		}

		if (!isAnimated && _alreadyRendered) {
			// If it's not animated, it can be rendered only once
			for (int i = Viewport::chain().size() - 1; i >= 0; i--) {
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