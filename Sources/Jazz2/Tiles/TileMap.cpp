#include "TileMap.h"
#include "../ContentResolver.h"
#include "../LevelHandler.h"
#include "../PreferencesCache.h"

#include "../../nCine/tracy.h"
#include "../../nCine/Base/Random.h"
#include "../../nCine/Graphics/RenderQueue.h"
#include "../../nCine/Graphics/RenderResources.h"

#include <Containers/GrowableArray.h>

namespace Jazz2::Tiles
{
	namespace
	{
		// The textured background ("Sky"/"Circle" layers) is a per-pixel procedural effect in GLSL. Its planar
		// variant is affine along every screen row, though, so the fixed-function tiers rebuild it out of
		// horizontal bands instead of a fragment shader - the shared geometry lives in
		// Shaders/Include/TexturedBackgroundWarp.inc, included by both consoles' fixed_function blocks. The
		// circular variant has no such structure, so it borrows the planar reconstruction rather than falling
		// back to a flat tilemap - much closer to the original than no warp at all.
		constexpr bool SupportsTexturedBackground = true;

		/**
			@brief Whether a level's tileset atlas is repacked around the tiles the level references

			A tileset describes everything its author drew; the levels measured reference 42% and 59% of theirs,
			and the rest sits in memory as texels nothing samples. Repacking costs a second read of the sheet at
			load time and gives back 592 KB on the largest of them - the difference, on a console with 8 MB of
			RDRAM in total, between a level that draws every sprite and one that starts refusing them.
			Only the Nintendo 64 takes that trade for now: the analysis cannot see a tile a script places at
			runtime (it draws blank there), and the platforms that can hold the whole sheet have no reason to
			risk it.
		*/
#if defined(DEATH_TARGET_N64)
		constexpr bool PruneAtlasToUsedTiles = true;
#else
		constexpr bool PruneAtlasToUsedTiles = false;
#endif
		constexpr bool SupportsTexturedBackgroundCircle = true;

#if defined(TILEMAP_USE_SINGLE_DRAW)
		// Interleaved per-vertex format of the tile-layer and debris meshes, shared with the lighting mesh:
		// position.xy, texcoords.uv, color.rgba
		constexpr std::uint32_t FloatsPerVertex = 8;

		// A quad is its four distinct corners plus six indices out of the pattern every quad mesh shares (see
		// RenderResources::GetQuadIndices()) - a third less vertex data to write and copy each frame than the
		// six-vertex form the same triangles need without indices, and a third fewer vertices for a
		// programmable backend to shade. The fixed-function tiers read the vertex stream themselves rather
		// than pulling it through the index buffer, so they resolve the indices in their own dispatch (see
		// `DispatchTileMesh()` in the GU, GX, PVR, GS, RDP and LegacyGL backends).
		constexpr std::uint32_t VerticesPerQuad = RenderResources::VerticesPerQuad;
		constexpr std::uint32_t FloatsPerQuad = VerticesPerQuad * FloatsPerVertex;
#endif

#if defined(DEATH_TARGET_DREAMCAST) || defined(DEATH_TARGET_N64) || defined(DEATH_TARGET_WII) || \
		defined(DEATH_TARGET_GAMECUBE) || defined(DEATH_TARGET_AMIGAOS)
		// Slots the render command pool keeps even when nothing needs them - roughly what one viewport of a level
		// asks for, so the common case never reallocates
		constexpr std::int32_t MinPooledRenderCommands = 32;
		// How long a pool has to stay below its peak before the slots above it are released again. A burst of
		// debris fades out over about 300 frames, so trimming sooner would only fight the effect still running.
		constexpr std::int32_t RenderCommandPoolTrimInterval = 600;
		// Capacity _debrisList keeps around, so the usual handful of particles never reallocates
		constexpr std::int32_t MinDebrisCapacity = 64;
		// Buffers the mesh vertex pool keeps around (one per drawn tile layer plus one per debris group, which a
		// level of eight layers with a burst running fits into), and the floats each of them keeps - 32 per
		// quad, so this holds a modest layer or burst without reallocating
		constexpr std::int32_t MinPooledMeshBuffers = 12;
		constexpr std::int32_t MinMeshBufferCapacity = 32 * 128;
#	if !defined(TILEMAP_USE_SINGLE_DRAW)
		// Absolute ceiling on the render command pool for the fallback path that rents one command per visible
		// particle (~840 bytes each on the Dreamcast). TileMap::MaxDebrisCount already bounds it for a single
		// viewport, but OnDraw() runs once per viewport and the count only resets at the end of the frame, so in
		// splitscreen the pool would grow with the number of players. Particles beyond this are not drawn.
		constexpr std::int32_t MaxPooledRenderCommands = 768;
#	endif
#else
		// Desktop targets have the memory for the effect at full detail, so nothing is bounded or trimmed
		constexpr std::int32_t MinPooledRenderCommands = 0;
		constexpr std::int32_t RenderCommandPoolTrimInterval = 0;
		constexpr std::int32_t MinDebrisCapacity = 0;
		constexpr std::int32_t MinPooledMeshBuffers = 0;
		constexpr std::int32_t MinMeshBufferCapacity = 0;
#	if !defined(TILEMAP_USE_SINGLE_DRAW)
		constexpr std::int32_t MaxPooledRenderCommands = 0;
#	endif
#endif
	}

	TileMap::TileMap(StringView tileSetPath, std::uint16_t captionTileId, bool applyPalette)
		: _owner(nullptr), _sprLayerIndex(-1), _pitType(PitType::FallForever), _hasRollbackCheckpoint(false),
			_renderCommandsCount(0), _renderCommandsPeak(0), _renderCommandsPeakAge(0), _collapsingTimer(0.0f),
			_animatedTilesOffset(0), _tileSetPath(tileSetPath), _captionTileId(captionTileId), _tilesOverridden(false),
			_triggerState(ValueInit, TriggerCount), _triggerStateForRollback(ValueInit, TriggerCount),
			_texturedBackgroundLayer(-1), _texturedBackgroundPass(this)
	{
		auto& tileSetPart = _tileSets.emplace_back();
		tileSetPart.Data = ContentResolver::Get().RequestTileSet(tileSetPath, captionTileId, applyPalette);
		DEATH_ASSERT(tileSetPart.Data != nullptr, ("Failed to load main tileset \"{}\"", tileSetPath), );
		
		tileSetPart.Offset = 0;
		tileSetPart.Count = tileSetPart.Data->TileCount;

		_renderCommands.reserve(128);
	}

	TileMap::~TileMap()
	{
		TracyPlot("TileMap Render Commands", 0LL);
#if defined(TILEMAP_USE_SINGLE_DRAW)
		TracyPlot("TileMap Mesh Commands", 0LL);
#endif
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
			return {};
		}

		return _layers[_sprLayerIndex].LayoutSize;
	}

	Vector2i TileMap::GetLevelBounds() const
	{
		if (_sprLayerIndex == -1) {
			return {};
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

	void TileMap::OnEndFrame()
	{
		if (RenderCommandPoolTrimInterval > 0) {
			// A pool only ever grew to its high-water mark, so one level with many unmeshable layers - or, on the
			// fallback path, a single burst of debris - pinned a few hundred kilobytes for the rest of the level.
			// Hand the slots above the recent peak back once the peak is long over; they are recreated on demand.
			if (_renderCommandsPeak < _renderCommandsCount) {
				_renderCommandsPeak = _renderCommandsCount;
			}
#if defined(TILEMAP_USE_SINGLE_DRAW)
			if (_meshVerticesPeak < _meshVerticesCount) {
				_meshVerticesPeak = _meshVerticesCount;
			}
#endif
			_renderCommandsPeakAge++;
			if (_renderCommandsPeakAge >= RenderCommandPoolTrimInterval) {
				std::size_t target = (std::size_t)std::max(_renderCommandsPeak, MinPooledRenderCommands);
				if (_renderCommands.size() > target) {
					// Destroying the owning pointers is what actually frees the commands; the slot array itself is
					// four bytes per slot and stays at its high-water mark, as shrinking it would have to relocate
					// the surviving `unique_ptr`s bitwise
					_renderCommands.pop_back_n(_renderCommands.size() - target);
					// Kept in sync with the pool, the cached pointers of the surviving slots stay valid because
					// only the tail is dropped (and a slot recreated later is refreshed as a fresh one anyway)
					if (_renderCommandUniforms.size() > target) {
						_renderCommandUniforms.pop_back_n(_renderCommandUniforms.size() - target);
						_renderCommandUniforms.shrink(target);
					}
				}
#if defined(TILEMAP_USE_SINGLE_DRAW)
				// Aggregating moved what a burst costs from the command pool into these vertex buffers - 192 bytes
				// per particle per viewport that sees it - so they are trimmed the same way. A buffer above the
				// peak is dropped whole, which is what frees the floats; the slot array is left at its high-water
				// mark because shrinking it reallocates bitwise, and a relocated buffer that never allocated
				// would be left believing the inline storage it no longer sits in is a heap block of its own.
				// The buffers still in use only give back capacity they have grown far past, as the layer or
				// group they serve asks for about the same size every frame.
				std::size_t bufferTarget = (std::size_t)std::max(_meshVerticesPeak, MinPooledMeshBuffers);
				if (_meshVertices.size() > bufferTarget) {
					_meshVertices.pop_back_n(_meshVertices.size() - bufferTarget);
				}
				for (auto& buffer : _meshVertices) {
					if (buffer.capacity() > (std::size_t)MinMeshBufferCapacity && buffer.size() * 4 < buffer.capacity()) {
						buffer.shrink(std::max(buffer.size() * 2, (std::size_t)MinMeshBufferCapacity));
					}
				}
				_meshVerticesPeak = 0;
#endif
				_renderCommandsPeak = 0;
				_renderCommandsPeakAge = 0;
			}

			// Same for the particle storage itself: 100 bytes per particle stayed reserved for the level's lifetime
			if (_debrisList.capacity() > (std::size_t)MinDebrisCapacity && _debrisList.size() * 4 < _debrisList.capacity()) {
				_debrisList.shrink(std::max(_debrisList.size() * 2, (std::size_t)MinDebrisCapacity));
			}
		}

		// The command cache must be reset every frame,
		// OnDraw() is called multiple times if multiple viewports are active
		_renderCommandsCount = 0;
#if defined(TILEMAP_USE_SINGLE_DRAW)
		_meshVerticesCount = 0;
		_meshCommandCount = 0;
#endif
	}

	bool TileMap::OnDraw(RenderQueue& renderQueue)
	{
		ZoneScopedC(0xA09359);

		const Viewport* viewport = RenderResources::GetCurrentViewport();
		Rectf cullingRect = viewport->GetCullingRect();
		Vector2f viewCenter = cullingRect.Center();

		for (auto& layer : _layers) {
			DrawLayer(renderQueue, layer, cullingRect, viewCenter);
		}

		if (_sprLayerIndex != -1) {
			auto& spriteLayer = _layers[_sprLayerIndex];
			// Render black bars if layout width is smaller than viewport width
			if (spriteLayer.LayoutSize.X * TileSet::DefaultTileSize < cullingRect.W) {
				std::int32_t w = (cullingRect.W - (spriteLayer.LayoutSize.X * TileSet::DefaultTileSize)) / 2;

				// Left
				{
					auto command = RentRenderCommand(LayerRendererType::Solid);
					command->SetType(RenderCommand::Type::TileMap);

					auto instanceBlock = command->GetInstanceBlock();
					instanceBlock->GetUniform(Material::SpriteSizeUniformName)->SetFloatValue(w, cullingRect.H);
					instanceBlock->GetUniform(Material::ColorUniformName)->SetFloatValue(0.0f, 0.0f, 0.0f, 1.0f);

					command->SetTransformation(Matrix4x4f::Translation(cullingRect.X, cullingRect.Y, 0.0f));
					command->SetLayer(spriteLayer.Description.Depth);

					renderQueue.AddCommand(command);
				}
				// Right
				{
					auto command = RentRenderCommand(LayerRendererType::Solid);
					command->SetType(RenderCommand::Type::TileMap);

					auto instanceBlock = command->GetInstanceBlock();
					instanceBlock->GetUniform(Material::SpriteSizeUniformName)->SetFloatValue(w, cullingRect.H);
					instanceBlock->GetUniform(Material::ColorUniformName)->SetFloatValue(0.0f, 0.0f, 0.0f, 1.0f);

					command->SetTransformation(Matrix4x4f::Translation(cullingRect.X + cullingRect.W - w, cullingRect.Y, 0.0f));
					command->SetLayer(spriteLayer.Description.Depth);

					renderQueue.AddCommand(command);
				}
			}
		}

		DrawDebris(renderQueue);

		// Counts only the per-tile/per-particle commands, which the mesh path leaves to the unmeshable layers -
		// the aggregated tile layers and debris groups are the second plot
		TracyPlot("TileMap Render Commands", static_cast<std::int64_t>(_renderCommandsCount));
#if defined(TILEMAP_USE_SINGLE_DRAW)
		TracyPlot("TileMap Mesh Commands", static_cast<std::int64_t>(_meshCommandCount));
#endif

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
			if (_pitType == PitType::StandOnPlatform) {
				return false;
			}
			ty = layoutSize.Y - 1;
		} else if (ty < 0) {
			ty = 0;
		}

		LayerTile& tile = _layers[_sprLayerIndex].Layout[ty * layoutSize.X + tx];
		std::int32_t tileId = ResolveTileID(tile);
		TileSet* tileSet = ResolveTileSet(tileId);
		return (tileSet == nullptr || tileSet->IsTileMaskEmpty(tileId));
	}

	bool TileMap::IsTileDestructible(std::int32_t tx, std::int32_t ty)
	{
		if (_sprLayerIndex == -1) {
			return false;
		}

		Vector2i layoutSize = _layers[_sprLayerIndex].LayoutSize;
		if (tx < 0 || ty < 0 || tx >= layoutSize.X || ty >= layoutSize.Y) {
			return false;
		}

		// The player can eventually pass through these by destroying them. Collapsing tiles are intentionally
		// excluded - the player stands on (and jumps over) them, they're not a passage.
		const LayerTile& tile = _layers[_sprLayerIndex].Layout[ty * layoutSize.X + tx];
		return (tile.DestructType == TileDestructType::Weapon || tile.DestructType == TileDestructType::Speed ||
				tile.DestructType == TileDestructType::Special);
	}

	bool TileMap::IsTileOneWay(std::int32_t tx, std::int32_t ty)
	{
		if (_sprLayerIndex == -1) {
			return false;
		}

		Vector2i layoutSize = _layers[_sprLayerIndex].LayoutSize;
		if (tx < 0 || ty < 0 || tx >= layoutSize.X || ty >= layoutSize.Y) {
			return false;
		}

		const LayerTile& tile = _layers[_sprLayerIndex].Layout[ty * layoutSize.X + tx];
		return ((tile.Flags & LayerTileFlags::OneWay) == LayerTileFlags::OneWay);
	}

	bool TileMap::IsTileCornerEmpty(std::int32_t tx, std::int32_t ty, std::int32_t cornerX, std::int32_t cornerY)
	{
		if (_sprLayerIndex == -1) {
			return true;
		}

		Vector2i layoutSize = _layers[_sprLayerIndex].LayoutSize;
		if (tx < 0 || ty < 0 || tx >= layoutSize.X || ty >= layoutSize.Y) {
			return false;
		}

		const LayerTile& tile = _layers[_sprLayerIndex].Layout[ty * layoutSize.X + tx];
		std::int32_t tileId = ResolveTileID(tile);
		TileSet* tileSet = ResolveTileSet(tileId);
		if (tileSet == nullptr || tileSet->IsTileMaskEmpty(tileId)) {
			return true;
		}
		if (tileSet->IsTileMaskFilled(tileId)) {
			return false;
		}

		// Check whether the ~1/3 corner of the tile mask (e.g., the empty triangle of a 45-degree slope) is clear,
		// so the path tracer can squeeze a diagonal move through partially-solid slope tiles
		constexpr std::int32_t Size = TileSet::DefaultTileSize;
		constexpr std::int32_t Corner = Size / 3;
		std::int32_t left = (cornerX < 0 ? 0 : Size - Corner);
		std::int32_t right = (cornerX < 0 ? Corner - 1 : Size - 1);
		std::int32_t top = (cornerY < 0 ? 0 : Size - Corner);
		std::int32_t bottom = (cornerY < 0 ? Corner - 1 : Size - 1);

		if ((tile.Flags & LayerTileFlags::FlipX) == LayerTileFlags::FlipX) {
			std::int32_t left2 = left;
			left = (Size - 1 - right);
			right = (Size - 1 - left2);
		}
		if ((tile.Flags & LayerTileFlags::FlipY) == LayerTileFlags::FlipY) {
			std::int32_t top2 = top;
			top = (Size - 1 - bottom);
			bottom = (Size - 1 - top2);
		}

		// Packed mask: one 32-bit row word tests the whole [left..right] column range at once
		const std::uint8_t* mask = tileSet->GetTileMask(tileId);
		const std::uint32_t rangeMask = (~0u >> (31 - right)) & (~0u << left);
		for (std::int32_t ry = top; ry <= bottom; ry++) {
			if (TileSet::GetTileMaskRow(mask, ry) & rangeMask) {
				return false;
			}
		}
		return true;
	}

	bool TileMap::IsTilePartiallySolid(std::int32_t tx, std::int32_t ty)
	{
		if (_sprLayerIndex == -1) {
			return false;
		}

		Vector2i layoutSize = _layers[_sprLayerIndex].LayoutSize;
		if (tx < 0 || ty < 0 || tx >= layoutSize.X || ty >= layoutSize.Y) {
			return false;
		}

		// True if the tile's collision mask is neither fully empty nor fully filled (e.g., a slope or a thin
		// solid band) - the player rests on its solid part rather than falling through it
		const LayerTile& tile = _layers[_sprLayerIndex].Layout[ty * layoutSize.X + tx];
		std::int32_t tileId = ResolveTileID(tile);
		TileSet* tileSet = ResolveTileSet(tileId);
		return (tileSet != nullptr && !tileSet->IsTileMaskEmpty(tileId) && !tileSet->IsTileMaskFilled(tileId));
	}

	bool TileMap::IsTileTrigger(std::int32_t tx, std::int32_t ty)
	{
		if (_sprLayerIndex == -1) {
			return false;
		}

		Vector2i layoutSize = _layers[_sprLayerIndex].LayoutSize;
		if (tx < 0 || ty < 0 || tx >= layoutSize.X || ty >= layoutSize.Y) {
			return false;
		}

		// A trigger-controlled tile, toggled solid/empty by a trigger crate (see SetTrigger)
		const LayerTile& tile = _layers[_sprLayerIndex].Layout[ty * layoutSize.X + tx];
		return ((tile.DestructType & TileDestructType::Trigger) == TileDestructType::Trigger);
	}

	bool TileMap::IsTilePointEmpty(std::int32_t x, std::int32_t y, bool downwards)
	{
		if (_sprLayerIndex == -1) {
			return true;
		}

		auto& sprLayer = _layers[_sprLayerIndex];
		const std::int32_t limitRightPx = sprLayer.LayoutSize.X * TileSet::DefaultTileSize;
		const std::int32_t limitBottomPx = sprLayer.LayoutSize.Y * TileSet::DefaultTileSize;

		// Out-of-level coordinates count as solid walls, exactly as in IsTileEmpty()
		if (x < 0 || x >= limitRightPx) {
			return false;
		}
		if (y >= limitBottomPx) {
			if (_pitType == PitType::StandOnPlatform) {
				return false;
			}
			y = limitBottomPx - 1;
		} else if (y < 0) {
			y = 0;
		}

		LayerTile& tile = sprLayer.Layout[(y / TileSet::DefaultTileSize) * sprLayer.LayoutSize.X + (x / TileSet::DefaultTileSize)];
		if (tile.HasSuspendType != SuspendType::None ||
			((tile.Flags & LayerTileFlags::OneWay) == LayerTileFlags::OneWay && !downwards)) {
			return true;
		}

		std::int32_t tileId = ResolveTileID(tile);
		TileSet* tileSet = ResolveTileSet(tileId);
		if (tileSet == nullptr || tileSet->IsTileMaskEmpty(tileId)) {
			return true;
		}
		if (tileSet->IsTileMaskFilled(tileId)) {
			return false;
		}

		std::int32_t px = x % TileSet::DefaultTileSize;
		std::int32_t py = y % TileSet::DefaultTileSize;
		if ((tile.Flags & LayerTileFlags::FlipX) == LayerTileFlags::FlipX) {
			px = TileSet::DefaultTileSize - 1 - px;
		}
		if ((tile.Flags & LayerTileFlags::FlipY) == LayerTileFlags::FlipY) {
			py = TileSet::DefaultTileSize - 1 - py;
		}
		return (TileSet::GetTileMaskRow(tileSet->GetTileMask(tileId), py) & (1u << px)) == 0;
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
		if (aabb.B >= limitBottomPx && _pitType == PitType::StandOnPlatform) {
			return false;
		}

		// Check all covered tiles for collisions; if all are empty, no need to do pixel collision checking
		std::int32_t hx1 = std::max<std::int32_t>((std::int32_t)aabb.L, 0);
		std::int32_t hx2 = std::min((std::int32_t)std::ceil(aabb.R), limitRightPx - 1);
		std::int32_t hy1 = std::clamp<std::int32_t>((std::int32_t)aabb.T, 0, limitBottomPx - 2);
		std::int32_t hy2 = std::clamp<std::int32_t>((std::int32_t)std::ceil(aabb.B), 1, limitBottomPx - 1);

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
					} else if (params.UsedWeaponType == WeaponType::Freezer && tile.DestructFrameIndex < GetTileDestructibleFrameCount(tile)) {
						std::int32_t tx = x * TileSet::DefaultTileSize + TileSet::DefaultTileSize / 2;
						std::int32_t ty = y * TileSet::DefaultTileSize + TileSet::DefaultTileSize / 2;
						_owner->OnTileFrozen(tx, ty);
						return false;
					}
				} else if (tile.DestructType == TileDestructType::Special && (params.DestructType & TileDestructType::Special) == TileDestructType::Special) {
					if ((params.DestructType & TileDestructType::VerticalMove) != TileDestructType::VerticalMove ||
						(y + 1) * TileSet::DefaultTileSize <= (hy1 + 8) || (hy2 - 8) <= y * TileSet::DefaultTileSize) {
						std::int32_t amount = 1;
						if (AdvanceDestructibleTileAnimation(tile, x, y, amount, "SceneryDestruct"_s)) {
							params.TilesDestroyed++;
							goto RecheckTile;
						}
					}
				} else if (tile.DestructType == TileDestructType::Speed && (params.DestructType & TileDestructType::Speed) == TileDestructType::Speed) {
					std::int32_t amount = 1;
					if (tile.TileParams <= params.Speed && AdvanceDestructibleTileAnimation(tile, x, y, amount, "SceneryDestruct"_s)) {
						params.TilesDestroyed++;
						goto RecheckTile;
					}
				} else if (tile.DestructType == TileDestructType::Collapse && (params.DestructType & TileDestructType::Collapse) == TileDestructType::Collapse) {
					// O(1) membership check via a runtime tile flag instead of scanning _activeCollapsingTiles
					if ((tile.Flags & LayerTileFlags::Collapsing) != LayerTileFlags::Collapsing) {
						tile.Flags |= LayerTileFlags::Collapsing;
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

					// The clipped pixel window below is always non-empty for a covered tile, so a fully
					// filled mask is guaranteed to collide - skip the per-pixel scan (flips don't matter
					// for a fully filled mask)
					if (tileSet->IsTileMaskFilled(tileId)) {
						return false;
					}

					std::int32_t tx = x * TileSet::DefaultTileSize;
					std::int32_t ty = y * TileSet::DefaultTileSize;

					std::int32_t left = std::max<std::int32_t>(hx1 - tx, 0);
					std::int32_t right = std::min(hx2 - tx, TileSet::DefaultTileSize - 1);
					std::int32_t top = std::max<std::int32_t>(hy1 - ty, 0);
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

					// Fast path: for tiles whose every column is vertically contiguous, an exact
					// per-column span overlap test replaces the per-pixel scan. left/right/top/bottom
					// are already flip-adjusted into mask space, and top/bottom are still row indices here.
					if (tileSet->IsColumnContiguous(tileId)) {
						const std::uint8_t* spans = tileSet->GetColumnSpans(tileId);
						for (std::int32_t rx = left; rx <= right; rx++) {
							std::int32_t spanTop = spans[rx * 2];
							std::int32_t spanBottom = spans[rx * 2 + 1];
							// Empty columns have spanTop == 0xFF, which never satisfies spanTop <= bottom
							if (spanTop <= bottom && spanBottom >= top) {
								return false;
							}
						}
						continue;
					}

					// Packed mask: one 32-bit row word tests the whole [left..right] column range at once
					const std::uint8_t* mask = tileSet->GetTileMask(tileId);
					const std::uint32_t rangeMask = (~0u >> (31 - right)) & (~0u << left);
					for (std::int32_t ry = top; ry <= bottom; ry++) {
						if (TileSet::GetTileMaskRow(mask, ry) & rangeMask) {
							return false;
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
		if (aabb.B >= limitBottomPx && _pitType == PitType::StandOnPlatform) {
			return false;
		}

		// Check all covered tiles for collisions; if all are empty, no need to do pixel collision checking
		std::int32_t hx1 = std::max<std::int32_t>((std::int32_t)aabb.L, 0);
		std::int32_t hx2 = std::min((std::int32_t)std::ceil(aabb.R), limitRightPx - 1);
		std::int32_t hy1 = std::clamp<std::int32_t>((std::int32_t)aabb.T, 0, limitBottomPx - 2);
		std::int32_t hy2 = std::clamp<std::int32_t>((std::int32_t)std::ceil(aabb.B), 1, limitBottomPx - 1);

		std::int32_t hx1t = hx1 / TileSet::DefaultTileSize;
		std::int32_t hx2t = hx2 / TileSet::DefaultTileSize;
		std::int32_t hy1t = hy1 / TileSet::DefaultTileSize;
		std::int32_t hy2t = hy2 / TileSet::DefaultTileSize;

		auto* sprLayerLayout = _layers[_sprLayerIndex].Layout.get();

		for (std::int32_t y = hy1t; y <= hy2t; y++) {
			for (std::int32_t x = hx1t; x <= hx2t; x++) {
				LayerTile& tile = sprLayerLayout[y * layoutSize.X + x];

				if ((tile.DestructType & TileDestructType::Weapon) == TileDestructType::Weapon && (params.DestructType & TileDestructType::Weapon) == TileDestructType::Weapon) {
					if (tile.DestructFrameIndex < GetTileDestructibleFrameCount(tile) &&
						((tile.TileParams & (1 << (std::uint16_t)params.UsedWeaponType)) != 0 || params.UsedWeaponType == WeaponType::Freezer)) {
						return true;
					}
				} else if ((tile.DestructType & TileDestructType::Special) == TileDestructType::Special && (params.DestructType & TileDestructType::Special) == TileDestructType::Special) {
					if ((params.DestructType & TileDestructType::VerticalMove) != TileDestructType::VerticalMove ||
						(y + 1) * TileSet::DefaultTileSize <= (hy1 + 8) || (hy2 - 8) <= y * TileSet::DefaultTileSize) {
						if (tile.DestructFrameIndex < GetTileDestructibleFrameCount(tile)) {
							return true;
						}
					}
				} else if ((tile.DestructType & TileDestructType::Speed) == TileDestructType::Speed && (params.DestructType & TileDestructType::Speed) == TileDestructType::Speed) {
					if (tile.DestructFrameIndex < GetTileDestructibleFrameCount(tile) && tile.TileParams <= params.Speed) {
						return true;
					}
				} else if ((tile.DestructType & TileDestructType::Collapse) == TileDestructType::Collapse && (params.DestructType & TileDestructType::Collapse) == TileDestructType::Collapse) {
					if ((tile.Flags & LayerTileFlags::Collapsing) != LayerTileFlags::Collapsing) {
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

					// The clipped pixel window below is always non-empty for a covered tile, so a fully
					// filled mask is guaranteed to collide - skip the per-pixel scan (flips don't matter
					// for a fully filled mask)
					if (tileSet->IsTileMaskFilled(tileId)) {
						return false;
					}

					std::int32_t tx = x * TileSet::DefaultTileSize;
					std::int32_t ty = y * TileSet::DefaultTileSize;

					std::int32_t left = std::max<std::int32_t>(hx1 - tx, 0);
					std::int32_t right = std::min(hx2 - tx, TileSet::DefaultTileSize - 1);
					std::int32_t top = std::max<std::int32_t>(hy1 - ty, 0);
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

					// Fast path: for tiles whose every column is vertically contiguous, an exact
					// per-column span overlap test replaces the per-pixel scan. left/right/top/bottom
					// are already flip-adjusted into mask space, and top/bottom are still row indices here.
					if (tileSet->IsColumnContiguous(tileId)) {
						const std::uint8_t* spans = tileSet->GetColumnSpans(tileId);
						for (std::int32_t rx = left; rx <= right; rx++) {
							std::int32_t spanTop = spans[rx * 2];
							std::int32_t spanBottom = spans[rx * 2 + 1];
							// Empty columns have spanTop == 0xFF, which never satisfies spanTop <= bottom
							if (spanTop <= bottom && spanBottom >= top) {
								return false;
							}
						}
						continue;
					}

					// Packed mask: one 32-bit row word tests the whole [left..right] column range at once
					const std::uint8_t* mask = tileSet->GetTileMask(tileId);
					const std::uint32_t rangeMask = (~0u >> (31 - right)) & (~0u << left);
					for (std::int32_t ry = top; ry <= bottom; ry++) {
						if (TileSet::GetTileMaskRow(mask, ry) & rangeMask) {
							return false;
						}
					}
				}
			}
		}

		return false;
	}

	SuspendType TileMap::GetTileSuspendState(float x, float y)
	{
		constexpr std::int32_t Tolerance = 4;

		if (_sprLayerIndex == -1) {
			return SuspendType::None;
		}

		std::int32_t tx = (std::int32_t)x / TileSet::DefaultTileSize;
		std::int32_t ty = (std::int32_t)y / TileSet::DefaultTileSize;

		Vector2i layoutSize = _layers[_sprLayerIndex].LayoutSize;
		if (tx < 0 || ty < 0 || tx >= layoutSize.X || ty >= layoutSize.Y) {
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

		const std::uint8_t* mask = tileSet->GetTileMask(tileId);

		std::int32_t rx = (std::int32_t)x & 31;
		std::int32_t ry = (std::int32_t)y & 31;

		if ((tile.Flags & LayerTileFlags::FlipX) == LayerTileFlags::FlipX) {
			rx = (TileSet::DefaultTileSize - 1 - rx);
		}
		if ((tile.Flags & LayerTileFlags::FlipY) == LayerTileFlags::FlipY) {
			ry = (TileSet::DefaultTileSize - 1 - ry);
		}

		// Walk the tolerance window bottom-up, testing column rx's bit in each packed row. The window is
		// clipped to the tile on purpose - it decides where exactly the player ends up hanging.
		const std::int32_t top = std::max<std::int32_t>(ry - Tolerance, 0);
		const std::int32_t bottom = std::min(ry + Tolerance, TileSet::DefaultTileSize - 1);

		for (std::int32_t row = bottom; row >= top; row--) {
			if (TileSet::IsTileMaskBitSet(mask, rx, row)) {
				return tile.HasSuspendType;
			}
		}

		return SuspendType::None;
	}

	SuspendType TileMap::GetTileSuspendState(float x, float y, float toleranceX, float toleranceUp, float toleranceDown, Vector2f& snapOffset)
	{
		snapOffset = Vector2f(0.0f, 0.0f);

		if (_sprLayerIndex == -1) {
			return SuspendType::None;
		}

		TileMapLayer& layer = _layers[_sprLayerIndex];

		std::int32_t originX = (std::int32_t)x;
		std::int32_t originY = (std::int32_t)y;
		std::int32_t left = std::max<std::int32_t>(originX - (std::int32_t)toleranceX, 0);
		std::int32_t right = std::min<std::int32_t>(originX + (std::int32_t)toleranceX, layer.LayoutSize.X * TileSet::DefaultTileSize - 1);
		std::int32_t top = std::max<std::int32_t>(originY - (std::int32_t)toleranceUp, 0);
		std::int32_t bottom = std::min<std::int32_t>(originY + (std::int32_t)toleranceDown, layer.LayoutSize.Y * TileSet::DefaultTileSize - 1);
		if (left > right || top > bottom) {
			return SuspendType::None;
		}

		std::int32_t txLeft = left / TileSet::DefaultTileSize;
		std::int32_t txRight = right / TileSet::DefaultTileSize;
		std::int32_t tyTop = top / TileSet::DefaultTileSize;

		// Walk the tolerance box bottom-up, so the player always attaches to the lowest point in reach
		for (std::int32_t ty = bottom / TileSet::DefaultTileSize; ty >= tyTop; ty--) {
			std::int32_t tileOriginY = ty * TileSet::DefaultTileSize;
			std::int32_t rowTop = std::max<std::int32_t>(top - tileOriginY, 0);
			std::int32_t rowBottom = std::min(bottom - tileOriginY, TileSet::DefaultTileSize - 1);

			// Attachment points of all tiles in the row range are collected first, because the closest
			// one can be in any of them
			SuspendType foundType[TileSet::DefaultTileSize];
			std::int32_t foundX[TileSet::DefaultTileSize];
			for (std::int32_t row = rowTop; row <= rowBottom; row++) {
				foundType[row] = SuspendType::None;
				foundX[row] = 0;
			}

			for (std::int32_t tx = txLeft; tx <= txRight; tx++) {
				LayerTile& tile = layer.Layout[tx + ty * layer.LayoutSize.X];
				if (tile.HasSuspendType == SuspendType::None) {
					continue;
				}

				std::int32_t tileId = ResolveTileID(tile);
				TileSet* tileSet = ResolveTileSet(tileId);
				if (tileSet == nullptr) {
					continue;
				}

				bool flipX = ((tile.Flags & LayerTileFlags::FlipX) == LayerTileFlags::FlipX);
				bool flipY = ((tile.Flags & LayerTileFlags::FlipY) == LayerTileFlags::FlipY);

				std::int32_t tileOriginX = tx * TileSet::DefaultTileSize;
				std::int32_t columnLeft = std::max<std::int32_t>(left - tileOriginX, 0);
				std::int32_t columnRight = std::min(right - tileOriginX, TileSet::DefaultTileSize - 1);
				if (flipX) {
					std::int32_t columnLeftFlipped = (TileSet::DefaultTileSize - 1 - columnRight);
					columnRight = (TileSet::DefaultTileSize - 1 - columnLeft);
					columnLeft = columnLeftFlipped;
				}

				// Packed mask: one 32-bit row word tests the whole column range at once
				const std::uint8_t* mask = tileSet->GetTileMask(tileId);
				const std::uint32_t rangeMask = (~0u >> (31 - columnRight)) & (~0u << columnLeft);

				for (std::int32_t row = rowTop; row <= rowBottom; row++) {
					std::uint32_t maskRow = TileSet::GetTileMaskRow(mask, flipY ? (TileSet::DefaultTileSize - 1 - row) : row) & rangeMask;
					if (maskRow == 0) {
						continue;
					}

					for (std::int32_t column = columnLeft; column <= columnRight; column++) {
						if ((maskRow & (1u << column)) == 0) {
							continue;
						}

						std::int32_t candidateX = tileOriginX + (flipX ? (TileSet::DefaultTileSize - 1 - column) : column);
						if (foundType[row] == SuspendType::None || std::abs(candidateX - originX) < std::abs(foundX[row] - originX)) {
							foundType[row] = tile.HasSuspendType;
							foundX[row] = candidateX;
						}
					}
				}
			}

			for (std::int32_t row = rowBottom; row >= rowTop; row--) {
				if (foundType[row] != SuspendType::None) {
					snapOffset = Vector2f((float)foundX[row] - x, (float)(tileOriginY + row) - y);
					return foundType[row];
				}
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

	bool TileMap::AdvanceDestructibleTileAnimation(LayerTile& tile, std::int32_t tx, std::int32_t ty, std::int32_t& amount, StringView soundName)
	{
		if (amount <= 0) {
			return false;
		}

		if (tile.DestructAnimation >= _animatedTilesOffset) {
			AnimatedTile& anim = _animatedTiles[tile.DestructAnimation - _animatedTilesOffset];
			std::int32_t max = (std::int32_t)anim.Tiles.size() - 2;
			if (tile.DestructFrameIndex < max) {
				// Tile not destroyed yet, advance counter by one
				std::int32_t frameCount = std::min(amount, max - tile.DestructFrameIndex);

				tile.DestructFrameIndex = std::int16_t(tile.DestructFrameIndex + frameCount);
				tile.TileID = anim.Tiles[tile.DestructFrameIndex].TileID;
				if (tile.DestructFrameIndex >= max) {
					if (!soundName.empty()) {
						_owner->PlayCommonSfx(soundName, Vector3f(tx * TileSet::DefaultTileSize + (TileSet::DefaultTileSize / 2),
							ty * TileSet::DefaultTileSize + (TileSet::DefaultTileSize / 2), 0.0f), 1.0f, Random().FastFloat(0.9f, 1.1f));
					}
					CreateTileDebris(anim.Tiles[anim.Tiles.size() - 1].TileID, tx, ty);
				}

				amount -= frameCount;

				_owner->OnAdvanceDestructibleTileAnimation(tx, ty, frameCount);
				return true;
			}
		} else {
			if (tile.DestructFrameIndex == 0) {
				std::int32_t frameCount = 1;
				tile.DestructFrameIndex = std::int16_t(tile.DestructFrameIndex + frameCount);
				tile.TileID = 0; // Set to empty tile

				if (!soundName.empty()) {
					_owner->PlayCommonSfx(soundName, Vector3f(tx * TileSet::DefaultTileSize + (TileSet::DefaultTileSize / 2),
						ty * TileSet::DefaultTileSize + (TileSet::DefaultTileSize / 2), 0.0f), 1.0f, Random().FastFloat(0.9f, 1.1f));
				}
				CreateTileDebris(tile.DestructAnimation, tx, ty);

				amount -= frameCount;
				_owner->OnAdvanceDestructibleTileAnimation(tx, ty, frameCount);
				return true;
			}
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

		auto it = _activeCollapsingTiles.begin();
		while (it != _activeCollapsingTiles.end()) {
			Vector2i tilePos = *it;
			auto& tile = _layers[_sprLayerIndex].Layout[tilePos.X + tilePos.Y * layoutSize.X];
			if (tile.TileParams == 0) {
				std::int32_t amount = 1;
				if (!AdvanceDestructibleTileAnimation(tile, tilePos.X, tilePos.Y, amount, "SceneryCollapse"_s)) {
					tile.DestructType = TileDestructType::None;
					tile.Flags = tile.Flags & ~LayerTileFlags::Collapsing;
					it = _activeCollapsingTiles.eraseUnordered(it);
					continue;
				} else {
					tile.TileParams = 4;
				}
			} else {
				tile.TileParams--;
			}
			++it;
		}
	}

	void TileMap::DrawLayer(RenderQueue& renderQueue, TileMapLayer& layer, const Rectf& cullingRect, Vector2f viewCenter)
	{
		ZoneScopedNC("Layer", 0xA09359);

		if (!layer.Visible) {
			return;
		}

		Vector2i tileCount = layer.LayoutSize;

		// Get current layer offsets and speeds
		float loX = layer.Description.OffsetX;
		float loY = layer.Description.OffsetY - (layer.Description.UseInherentOffset ? (cullingRect.H - 200) / 2 : 0) + 1;

		// Find out coordinates for a tile from outside the boundaries from topleft corner of the screen 
		float x1 = cullingRect.X - HardcodedOffset;
		float y1 = cullingRect.Y - HardcodedOffset;

		// Without the warped background pass, a sky layer is just an ordinary repeating tile layer and has to
		// be drawn with the plain tile renderer. Leaving it on the TexturedBackground shader made every tile
		// its own draw call - that effect is not batched, so a single 8x8 sky layer covering the screen cost
		// a few hundred of them - and fed flat tiles through a shader that samples a whole warped sheet.
		const bool isProceduralLayer = (layer.Description.RendererType == LayerRendererType::Circle
			? SupportsTexturedBackgroundCircle
			: (layer.Description.RendererType == LayerRendererType::Sky ? SupportsTexturedBackground : false));

		LayerRendererType rendererType = layer.Description.RendererType;
		Vector4f layerColor = layer.Description.Color;
		if (!isProceduralLayer && rendererType >= LayerRendererType::Sky && rendererType <= LayerRendererType::Circle) {
			rendererType = LayerRendererType::Default;
			// A sky layer's colour is the horizon tint the warped background shader blends towards (and its
			// W enables the star field), not a per-tile modulation. Handing it to the plain tile renderer
			// darkened the whole layer - and a level with a black horizon lost its background entirely.
			layerColor = Vector4f(1.0f, 1.0f, 1.0f, 1.0f);
		}

		if (isProceduralLayer && tileCount.Y == 8 && tileCount.X == 8) {
			constexpr float PerspectiveSpeedX = 0.4f;
			constexpr float PerspectiveSpeedY = 0.16f;
			RenderTexturedBackground(renderQueue, cullingRect, viewCenter, layer, x1 * PerspectiveSpeedX + loX, y1 * PerspectiveSpeedY + loY);
		} else {
			float xt, yt;
			switch (layer.Description.SpeedModelX) {
				case LayerSpeedModel::AlwaysOnTop:
					xt = -HardcodedOffset;
					break;
				case LayerSpeedModel::FitLevel: {
					float progress = (float)viewCenter.X / (_layers[_sprLayerIndex].LayoutSize.X * TileSet::DefaultTileSize);
					xt = std::clamp(progress, 0.0f, 1.0f)
						* ((layer.LayoutSize.X * TileSet::DefaultTileSize) - cullingRect.W + HardcodedOffset)
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
					xt = TranslateCoordinate(x1, layer.Description.SpeedX, loX, cullingRect.W, false);
					break;
			}
			switch (layer.Description.SpeedModelY) {
				case LayerSpeedModel::AlwaysOnTop:
					yt = -HardcodedOffset;
					break;
				case LayerSpeedModel::FitLevel: {
					float progress = (float)viewCenter.Y / (_layers[_sprLayerIndex].LayoutSize.Y * TileSet::DefaultTileSize);
					yt = std::clamp(progress, 0.0f, 1.0f)
						* ((layer.LayoutSize.Y * TileSet::DefaultTileSize) - cullingRect.H + HardcodedOffset)
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

					yt = TranslateCoordinate(y1, layer.Description.SpeedY, loY, cullingRect.H, true);
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
			
			// Save the tile X at the left border so that we can roll back to it at the start of every row
			std::int32_t tileXs = tileX;

			// Calculate the last coordinates we want to draw to
			float x3 = x1 + (TileSet::DefaultTileSize * 2) + cullingRect.W;
			float y3 = y1 + (TileSet::DefaultTileSize * 2) + cullingRect.H;

#if defined(WITH_RHI_SOFTWARE)
			// Whether every non-zero entry of the sprite palette (row 0, the one tile layers sample) is fully
			// opaque. Combined with a tile's IsTileFilled() flag (no index-0 texel, the transparent base
			// entry) this proves the tile draws all 32x32 pixels opaque, so it may go out with blending off
			// (see below). Scanned per call because scripts can recolor the palette at any time; very few
			// tilesets ship translucent palette entries, and those simply keep the blended path everywhere.
			bool spritePaletteOpaque = true;
			{
				auto palettes = ContentResolver::Get().GetPalettes();
				for (std::int32_t i = 1; i < ContentResolver::ColorsPerPalette; i++) {
					if ((palettes[i] >> 24) != 255) {
						spritePaletteOpaque = false;
						break;
					}
				}
			}
#endif

			// Standard tile layers backed by a single tileset are drawn as one mesh (one draw call for the whole
			// visible layer, or one per texture chunk when the device texture-size limit split the tileset
			// atlas). Other renderer types (tinted/solid) and multi-tileset levels fall back to one command
			// per tile.
#if defined(TILEMAP_USE_SINGLE_DRAW)
			bool meshMode = (rendererType == LayerRendererType::Default && _tileSets.size() == 1 && _tileSets[0].Data != nullptr);
			TileSet* meshTileSet = (meshMode ? _tileSets[0].Data.get() : nullptr);
			// One vertex buffer per chunk, rented on first use - a layer usually touches only some of them.
			// Indices rather than pointers, because renting can grow (and so reallocate) _meshVertices.
			SmallVector<std::int32_t, 2> chunkVertices;
			if DEATH_LIKELY(meshMode) {
				chunkVertices.resize(meshTileSet->GetTextureCount(), -1);
			}
#	if defined(TILEMAP_GROUP_MESH_BY_TILE)
			_meshTileEntries.clear();
#	endif
#endif

			// Remembers the reciprocals of the last tile texture's dimensions across the walk below; a layer's
			// tiles overwhelmingly share one texture, so this turns four divisions per tile into two per layer
			Texture* lastTileTexture = nullptr;
			float lastTexInvW = 0.0f, lastTexInvH = 0.0f;

			// Y is the OUTER loop, so the walk runs along the layout's rows. `Layout` is indexed
			// `tileX + tileY * LayoutSize.X`, so with X outside (as it used to be) the inner step jumped a
			// whole row - 12 bytes per LayerTile times a level width of a couple of hundred, about 3 KB - and
			// every one of the ~200 tiles a layer visits landed on its own cache line. Along a row instead,
			// eighteen consecutive tiles share four lines. The same tiles are visited either way; only the
			// order the quads are appended in changes, and tiles of one layer never overlap.
			std::int32_t tile_yo = -1;
			for (float y2 = y1; y2 <= y3; y2 += TileSet::DefaultTileSize) {
				// A compare-and-wrap rather than a modulo: tileY is always already below tileCount.Y, so the
				// two agree, and an integer division is about 36 cycles on the consoles' in-order cores
				if (++tileY >= tileCount.Y) { tileY = 0; }
				tile_yo++;
				if (!layer.Description.RepeatY) {
					// If the current tile isn't in the first iteration of the layer vertically, skip this row
					if (tileAbsY + tile_yo + 1 < 0 || tileAbsY + tile_yo + 1 >= tileCount.Y) {
						continue;
					}
				}
				tileX = tileXs;
				std::int32_t tile_xo = -1;
				for (float x2 = x1; x2 <= x3; x2 += TileSet::DefaultTileSize) {
					// As above, and this one is per TILE rather than per row
					if (++tileX >= tileCount.X) { tileX = 0; }
					tile_xo++;

					LayerTile tile = layer.Layout[tileX + tileY * layer.LayoutSize.X];

					if (!layer.Description.RepeatX) {
						// If the current tile isn't in the first iteration of the layer horizontally, don't draw it
						if (tileAbsX + tile_xo + 1 < 0 || tileAbsX + tile_xo + 1 >= tileCount.X) {
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

#if defined(WITH_RHI_SOFTWARE)
					// Whether every pixel this tile samples is provably opaque (see spritePaletteOpaque
					// above). Read before ResolveTextureDiffuse(), which rebases the ID into its texture chunk
					const bool tileFilled = tileSet->IsTileFilled(tileId) && (!tileSet->IsIndexed || spritePaletteOpaque);
#endif

#if defined(TILEMAP_USE_SINGLE_DRAW)
					// Which texture chunk holds this tile. Has to be read BEFORE ResolveTextureDiffuse(), which
					// takes the ID by reference and rebases it into that chunk - afterwards the ID is always
					// below TilesPerTexture and this would collapse to chunk 0, drawing the whole layer out of
					// the first texture. A no-op single-texture lookup normally; only a device texture-size
					// limit small enough to split the tileset atlas (the consoles) makes it matter.
					// Through the atlas mapping first: where the atlas holds only the tiles this level uses, the
					// chunk a tile lives in follows its packed slot, not its ID. Kept in a local of its own so
					// ResolveTextureDiffuse() below still maps the ID itself exactly once.
					const std::int32_t tileSlot = tileSet->MapToAtlasSlot(tileId);
					const std::int32_t tileChunk = (tileSet->TilesPerTexture > 0 && tileSlot >= tileSet->TilesPerTexture
						? tileSlot / tileSet->TilesPerTexture : 0);
#endif
					Texture* tileTexture = tileSet->ResolveTextureDiffuse(tileId);
					if DEATH_UNLIKELY(tileTexture == nullptr) {
						continue;
					}

					// The two reciprocals depend only on the texture, and a layer's tiles overwhelmingly share
					// one, so they are remembered across iterations instead of dividing four times per tile.
					// The tile's row and column come from ONE integer division as well - the remainder is
					// recovered with a multiply. That is six divisions per tile removed, and on the consoles'
					// in-order cores a division is both slow (about 29 cycles for `div.s`, 36 for integer) and
					// unpipelined, which made this loop's arithmetic mostly waiting.
					if DEATH_UNLIKELY(tileTexture != lastTileTexture) {
						lastTileTexture = tileTexture;
						const Vector2i texSize = tileTexture->GetSize();
						lastTexInvW = (texSize.X > 0 ? 1.0f / float(texSize.X) : 0.0f);
						lastTexInvH = (texSize.Y > 0 ? 1.0f / float(texSize.Y) : 0.0f);
					}
					const std::int32_t tileRow = tileId / tileSet->TilesPerRow;
					const std::int32_t tileCol = tileId - tileRow * tileSet->TilesPerRow;
					float texScaleX = TileSet::DefaultTileSize * lastTexInvW;
					float texBiasX = (tileCol * (TileSet::DefaultTileSize + 2.0f) + 1.0f) * lastTexInvW;
					float texScaleY = TileSet::DefaultTileSize * lastTexInvH;
					float texBiasY = (tileRow * (TileSet::DefaultTileSize + 2.0f) + 1.0f) * lastTexInvH;

					// ToDo: Flip normal map somehow
					if ((tile.Flags & LayerTileFlags::FlipX) == LayerTileFlags::FlipX) {
						texBiasX += texScaleX;
						texScaleX *= -1;
					}
					if ((tile.Flags & LayerTileFlags::FlipY) == LayerTileFlags::FlipY) {
						texBiasY += texScaleY;
						texScaleY *= -1;
					}

					float x2r = x2, y2r = y2;
					if (!PreferencesCache::UnalignedViewport) {
						// MIPS and SH-4 have no floor instruction, so `std::floor` here is a real `jal floorf`
						// into libm - twice per tile. A truncating cast with the negative correction is the
						// same value for any coordinate that fits in an int32, which a tile position on
						// screen always does.
						const float tx = float(std::int32_t(x2r)), ty = float(std::int32_t(y2r));
						x2r = (tx > x2r ? tx - 1.0f : tx);
						y2r = (ty > y2r ? ty - 1.0f : ty);
					}

#if defined(TILEMAP_USE_SINGLE_DRAW)
					if DEATH_LIKELY(meshMode) {
#	if defined(TILEMAP_GROUP_MESH_BY_TILE)
						// Held back so the whole layer can be emitted grouped by atlas slot below; the quad this
						// would have appended is fully described by what goes into the entry
						_meshTileEntries.push_back({ x2r, y2r, texScaleX, texBiasX, texScaleY, texBiasY,
							tile.Alpha / 255.0f, (std::uint16_t)tileSlot, (std::uint16_t)tileChunk });
#	else
						// Accumulate this tile into its chunk's mesh; the layer tint and palette are applied once
						// per emitted mesh in EmitMesh(). The per-tile alpha rides along in the vertex color.
						std::int32_t& verticesIndex = chunkVertices[tileChunk];
						if (verticesIndex < 0) {
							verticesIndex = RentMeshVertices();
						}
						AppendTileQuad(_meshVertices[verticesIndex], x2r, y2r, (float)TileSet::DefaultTileSize,
							texScaleX, texBiasX, texScaleY, texBiasY, tile.Alpha / 255.0f);
#	endif
						continue;
					}
#endif

					TileCommandUniforms* commandUniforms;
					auto command = RentRenderCommand(rendererType, tileSet->IsIndexed, &commandUniforms);
					command->SetType(RenderCommand::Type::TileMap);
					command->GetMaterial().SetBlendingFactors(BlendingFactor::SrcAlpha, BlendingFactor::OneMinusSrcAlpha);

					commandUniforms->TexRect->SetFloatValue(texScaleX, texBiasX, texScaleY, texBiasY);
					commandUniforms->SpriteSize->SetFloatValue(TileSet::DefaultTileSize, TileSet::DefaultTileSize);

					Vector4f color = layerColor;
					color.W *= tile.Alpha / 255.0f;
					commandUniforms->Color->SetFloatVector(color.Data());

#if defined(WITH_RHI_SOFTWARE)
					// A fully opaque, unfaded tile draws the exact same bytes with blending off (src-over of
					// an opaque source is a copy), so hint it: the command keeps its painter's-order spot in
					// the transparent queue, but the software rasterizer overwrites instead of blending - and
					// culls every draw the tile hides (SwTileRenderer's reverse-painter cull: parallax layers
					// and actors behind solid ground stop costing anything). Worth nothing to a GPU backend -
					// it would only split its sprite batches - so it stays software-only.
					if (tileFilled && color.W >= 1.0f && rendererType == LayerRendererType::Default) {
						command->GetMaterial().SetOpaqueContentHint(true);
					}
#endif

					command->SetTransformation(Matrix4x4f::Translation(x2r, y2r, 0.0f));
					command->SetLayer(layer.Description.Depth);
					// Tiles use the default sprite palette (row 0, offset 0); binds the shared palette texture when
					// indexed. Open-coded rather than through ContentResolver::BindSpritePalette() so the palette
					// offset goes through the cached uniform instead of a by-name lookup per tile.
					command->GetMaterial().SetTexture(0, *tileTexture);
					if (tileSet->IsIndexed) {
						Texture* paletteTexture = ContentResolver::Get().GetPaletteTexture();
						if (paletteTexture != nullptr) {
							command->GetMaterial().SetTexture(1, *paletteTexture);
						}
						if (commandUniforms->PaletteOffset != nullptr) {
							commandUniforms->PaletteOffset->SetFloatValue(0.0f);
						}
					}

					renderQueue.AddCommand(command);
				}
			}

#if defined(TILEMAP_GROUP_MESH_BY_TILE)
			if DEATH_LIKELY(meshMode) {
				// The tiles of this layer are now emitted grouped by the atlas slot they sample rather than in
				// the screen order they were visited in, which is what lets the consumer's texture residency
				// serve every repeat of a tile id from the window already loaded (see the note above the gate).
				// Safe because a layer's visible tiles occupy DISJOINT screen cells - the two loops above step
				// one tile cell at a time and never revisit one, whatever the layer repeats - so nothing within
				// a layer's mesh is drawn over anything else in it and the submission order is unobservable.
				// Layers are never mixed: this reorders inside one DrawLayer() call only.
				//
				// A counting sort rather than a comparison one: the key is the packed atlas slot, so its range
				// is known and small (the atlas holds only the tiles the level uses), and a few hundred
				// dependent RDRAM loads per compare would have eaten the upload traffic it saves. The bucket
				// array is offset by the lowest slot on screen, which is usually a small fraction of the atlas.
				auto appendEntry = [&](const MeshTileEntry& entry) {
					std::int32_t& verticesIndex = chunkVertices[entry.Chunk];
					if (verticesIndex < 0) {
						verticesIndex = RentMeshVertices();
					}
					AppendTileQuad(_meshVertices[verticesIndex], entry.X, entry.Y, (float)TileSet::DefaultTileSize,
						entry.TexScaleX, entry.TexBiasX, entry.TexScaleY, entry.TexBiasY, entry.Alpha);
				};

				const std::uint32_t entryCount = (std::uint32_t)_meshTileEntries.size();
				if (entryCount > 1 && entryCount <= MaxGroupedMeshTiles) {
					std::uint32_t minSlot = UINT32_MAX, maxSlot = 0;
					for (const auto& entry : _meshTileEntries) {
						if (entry.Slot < minSlot) { minSlot = entry.Slot; }
						if (entry.Slot > maxSlot) { maxSlot = entry.Slot; }
					}
					const std::uint32_t bucketCount = maxSlot - minSlot + 2;
					_meshTileBuckets.resize_for_overwrite(bucketCount);
					std::memset(_meshTileBuckets.data(), 0, bucketCount * sizeof(std::uint16_t));
					for (const auto& entry : _meshTileEntries) {
						_meshTileBuckets[entry.Slot - minSlot + 1]++;
					}
					for (std::uint32_t i = 1; i < bucketCount; i++) {
						_meshTileBuckets[i] += _meshTileBuckets[i - 1];
					}
					_meshTileOrder.resize_for_overwrite(entryCount);
					for (std::uint32_t i = 0; i < entryCount; i++) {
						_meshTileOrder[_meshTileBuckets[_meshTileEntries[i].Slot - minSlot]++] = (std::uint16_t)i;
					}
					for (std::uint32_t i = 0; i < entryCount; i++) {
						appendEntry(_meshTileEntries[_meshTileOrder[i]]);
					}
				} else {
					// Nothing to group (or more tiles than the 16-bit order array indexes, which no viewport of
					// a tile layer reaches); emitted in the order they were visited
					for (const auto& entry : _meshTileEntries) {
						appendEntry(entry);
					}
				}
			}
#endif

#if defined(TILEMAP_USE_SINGLE_DRAW)
			if DEATH_LIKELY(meshMode) {
				// Whole visible layer is submitted as one command per touched texture chunk (or a few
				// <=64 KB pieces for very large layers). Tiles within a layer never overlap, so the order
				// between chunks doesn't matter - they all share the layer's depth.
				for (std::int32_t chunk = 0; chunk < (std::int32_t)chunkVertices.size(); chunk++) {
					const std::int32_t verticesIndex = chunkVertices[chunk];
					if (verticesIndex < 0 || _meshVertices[verticesIndex].empty()) {
						continue;
					}
					// Tiles use the default sprite palette (row 0, offset 0); every tile accumulated into these
					// vertices resolved to this chunk of the tileset atlas
					EmitMesh(renderQueue, _meshVertices[verticesIndex], *meshTileSet->TextureDiffuse[chunk],
						meshTileSet->IsIndexed, 0, layerColor, layer.Description.Depth, RenderCommand::Type::TileMap, false);
				}
			}
#endif
		}
	}

	float TileMap::TranslateCoordinate(float coordinate, float speed, float offset, std::int32_t viewSize, bool isY)
	{
		std::int32_t alignment = ((isY ? (viewSize - 200) : (viewSize - 320)) / 2) + HardcodedOffset;
		return (coordinate * speed + offset + alignment * (speed - 1.0f));
	}

	RenderCommand* TileMap::RentRenderCommand(LayerRendererType type, bool indexed, TileCommandUniforms** uniforms)
	{
		RenderCommand* command;
		bool freshSlot = false;
		if (_renderCommandsCount < _renderCommands.size()) {
			command = _renderCommands[_renderCommandsCount].get();
			_renderCommandsCount++;
		} else {
			command = _renderCommands.emplace_back(std::make_unique<RenderCommand>()).get();
			_renderCommandsCount++;
			command->GetMaterial().SetBlendingEnabled(true);
			freshSlot = true;
		}
		// Pool slots persist across frames and DrawLayer's opaque-tile fast path may have hinted a slot's
		// content opaque; reset that on every rent (the tile path re-hints below when it still applies)
		command->GetMaterial().SetOpaqueContentHint(false);
		const std::int32_t slot = _renderCommandsCount - 1;
		if (_renderCommandUniforms.size() < _renderCommands.size()) {
			_renderCommandUniforms.resize(_renderCommands.size());
		}

		bool shaderChanged;
		switch (type) {
			case LayerRendererType::Solid: shaderChanged = command->GetMaterial().SetShaderProgramType(Material::ShaderProgramType::SpriteNoTexture); break;
			case LayerRendererType::Tinted: shaderChanged = command->GetMaterial().SetShader(ContentResolver::Get().GetShader(indexed ? PrecompiledShader::TintedPalette : PrecompiledShader::Tinted)); break;
			case LayerRendererType::Sky: shaderChanged = command->GetMaterial().SetShader(ContentResolver::Get().GetShader(PreferencesCache::BackgroundDithering ? PrecompiledShader::TexturedBackgroundDither : PrecompiledShader::TexturedBackground)); break;
			case LayerRendererType::Circle: shaderChanged = command->GetMaterial().SetShader(ContentResolver::Get().GetShader(PreferencesCache::BackgroundDithering ? PrecompiledShader::TexturedBackgroundCircleDither : PrecompiledShader::TexturedBackgroundCircle)); break;
			default: shaderChanged = (indexed
				? command->GetMaterial().SetShader(ContentResolver::Get().GetShader(PrecompiledShader::PaletteRemap))
				: command->GetMaterial().SetShaderProgramType(Material::ShaderProgramType::Sprite)); break;
		}
		if (shaderChanged) {
			command->GetMaterial().ReserveUniformsDataMemory();
			command->GetGeometry().SetDrawParameters(PrimitiveType::TriangleStrip, 0, 4);

			auto* textureUniform = command->GetMaterial().Uniform(Material::TextureUniformName);
			if (textureUniform && textureUniform->GetIntValue(0) != 0) {
				textureUniform->SetIntValue(0); // GL_TEXTURE0
			}
			// Palette shaders (PaletteRemap / TintedPalette) sample the shared palette texture on unit 1
			auto* paletteUniform = command->GetMaterial().Uniform("uTexturePalette");
			if (paletteUniform != nullptr) {
				paletteUniform->SetIntValue(1); // GL_TEXTURE1
			}
		}

		// The block caches - and so these pointers - are only rebuilt by a shader change, so the
		// by-name lookups run once per pool slot instead of once per tile. The refresh runs even when
		// the caller doesn't ask for the uniforms: a shader change rebuilds the instance block, and a
		// later rent of this slot with an unchanged shader must not hand out pointers into the old one
		TileCommandUniforms& cached = _renderCommandUniforms[slot];
		if (shaderChanged || freshSlot) {
			auto* instanceBlock = command->GetInstanceBlock();
			cached.TexRect = (instanceBlock != nullptr ? instanceBlock->GetUniform(Material::TexRectUniformName) : nullptr);
			cached.SpriteSize = (instanceBlock != nullptr ? instanceBlock->GetUniform(Material::SpriteSizeUniformName) : nullptr);
			cached.Color = (instanceBlock != nullptr ? instanceBlock->GetUniform(Material::ColorUniformName) : nullptr);
			cached.PaletteOffset = (instanceBlock != nullptr ? instanceBlock->GetUniform(Material::PaletteOffsetUniformName) : nullptr);
		}
		if (uniforms != nullptr) {
			*uniforms = &cached;
		}

		return command;
	}

#if defined(TILEMAP_USE_SINGLE_DRAW)
	void TileMap::AppendTileQuad(SmallVector<float, 0>& vertices, float x, float y, float size,
		float texScaleX, float texBiasX, float texScaleY, float texBiasY, float alpha)
	{
		// UVs at the quad corners: u = px * texScaleX + texBiasX (px in {0,1}), same for v. Any flip is already
		// folded into the scale/bias by the caller.
		float u0 = texBiasX, v0 = texBiasY;
		float u1 = texScaleX + texBiasX, v1 = texScaleY + texBiasY;
		float xr = x + size, yr = y + size;

		// Two triangles, 8 floats per vertex: position.xy, texcoords.uv, color.rgba (white * per-tile alpha; the
		// layer tint is applied via the command's instance color in EmitMesh)
		std::size_t base = vertices.size();
		// Every float is written below, so the zero-initialization resize() would do first is wasted work
		vertices.resize_for_overwrite(base + FloatsPerQuad);
		float* v = vertices.data() + base;
		auto put = [&](float px, float py, float pu, float pv) {
			*v++ = px; *v++ = py; *v++ = pu; *v++ = pv;
			*v++ = 1.0f; *v++ = 1.0f; *v++ = 1.0f; *v++ = alpha;
		};
		// The four corners in the order of the shared quad index pattern
		put(x,  y,  u0, v0);
		put(xr, y,  u1, v0);
		put(xr, yr, u1, v1);
		put(x,  yr, u0, v1);
	}

	std::int32_t TileMap::RentMeshVertices()
	{
		if (_meshVerticesCount >= (std::int32_t)_meshVertices.size()) {
			_meshVertices.emplace_back();
		}
		std::int32_t verticesIndex = _meshVerticesCount++;
		_meshVertices[verticesIndex].clear();
		return verticesIndex;
	}

	void TileMap::AppendDebrisQuad(SmallVector<float, 0>& vertices, const DestructibleDebris& debris)
	{
		// The sprite shader would have built this quad from the particle's model matrix: a unit quad scaled by
		// Size, rotated around the centre of the drawn area and translated to Pos. The mesh stream is in world
		// space, so the same Translation * RotationZ * Scaling * Translation is folded into the four corners here
		// - which is the whole point, as it costs less than the three 4x4 multiplies the chain used to.
		const float c = std::cos(debris.Angle);
		const float s = std::sin(debris.Angle);
		const float ns = std::sin(-debris.Angle);	// Never "-s", see the note in Matrix4x4::RotationZ()
		const float xx = c * debris.Scale, xy = s * debris.Scale;
		const float yx = ns * debris.Scale, yy = c * debris.Scale;
		// Local extent of the quad before the rotation, centred on the drawn area (see GetFrameOffset())
		const float localX = debris.FrameOffset.X - debris.Size.X * 0.5f;
		const float localY = debris.FrameOffset.Y - debris.Size.Y * 0.5f;
		// One corner plus the two rotated edge vectors, so the remaining three corners are additions
		const float x0 = debris.Pos.X + xx * localX + yx * localY;
		const float y0 = debris.Pos.Y + xy * localX + yy * localY;
		const float ex = xx * debris.Size.X, ey = xy * debris.Size.X;
		const float fx = yx * debris.Size.Y, fy = yy * debris.Size.Y;

		// UVs at the quad corners, exactly as the sprite vertex stage maps them: u = px * texScaleX + texBiasX
		const float u0 = debris.TexBiasX, v0 = debris.TexBiasY;
		const float u1 = debris.TexScaleX + debris.TexBiasX, v1 = debris.TexScaleY + debris.TexBiasY;

		// Same 8-float layout and same corner order as AppendTileQuad(), so a particle is still recognized as
		// a quad by the backends that fold the two triangles back into one four-vertex strip
		std::size_t base = vertices.size();
		vertices.resize_for_overwrite(base + FloatsPerQuad);
		float* v = vertices.data() + base;
		auto put = [&](float px, float py, float pu, float pv) {
			*v++ = px; *v++ = py; *v++ = pu; *v++ = pv;
			*v++ = 1.0f; *v++ = 1.0f; *v++ = 1.0f; *v++ = debris.Alpha;
		};
		put(x0,           y0,           u0, v0);
		put(x0 + ex,      y0 + ey,      u1, v0);
		put(x0 + ex + fx, y0 + ey + fy, u1, v1);
		put(x0 + fx,      y0 + fy,      u0, v1);
	}

	void TileMap::EmitMesh(RenderQueue& renderQueue, SmallVector<float, 0>& vertices, const Texture& texture, bool indexed,
		std::uint16_t paletteOffset, const Vector4f& color, std::uint16_t depth, RenderCommand::Type type, bool additiveBlending)
	{
		// Cap quads per command to what one draw can actually reach through the shared streaming buffers, queried
		// at runtime from the buffer manager (same source RenderBatcher uses) instead of a fixed size - so it
		// adapts to the configured buffer sizes rather than assuming 64 KB. Counting in quads is what puts the
		// chunk boundaries on quad boundaries, whichever form a quad takes.
		const std::uint32_t maxQuadsPerChunk = RenderResources::GetMaxQuadsPerDraw(FloatsPerVertex);
		const std::uint16_t* quadIndices = RenderResources::GetQuadIndices();

		std::uint32_t totalQuads = (std::uint32_t)(vertices.size() / FloatsPerQuad);
		for (std::uint32_t firstQuad = 0; firstQuad < totalQuads; firstQuad += maxQuadsPerChunk) {
			std::uint32_t count = std::min(maxQuadsPerChunk, totalQuads - firstQuad);

			if (_meshCommandCount >= (std::int32_t)_meshCommands.size()) {
				auto& newCommand = _meshCommands.emplace_back(std::make_unique<RenderCommand>());
				newCommand->GetMaterial().SetBlendingEnabled(true);
			}
			RenderCommand* command = _meshCommands[_meshCommandCount++].get();

			command->SetType(type);
			command->GetMaterial().SetBlendingFactors(BlendingFactor::SrcAlpha,
				additiveBlending ? BlendingFactor::One : BlendingFactor::OneMinusSrcAlpha);

			bool shaderChanged = command->GetMaterial().SetShader(ContentResolver::Get().GetShader(
				indexed ? PrecompiledShader::TileMapMeshPalette : PrecompiledShader::TileMapMesh));
			if (shaderChanged) {
				command->GetMaterial().ReserveUniformsDataMemory();

				auto* textureUniform = command->GetMaterial().Uniform(Material::TextureUniformName);
				if (textureUniform != nullptr && textureUniform->GetIntValue(0) != 0) {
					textureUniform->SetIntValue(0); // GL_TEXTURE0
				}
				// Palette shaders sample the shared palette texture on unit 1
				auto* paletteUniform = command->GetMaterial().Uniform("uTexturePalette");
				if (paletteUniform != nullptr) {
					paletteUniform->SetIntValue(1); // GL_TEXTURE1
				}
			}

			// The mesh-wide tint goes in the instance color; the per-vertex color carries each quad's own alpha
			auto instanceBlock = command->GetInstanceBlock();
			instanceBlock->GetUniform(Material::ColorUniformName)->SetFloatVector(color.Data());

			auto& geometry = command->GetGeometry();
			geometry.SetElementsPerVertex(FloatsPerVertex);
			geometry.SetHostVertexPointer(vertices.data() + firstQuad * FloatsPerQuad);
			// Indices count from the chunk's own first vertex, which the base vertex of the draw supplies, so
			// every chunk reads the shared array from its start
			geometry.SetIndexCount(count * RenderResources::IndicesPerQuad);
			geometry.SetHostIndexPointer(quadIndices);
			geometry.SetDrawParameters(PrimitiveType::Triangles, 0, count * VerticesPerQuad);

			// Vertex positions are already in world space, so the model matrix is identity
			command->SetTransformation(Matrix4x4f::Translation(0.0f, 0.0f, 0.0f));
			command->SetLayer(depth);
			// Binds diffuse on unit 0 and, when the mesh is recolored at draw time, the palette on unit 1
			ContentResolver::Get().BindSpritePalette(*command, texture, indexed, paletteOffset);

			renderQueue.AddCommand(command);
		}
	}
#endif

	void TileMap::AddTileSet(StringView tileSetPath, std::uint16_t offset, std::uint16_t count, const std::uint8_t* paletteRemapping)
	{
		auto& tileSetPart = _tileSets.emplace_back();
		tileSetPart.Data = ContentResolver::Get().RequestTileSet(tileSetPath, 0, false, paletteRemapping);
		tileSetPart.Offset = offset;
		tileSetPart.Count = count;

		if (tileSetPart.Data == nullptr) {
			LOGE("Cannot load extra tileset \"{}\"", tileSetPath);
		}
	}

	void TileMap::ReadLayerConfiguration(Stream& s)
	{
		LayerType layerType = (LayerType)s.ReadValue<std::uint8_t>();
		std::uint16_t layerFlags = s.ReadValueAsLE<std::uint16_t>();

		if (layerType == LayerType::Sprite) {
			_sprLayerIndex = (std::int32_t)_layers.size();
		}

		TileMapLayer& newLayer = _layers.emplace_back();

		std::int32_t width = s.ReadValueAsLE<std::int32_t>();
		std::int32_t height = s.ReadValueAsLE<std::int32_t>();
		newLayer.LayoutSize = Vector2i(width, height);
		newLayer.Visible = ((layerFlags & 0x08) == 0x08);

		if (layerType != LayerType::Sprite) {
			std::uint8_t combinedSpeedModels = s.ReadValue<std::uint8_t>();
			newLayer.Description.SpeedModelX = (LayerSpeedModel)(combinedSpeedModels & 0x0f);
			newLayer.Description.SpeedModelY = (LayerSpeedModel)((combinedSpeedModels >> 4) & 0x0f);

			newLayer.Description.OffsetX = s.ReadValueAsLE<float>();
			newLayer.Description.OffsetY = s.ReadValueAsLE<float>();
			newLayer.Description.SpeedX = s.ReadValueAsLE<float>();
			newLayer.Description.SpeedY = s.ReadValueAsLE<float>();
			newLayer.Description.AutoSpeedX = s.ReadValueAsLE<float>();
			newLayer.Description.AutoSpeedY = s.ReadValueAsLE<float>();
			newLayer.Description.RepeatX = ((layerFlags & 0x01) == 0x01);
			newLayer.Description.RepeatY = ((layerFlags & 0x02) == 0x02);
			std::int16_t depth = s.ReadValueAsLE<std::int16_t>();
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
			// A tile index is masked down to the tile set bound by the converter, so it always fits LayerTile::TileID
			std::uint16_t tileIdx = s.ReadValueAsLE<std::uint16_t>();

			std::uint8_t tileModifier = (std::uint8_t)(tileFlags >> 4);

			LayerTile& tile = newLayer.Layout[i];
			tile.TileID = tileIdx;
			tile.DestructAnimation = -1;

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
		_animatedTilesOffset = s.ReadValueAsLE<std::uint16_t>();

		std::int32_t count = s.ReadValueAsLE<std::uint16_t>();

		_animatedTiles.reserve(count);

		for (std::int32_t i = 0; i < count; i++) {
			std::uint8_t frameCount = s.ReadValue<std::uint8_t>();
			if (frameCount == 0) {
				continue;
			}

			AnimatedTile& animTile = _animatedTiles.emplace_back();

			// FrameDuration is multiplied by 16 before saving, so divide it here back
			animTile.FrameDuration = s.ReadValueAsLE<std::uint16_t>() / 16.0f;
			animTile.Delay = s.ReadValueAsLE<std::uint16_t>();
			animTile.DelayJitter = s.ReadValueAsLE<std::uint16_t>();

			animTile.IsPingPong = s.ReadValue<std::uint8_t>();
			animTile.PingPongDelay = s.ReadValueAsLE<std::uint16_t>();

			for (std::int32_t j = 0; j < frameCount; j++) {
				auto& frame = animTile.Tiles.emplace_back();
				// TODO: flags
				/*std::uint8_t flag =*/ s.ReadValue<std::uint8_t>();
				frame.TileID = s.ReadValueAsLE<std::uint16_t>();
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

	/** @brief Overrides the diffuse texture of the specified tile */
	bool TileMap::OverrideTileDiffuse(std::int32_t tileId, StaticArrayView<(TileSet::DefaultTileSize + 2) * (TileSet::DefaultTileSize + 2), std::uint32_t> tileDiffuse)
	{
		TileSet* tileSet = ResolveTileSet(tileId);
		if (tileSet == nullptr) {
			return false;
		}

		// Remembered so PruneTilesetAtlas() knows this atlas carries patched-in tiles it must not rebuild away
		_tilesOverridden = true;
		return tileSet->OverrideTileDiffuse(tileId, tileDiffuse);
	}

	bool TileMap::IsTileSetIndexed(std::int32_t tileId)
	{
		TileSet* tileSet = ResolveTileSet(tileId);
		return (tileSet != nullptr && tileSet->IsIndexed);
	}

	/** @brief Overrides the collision mask of the specified tile */
	bool TileMap::OverrideTileMask(std::int32_t tileId, StaticArrayView<TileSet::DefaultTileSize * TileSet::DefaultTileSize, std::uint8_t> tileMask)
	{
		TileSet* tileSet = ResolveTileSet(tileId);
		if (tileSet == nullptr) {
			return false;
		}

		// Remembered so PruneTilesetAtlas() knows this atlas carries patched-in masks it must not rebuild away
		_tilesOverridden = true;
		return tileSet->OverrideTileMask(tileId, tileMask);
	}

	void TileMap::SetTileDestructibleEventParams(LayerTile& tile, TileDestructType type, std::uint16_t tileParams)
	{
		tile.DestructType = type;
		tile.DestructAnimation = std::int16_t(tile.TileID);
		if (tile.TileID >= _animatedTilesOffset) {
			tile.TileID = _animatedTiles[tile.DestructAnimation - _animatedTilesOffset].Tiles[0].TileID;
		}
		tile.TileParams = tileParams;
		tile.DestructFrameIndex = 0;
	}

	std::int32_t TileMap::GetTileDestructibleFrameCount(const LayerTile& tile)
	{
		if (tile.DestructAnimation >= _animatedTilesOffset) {
			return (std::int32_t)_animatedTiles[tile.DestructAnimation - _animatedTilesOffset].Tiles.size() - 2;
		}
		return 1;
	}

	Array<StringView> TileMap::GetUsedTileSetPaths() const
	{
		Array<StringView> result;
		arrayReserve(result, _tileSets.size());

		for (const auto& tileSetPart : _tileSets) {
			if (tileSetPart.Data != nullptr && !tileSetPart.Data->FilePath.empty()) {
				arrayAppend(result, tileSetPart.Data->FilePath);
			}
		}

		return result;
	}

	std::int32_t TileMap::GetParticleDebrisStep(std::int32_t debrisSize, std::int32_t frameWidth, std::int32_t frameHeight)
	{
		std::int32_t step = debrisSize + 1;
		if (MaxParticleDebrisPerBurst > 0) {
			// Walk the frame in coarser steps until the burst fits the budget. The producers derive the particle
			// size from the step, so the sprite is still covered edge to edge - by fewer, bigger particles, which
			// for an exploding sprite is nearly indistinguishable in motion. The upper bound keeps the loop
			// finite for a degenerate frame; the biggest sprite in the game needs 9.
			while (step < 64 && ((frameWidth + step - 1) / step) * ((frameHeight + step - 1) / step) > MaxParticleDebrisPerBurst) {
				step++;
			}
		}
		return step;
	}

	void TileMap::CreateDebris(const DestructibleDebris& debris)
	{
		// The user can switch the effect off altogether (see ParticleQuality); the reduced quality is applied by
		// the producers, which know what half of their burst is - a single particle handed in here is left alone
		if (PreferencesCache::Particles == ParticleQuality::Off) {
			return;
		}

		// Every live particle pins a pooled render command and a slice of the streaming uniform buffers, so on the
		// consoles the effect has a budget (see MaxDebrisCount) and new particles are dropped once it is used up
		if (MaxDebrisCount > 0 && (std::int32_t)_debrisList.size() >= MaxDebrisCount) {
			return;
		}

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
		if (tileId == 0 || PreferencesCache::Particles == ParticleQuality::Off) {
			return;
		}

		// A tile always breaks into its four quarters, so it is dropped as a whole once the budget is used up
		if (MaxDebrisCount > 0 && (std::int32_t)_debrisList.size() + 4 > MaxDebrisCount) {
			return;
		}

		TileSet* tileSet = ResolveTileSet(tileId);
		if (tileSet == nullptr) {
			return;
		}
		// Rebase into the containing texture chunk (a no-op single-texture lookup normally)
		Texture* tileTexture = tileSet->ResolveTextureDiffuse(tileId);
		if (tileTexture == nullptr) {
			return;
		}

		std::uint16_t z = _layers[_sprLayerIndex].Description.Depth + 80;

		Vector2i texSize = tileTexture->GetSize();
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

			debris.DiffuseTexture = tileTexture;
			// The tileset atlas is indexed now, so recolor tile debris through palette row 0 (or -1 if baked)
			debris.PaletteOffset = (tileSet->IsIndexed ? 0 : -1);
			debris.Flags = DebrisFlags::None;
		}
	}

	void TileMap::CreateParticleDebris(const GraphicResource* res, Vector3f pos, Vector2f force, std::int32_t currentFrame, bool isFacingLeft)
	{
		constexpr std::int32_t DebrisSize = 3;

		if (res->Base->TextureDiffuse == nullptr || PreferencesCache::Particles == ParticleQuality::Off) {
			return;
		}

		float x = pos.X - res->Base->Hotspot.X;
		float y = pos.Y - res->Base->Hotspot.Y;
		Vector2i texSize = res->Base->TextureDiffuse->GetSize();
		// Walk the frame's own area - anything outside it belongs to another frame in the sheet - and
		// spawn the particles where the trimmed frame is drawn, shifted by its offset inside the cell
		const Recti debrisRect = res->Base->GetFrameRect(currentFrame);
		const Vector2i frameOffset = res->Base->GetFrameOffset(currentFrame);

		// A big sprite would emit over a thousand particles at the plain step, which the consoles cannot afford
		const std::int32_t step = GetParticleDebrisStep(DebrisSize, debrisRect.W, debrisRect.H);
		const float particleSize = (float)(step - 1);
		// The low quality leaves out every other cell of the walk (a checkerboard), which halves the burst while
		// still covering the whole sprite - the particles keep their size, the cloud just comes out thinner
		const bool halved = (PreferencesCache::Particles == ParticleQuality::Low);

		for (std::int32_t fy = 0; fy < debrisRect.H; fy += step) {
			if (MaxDebrisCount > 0 && (std::int32_t)_debrisList.size() >= MaxDebrisCount) {
				break;
			}
			for (std::int32_t fx = 0; fx < debrisRect.W; fx += step) {
				if (halved && (((fx + fy) / step) & 1) != 0) {
					continue;
				}

				float currentSize = particleSize * Random().FastFloat(0.2f, 1.1f);

				DestructibleDebris& debris = _debrisList.emplace_back();
				debris.Pos = Vector2f(x + (isFacingLeft ? res->Base->FrameDimensions.X - frameOffset.X - fx : frameOffset.X + fx), y + frameOffset.Y + fy);
				debris.Depth = (std::uint16_t)pos.Z;
				debris.Size = Vector2f(currentSize, currentSize);
				debris.Speed = Vector2f(force.X + ((fx - debrisRect.W / 2) + Random().FastFloat(-2.0f, 2.0f)) * (isFacingLeft ? -1.0f : 1.0f) * Random().FastFloat(2.0f, 8.0f) / debrisRect.W,
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
				debris.TexBiasX = ((float(debrisRect.X) + (float)fx) / float(texSize.X));
				debris.TexScaleY = (currentSize / float(texSize.Y));
				debris.TexBiasY = ((float(debrisRect.Y) + (float)fy) / float(texSize.Y));

				debris.DiffuseTexture = res->Base->TextureDiffuse.get();
				// Indexed sprite debris is recolored at draw time; -1 keeps a baked (e.g., tileset) texture on plain Sprite
				debris.PaletteOffset = (((res->Base->Flags & GenericGraphicResourceFlags::Indexed) == GenericGraphicResourceFlags::Indexed) ? (std::int32_t)res->PaletteOffset : -1);
				debris.Flags = DebrisFlags::Bounce;
			}
		}
	}

	void TileMap::CreateSpriteDebris(const GraphicResource* res, Vector3f pos, std::int32_t count)
	{
		if (res->Base->TextureDiffuse == nullptr || PreferencesCache::Particles == ParticleQuality::Off) {
			return;
		}

		float x = pos.X - res->Base->Hotspot.X;
		float y = pos.Y - res->Base->Hotspot.Y;
		Vector2i texSize = res->Base->TextureDiffuse->GetSize();

		if (PreferencesCache::Particles == ParticleQuality::Low) {
			// Half the burst, but never none of it (the speed below scales with the count, so the survivors
			// also fly a little slower - a smaller burst reads as one either way)
			count = (count + 1) / 2;
		}

		if (MaxDebrisCount > 0) {
			// Clamped instead of dropped: the count is the caller's intent (and also scales the speed below),
			// so a partial burst still reads as the same effect
			count = std::min(count, MaxDebrisCount - (std::int32_t)_debrisList.size());
		}

		for (std::int32_t i = 0; i < count; i++) {
			float speedX = Random().FastFloat(-1.0f, 1.0f) * Random().FastFloat(0.2f, 0.8f) * count;

			std::int32_t curAnimFrame = res->FrameOffset + Random().Next(0, res->FrameCount);
			Recti frameRect = res->Base->GetFrameRect(curAnimFrame);
			Vector2i frameOffset = res->Base->GetFrameOffset(curAnimFrame);

			DestructibleDebris& debris = _debrisList.emplace_back();
			debris.Pos = Vector2f(x, y);
			debris.Depth = (std::uint16_t)pos.Z;
			// Sized by the frame's own area rather than the logical cell: with trimmed frames the two
			// differ per frame, and stretching a trimmed frame over the whole cell visibly distorts it
			debris.Size = Vector2f((float)frameRect.W, (float)frameRect.H);
			debris.FrameOffset = Vector2f(frameOffset.X + (frameRect.W - res->Base->FrameDimensions.X) * 0.5f,
				frameOffset.Y + (frameRect.H - res->Base->FrameDimensions.Y) * 0.5f);
			debris.Speed = Vector2f(speedX, -1.0f * Random().FastFloat(2.2f, 4.0f));
			debris.Acceleration = Vector2f(0.0f, 0.2f);

			debris.Scale = 1.0f;
			debris.ScaleSpeed = -0.002f;
			debris.Angle = Random().FastFloat(0.0f, fTwoPi);
			debris.AngleSpeed = speedX * 0.02f;

			debris.Alpha = 1.0f;
			debris.AlphaSpeed = -0.002f;

			debris.Time = 560.0f;

			debris.TexScaleX = (float(frameRect.W) / float(texSize.X));
			debris.TexBiasX = (float(frameRect.X) / float(texSize.X));
			debris.TexScaleY = (float(frameRect.H) / float(texSize.Y));
			debris.TexBiasY = (float(frameRect.Y) / float(texSize.Y));

			debris.DiffuseTexture = res->Base->TextureDiffuse.get();
			// Indexed sprite debris is recolored at draw time; -1 keeps a baked texture on the plain Sprite shader
			debris.PaletteOffset = (((res->Base->Flags & GenericGraphicResourceFlags::Indexed) == GenericGraphicResourceFlags::Indexed) ? (std::int32_t)res->PaletteOffset : -1);
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
				// Time's up - fade out smoothly instead of popping while still partly visible (e.g., the Fire/Lightning
				// death effects, whose own AlphaSpeed is too slow to reach zero within their lifetime)
				debris.AlphaSpeed = std::min(debris.AlphaSpeed, -0.08f);
			}

			if ((debris.Flags & (DebrisFlags::Disappear | DebrisFlags::Bounce)) != DebrisFlags::None) {
				// Debris should collide with tilemap
				float nx = debris.Pos.X + debris.Speed.X * timeMult;
				float ny = debris.Pos.Y + debris.Speed.Y * timeMult;
				// Debris is a few pixels across and destroys nothing, so it samples the collision mask at
				// its centre rather than sweeping its whole box - a burst after an enemy dies is hundreds
				// of these, and the box test carries setup a point sample does not need
				if (IsTilePointEmpty((std::int32_t)nx, (std::int32_t)ny, true)) {
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
					if (IsTilePointEmpty((std::int32_t)nx, (std::int32_t)debris.Pos.Y, true)) {
						if (debris.Speed.Y > 0.0f) {
							debris.Speed.Y = -(debris.Elasticity * debris.Speed.Y);
							//OnHitFloorHook();
						} else {
							debris.Speed.Y = 0;
							//OnHitCeilingHook();
						}
					}

					// If the actor didn't move all the way horizontally,
					// it hit a wall (or was already touching it)
					if (IsTilePointEmpty((std::int32_t)debris.Pos.X, (std::int32_t)ny, true)) {
						debris.Speed.X = -(debris.Elasticity * debris.Speed.X);
						debris.AngleSpeed = -(debris.Elasticity * debris.AngleSpeed);
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

		Rectf viewportRect = RenderResources::GetCurrentViewport()->GetCullingRect();
		viewportRect.X -= MaxDebrisSize;
		viewportRect.Y -= MaxDebrisSize;
		viewportRect.W += MaxDebrisSize * 2.0f;
		viewportRect.H += MaxDebrisSize * 2.0f;

#if defined(TILEMAP_USE_SINGLE_DRAW)
		// Particles are aggregated exactly like a tile layer is: everything that has to stay per command - the
		// texture, the palette row, the blending and the depth - becomes a group key, and everything else (the
		// rotated corners, the UVs, the fading alpha) goes into the vertex stream. A death burst is one sprite at
		// one depth, so the whole effect ends up as a single draw instead of one command per particle.
		_debrisMeshGroups.clear();

		for (const auto& debris : _debrisList) {
			if (!viewportRect.Contains(debris.Pos)) {
				continue;
			}

			const bool additiveBlending = ((debris.Flags & DebrisFlags::AdditiveBlending) == DebrisFlags::AdditiveBlending);
			std::int32_t verticesIndex = -1;
			// A handful of groups at most (the burst, the tile debris, the weather), so a linear scan beats a map
			for (auto& group : _debrisMeshGroups) {
				if (group.DiffuseTexture == debris.DiffuseTexture && group.PaletteOffset == debris.PaletteOffset &&
					group.Depth == debris.Depth && group.AdditiveBlending == additiveBlending) {
					verticesIndex = group.VerticesIndex;
					break;
				}
			}
			if (verticesIndex < 0) {
				verticesIndex = RentMeshVertices();
				_debrisMeshGroups.push_back({ debris.DiffuseTexture, debris.PaletteOffset, debris.Depth,
					additiveBlending, verticesIndex });
			}

			AppendDebrisQuad(_meshVertices[verticesIndex], debris);
		}

		for (const auto& group : _debrisMeshGroups) {
			// Indexed sprite debris is recolored at draw time through the palette shader; baked debris (a tileset
			// texture, for instance) carries its colors already and uses the plain mesh shader
			const bool indexed = (group.PaletteOffset >= 0);
			EmitMesh(renderQueue, _meshVertices[group.VerticesIndex], *group.DiffuseTexture, indexed,
				(std::uint16_t)(indexed ? group.PaletteOffset : 0), Vector4f(1.0f, 1.0f, 1.0f, 1.0f),
				group.Depth, RenderCommand::Type::Particle, group.AdditiveBlending);
		}
#else
		// Constant for every debris of the frame, so resolved once instead of per particle
		auto& resolver = ContentResolver::Get();
		Texture* paletteTexture = resolver.GetPaletteTexture();

		for (const auto& debris : _debrisList) {
			if (!viewportRect.Contains(debris.Pos)) {
				continue;
			}

			// Backstop for the command pool the loop rents from: it grows to its high-water mark and one slot is
			// ~840 bytes, so the consoles refuse to draw beyond the budget rather than risk the heap. The live
			// count is already capped, this only bites when several viewports draw the same particles.
			if (MaxPooledRenderCommands > 0 && _renderCommandsCount >= MaxPooledRenderCommands) {
				break;
			}

			// Indexed sprite debris is recolored at draw time through the palette shader; baked debris stays
			// on Sprite. Renting with that choice picks the same shader ConfigureSpriteShader() would, and
			// hands back the instance uniforms already resolved - an exploding enemy emits hundreds of these
			// in one frame, so a by-name lookup per uniform per debris is worth avoiding.
			bool debrisIndexed = (debris.PaletteOffset >= 0);
			TileCommandUniforms* commandUniforms;
			auto command = RentRenderCommand(LayerRendererType::Default, debrisIndexed, &commandUniforms);
			command->SetType(RenderCommand::Type::Particle);

			if ((debris.Flags & DebrisFlags::AdditiveBlending) == DebrisFlags::AdditiveBlending) {
				command->GetMaterial().SetBlendingFactors(BlendingFactor::SrcAlpha, BlendingFactor::One);
			} else {
				command->GetMaterial().SetBlendingFactors(BlendingFactor::SrcAlpha, BlendingFactor::OneMinusSrcAlpha);
			}

			commandUniforms->TexRect->SetFloatValue(debris.TexScaleX, debris.TexBiasX, debris.TexScaleY, debris.TexBiasY);
			commandUniforms->SpriteSize->SetFloatValue(debris.Size.X, debris.Size.Y);
			commandUniforms->Color->SetFloatVector(Colorf(1.0f, 1.0f, 1.0f, debris.Alpha).Data());

			// Translation * RotationZ * Scaling * Translation, composed directly. Chaining the four
			// operations meant three 4x4 multiplies per particle - and a burst of debris is hundreds of
			// them in one frame - where the result is just a scaled rotation plus an offset origin.
			const float c = std::cos(debris.Angle);
			const float s = std::sin(debris.Angle);
			const float ns = std::sin(-debris.Angle);	// Never "-s", see the note in Matrix4x4::RotationZ()
			const float xx = c * debris.Scale, xy = s * debris.Scale;
			const float yx = ns * debris.Scale, yy = c * debris.Scale;
			const float localX = debris.FrameOffset.X - debris.Size.X * 0.5f;
			const float localY = debris.FrameOffset.Y - debris.Size.Y * 0.5f;
			command->SetTransformation(Matrix4x4f(
				Vector4f(xx, xy, 0.0f, 0.0f),
				Vector4f(yx, yy, 0.0f, 0.0f),
				Vector4f(0.0f, 0.0f, 1.0f, 0.0f),
				Vector4f(debris.Pos.X + xx * localX + yx * localY,
					debris.Pos.Y + xy * localX + yy * localY, 0.0f, 1.0f)));
			command->SetLayer(debris.Depth);
			command->GetMaterial().SetTexture(0, *debris.DiffuseTexture);
			if (debrisIndexed) {
				if (paletteTexture != nullptr) {
					command->GetMaterial().SetTexture(1, *paletteTexture);
				}
				if (commandUniforms->PaletteOffset != nullptr) {
					commandUniforms->PaletteOffset->SetFloatValue((float)debris.PaletteOffset);
				}
			}

			renderQueue.AddCommand(command);
		}
#endif
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

		_triggerState.set(triggerId, newState);

		// Go through all tiles and update any that are influenced by this trigger
		Vector2i layoutSize = _layers[_sprLayerIndex].LayoutSize;
		std::int32_t n = layoutSize.X * layoutSize.Y;
		for (std::int32_t i = 0; i < n; i++) {
			LayerTile& tile = _layers[_sprLayerIndex].Layout[i];
			if (tile.DestructType == TileDestructType::Trigger && tile.TileParams == triggerId) {
				if (tile.DestructAnimation >= _animatedTilesOffset) {
					if (_animatedTiles[tile.DestructAnimation - _animatedTilesOffset].Tiles.size() > 1) {
						tile.DestructFrameIndex = (newState ? 1 : 0);
						tile.TileID = _animatedTiles[tile.DestructAnimation - _animatedTilesOffset].Tiles[tile.DestructFrameIndex].TileID;
					}
				} else {
					tile.DestructFrameIndex = (newState ? 1 : 0);
					tile.TileID = (newState ? std::uint16_t(0) /*Empty*/ : std::uint16_t(tile.DestructAnimation));
				}
			}
		}
	}

	Vector2i TileMap::GetLayerSize(std::int32_t layerIndex) const
	{
		if (layerIndex < 0 || layerIndex >= (std::int32_t)_layers.size()) {
			return {};
		}
		return _layers[layerIndex].LayoutSize;
	}

	std::uint16_t TileMap::GetTile(std::int32_t layerIndex, std::int32_t x, std::int32_t y) const
	{
		if (layerIndex < 0 || layerIndex >= (std::int32_t)_layers.size()) {
			return 0;
		}

		const TileMapLayer& layer = _layers[layerIndex];
		if (x < 0 || y < 0 || x >= layer.LayoutSize.X || y >= layer.LayoutSize.Y) {
			return 0;
		}

		const LayerTile& tile = layer.Layout[y * layer.LayoutSize.X + x];

		std::uint16_t result;
		if (tile.TileID >= (std::int32_t)_animatedTilesOffset) {
			result = (std::uint16_t)((tile.TileID - _animatedTilesOffset) & TileIndexMask) | TileFlagAnimated;
		} else {
			result = (std::uint16_t)(tile.TileID & TileIndexMask);
		}
		if ((tile.Flags & LayerTileFlags::FlipX) == LayerTileFlags::FlipX) {
			result |= TileFlagFlipX;
		}
		if ((tile.Flags & LayerTileFlags::FlipY) == LayerTileFlags::FlipY) {
			result |= TileFlagFlipY;
		}
		return result;
	}

	bool TileMap::SetTile(std::int32_t layerIndex, std::int32_t x, std::int32_t y, std::uint16_t tileValue)
	{
		if (layerIndex < 0 || layerIndex >= (std::int32_t)_layers.size()) {
			return false;
		}

		TileMapLayer& layer = _layers[layerIndex];
		if (x < 0 || y < 0 || x >= layer.LayoutSize.X || y >= layer.LayoutSize.Y) {
			return false;
		}

		std::int32_t layoutIndex = y * layer.LayoutSize.X + x;
		LayerTile& tile = layer.Layout[layoutIndex];

		// A script can overwrite any tile, including one the checkpoint scan didn't consider mutable, so the
		// value it replaces has to join the checkpoint - otherwise a rollback would keep the scripted tile
		if (_hasRollbackCheckpoint && layerIndex == _sprLayerIndex) {
			SaveTileForRollback((std::uint32_t)layoutIndex, tile);
		}

		std::uint16_t tileIndex = (tileValue & TileIndexMask);
		if ((tileValue & TileFlagAnimated) == TileFlagAnimated) {
			tile.TileID = (std::uint16_t)(_animatedTilesOffset + tileIndex);
		} else {
			tile.TileID = tileIndex;
		}

		// Replace just the flip flags, preserving collision/other flags (e.g., OneWay)
		std::uint8_t flags = (std::uint8_t)tile.Flags & ~(std::uint8_t)(LayerTileFlags::FlipX | LayerTileFlags::FlipY);
		if ((tileValue & TileFlagFlipX) == TileFlagFlipX) {
			flags |= (std::uint8_t)LayerTileFlags::FlipX;
		}
		if ((tileValue & TileFlagFlipY) == TileFlagFlipY) {
			flags |= (std::uint8_t)LayerTileFlags::FlipY;
		}
		tile.Flags = (LayerTileFlags)flags;
		return true;
	}

	void TileMap::SaveTileForRollback(std::uint32_t tileIndex, const LayerTile& tile)
	{
		// Binary search keeps the list sorted by index and doubles as the duplicate check: only the first
		// saved value of a tile is its checkpoint value, later overwrites must not replace it
		std::size_t lo = 0, hi = _sprLayerForRollback.size();
		while (lo < hi) {
			std::size_t mid = lo + (hi - lo) / 2;
			if (_sprLayerForRollback[mid].TileIndex < tileIndex) {
				lo = mid + 1;
			} else {
				hi = mid;
			}
		}

		if (lo < _sprLayerForRollback.size() && _sprLayerForRollback[lo].TileIndex == tileIndex) {
			return;
		}

		_sprLayerForRollback.insert(_sprLayerForRollback.begin() + lo, RollbackTile { tileIndex, tile });
	}

	void TileMap::CreateCheckpointForRollback()
	{
		// Only tiles that can still change are saved, in ascending index order. A destructible tile moves its
		// ID, frame index, params, flags and (when it collapses) its destruct type; every other tile is fixed
		// for the level's lifetime unless a script overwrites it, which SetTile() records as it happens. And a
		// destruct type is only ever cleared, never gained, so this set cannot grow behind our back.
		auto& sprLayer = _layers[_sprLayerIndex];
		std::int32_t layoutSize = sprLayer.LayoutSize.X * sprLayer.LayoutSize.Y;
		const LayerTile* layout = sprLayer.Layout.get();

		_sprLayerForRollback.clear();
		for (std::int32_t i = 0; i < layoutSize; i++) {
			if (layout[i].DestructType != TileDestructType::None) {
				_sprLayerForRollback.push_back(RollbackTile { (std::uint32_t)i, layout[i] });
			}
		}
		_hasRollbackCheckpoint = true;

		std::memcpy(_triggerStateForRollback.data(), _triggerState.data(), _triggerState.sizeInBytes());
	}

	void TileMap::RollbackToCheckpoint()
	{
		if (!_hasRollbackCheckpoint) {
			return;
		}

		LayerTile* layout = _layers[_sprLayerIndex].Layout.get();
		for (const auto& saved : _sprLayerForRollback) {
			layout[saved.TileIndex] = saved.Tile;
		}

		std::memcpy(_triggerState.data(), _triggerStateForRollback.data(), _triggerState.sizeInBytes());
	}

	void TileMap::InitializeFromStream(Stream& src)
	{
		std::int32_t layoutSize = src.ReadVariableInt32();
		if (layoutSize == -1) {
			return;
		}

		DEATH_ASSERT(_sprLayerIndex != -1, "Sprite layer not defined", );
		
		auto& spriteLayer = _layers[_sprLayerIndex];
		std::int32_t realLayoutSize = spriteLayer.LayoutSize.X * spriteLayer.LayoutSize.Y;
		DEATH_ASSERT(layoutSize == realLayoutSize, "Layout size mismatch", );

		for (std::int32_t i = 0; i < layoutSize; i++) {
			auto& tile = spriteLayer.Layout[i];
			tile.DestructFrameIndex = std::int16_t(src.ReadVariableInt32());
			if (tile.DestructAnimation >= 0) {
				if (tile.DestructAnimation >= _animatedTilesOffset) {
					if (tile.DestructAnimation - _animatedTilesOffset < (std::int32_t)_animatedTiles.size()) {
						auto& anim = _animatedTiles[tile.DestructAnimation - _animatedTilesOffset];
						std::int32_t max = (std::int32_t)anim.Tiles.size() - 2;
						if (tile.DestructFrameIndex > max) {
							LOGW("Serialized tile {} with animation frame {} is out of range", i, tile.DestructFrameIndex);
							tile.DestructFrameIndex = std::int16_t(max);
						}
						if (tile.DestructFrameIndex < 0) {
							LOGW("Serialized tile {} with animation frame {} is out of range", i, tile.DestructFrameIndex);
							tile.DestructFrameIndex = 0;
						}
						tile.TileID = anim.Tiles[tile.DestructFrameIndex].TileID;
					} else {
						LOGW("Invalid animated tile ID {}", tile.DestructAnimation);
					}
				} else {
					if (tile.DestructFrameIndex >= 1) {
						tile.DestructFrameIndex = 1;
						tile.TileID = 0; // Empty tile
					}
				}
			}
		}

		src.Read(_triggerState.data(), _triggerState.sizeInBytes());
	}

	void TileMap::SerializeResumableToStream(Stream& dest, bool fromCheckpoint)
	{
		if (_sprLayerIndex == -1) {
			dest.WriteValue<std::int32_t>(-1);
			return;
		}

		auto& spriteLayer = _layers[_sprLayerIndex];
		std::int32_t layoutSize = spriteLayer.LayoutSize.X * spriteLayer.LayoutSize.Y;
		const LayerTile* layout = spriteLayer.Layout.get();
		dest.WriteVariableInt32(layoutSize);

		if (fromCheckpoint && _hasRollbackCheckpoint) {
			// A tile missing from the checkpoint hasn't changed since it was taken, so the live layer already
			// holds its checkpoint value; the saved ones are merged in by index, the list being sorted
			std::size_t next = 0;
			for (std::int32_t i = 0; i < layoutSize; i++) {
				if (next < _sprLayerForRollback.size() && _sprLayerForRollback[next].TileIndex == (std::uint32_t)i) {
					dest.WriteVariableInt32(_sprLayerForRollback[next].Tile.DestructFrameIndex);
					next++;
				} else {
					dest.WriteVariableInt32(layout[i].DestructFrameIndex);
				}
			}

			dest.Write(_triggerStateForRollback.data(), _triggerStateForRollback.sizeInBytes());
		} else {
			for (std::int32_t i = 0; i < layoutSize; i++) {
				dest.WriteVariableInt32(layout[i].DestructFrameIndex);
			}

			dest.Write(_triggerState.data(), _triggerState.sizeInBytes());
		}
	}

	void TileMap::RenderTexturedBackground(RenderQueue& renderQueue, const Rectf& cullingRect, Vector2f viewCenter, TileMapLayer& layer, float x, float y)
	{
		auto target = _texturedBackgroundPass._target.get();
		if (target == nullptr) {
			return;
		}

		auto* command = RentRenderCommand(layer.Description.RendererType);

		auto* instanceBlock = command->GetInstanceBlock();
		instanceBlock->GetUniform(Material::TexRectUniformName)->SetFloatValue(1.0f, 0.0f, 1.0f, 0.0f);
		instanceBlock->GetUniform(Material::SpriteSizeUniformName)->SetFloatValue((float)cullingRect.W, (float)cullingRect.H);
		instanceBlock->GetUniform(Material::ColorUniformName)->SetFloatVector(Colorf(1.0f, 1.0f, 1.0f, 1.0f).Data());

		command->GetMaterial().Uniform("uViewSize")->SetFloatValue((float)cullingRect.W, (float)cullingRect.H);
		command->GetMaterial().Uniform("uCameraPos")->SetFloatVector(viewCenter.Data());
		command->GetMaterial().Uniform("uShift")->SetFloatValue(x, y);
		command->GetMaterial().Uniform("uHorizonColor")->SetFloatVector(layer.Description.Color.Data());

		command->SetTransformation(Matrix4x4f::Translation(cullingRect.X, cullingRect.Y, 0.0f));
		command->SetLayer(layer.Description.Depth);
		command->GetMaterial().SetTexture(*target);

		renderQueue.AddCommand(command);
	}

	void TileMap::PruneTilesetAtlas()
	{
		if (!PruneAtlasToUsedTiles || _tileSets.size() != 1 || _tileSets[0].Data == nullptr) {
			// More than one part means the level brought its own extra tiles (MLLE); the ids then span several
			// sets and the mapping below would not describe them, so those are left whole
			return;
		}
		if (_tilesOverridden) {
			// The level's per-tile overrides (MLLE) were already patched into this atlas and its masks;
			// rebuilding from the source sheet would silently revert them - both the graphics and the
			// collision - so such a level keeps its whole atlas
			return;
		}

		const std::int32_t tileCount = _tileSets[0].Data->TileCount;
		if (tileCount <= 0) {
			return;
		}

		BitArray used(ValueInit, tileCount);
		const auto mark = [&](std::int32_t id) {
			if (id >= 0 && id < tileCount) {
				used.set(id);
			}
		};

		// Deliberately more generous than the layers alone: every frame of every animated tile is kept even if
		// nothing points at that animation yet (a trigger or a script can start using it later), and so is the
		// caption tile and tile 0. What cannot be covered is a script calling SetTile() with an id this level
		// never mentions - that one maps to the blank slot and draws EMPTY while its collision mask (indexed
		// by the original id) still applies, so it would be invisible but solid; the reserve exists to at
		// least make that deterministic rather than sampling a neighbour.
		mark(0);
		mark(_captionTileId);
		for (auto& anim : _animatedTiles) {
			for (auto& frame : anim.Tiles) {
				mark(frame.TileID);
			}
		}
		for (auto& layer : _layers) {
			if (layer.Layout == nullptr) {
				continue;
			}
			const std::int32_t count = layer.LayoutSize.X * layer.LayoutSize.Y;
			for (std::int32_t i = 0; i < count; i++) {
				const LayerTile& tile = layer.Layout[i];
				if (tile.TileID < _animatedTilesOffset) {
					mark(tile.TileID);
				}
			}
		}

		std::int32_t distinct = 0;
		for (std::int32_t i = 0; i < tileCount; i++) {
			distinct += (used[i] ? 1 : 0);
		}
		if (distinct >= tileCount) {
			return;	// Nothing to gain
		}

		// The whole atlas goes before the packed one is built, so only one of the two is ever resident - which
		// is the point of doing this at all. The source sheet is read again for it; that is one more pass over
		// the cartridge at load time in exchange for the memory for the rest of the level.
		const String path = _tileSetPath;
		const std::uint16_t captionTileId = _captionTileId;
		_tileSets[0].Data = nullptr;

		auto pruned = ContentResolver::Get().RequestTileSet(path, captionTileId, false, nullptr, &used);
		if (pruned == nullptr) {
			// The old atlas is already gone (freeing it first is the point), so there is nothing to fall
			// back to in place - retry the plain unpruned request once before giving up
			LOGE("Cannot repack tileset \"{}\", reloading it whole", path);
			pruned = ContentResolver::Get().RequestTileSet(path, captionTileId, false);
			if (pruned == nullptr) {
				// Every lookup now reports a missing tile instead of walking a null tileset; the level is
				// unplayable either way, but it fails as empty layers rather than as a crash
				LOGE("Cannot reload tileset \"{}\", the level has no tiles to draw", path);
				_tileSets[0].Count = 0;
				return;
			}
		} else {
			LOGI("Tileset \"{}\" repacked to the {} of {} tiles this level references", path, distinct, tileCount);
		}

		_tileSets[0].Data = Death::move(pruned);
		_tileSets[0].Count = _tileSets[0].Data->TileCount;
	}

	void TileMap::OnInitializeViewport()
	{
		if ((SupportsTexturedBackground || SupportsTexturedBackgroundCircle) && _texturedBackgroundLayer != -1) {
			// Skipped entirely when unsupported, which also saves the pass's render target
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

	std::int32_t TileMap::ResolveTileID(const LayerTile& tile) const
	{
		std::int32_t tileId = tile.TileID;
		if (tileId >= _animatedTilesOffset) {
			tileId -= _animatedTilesOffset;
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
			_camera->SetOrthoProjection(0.0f, (float)width, 0.0f, (float)height);
			_camera->SetView(0, 0, 0, 1);
			_target = std::make_unique<Texture>(nullptr, Texture::ColorTargetFormat, width, height);
			_view = std::make_unique<Viewport>(_target.get(), Viewport::DepthStencilFormat::None);
			_view->SetRootNode(this);
			_view->SetCamera(_camera.get());
			//_view->setClearMode(Viewport::ClearMode::Never);
			_target->SetMagFiltering(SamplerFilter::Linear);
			_target->SetWrap(SamplerWrapping::Repeat);

			// Prepare render commands
			std::int32_t renderCommandCount = (width * height) / (TileSet::DefaultTileSize * TileSet::DefaultTileSize);
			_renderCommands.reserve(renderCommandCount);
			for (std::int32_t i = 0; i < renderCommandCount; i++) {
				std::unique_ptr<RenderCommand>& command = _renderCommands.emplace_back(std::make_unique<RenderCommand>());
				command->GetMaterial().SetShaderProgramType(Material::ShaderProgramType::Sprite);
				command->GetMaterial().ReserveUniformsDataMemory();
				command->GetGeometry().SetDrawParameters(PrimitiveType::TriangleStrip, 0, 4);

				auto* textureUniform = command->GetMaterial().Uniform(Material::TextureUniformName);
				if (textureUniform && textureUniform->GetIntValue(0) != 0) {
					textureUniform->SetIntValue(0); // GL_TEXTURE0
				}
			}
		}

		Viewport::GetChain().push_back(_view.get());
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

				// Rebase into the containing texture chunk (a no-op single-texture lookup normally)
				Texture* tileTexture = tileSet->ResolveTextureDiffuse(tileId);
				if DEATH_UNLIKELY(tileTexture == nullptr) {
					continue;
				}

				auto command = _renderCommands[renderCommandIndex++].get();
				// Indexed tilesets bake their colors into the background render target through the palette shader
				ContentResolver::Get().ConfigureSpriteShader(*command, tileSet->IsIndexed);

				Vector2i texSize = tileTexture->GetSize();
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

				auto instanceBlock = command->GetInstanceBlock();
				instanceBlock->GetUniform(Material::TexRectUniformName)->SetFloatValue(texScaleX, texBiasX, texScaleY, texBiasY);
				instanceBlock->GetUniform(Material::SpriteSizeUniformName)->SetFloatValue(TileSet::DefaultTileSize, TileSet::DefaultTileSize);
				instanceBlock->GetUniform(Material::ColorUniformName)->SetFloatVector(Colorf::White.Data());

				command->SetTransformation(Matrix4x4f::Translation(x * TileSet::DefaultTileSize, y * TileSet::DefaultTileSize, 0.0f));
				ContentResolver::Get().BindSpritePalette(*command, *tileTexture, tileSet->IsIndexed, 0);

				renderQueue.AddCommand(command);
			}
		}

		if (!isAnimated && _alreadyRendered) {
			// If it's not animated, it can be rendered only once
			auto& chain = Viewport::GetChain();
			for (std::int32_t i = std::int32_t(chain.size()) - 1; i >= 0; i--) {
				auto& item = chain[i];
				if (item == _view.get()) {
					chain.erase(&item);
					break;
				}
			}
		}

		_alreadyRendered = true;
		return true;
	}
}