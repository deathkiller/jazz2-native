#include "TileMap.h"
#include "../ContentResolver.h"
#include "../LevelHandler.h"
#include "../Formats/LevelFile.h"
#include "../PreferencesCache.h"

#include "../../nCine/Application.h"
#include "../../nCine/tracy.h"
#include "../../nCine/Base/FrameTimer.h"
#include "../../nCine/Base/Random.h"
#include "../../nCine/Graphics/RenderQueue.h"
#include "../../nCine/Graphics/RenderResources.h"
#if defined(TILEMAP_GROUP_MESH_BY_TILE)
#	include "../../nCine/Graphics/RHI/RDP/RdpTileRecord.h"
#elif defined(TILEMAP_PACKED_TILE_RECORDS)
#	include "../../nCine/Graphics/RHI/PVR/PvrTileRecord.h"
#endif

#include <algorithm>
#include <cmath>

#include <Containers/GrowableArray.h>

namespace Jazz2::Tiles
{
	namespace
	{
		// The textured background ("Sky"/"Tunnel"/"Cylinder" layers) is a per-pixel procedural effect in GLSL. Its
		// planar variant is affine along every screen row, though, so the fixed-function tiers rebuild it out of
		// horizontal bands instead of a fragment shader - the shared geometry lives in
		// Shaders/Include/TexturedBackgroundWarp.inc, included by both consoles' fixed_function blocks. The
		// tunnel has no such structure, so it borrows the planar reconstruction rather than falling back to a
		// flat tilemap - much closer to the original than no warp at all. The cylinder borrows it too. The wave
		// moves single rows and columns, the fixed-function tiers draw its tiles instead, where its texture is.
		constexpr bool SupportsTexturedBackground = true;

		/**
			@brief Whether a level's tileset atlas is repacked around the tiles the level references

			A tileset describes everything its author drew; the levels measured reference 42% and 59% of theirs,
			and the rest sits in memory as texels nothing samples. Repacking costs a second read of the masks at
			load time (the atlas itself is only built once, see TileMap()) and gives back 592 KB on the largest of them - the difference, on a console with 8 MB of
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
		constexpr bool SupportsTexturedBackgroundTunnel = true;
		constexpr bool SupportsTexturedBackgroundCylinder = true;

		// The textured background pass renders this much of a layer at most, the 8x8 tiles at its top left - the
		// textured backgrounds draw only a layer of that size, a reflection takes those of a larger one - unless the
		// layer has a texture of its own, of the same size (see TileMapLayer::TexturePixels)
		constexpr std::int32_t TexturedBackgroundTiles = 8;

		// How far the time of a tile menu moves each frame, whatever its auto speeds are - JJ2+ turns its frontmost image by
		// 0.1766 degrees each tick of the original, which is 0.3 of this time (see DrawMenuBackground()), measured in JJ2+.
		// A little slower than the main menu, whose background it is (see UI::Canvas::AnimTimeMultiplier).
		constexpr float MenuTimePerFrame = 0.012f;

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

#if defined(TILEMAP_GROUP_MESH_BY_TILE)
		// Where the grouped tiles go, they are the RDP's packed records (see RHI::RDP::TileRecord), four floats
		// of storage each in the vertex buffers they are written into
		constexpr std::uint32_t TileRecordFloats = sizeof(RHI::RDP::TileRecord) / sizeof(float);
		static_assert(RHI::RDP::TileRecord::Size == TileSet::DefaultTileSize, "The packed tile records assume the tile size");

		/** @brief Record flags for a layer tile's flip bits */
		inline std::uint8_t MeshTileFlags(LayerTileFlags flags)
		{
			return std::uint8_t(((flags & LayerTileFlags::FlipX) == LayerTileFlags::FlipX ? RHI::RDP::TileRecord::FlipX : 0) |
				((flags & LayerTileFlags::FlipY) == LayerTileFlags::FlipY ? RHI::RDP::TileRecord::FlipY : 0));
		}

		/** @brief Writes a grouped tile entry at @p x, @p y as a packed record into vertex storage */
		template<class Entry>
		inline void WriteTileRecord(float* dst, const Entry& e, float x, float y)
		{
			RHI::RDP::TileRecord record;
			record.X = x;
			record.Y = y;
			record.TexX = e.TexX;
			record.TexY = e.TexY;
			record.Alpha = e.Alpha;
			record.Flags = e.Flags;
			record.Reserved = 0;
			// Copied rather than stored through a cast pointer: the storage is typed as floats
			std::memcpy(dst, &record, sizeof(record));
		}
#endif

#if defined(TILEMAP_PACKED_TILE_RECORDS)
		// The PVR's packed records (see RHI::PVR::TileRecord), four floats of storage each in the layer mesh buffers
		constexpr std::uint32_t TileRecordFloats = sizeof(RHI::PVR::TileRecord) / sizeof(float);
		static_assert(RHI::PVR::TileRecord::Size == TileSet::DefaultTileSize, "The packed tile records assume the tile size");

		/** @brief Appends one tile to a layer mesh buffer as a packed record, its texel position being the tile's top-left texel in its atlas chunk */
		inline void AppendTileRecord(SmallVector<float, 0>& records, float x, float y, std::int32_t texX, std::int32_t texY,
			std::uint8_t alpha, LayerTileFlags flags)
		{
			const std::size_t base = records.size();
			records.resize_for_overwrite(base + TileRecordFloats);
			// Stored in place rather than copied in: GCC makes a 16-byte memcpy() a library call on this target, which
			// was one per tile of every layer, and the engine is built without strict aliasing
			RHI::PVR::TileRecord& record = *reinterpret_cast<RHI::PVR::TileRecord*>(records.data() + base);
			record.X = x;
			record.Y = y;
			record.TexX = std::uint16_t(texX);
			record.TexY = std::uint16_t(texY);
			record.Alpha = alpha;
			record.Flags = std::uint8_t(((flags & LayerTileFlags::FlipX) == LayerTileFlags::FlipX ? RHI::PVR::TileRecord::FlipX : 0) |
				((flags & LayerTileFlags::FlipY) == LayerTileFlags::FlipY ? RHI::PVR::TileRecord::FlipY : 0));
			record.Reserved = 0;
		}
#endif

#if defined(DEATH_TARGET_DREAMCAST) || defined(DEATH_TARGET_N64) || defined(DEATH_TARGET_WII) || \
		defined(DEATH_TARGET_GAMECUBE) || defined(DEATH_TARGET_3DS) || defined(DEATH_TARGET_AMIGAOS)
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
		// Desktop targets have the memory for the effect at full detail, so nothing is bounded, and the game's pools
		// are never trimmed. Only those of a level shown otherwise than the game shows it are, by the level editor
		// zoomed far out, with up to a hundred thousand tiles of a tinted layer in view; the particle storage isn't.
		constexpr std::int32_t MinPooledRenderCommands = 4096;
		constexpr std::int32_t RenderCommandPoolTrimInterval = 600;
		constexpr std::int32_t MinDebrisCapacity = INT32_MAX;
		constexpr std::int32_t MinPooledMeshBuffers = 64;
		constexpr std::int32_t MinMeshBufferCapacity = 32 * 4096;
#		define TILEMAP_TRIM_ONLY_EDITOR_VIEWS
#	if !defined(TILEMAP_USE_SINGLE_DRAW)
		constexpr std::int32_t MaxPooledRenderCommands = 0;
#	endif
#endif
	}

	TileMap::TileMap(StringView tileSetPath, std::uint16_t captionTileId, bool applyPalette)
		: _owner(nullptr), _sprLayerIndex(-1), _pitType(PitType::FallForever), _hasRollbackCheckpoint(false),
			_renderCommandsCount(0), _renderCommandsPeak(0), _renderCommandsPeakAge(0), _collapsingTimer(0.0f),
			_animatedTilesOffset(0), _tileSetPath(tileSetPath), _captionTileId(captionTileId), _tilesOverridden(false), _diffuseDeferred(false),
			_triggerState(ValueInit, TriggerCount), _triggerStateForRollback(ValueInit, TriggerCount)
	{
		// Where the atlas is repacked around the tiles the level uses, only the masks are loaded here - the
		// layers that say which tiles those are have not been read yet, and building the whole atlas only to
		// throw it away in PruneTilesetAtlas() was a full decode of the sheet and an upload of every tile for
		// nothing (0.7 s of a 3.6 s level load on the Nintendo 64)
		auto& tileSetPart = _tileSets.emplace_back();
		tileSetPart.Data = ContentResolver::Get().RequestTileSet(tileSetPath, captionTileId, applyPalette, nullptr, nullptr, !PruneAtlasToUsedTiles);
		tileSetPart.Offset = 0;
		if (tileSetPart.Data == nullptr) {
			// A level can name a tile set this copy of the game doesn't have, IsValid() then tells the caller
			LOGE("Failed to load main tileset \"{}\"", tileSetPath);
			tileSetPart.Count = 0;
		} else {
			_diffuseDeferred = PruneAtlasToUsedTiles;
			tileSetPart.Count = tileSetPart.Data->TileCount;
		}

		_renderCommands.reserve(128);
	}

	TileMap::~TileMap()
	{
		if (!_shiftedPaletteRows.empty()) {
			ReleaseShiftedPalettes(true);
		}
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
							// At least a frame, Delay can be set from scripts too
							std::int32_t delayFrames = 1 + std::max<std::int32_t>(animTile.Delay, 0);
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
						std::int32_t delayFrames = 1 + std::max<std::int32_t>(animTile.Delay, 0);
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

		// Wraps the offset of a repeating layer into its size. A single step is the usual case and is done as it always
		// was, so the layers move exactly as before; only an offset many sizes away (from a file or a script) is wrapped
		// at once, and a layer without a size or with a broken offset can't keep the wrapping going forever.
		auto wrapLayerOffset = [](float& offset, std::int32_t periodPixels, bool forwards) {
			if DEATH_UNLIKELY(!std::isfinite(offset)) {
				offset = 0.0f;
				return;
			}
			if (periodPixels <= 0) {
				return;
			}
			const float period = float(periodPixels);
			if (forwards) {
				if (offset > period) {
					offset -= period;
					if DEATH_UNLIKELY(offset > period) {
						offset = std::fmod(offset, period);
					}
				}
			} else if (offset < 0.0f) {
				offset += period;
				if DEATH_UNLIKELY(offset < 0.0f) {
					offset = std::fmod(offset, period);
					if (offset < 0.0f) {
						offset += period;
					}
				}
			}
		};

		// Update layer scrolling, unless the editor keeps the layers still. A layer fitted to the level doesn't move by
		// itself, and the auto speed of one placed by the size of the view is where it ends up at the end of the level.
		auto movesByAutoSpeed = [](LayerSpeedModel model) {
			return (model != LayerSpeedModel::FitLevel && model != LayerSpeedModel::SpeedMultipliers);
		};
		// A textured background moves its texture by the offsets as well, which repeats every 256 pixels - and only every 1024
		// pixels around a tunnel or along a cylinder of JJ2+, which the offset X moves a quarter as fast - so the offsets wrap
		// where both the texture and the tiles of the layer repeat
		auto wrapPeriod = [](std::int32_t tiles, std::int32_t texturePeriod) {
			std::int32_t period = tiles * 32;
			if (texturePeriod > 0 && period > 0) {
				std::int32_t a = period, b = texturePeriod;
				while (b != 0) {
					std::int32_t r = a % b;
					a = b;
					b = r;
				}
				period = period / a * texturePeriod;
			}
			return period;
		};
		for (auto& layer : _layers) {
			if DEATH_UNLIKELY(_viewOptions.PauseLayerMovement) {
				break;
			}
			const bool isTextured = LayerDescription::IsTexturedBackground(layer.Description.RendererType);
			// A sky, a tunnel or a cylinder placed where layer 5 is on an axis (see DrawTexturedLayer()) is moved only by its auto
			// speed there, not by its offset, so that's kept apart - where its texture repeats
			if (movesByAutoSpeed(layer.Description.SpeedModelX) && std::abs(layer.Description.AutoSpeedX) > 0) {
				if (layer.Description.IsTexturePlacedByLayer5(false)) {
					layer.TextureOffset.X = std::fmod(layer.TextureOffset.X + layer.Description.AutoSpeedX * timeMult, 1024.0f);
				} else {
					layer.Description.OffsetX += layer.Description.AutoSpeedX * timeMult;
					if (layer.Description.RepeatX) {
						wrapLayerOffset(layer.Description.OffsetX, wrapPeriod(layer.LayoutSize.X, isTextured ? 1024 : 0), layer.Description.AutoSpeedX > 0);
					}
				}
			}
			if (movesByAutoSpeed(layer.Description.SpeedModelY) && std::abs(layer.Description.AutoSpeedY) > 0) {
				if (layer.Description.IsTexturePlacedByLayer5(true)) {
					layer.TextureOffset.Y = std::fmod(layer.TextureOffset.Y + layer.Description.AutoSpeedY * timeMult, 256.0f);
				} else {
					layer.Description.OffsetY += layer.Description.AutoSpeedY * timeMult;
					if (layer.Description.RepeatY) {
						wrapLayerOffset(layer.Description.OffsetY, wrapPeriod(layer.LayoutSize.Y, isTextured ? 256 : 0), layer.Description.AutoSpeedY > 0);
					}
				}
			}
			// The texture of a textured or a reflection layer drawn only where the layer is moves inside it by the inner auto
			// speeds, as the layer itself does by its auto speeds. Over the whole view the auto speed Y moves the texture of a
			// reflection twice as far as it moves the layer, measured in the original (X just as far), so it's added here
			// once more. Wrapped into the size of the texture, which repeats.
			const bool hasInnerSurface = layer.Description.HasInnerSurface();
			if (layer.Description.RendererType == LayerRendererType::Reflection ||
				(hasInnerSurface && layer.Description.RendererType != LayerRendererType::TileMenu)) {
				if (hasInnerSurface) {
					if (layer.Description.TextureInnerAutoSpeed.X != 0.0f || layer.Description.TextureInnerAutoSpeed.Y != 0.0f) {
						const Vector2i textureSize = GetLayerTextureSize(layer);
						layer.TextureOffset.X = std::fmod(layer.TextureOffset.X + layer.Description.TextureInnerAutoSpeed.X * timeMult, float(textureSize.X));
						layer.TextureOffset.Y = std::fmod(layer.TextureOffset.Y + layer.Description.TextureInnerAutoSpeed.Y * timeMult, float(textureSize.Y));
					}
				} else if (movesByAutoSpeed(layer.Description.SpeedModelY) && layer.Description.AutoSpeedY != 0.0f) {
					const Vector2i textureSize = GetLayerTextureSize(layer);
					layer.TextureOffset.Y = std::fmod(layer.TextureOffset.Y + layer.Description.AutoSpeedY * timeMult, float(textureSize.Y));
				}
			} else if (layer.Description.RendererType == LayerRendererType::Wave) {
				// How far the waves of a wave have moved, in periods: the speed (the blue of the color, a signed byte) is 1/1024
				// of a period each 70th of a second, measured in the original, whatever the wavelength
				std::int8_t speed = std::int8_t(std::uint8_t(layer.Description.Color.Z * 255.0f + 0.5f));
				if (speed != 0) {
					constexpr float PeriodsPerFrame = 70.0f / (1024.0f * FrameTimer::FramesPerSecond);
					float phase = std::fmod(layer.TextureOffset.X + float(speed) * PeriodsPerFrame * timeMult, 1.0f);
					layer.TextureOffset.X = (phase < 0.0f ? phase + 1.0f : phase);
				}
			} else if (layer.Description.RendererType == LayerRendererType::TileMenu) {
				// How far a tile menu has turned (X) and zoomed (Y), at its own pace - JJ2+ turns it as fast whatever the auto
				// speeds, or the inner ones, are
				layer.TextureOffset += Vector2f(timeMult * MenuTimePerFrame, timeMult * MenuTimePerFrame);
			}
		}

#if defined(TILEMAP_HEAT_TILES)
		// The heat runs on the game ticks of the original, a row of its table every 4 of them, so its 32 rows come round
		// every 128 ticks
		_heatTicks = std::fmod(_heatTicks + timeMult * (70.0f / FrameTimer::FramesPerSecond), 128.0f);
#endif

		AdvanceCollapsingTileTimers(timeMult);
		UpdateDebris(timeMult);
	}

	void TileMap::OnEndFrame()
	{
		if DEATH_UNLIKELY(!_shiftedPaletteRows.empty()) {
			ReleaseShiftedPalettes(false);
		}

#if defined(TILEMAP_TRIM_ONLY_EDITOR_VIEWS)
		const bool trimsPools = (_viewOptions.Placement != LayerPlacement::Gameplay);
#else
		const bool trimsPools = (RenderCommandPoolTrimInterval > 0);
#endif
		if (trimsPools) {
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
#if defined(TILEMAP_LAYER_EFFECTS)
		_effectMeshCommandCount = 0;
		_reflectionCommandCount = 0;
#endif
#if defined(TILEMAP_HEAT_TILES)
		_heatMeshCommandCount = 0;
#endif
#if defined(TILEMAP_CACHE_LAYER_WINDOW)
		_layerWindowFrame++;
#endif
	}

	bool TileMap::OnDraw(RenderQueue& renderQueue)
	{
		ZoneScopedC(0xA09359);

		const Viewport* viewport = RenderResources::GetCurrentViewport();
		Rectf cullingRect = viewport->GetCullingRect();
		Vector2f viewCenter = cullingRect.Center();

#if defined(TILEMAP_USE_SINGLE_DRAW)
		UpdateMeshSlots();
#endif

		// The layers are placed by the view drawn, unless the view options name another one of the same center
		Rectf placementRect = cullingRect;
		if (_viewOptions.Placement == LayerPlacement::ReferenceView && _viewOptions.ReferenceViewSize.X > 0 && _viewOptions.ReferenceViewSize.Y > 0) {
			placementRect = Rectf(viewCenter.X - _viewOptions.ReferenceViewSize.X * 0.5f, viewCenter.Y - _viewOptions.ReferenceViewSize.Y * 0.5f,
				(float)_viewOptions.ReferenceViewSize.X, (float)_viewOptions.ReferenceViewSize.Y);
		}

#if defined(WITH_RHI_SOFTWARE) || defined(DEATH_TARGET_N64) || (defined(TILEMAP_OPAQUE_COVERAGE) && defined(WITH_ANGELSCRIPT))
		// See DrawLayer(), which reads this for every layer - nothing can recolor the palette in between
		_spritePaletteOpaque = true;
		{
			auto palettes = ContentResolver::Get().GetPalettes();
			for (std::int32_t i = 1; i < ContentResolver::ColorsPerPalette; i++) {
				if ((palettes[i] >> 24) != 255) {
					_spritePaletteOpaque = false;
					break;
				}
			}
		}
#endif

#if defined(TILEMAP_OPAQUE_COVERAGE)
		// The sprite layer goes first so that its opaque coverage exists when the layers behind it are
		// walked (their tiles under solid ground are then never emitted); the render queue orders the
		// commands by depth, so the emission order is unobservable
		_opaqueCoverage.Valid = false;
		if (_sprLayerIndex >= 0 && _sprLayerIndex < (std::int32_t)_layers.size()) {
			DrawLayer(renderQueue, _layers[_sprLayerIndex], cullingRect, placementRect, viewCenter);
		}
		for (std::int32_t i = 0; i < (std::int32_t)_layers.size(); i++) {
			if (i != _sprLayerIndex) {
				DrawLayer(renderQueue, _layers[i], cullingRect, placementRect, viewCenter);
			}
		}
#else
		for (auto& layer : _layers) {
			DrawLayer(renderQueue, layer, cullingRect, placementRect, viewCenter);
		}
#endif

		if (_sprLayerIndex != -1 && _viewOptions.DrawSideBars) {
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
		std::int32_t hx2 = std::min((std::int32_t)ceilFast(aabb.R), limitRightPx - 1);
		std::int32_t hy1 = std::clamp<std::int32_t>((std::int32_t)aabb.T, 0, limitBottomPx - 2);
		std::int32_t hy2 = std::clamp<std::int32_t>((std::int32_t)ceilFast(aabb.B), 1, limitBottomPx - 1);

		std::int32_t hx1t = hx1 / TileSet::DefaultTileSize;
		std::int32_t hx2t = hx2 / TileSet::DefaultTileSize;
		std::int32_t hy1t = hy1 / TileSet::DefaultTileSize;
		std::int32_t hy2t = hy2 / TileSet::DefaultTileSize;

		auto& sprLayerLayout = _layers[_sprLayerIndex].Layout;

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
		std::int32_t hx2 = std::min((std::int32_t)ceilFast(aabb.R), limitRightPx - 1);
		std::int32_t hy1 = std::clamp<std::int32_t>((std::int32_t)aabb.T, 0, limitBottomPx - 2);
		std::int32_t hy2 = std::clamp<std::int32_t>((std::int32_t)ceilFast(aabb.B), 1, limitBottomPx - 1);

		std::int32_t hx1t = hx1 / TileSet::DefaultTileSize;
		std::int32_t hx2t = hx2 / TileSet::DefaultTileSize;
		std::int32_t hy1t = hy1 / TileSet::DefaultTileSize;
		std::int32_t hy2t = hy2 / TileSet::DefaultTileSize;

		auto& sprLayerLayout = _layers[_sprLayerIndex].Layout;

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
			const std::int32_t animIndex = tile.DestructAnimation - std::int32_t(_animatedTilesOffset);
			if (animIndex >= std::int32_t(_animatedTiles.size())) {
				// The animated tiles were replaced since (by a script or the editor), there is nothing to advance
				return false;
			}
			AnimatedTile& anim = _animatedTiles[animIndex];
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

	void TileMap::DrawLayer(RenderQueue& renderQueue, TileMapLayer& layer, const Rectf& cullingRect, const Rectf& placementRect, Vector2f viewCenter)
	{
		ZoneScopedNC("Layer", 0xA09359);

		if (!layer.Visible) {
			return;
		}

		// A textured background or a reflection is drawn as a whole (see DrawTexturedLayer()), or as tiles where it can't be
		const Vector2f layerOffsets = layer.Description.GetDrawOffsets(placementRect.H);
		if (DrawTexturedLayer(renderQueue, layer, cullingRect, placementRect, viewCenter, layerOffsets)) {
			return;
		}

		Vector2i tileCount = layer.LayoutSize;
		// The current layer offsets, with the inherent offset where it applies
		float loX = layerOffsets.X;
		float loY = layerOffsets.Y;

		// Find out coordinates for a tile from outside the boundaries from topleft corner of the screen
		float x1 = cullingRect.X - HardcodedOffset;
		float y1 = cullingRect.Y - HardcodedOffset;
		// The same for the view the layer is placed by, the same one in the game (see TileMapViewOptions)
		const float px1 = placementRect.X - HardcodedOffset;
		const float py1 = placementRect.Y - HardcodedOffset;

		LayerRendererType rendererType = layer.Description.RendererType;
		Vector4f layerColor = layer.Description.Color;
		// The palette the indexed tiles are drawn through, the sprite palette unless the layer is palette-shifted
		float layerPaletteOffset = 0.0f;
		if (rendererType == LayerRendererType::Tinted) {
			// The tint is an entry of the palette, resolved every frame so it follows the palette as it changes
			layerColor = ResolveTintColor(layer.Description.Color);
		} else if (rendererType == LayerRendererType::SingleColor) {
			// The same for every pixel (see the shader the command gets in RentRenderCommand()), but only the color of the
			// entry is taken, as JJ2+ does - scripts pick entries the level doesn't use, which may be transparent
			layerColor = ResolveTintColor(Vector4f(layer.Description.Color.X, 0.0f, 0.0f, 1.0f));
			layerColor.W = layer.Description.Color.W;
		} else if (rendererType == LayerRendererType::PaletteShift) {
			// A row of the palette shifted by the red of the layer's color, built from the palette as it is, so the tiles
			// are drawn as any other through it; the alpha is the opacity of the layer, tiles in true color stay as they are
			layerPaletteOffset = (float)AcquireShiftedPalette(std::uint8_t(layer.Description.Color.X * 255.0f + 0.5f));
			layerColor = Vector4f(1.0f, 1.0f, 1.0f, layer.Description.Color.W);
		} else if (LayerDescription::IsTexturedBackground(rendererType)) {
			// Not drawn as a textured background (see DrawTexturedLayer()), so as an ordinary repeating tile layer
			rendererType = LayerRendererType::Default;
			// A sky layer's colour is the horizon tint the warped background shader blends towards (and its
			// W enables the star field), not a per-tile modulation. Handing it to the plain tile renderer
			// darkened the whole layer - and a level with a black horizon lost its background entirely.
			layerColor = Vector4f(1.0f, 1.0f, 1.0f, 1.0f);
		}

#if defined(TILEMAP_LAYER_EFFECTS)
		_effectLayer.Type = LayerRendererType::Default;
		if (rendererType == LayerRendererType::XRay || rendererType == LayerRendererType::BlurBehind || rendererType == LayerRendererType::ChromaKey) {
			if (!BeginEffectLayer(layer, rendererType)) {
				return;
			}
			if (rendererType == LayerRendererType::ChromaKey) {
				// The key is an entry of the palette as the tint of a tinted layer is, the tiles are drawn in their own colors
				Vector4f keyColor = ResolveTintColor(Vector4f(layer.Description.Color.X, 0.0f, 0.0f, 1.0f));
				_effectLayer.KeyColor = Vector3f(keyColor.X, keyColor.Y, keyColor.Z);
				layerColor = Vector4f(1.0f, 1.0f, 1.0f, layer.Description.Color.W);
			}
			// The static tiles of an x-ray layer are ordinary ones and take the ordinary path, only its animated tiles
			// are intercepted in the walk below. A blur layer stays what it is, which keeps it out of the mesh path and
			// its window cache - every tile of it goes into the blur mesh instead.
			if (rendererType == LayerRendererType::XRay) {
				rendererType = LayerRendererType::Default;
			}
		}
#endif

		// The layer coordinate at the left/top edge of the view the layer is placed by
		Vector2f layerCoords = layer.Description.GetViewCoordinates(layer.LayoutSize, _layers[_sprLayerIndex].LayoutSize,
			placementRect, loX, loY);
		float xt = layerCoords.X, yt = layerCoords.Y;

		// Moved to the edge of what is drawn, which is the same in the game; one layer pixel is one world pixel, the
		// speed only moves the layer as a whole
		xt += (x1 - px1);
		yt += (y1 - py1);
		if (_viewOptions.FlatAmount > 0.0f) {
			// Towards the layer placed like the sprite layer, its tile (0, 0) at the world position (0, 0)
			xt += (x1 - xt) * _viewOptions.FlatAmount;
			yt += (y1 - yt) * _viewOptions.FlatAmount;
		}

		// Images may be drawn in place of the tiles (see TileMapViewOptions::LayerOverride), the walk below then draws only the
		// tiles of the areas the images don't show yet
		const bool clipped = (_viewOptions.LayerOverride != nullptr && rendererType != LayerRendererType::BlurBehind &&
			rendererType != LayerRendererType::ChromaKey && DrawLayerImages(renderQueue, layer, rendererType, layerColor, cullingRect, Vector2f(x1 - xt, y1 - yt)));
		if (clipped && _layerTileAreas.empty()) {
#if defined(TILEMAP_LAYER_EFFECTS)
			_effectLayer.Type = LayerRendererType::Default;
#endif
			return;
		}

		// The tile of the layout the walk starts at on an axis, counted from the tile (0, 0) without wrapping and wrapped into
		// the layout
		const auto firstTile = [](float coord, std::int32_t count, std::int32_t& absolute, std::int32_t& wrapped) {
			absolute = (std::int32_t)(coord > 0 ? floorFast(coord / (float)TileSet::DefaultTileSize) : ceilFast(coord / (float)TileSet::DefaultTileSize));
			wrapped = absolute % count;
			if (wrapped < 0) {
				wrapped += count;
			}
		};
		std::int32_t tileX, tileY, tileAbsX, tileAbsY;
		firstTile(xt, tileCount.X, tileAbsX, tileX);
		firstTile(yt, tileCount.Y, tileAbsY, tileY);

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

#if defined(WITH_RHI_SOFTWARE) || defined(DEATH_TARGET_N64) || (defined(TILEMAP_OPAQUE_COVERAGE) && defined(WITH_ANGELSCRIPT))
		// Whether every non-zero entry of the sprite palette (row 0, the one tile layers sample) is fully
		// opaque. Combined with a tile's IsTileFilled() flag (no index-0 texel, the transparent base
		// entry) this proves the tile draws all 32x32 pixels opaque, so it may go out with blending off
		// (see below). Scanned once per OnDraw() rather than once per layer, but still every frame, because
		// scripts can recolor the palette at any time; very few tilesets ship translucent palette entries,
		// and those simply keep the blended path everywhere.
		const bool spritePaletteOpaque = _spritePaletteOpaque;
#endif

		// Standard tile layers are drawn as one mesh (one draw call for the whole visible layer, or one per
		// mesh slot - each texture chunk of each tile set, a device texture-size limit can split the atlas of
		// a tile set). Other renderer types (tinted/solid) fall back to one command per tile. The grouped
		// emission of the RDP keeps to levels of a single tile set.
#if defined(TILEMAP_USE_SINGLE_DRAW)
#	if defined(TILEMAP_GROUP_MESH_BY_TILE)
		bool meshMode = (rendererType == LayerRendererType::Default && _tileSets.size() == 1 && _tileSets[0].Data != nullptr);
#	else
		bool meshMode = (rendererType == LayerRendererType::Default && !_meshSlots.empty());
#	endif
#	if defined(TILEMAP_GROUP_MESH_BY_TILE) || defined(DEATH_TARGET_N64)
		TileSet* meshTileSet = (meshMode ? _tileSets[0].Data.get() : nullptr);
#	endif
		// One vertex buffer per mesh slot, rented on first use - a layer usually touches only some of them.
		// Indices rather than pointers, because renting can grow (and so reallocate) _meshVertices. Room for
		// eight inline: a console splits a large tileset into several atlas chunks (three 512-texel ones for
		// the castle tileset on the PSP), and growing past the inline storage was a heap allocation per
		// layer per frame - the only allocations a level frame made there.
		SmallVector<std::int32_t, 8> chunkVertices;
		if DEATH_LIKELY(meshMode) {
			chunkVertices.resize(_meshSlots.size(), -1);
		}
#	if defined(TILEMAP_GROUP_MESH_BY_TILE)
		_meshTileEntries.clear();
#	endif
#endif

		// Remembers the reciprocals of the last tile texture's dimensions across the walk below; a layer's
		// tiles overwhelmingly share one texture, so this turns four divisions per tile into two per layer
		Texture* lastTileTexture = nullptr;
		float lastTexInvW = 0.0f, lastTexInvH = 0.0f;
#if defined(TILEMAP_PACKED_TILE_RECORDS)
		float lastTilesPerRowInv = 0.0f;
		const TileSet* lastChunkTileSet = nullptr;
		float lastTilesPerTextureInv = 0.0f;
#endif
		bool meshCacheHit = false;
#if defined(TILEMAP_HEAT_TILES)
		// Only an ordinary layer draws its tiles of the heat effect so, and only where the shader is there; read once
		const bool heatTiles = (meshMode && ContentResolver::Get().GetShader(PrecompiledShader::TileMapMeshHeat) != nullptr);
		_heatMeshGroups.clear();
#endif
#if defined(TILEMAP_LAYER_EFFECTS)
		// Read once rather than per tile - the walk below is the hot path of every layer
		const bool xrayLayer = (_effectLayer.Type == LayerRendererType::XRay);
		const bool blurLayer = (_effectLayer.Type == LayerRendererType::BlurBehind);
		const bool chromaKeyLayer = (_effectLayer.Type == LayerRendererType::ChromaKey);
		if (blurLayer) {
			// No run of reflective tiles is known yet in any column of the window
			_reflectionColumns.clear();
		}
#endif

		// Y is the OUTER loop, so the walk runs along the layout's rows. `Layout` is indexed
		// `tileX + tileY * LayoutSize.X`, so with X outside (as it used to be) the inner step jumped a
		// whole row - 12 bytes per LayerTile times a level width of a couple of hundred, about 3 KB - and
		// every one of the ~200 tiles a layer visits landed on its own cache line. Along a row instead,
		// eighteen consecutive tiles share four lines. The same tiles are visited either way; only the
		// order the quads are appended in changes, and tiles of one layer never overlap.
#if defined(TILEMAP_OPAQUE_COVERAGE)
		/*
			Tiles that cannot be seen are not drawn. A level's sprite layer is mostly solid ground and
			walls, and every background layer keeps drawing its own tiles underneath - on the Nintendo 64
			that was a quarter of the ~370 rectangles of a frame, each with its RDP fill and often a TMEM
			load, and the PowerVR renders its translucent list without any hidden-surface removal, so
			there every hidden tile costs its submission and the fill of all its pixels - for pixels that
			the sprite layer overwrites a moment later. The sprite layer is walked first (see OnDraw) and
			records which of its screen cells hold a FULLY opaque tile: no transparent texel
			(IsTileFilled), no texel in a translucent palette colour (see below), full tile and layer
			alpha. A tile of a layer behind it is skipped when the up-to-four cells its 32x32 screen
			rectangle touches are all covered. Layers in front are never culled, and neither is the procedural background,
			which does not come through this loop.
		*/
		const bool buildsCoverage = (&layer == &_layers[_sprLayerIndex] && rendererType == LayerRendererType::Default &&
			layerColor.W >= 1.0f && PreferencesCache::UnalignedViewport == false);
		const bool cullsByCoverage = (!buildsCoverage && _opaqueCoverage.Valid && rendererType == LayerRendererType::Default &&
			layer.Description.Depth < _opaqueCoverage.Depth);
		auto floorToInt = [](float v) { const std::int32_t i = std::int32_t(v); return (float(i) > v ? i - 1 : i); };
		const std::int32_t x1i = floorToInt(x1), y1i = floorToInt(y1);
#endif
#if defined(DEATH_TARGET_N64)
		/*
			The window cache (see LayerMeshCache). The tiles a layer shows change only when its window
			moves by a whole tile - every 32 pixels of ITS scroll, so rarely for the slow parallax layers -
			while the walk below (tile IDs, atlas slots, coverage, texture coordinates, the quads) cost
			~7 ms of every frame for the eight layers of a castle level. The window is identified by its
			first tile, its size, the palette's opacity, and for a culled layer the coverage grid it was
			culled against together with the whole-cell offset between the two grids (the cull depends
			on nothing finer, see the mask below); the window's tile IDs are compared as well, which is
			what catches a destroyed or triggered tile. On a hit the covering layer's own coverage grid
			comes back from the cache, under the current origin.
		*/
		auto floorDivTile = [](std::int32_t d) { return (d >= 0 ? d / (std::int32_t)TileSet::DefaultTileSize : -((-d + (std::int32_t)TileSet::DefaultTileSize - 1) / (std::int32_t)TileSet::DefaultTileSize)); };
		const std::int32_t windowCols = std::int32_t((x3 - x1) / TileSet::DefaultTileSize) + 1;
		const std::int32_t windowRows = std::int32_t((y3 - y1) / TileSet::DefaultTileSize) + 1;
		LayerMeshCache* meshCache = nullptr;
		std::int32_t cullShiftX = 0, cullShiftY = 0;
		// Not for a few areas of the layer, the window is all of it
		if (meshMode && !clipped) {
			if (_layerMeshCaches.size() != _layers.size()) {
				_layerMeshCaches.resize(_layers.size());
			}
			meshCache = &_layerMeshCaches[std::size_t(&layer - _layers.data())];
			if (cullsByCoverage) {
				cullShiftX = floorDivTile(x1i - _opaqueCoverage.OriginX);
				cullShiftY = floorDivTile(y1i - _opaqueCoverage.OriginY);
			}
			meshCacheHit = (meshCache->Valid && meshCache->TileAbsX == tileAbsX && meshCache->TileAbsY == tileAbsY &&
				meshCache->Cols == windowCols && meshCache->Rows == windowRows && meshCache->PaletteOpaque == spritePaletteOpaque &&
				meshCache->Covering == buildsCoverage && meshCache->Culled == cullsByCoverage &&
				(!cullsByCoverage || (meshCache->CoverageStamp == _coverageStamp && meshCache->CullShiftX == cullShiftX && meshCache->CullShiftY == cullShiftY)) &&
				((layer.Layout.IsCompact() && meshCache->LayoutVersion == layer.Layout.GetVersion()) ||
				 LayerMeshWindowUnchanged(layer, *meshCache, tileAbsX, tileAbsY, tileXs, tileY)));
			// A window at the same place in a layout nobody has written to since holds the same tiles, so the
			// tile-by-tile comparison - a few hundred lookups a frame over every layer - is needed only after a
			// write, and only once: the window then stands checked against the current stamp
			if (meshCacheHit) {
				meshCache->LayoutVersion = layer.Layout.GetVersion();
			}
			if (meshCacheHit && buildsCoverage) {
				_opaqueCoverage = meshCache->Coverage;
				_opaqueCoverage.OriginX = x1i;
				_opaqueCoverage.OriginY = y1i;
				_opaqueCoverage.Valid = true;
			}
			if (!meshCacheHit) {
				meshCache->WindowIds.clear();
				meshCache->Animated.clear();
			}
		}
		OpaqueCoverage previousCoverage = _opaqueCoverage;
#endif
#if defined(TILEMAP_OPAQUE_COVERAGE)
		if (buildsCoverage && !meshCacheHit) {
			_opaqueCoverage.OriginX = x1i;
			_opaqueCoverage.OriginY = y1i;
			_opaqueCoverage.Cols = std::min<std::int32_t>(std::int32_t((x3 - x1) / TileSet::DefaultTileSize) + 1, OpaqueCoverage::MaxCols);
			_opaqueCoverage.Rows = std::min<std::int32_t>(std::int32_t((y3 - y1) / TileSet::DefaultTileSize) + 1, OpaqueCoverage::MaxRows);
			_opaqueCoverage.Depth = layer.Description.Depth;
			std::memset(_opaqueCoverage.RowBits, 0, sizeof(_opaqueCoverage.RowBits));
			_opaqueCoverage.Valid = true;
		}
		// Floor division by the tile size for a screen offset that can be negative
		auto cellOf = [](std::int32_t d) { return (d >= 0 ? d / (std::int32_t)TileSet::DefaultTileSize : -((-d + (std::int32_t)TileSet::DefaultTileSize - 1) / (std::int32_t)TileSet::DefaultTileSize)); };
#endif

#if defined(TILEMAP_CACHE_LAYER_WINDOW)
		// The window cache (see LayerWindowCache). While it's being rebuilt, the static tiles of the walk below
		// go into it relative to the window's origin, and only the animated ones are streamed.
		LayerWindowCache* windowCache = nullptr;
		float windowOriginX = 0.0f, windowOriginY = 0.0f;
		// Not for a few areas of the layer, the window is all of it
		if (meshMode && !clipped) {
			// Counted with the very float additions the walk makes, so a window that ends exactly on a tile
			// boundary cannot come out a cell longer or shorter than the walk
			auto countSteps = [](float from, float to) {
				std::int32_t steps = 0;
				for (float v = from; v <= to; v += TileSet::DefaultTileSize) {
					steps++;
				}
				return steps;
			};
			const std::int32_t windowCols = countSteps(x1, x3);
			const std::int32_t windowRows = countSteps(y1, y3);
			// The walk puts a tile at the floor of its position when aligned, which for whole-tile steps is the
			// floor of the origin plus whole tiles
			const bool unaligned = PreferencesCache::UnalignedViewport;
			windowOriginX = (unaligned ? x1 : std::floor(x1));
			windowOriginY = (unaligned ? y1 : std::floor(y1));

			windowCache = &GetLayerWindowCache(std::int32_t(&layer - _layers.data()));
			const TileSet* mainTileSet = _tileSets[0].Data.get();
			const std::size_t chunkCount = std::max<std::size_t>(_meshSlots.size(), 1);
			meshCacheHit = (windowCache->Valid && windowCache->TileSetData == mainTileSet && windowCache->Unaligned == unaligned &&
				windowCache->RepeatX == layer.Description.RepeatX && windowCache->RepeatY == layer.Description.RepeatY &&
				windowCache->LayoutSize == tileCount && windowCache->TileAbsX == tileAbsX && windowCache->TileAbsY == tileAbsY &&
				windowCache->Cols == windowCols && windowCache->Rows == windowRows && windowCache->Chunks.size() == chunkCount &&
				LayerWindowUnchanged(layer, *windowCache, tileAbsY, tileXs, tileY));
			for (std::int32_t c = 0; meshCacheHit && c < (std::int32_t)windowCache->Chunks.size(); c++) {
				// The atlas textures can be recreated, which the cached commands would still sample
				const auto& chunk = windowCache->Chunks[c];
				meshCacheHit = (chunk.QuadCount == 0 ||
					(c < (std::int32_t)_meshSlots.size() && chunk.DiffuseTexture == _meshSlots[c].GetTexture()));
			}
			if (!meshCacheHit) {
				windowCache->Valid = false;
				windowCache->TileSetData = mainTileSet;
				windowCache->Unaligned = unaligned;
				windowCache->RepeatX = layer.Description.RepeatX;
				windowCache->RepeatY = layer.Description.RepeatY;
				windowCache->LayoutSize = tileCount;
				windowCache->TileAbsX = tileAbsX;
				windowCache->TileAbsY = tileAbsY;
				windowCache->Cols = windowCols;
				windowCache->Rows = windowRows;
				windowCache->Window.clear();
				windowCache->Animated.clear();
				if (windowCache->Chunks.size() != chunkCount) {
					windowCache->Chunks.resize(chunkCount);
				}
				for (auto& chunk : windowCache->Chunks) {
					chunk.Vertices.clear();
					chunk.QuadCount = 0;
				}
			}
		}
#endif

		// The parts of the row being walked that are drawn while the walk is clipped to the areas of the layer
		// without images (see TileMapLayerImage), from the first tile to the one after the last one, in the tiles
		// of the layer as they're counted from its tile (0, 0) without wrapping
		SmallVector<Vector2i, 8> clipSpans;
		std::size_t clipNext = 0;
		// The rows are walked downwards, the spans are the same until the next row an area starts or ends at
		std::int32_t clipSpansUntil = INT32_MIN;
		auto collectClipSpans = [this, &clipSpans, &clipNext, &clipSpansUntil](std::int32_t row) {
			clipNext = 0;
			if (row < clipSpansUntil) {
				return;
			}
			clipSpans.clear();
			clipSpansUntil = INT32_MAX;
			for (const Recti& area : _layerTileAreas) {
				if (area.Y > row) {
					clipSpansUntil = std::min<std::int32_t>(clipSpansUntil, area.Y);
				} else if (area.Y + area.H > row) {
					clipSpansUntil = std::min<std::int32_t>(clipSpansUntil, area.Y + area.H);
					if (area.W > 0) {
						clipSpans.push_back(Vector2i(area.X, area.X + area.W));
					}
				}
			}
			std::sort(clipSpans.begin(), clipSpans.end(), [](const Vector2i& a, const Vector2i& b) { return a.X < b.X; });
			std::size_t merged = 0;
			for (std::size_t i = 0; i < clipSpans.size(); i++) {
				if (merged > 0 && clipSpans[i].X <= clipSpans[merged - 1].Y) {
					clipSpans[merged - 1].Y = std::max(clipSpans[merged - 1].Y, clipSpans[i].Y);
				} else {
					clipSpans[merged++] = clipSpans[i];
				}
			}
			clipSpans.resize(merged);
		};

		// Where a tile at a position of the walk is drawn: at whole pixels unless the viewport is unaligned. MIPS and SH-4 have no
		// floor instruction, so `std::floor` would be a real `jal floorf` into libm - twice per tile. A truncating cast with the
		// negative correction is the same value for any coordinate that fits in an int32, which a tile position on screen
		// always does.
		const bool unalignedViewport = PreferencesCache::UnalignedViewport;
		const auto alignTilePos = [unalignedViewport](float pos) {
			if (unalignedViewport) {
				return pos;
			}
			const float truncated = float(std::int32_t(pos));
			return (truncated > pos ? truncated - 1.0f : truncated);
		};

		std::int32_t tile_yo = -1;
		if (!meshCacheHit)
		for (float y2 = y1; y2 <= y3; y2 += TileSet::DefaultTileSize) {
			// A compare-and-wrap rather than a modulo: tileY is always already below tileCount.Y, so the
			// two agree, and an integer division is about 36 cycles on the consoles' in-order cores
			if (++tileY >= tileCount.Y) { tileY = 0; }
			tile_yo++;
			if (!layer.Description.RepeatY) {
				// If the current tile isn't in the first iteration of the layer vertically, skip this row
				if (tileAbsY + tile_yo + 1 < 0 || tileAbsY + tile_yo + 1 >= tileCount.Y) {
#if defined(DEATH_TARGET_N64)
					if (meshCache != nullptr) {
						for (std::int32_t k = 0; k < windowCols; k++) { meshCache->WindowIds.push_back(0); }
					}
#endif
#if defined(TILEMAP_CACHE_LAYER_WINDOW)
					if (windowCache != nullptr) {
						for (std::int32_t k = 0; k < windowCache->Cols; k++) { windowCache->Window.push_back(0); }
					}
#endif
					continue;
				}
			}
			if DEATH_UNLIKELY(clipped) {
				// Neither cache is used then, nothing has to be recorded for the cells skipped
				collectClipSpans(tileAbsY + tile_yo + 1);
				if (clipSpans.empty()) {
					continue;
				}
			}
			tileX = tileXs;
			std::int32_t tile_xo = -1;
			for (float x2 = x1; x2 <= x3; x2 += TileSet::DefaultTileSize) {
				// As above, and this one is per TILE rather than per row
				if (++tileX >= tileCount.X) { tileX = 0; }
				tile_xo++;
				if DEATH_UNLIKELY(clipped) {
					const std::int32_t absX = tileAbsX + tile_xo + 1;
					while (clipNext < clipSpans.size() && clipSpans[clipNext].Y <= absX) {
						clipNext++;
					}
					if (clipNext >= clipSpans.size()) {
						// Nothing more to draw in the row, from far away it's thousands of cells
						break;
					}
					if (absX < clipSpans[clipNext].X) {
						// Straight to the cell before the next span, the loop steps onto it
						const std::int32_t skip = clipSpans[clipNext].X - absX - 1;
						if (skip > 0) {
							tile_xo += skip;
							tileX = (tileX + skip) % tileCount.X;
							x2 += float(skip) * TileSet::DefaultTileSize;
						}
						continue;
					}
				}

				// By reference: nothing below looks another tile up while this one is in use, which is all a
				// compact layer's decoded tile needs to stay valid (see LayerLayout), and the copy was 12 bytes
				// per visited cell
				const LayerTile& tile = layer.Layout[tileX + tileY * layer.LayoutSize.X];
#if defined(DEATH_TARGET_N64)
				if (meshCache != nullptr) {
					meshCache->WindowIds.push_back(tile.TileID);
				}
#endif
#if defined(TILEMAP_CACHE_LAYER_WINDOW)
				if (windowCache != nullptr) {
					windowCache->Window.push_back(PackWindowTile(tile));
				}
#endif

				if (!layer.Description.RepeatX) {
					// If the current tile isn't in the first iteration of the layer horizontally, don't draw it
					if (tileAbsX + tile_xo + 1 < 0 || tileAbsX + tile_xo + 1 >= tileCount.X) {
						continue;
					}
				}

				// An animated tile changes frame on its own schedule: it never enters the window cache or
				// the coverage grid (its opacity may change too) and is resolved again every frame. Its
				// cell is recorded BEFORE the current frame is looked at - a frame that happens to be the
				// empty tile right now must not drop the cell from the window for the window's lifetime
				const bool animatedTile = (tile.TileID >= _animatedTilesOffset);
				static_cast<void>(animatedTile);
#if defined(DEATH_TARGET_N64)
				if (animatedTile && meshCache != nullptr && tile.Alpha != 0) {
					meshCache->Animated.push_back({ (std::uint16_t)tile_xo, (std::uint16_t)tile_yo, tileX + tileY * layer.LayoutSize.X });
				}
#endif
#if defined(TILEMAP_CACHE_LAYER_WINDOW)
				// A tile of the heat effect is streamed every frame too, into the heat mesh (see below)
#	if defined(TILEMAP_HEAT_TILES)
				const bool streamedTile = (animatedTile || (heatTiles && (tile.Flags & LayerTileFlags::Heat) == LayerTileFlags::Heat));
#	else
				const bool streamedTile = animatedTile;
#	endif
				if (streamedTile && windowCache != nullptr && tile.Alpha != 0) {
					windowCache->Animated.push_back({ (std::uint16_t)tile_xo, (std::uint16_t)tile_yo, tileX + tileY * layer.LayoutSize.X });
				}
#endif
#if defined(TILEMAP_LAYER_EFFECTS)
				// The current frame and the mask frame of an x-ray tile. Placed where a streamed tile of
				// the layer would be (see x2r below), which is where a window cache hit places it as well.
				if (animatedTile && xrayLayer && tile.Alpha != 0 &&
					AppendXRayTile(tile, alignTilePos(x2), alignTilePos(y2))) {
					continue;
				}
#endif
				std::int32_t tileId = ResolveTileID(tile);
				if (tileId == 0 || tile.Alpha == 0) {
					continue;
				}
#if defined(TILEMAP_LAYER_EFFECTS)
				if (blurLayer) {
					const float blurX = alignTilePos(x2);
					const float blurY = alignTilePos(y2);
					AppendEffectTile(EffectMeshKind::Blur, tileId, tile.Flags, tile.Alpha / 255.0f, blurX, blurY,
						GetReflectionAxis(layer, tileX, tileY, tile_xo, tile_yo, tileId, blurY));
					continue;
				}
				if (chromaKeyLayer) {
					AppendEffectTile(EffectMeshKind::ChromaKey, tileId, tile.Flags, tile.Alpha / 255.0f, alignTilePos(x2), alignTilePos(y2));
					continue;
				}
#endif
#if defined(TILEMAP_HEAT_TILES)
				// A tile of the heat effect goes into the heat mesh, drawn after the layer (see EmitHeatMeshes()), placed where
				// a streamed tile of the layer would be (see x2r below), which is where a window cache hit places it as well
				if (heatTiles && IsHeatTile(tile) && AppendHeatTile(tileId, tile.Flags, tile.Alpha / 255.0f, alignTilePos(x2), alignTilePos(y2))) {
					continue;
				}
#endif
#if defined(TILEMAP_USE_SINGLE_DRAW)
				std::int32_t meshSlotBase = 0;
				TileSet* tileSet = ResolveTileSet(tileId, meshSlotBase);
#else
				TileSet* tileSet = ResolveTileSet(tileId);
#endif
				if (tileSet == nullptr) {
					continue;
				}

#if defined(WITH_RHI_SOFTWARE) || defined(DEATH_TARGET_N64) || (defined(TILEMAP_OPAQUE_COVERAGE) && defined(WITH_ANGELSCRIPT))
				// Whether every pixel this tile samples is provably opaque (see spritePaletteOpaque
				// above). Read before ResolveTextureDiffuse(), which rebases the ID into its texture chunk
				const bool tileFilled = tileSet->IsTileFilled(tileId) && (!tileSet->IsIndexed || spritePaletteOpaque);
#endif
#if defined(TILEMAP_OPAQUE_COVERAGE)
				if (buildsCoverage) {
					// A script can recolor the palette while the level runs, which leaves only a palette with no
					// translucent colour at all as proof; without scripting the palette stays the one the tile set
					// was built with, so its own per-tile flag holds - the converted palettes nearly all carry a
					// few translucent entries, which the whole-palette test would let no tile past.
					// The Nintendo 64 keeps the whole-palette test for now: there the coverage also keys the window
					// caches of the culled layers (see LayerMeshCache), so a coverage that changes as the view
					// scrolls trades their cache hits for the skipped rectangles - not yet measured on that console.
#	if defined(TILEMAP_PER_TILE_OPACITY)
					const bool tileOpaque = tileSet->IsTileOpaque(tileId);
#	else
					const bool tileOpaque = tileFilled;
#	endif
					if (tileOpaque && tile.Alpha == 255 && !animatedTile && tile_xo < _opaqueCoverage.Cols && tile_yo < _opaqueCoverage.Rows) {
						_opaqueCoverage.RowBits[tile_yo] |= (1u << tile_xo);
					}
				} else if (cullsByCoverage && !animatedTile) {
					// The tile lands at floor(x2), floor(y2) (see x2r below); its rectangle spans at most
					// two coverage cells per axis
					// floor by truncation with a correction: two libm calls per tile otherwise (no floor
					// instruction on this CPU), for coordinates far inside the int range
					std::int32_t fx = std::int32_t(x2), fy = std::int32_t(y2);
					if (float(fx) > x2) { fx--; }
					if (float(fy) > y2) { fy--; }
					const std::int32_t dx = fx - _opaqueCoverage.OriginX;
					const std::int32_t dy = fy - _opaqueCoverage.OriginY;
					const std::int32_t cx0 = cellOf(dx), cx1 = cellOf(dx + (std::int32_t)TileSet::DefaultTileSize - 1);
					const std::int32_t cy0 = cellOf(dy), cy1 = cellOf(dy + (std::int32_t)TileSet::DefaultTileSize - 1);
					if (cx0 >= 0 && cx1 < _opaqueCoverage.Cols && cy0 >= 0 && cy1 < _opaqueCoverage.Rows) {
						const std::uint32_t mask = (cx1 > cx0 ? (3u << cx0) : (1u << cx0));
						bool hidden = ((_opaqueCoverage.RowBits[cy0] & mask) == mask);
						if (hidden && cy1 > cy0) {
							hidden = ((_opaqueCoverage.RowBits[cy1] & mask) == mask);
						}
						if (hidden) {
							continue;
						}
					}
				}
#endif

#if defined(TILEMAP_USE_SINGLE_DRAW)
				// Which texture chunk holds this tile, from the ID as it is here - rebased into its chunk, the
				// ID is always below TilesPerTexture and this would collapse to chunk 0, drawing the whole layer
				// out of the first texture. A no-op single-texture lookup normally; only a device texture-size
				// limit small enough to split the tileset atlas (the consoles) makes it matter.
				// Through the atlas mapping first: where the atlas holds only the tiles this level uses, the
				// chunk a tile lives in follows its packed slot, not its ID.
				const std::int32_t tileSlot = tileSet->MapToAtlasSlot(tileId);
#	if defined(TILEMAP_PACKED_TILE_RECORDS)
				// No integer divider on the SH-4, so the division is a library call - the same exact float quotient
				// as the row below instead, with the reciprocal kept for as long as the tile set stays the same
				if DEATH_UNLIKELY(tileSet != lastChunkTileSet) {
					lastChunkTileSet = tileSet;
					lastTilesPerTextureInv = (tileSet->TilesPerTexture > 0 ? 1.0f / float(tileSet->TilesPerTexture) : 0.0f);
				}
				const std::int32_t tileChunk = (tileSet->TilesPerTexture > 0 && tileSlot >= tileSet->TilesPerTexture
					? std::int32_t((float(tileSlot) + 0.5f) * lastTilesPerTextureInv) : 0);
#	else
				const std::int32_t tileChunk = (tileSet->TilesPerTexture > 0 && tileSlot >= tileSet->TilesPerTexture
					? tileSlot / tileSet->TilesPerTexture : 0);
#	endif
				// What ResolveTextureDiffuse() would return, from the chunk that is already known: the texture of
				// that chunk, and the ID rebased into it - without mapping the ID and dividing a second time
				Texture* tileTexture = (tileChunk < tileSet->GetTextureCount() ? tileSet->TextureDiffuse[tileChunk].get() : nullptr);
				tileId = tileSlot - tileChunk * tileSet->TilesPerTexture;
#else
				Texture* tileTexture = tileSet->ResolveTextureDiffuse(tileId);
#endif
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
#if defined(TILEMAP_PACKED_TILE_RECORDS)
					// A texture belongs to one tileset, so its row width changes only with the texture
					lastTilesPerRowInv = 1.0f / float(tileSet->TilesPerRow);
#endif
				}
#if defined(TILEMAP_PACKED_TILE_RECORDS)
				// No integer divider on the SH-4: the division is a library call, made for every tile. Offset by
				// half an ID, the quotient's fraction stays at least half a row width away from the next integer
				// for any ID a layer tile can hold, so the float product truncates to the exact row.
				const std::int32_t tileRow = std::int32_t((float(tileId) + 0.5f) * lastTilesPerRowInv);
#else
				const std::int32_t tileRow = tileId / tileSet->TilesPerRow;
#endif
				const std::int32_t tileCol = tileId - tileRow * tileSet->TilesPerRow;
				// Where the tile is in its texture, as the scale and bias the quad paths below map their corners
				// with. The packed tile records carry the texel origin and the flips themselves, so with them only
				// the per-tile commands at the end of the loop need these, and the mesh path skips the arithmetic
				float texScaleX, texBiasX, texScaleY, texBiasY;
				const auto computeTexRect = [&]() {
					texScaleX = TileSet::DefaultTileSize * lastTexInvW;
					texBiasX = (tileCol * float(TileSet::PaddedTileSize) + TileSet::TilePadding) * lastTexInvW;
					texScaleY = TileSet::DefaultTileSize * lastTexInvH;
					texBiasY = (tileRow * float(TileSet::PaddedTileSize) + TileSet::TilePadding) * lastTexInvH;

					// ToDo: Flip normal map somehow
					if ((tile.Flags & LayerTileFlags::FlipX) == LayerTileFlags::FlipX) {
						texBiasX += texScaleX;
						texScaleX *= -1;
					}
					if ((tile.Flags & LayerTileFlags::FlipY) == LayerTileFlags::FlipY) {
						texBiasY += texScaleY;
						texScaleY *= -1;
					}
				};
#if !defined(TILEMAP_PACKED_TILE_RECORDS)
				computeTexRect();
#endif

				float x2r = alignTilePos(x2), y2r = alignTilePos(y2);

#if defined(TILEMAP_USE_SINGLE_DRAW)
				if DEATH_LIKELY(meshMode) {
#	if defined(TILEMAP_GROUP_MESH_BY_TILE)
					// Held back so the whole layer can be emitted grouped by atlas slot below; the quad this
					// would have appended is fully described by what goes into the entry
					_meshTileEntries.push_back({ x2r, y2r,
						(std::uint16_t)(tileCol * TileSet::PaddedTileSize + TileSet::TilePadding),
						(std::uint16_t)(tileRow * TileSet::PaddedTileSize + TileSet::TilePadding),
						tile.Alpha, MeshTileFlags(tile.Flags), (std::uint16_t)tileSlot, (std::uint16_t)tileChunk,
						(std::uint16_t)tile_xo, (std::uint16_t)tile_yo, tileX + tileY * layer.LayoutSize.X, animatedTile });
#	else
					// The texture chunk among those of all tile sets
					const std::int32_t meshSlot = meshSlotBase + tileChunk;
#		if defined(TILEMAP_CACHE_LAYER_WINDOW)
					if (windowCache != nullptr) {
						const float cellX = float(tile_xo * (std::int32_t)TileSet::DefaultTileSize);
						const float cellY = float(tile_yo * (std::int32_t)TileSet::DefaultTileSize);
						if (!animatedTile && meshSlot < (std::int32_t)windowCache->Chunks.size()) {
							// Relative to the window's origin, the cached commands are translated there
							AppendTileQuad(windowCache->Chunks[meshSlot].Vertices, cellX, cellY, (float)TileSet::DefaultTileSize,
								texScaleX, texBiasX, texScaleY, texBiasY, tile.Alpha / 255.0f);
							continue;
						}
						// Streamed at the very position a cache hit streams it at (see AppendAnimatedWindowTiles())
						x2r = windowOriginX + cellX;
						y2r = windowOriginY + cellY;
					}
#		endif
					// Accumulate this tile into its slot's mesh; the layer tint and palette are applied once
					// per emitted mesh in EmitMesh(). The per-tile alpha rides along in the vertex color.
					std::int32_t& verticesIndex = chunkVertices[meshSlot];
					if (verticesIndex < 0) {
						verticesIndex = RentMeshVertices();
					}
#		if defined(TILEMAP_PACKED_TILE_RECORDS)
					AppendTileRecord(_meshVertices[verticesIndex], x2r, y2r, tileCol * TileSet::PaddedTileSize + TileSet::TilePadding,
						tileRow * TileSet::PaddedTileSize + TileSet::TilePadding, tile.Alpha, tile.Flags);
#		else
					AppendTileQuad(_meshVertices[verticesIndex], x2r, y2r, (float)TileSet::DefaultTileSize,
						texScaleX, texBiasX, texScaleY, texBiasY, tile.Alpha / 255.0f);
#		endif
#	endif
					continue;
				}
#endif

#if defined(TILEMAP_PACKED_TILE_RECORDS)
				computeTexRect();
#endif
				TileCommandUniforms* commandUniforms;
				auto command = RentRenderCommand(rendererType, tileSet->IsIndexed, &commandUniforms);
				command->SetType(RenderCommand::Type::TileMap);
				command->GetMaterial().SetBlendingFactors(BlendingFactor::SrcAlpha, BlendingFactor::OneMinusSrcAlpha);

				// A solid color layer draws its tiles without a texture, its shader has no texture rectangle
				if (commandUniforms->TexRect != nullptr) {
					commandUniforms->TexRect->SetFloatValue(texScaleX, texBiasX, texScaleY, texBiasY);
				}
				if (commandUniforms->SpriteSize != nullptr) {
					commandUniforms->SpriteSize->SetFloatValue(TileSet::DefaultTileSize, TileSet::DefaultTileSize);
				}

				Vector4f color = layerColor;
				color.W *= tile.Alpha / 255.0f;
				if (commandUniforms->Color != nullptr) {
					commandUniforms->Color->SetFloatVector(color.Data());
				}

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
						commandUniforms->PaletteOffset->SetFloatValue(layerPaletteOffset);
					}
				}

				renderQueue.AddCommand(command);
			}
		}

#if defined(DEATH_TARGET_N64)
		if (buildsCoverage && !meshCacheHit) {
			// A grid that came out different from the last one invalidates every layer culled against it
			if (!previousCoverage.Valid || previousCoverage.Cols != _opaqueCoverage.Cols || previousCoverage.Rows != _opaqueCoverage.Rows ||
				std::memcmp(previousCoverage.RowBits, _opaqueCoverage.RowBits, sizeof(_opaqueCoverage.RowBits)) != 0) {
				_coverageStamp++;
			}
		}
#endif
#if defined(TILEMAP_GROUP_MESH_BY_TILE)
		if DEATH_LIKELY(meshMode) {
#	if defined(DEATH_TARGET_N64)
			if (meshCacheHit) {
				// The window's static tiles go out as the cached commands; only the animated cells are
				// resolved and emitted afresh
				AddCachedLayerMesh(renderQueue, *meshCache, x1i, y1i, layerColor, layer.Description.Depth);
				PushAnimatedMeshEntries(layer, *meshCache, *meshTileSet, x1i, y1i);
			}
#	endif
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
			const std::uint32_t entryCount = (std::uint32_t)_meshTileEntries.size();
			// Every entry of the layer goes out through EmitTileRuns() below, straight into the streaming
			// vertex buffer; `_meshTileOrder` is the emission order (identity when nothing was grouped)
			_meshTileOrder.resize_for_overwrite(entryCount);
			for (std::uint32_t i = 0; i < entryCount; i++) {
				_meshTileOrder[i] = (std::uint16_t)i;
			}
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
				for (std::uint32_t i = 0; i < entryCount; i++) {
					_meshTileOrder[_meshTileBuckets[_meshTileEntries[i].Slot - minSlot]++] = (std::uint16_t)i;
				}
			}
			// Slots ascend through the atlas chunks, so the sorted order is also grouped by chunk: each
			// run of one chunk becomes its own draw(s), sampling that chunk's texture
#	if defined(DEATH_TARGET_N64)
			if (!meshCacheHit && meshCache != nullptr) {
				// Records the window: the static tiles (in their grouped order) become the layer's own
				// commands, the animated ones stay in the order list for the streaming emission below
				meshCache->Valid = true;
				meshCache->Covering = buildsCoverage;
				meshCache->Culled = cullsByCoverage;
				meshCache->PaletteOpaque = spritePaletteOpaque;
				meshCache->TileAbsX = tileAbsX;
				meshCache->TileAbsY = tileAbsY;
				meshCache->Cols = windowCols;
				meshCache->Rows = windowRows;
				meshCache->CullShiftX = cullShiftX;
				meshCache->CullShiftY = cullShiftY;
				meshCache->CoverageStamp = _coverageStamp;
				meshCache->LayoutVersion = layer.Layout.GetVersion();
				if (buildsCoverage) {
					meshCache->Coverage = _opaqueCoverage;
				}
				BuildLayerMeshCache(*meshCache, *meshTileSet, x1i, y1i);
				AddCachedLayerMesh(renderQueue, *meshCache, x1i, y1i, layerColor, layer.Description.Depth);
			}
			const std::uint32_t streamedCount = (std::uint32_t)_meshTileOrder.size();
#	else
			const std::uint32_t streamedCount = entryCount;
#	endif
			std::uint32_t runStart = 0;
			while (runStart < streamedCount) {
				const std::uint16_t chunk = _meshTileEntries[_meshTileOrder[runStart]].Chunk;
				std::uint32_t runEnd = runStart + 1;
				while (runEnd < streamedCount && _meshTileEntries[_meshTileOrder[runEnd]].Chunk == chunk) {
					runEnd++;
				}
				if (chunk < meshTileSet->GetTextureCount()) {
					EmitTileRuns(renderQueue, *meshTileSet->TextureDiffuse[chunk], meshTileSet->IsIndexed, layerColor,
						layer.Description.Depth, runStart, runEnd - runStart);
				}
				runStart = runEnd;
			}
		}
#endif

#if defined(TILEMAP_USE_SINGLE_DRAW) && !defined(TILEMAP_GROUP_MESH_BY_TILE)
		if DEATH_LIKELY(meshMode) {
#	if defined(TILEMAP_CACHE_LAYER_WINDOW)
			if (windowCache != nullptr) {
				if (meshCacheHit) {
					AppendAnimatedWindowTiles(layer, *windowCache, windowOriginX, windowOriginY, chunkVertices);
				} else {
					UploadLayerWindowCache(*windowCache);
					windowCache->Valid = true;
				}
				for (auto& cached : windowCache->Chunks) {
					if (cached.QuadCount == 0) {
						continue;
					}
					for (auto& piece : cached.Pieces) {
						if (piece.QuadCount == 0) {
							continue;
						}
						RenderCommand* command = piece.Command.get();
						command->GetInstanceBlock()->GetUniform(Material::ColorUniformName)->SetFloatVector(layerColor.Data());
						command->SetTransformation(Matrix4x4f::Translation(windowOriginX, windowOriginY, 0.0f));
						command->SetLayer(layer.Description.Depth);
						renderQueue.AddCommand(command);
					}
				}
			}
#	endif
			// Whole visible layer is submitted as one command per touched mesh slot (or a few <=64 KB
			// pieces for very large layers). Tiles within a layer never overlap, so the order between
			// slots doesn't matter - they all share the layer's depth.
			for (std::int32_t slot = 0; slot < (std::int32_t)chunkVertices.size(); slot++) {
				const std::int32_t verticesIndex = chunkVertices[slot];
				if (verticesIndex < 0 || _meshVertices[verticesIndex].empty()) {
					continue;
				}
				// Tiles use the default sprite palette (row 0, offset 0); every tile accumulated into these
				// vertices resolved to this chunk of the atlas of this tile set
				const MeshSlot& meshSlot = _meshSlots[slot];
#	if defined(TILEMAP_PACKED_TILE_RECORDS)
				EmitTileRecords(renderQueue, _meshVertices[verticesIndex], *meshSlot.GetTexture(),
					meshSlot.Set->IsIndexed, layerColor, layer.Description.Depth);
#	else
				EmitMesh(renderQueue, _meshVertices[verticesIndex], *meshSlot.GetTexture(),
					meshSlot.Set->IsIndexed, 0, layerColor, layer.Description.Depth, RenderCommand::Type::TileMap, false);
#	endif
			}
#	if defined(TILEMAP_HEAT_TILES)
			if (!_heatMeshGroups.empty()) {
				EmitHeatMeshes(renderQueue, layerColor, layer.Description.Depth);
			}
#	endif
		}
#endif
#if defined(TILEMAP_LAYER_EFFECTS)
		if (_effectLayer.Type != LayerRendererType::Default) {
			EmitEffectMeshes(renderQueue, layerColor, layer.Description.Depth);
		}
#endif
	}

	bool TileMap::DrawTexturedLayer(RenderQueue& renderQueue, TileMapLayer& layer, const Rectf& cullingRect, const Rectf& placementRect,
		Vector2f viewCenter, Vector2f layerOffsets)
	{
		const LayerRendererType type = layer.Description.RendererType;
		if (!LayerDescription::IsTexturedBackground(type) && type != LayerRendererType::Reflection) {
			return false;
		}

		const std::int32_t layerIndex = std::int32_t(&layer - _layers.data());
		const float loX = layerOffsets.X;
		const float loY = layerOffsets.Y;
		// The view the layer is placed by, minus the hardcoded offset, as in DrawLayer()
		const float px1 = placementRect.X - HardcodedOffset;
		const float py1 = placementRect.Y - HardcodedOffset;

		// A textured or a reflection layer is drawn over the whole view, or only where the layer is placed as an ordinary one,
		// as its tiles would be drawn (a dimension it repeats in is that of the view), as SURFACE::INNERWINDOW and INNERLAYER of
		// JJ2+. Its horizon, center or pivot is a fraction of the view, or of the layer with INNERLAYER. Over the whole view
		// the texture moves with the view by the speeds of the layer and by itself by its automatic speeds, inside the layer
		// the speeds place the layer, so the texture moves by the inner speeds and the inner automatic speeds the same way.
		const bool isInner = layer.Description.HasInnerSurface();
		Rectf area(0.0f, 0.0f, cullingRect.W, cullingRect.H);
		Vector2f innerShift;
		if (isInner) {
			Vector2f layerCoords = layer.Description.GetViewCoordinates(layer.LayoutSize, _layers[_sprLayerIndex].LayoutSize,
				placementRect, loX, loY);
			if (!layer.Description.RepeatX) {
				area.X = px1 - cullingRect.X - layerCoords.X;
				area.W = float(layer.LayoutSize.X * TileSet::DefaultTileSize);
			}
			if (!layer.Description.RepeatY) {
				area.Y = py1 - cullingRect.Y - layerCoords.Y;
				area.H = float(layer.LayoutSize.Y * TileSet::DefaultTileSize);
			}
			innerShift = Vector2f(px1 * layer.Description.TextureInnerSpeed.X + layer.TextureOffset.X,
				py1 * layer.Description.TextureInnerSpeed.Y + layer.TextureOffset.Y);
		}
		const Rectf reference = ((layer.Description.TextureFlags & Formats::TexturedLayerParams::InnerLayer) != 0 && isInner
			? area : Rectf(0.0f, 0.0f, cullingRect.W, cullingRect.H));

		if DEATH_UNLIKELY(type == LayerRendererType::TileMenu) {
			DrawMenuBackground(renderQueue, cullingRect, layer, area, reference);
			return true;
		}
		if (type == LayerRendererType::Reflection) {
			// Its tiles are never shown, they only ripple what it mirrors - the layers are reduced to hidden ones when the level
			// is loaded where it can't be (see LayerRendererType::Reflection)
#if defined(TILEMAP_LAYER_EFFECTS)
			// The auto speed Y once more, see OnUpdate()
			Vector2f shift = (isInner ? innerShift
				: Vector2f(px1 * layer.Description.SpeedX + loX, py1 * layer.Description.SpeedY + loY + layer.TextureOffset.Y));
			DrawReflection(renderQueue, cullingRect, area, layer, GetLayerPassTarget(layerIndex, true), shift);
#endif
			return true;
		}

		// Without the warped background pass, a sky layer is just an ordinary repeating tile layer and has to
		// be drawn with the plain tile renderer. Leaving it on the TexturedBackground shader made every tile
		// its own draw call - that effect is not batched, so a single 8x8 sky layer covering the screen cost
		// a few hundred of them - and fed flat tiles through a shader that samples a whole warped sheet.
		bool hasShader;
		switch (type) {
			case LayerRendererType::Sky: hasShader = SupportsTexturedBackground; break;
			case LayerRendererType::Tunnel: hasShader = SupportsTexturedBackgroundTunnel; break;
			// Its shader may be missing where the offline tables of the backend predate it (see ContentResolver::CompileShaders())
			case LayerRendererType::Cylinder: hasShader = (SupportsTexturedBackgroundCylinder &&
				ContentResolver::Get().GetShader(PrecompiledShader::TexturedBackgroundCylinder) != nullptr); break;
			// The fixed-function backends have none, the tiles are where its texture would be then, only without the waves
			case LayerRendererType::Wave: hasShader = (ContentResolver::Get().GetShader(PrecompiledShader::TexturedBackgroundWave) != nullptr); break;
			default: hasShader = false; break;
		}
		// Drawn as tiles until its pass has the texture - of a layer of 8x8 tiles, those are the texture, or of one with a
		// texture of its own, whatever its size
		Texture* passTarget = (hasShader ? GetLayerPassTarget(layerIndex, false) : nullptr);
		const bool hasTexture = ((layer.LayoutSize.X == TexturedBackgroundTiles && layer.LayoutSize.Y == TexturedBackgroundTiles) ||
			!layer.TexturePixels.empty());
		if (passTarget == nullptr || !hasTexture) {
			return false;
		}

		// How far the texture is moved, in its pixels
		Vector2f shift;
		if (type == LayerRendererType::Wave) {
			// The texture is where the tiles of the layer would be, it repeats from its tile (0, 0) as they do, measured in
			// the original - the tile walk of DrawLayer() draws the tile with the coordinate xt at x1, whole pixels unless the
			// viewport is unaligned
			const float x1 = cullingRect.X - HardcodedOffset;
			const float y1 = cullingRect.Y - HardcodedOffset;
			Vector2f layerCoords = layer.Description.GetViewCoordinates(layer.LayoutSize, _layers[_sprLayerIndex].LayoutSize,
				placementRect, loX, loY);
			float xt = layerCoords.X + (x1 - px1);
			float yt = layerCoords.Y + (y1 - py1);
			if (_viewOptions.FlatAmount > 0.0f) {
				xt += (x1 - xt) * _viewOptions.FlatAmount;
				yt += (y1 - yt) * _viewOptions.FlatAmount;
			}
			float originX = x1 - xt;
			float originY = y1 - yt;
			if (!PreferencesCache::UnalignedViewport) {
				originX = std::floor(originX);
				originY = std::floor(originY);
			}
			shift = Vector2f(cullingRect.X - originX, cullingRect.Y - originY);
		} else if (isInner) {
			shift = innerShift;
		} else if (layer.Description.HasPlusPerspective()) {
			// SURFACE::FULLSCREEN of JJ2+, the texture is where the layer would be as an ordinary one - by its speeds, auto
			// speeds and offsets, the vertical one less by the inherent offset where it applies (limitVisibleRegion of JJ2+),
			// but without the pixel an ordinary layer is drawn higher by - measured in JJ2+
			Vector2f layerCoords = layer.Description.GetViewCoordinates(layer.LayoutSize, _layers[_sprLayerIndex].LayoutSize,
				placementRect, loX, loY - 1.0f);
			const bool placedByLayer5X = layer.Description.IsTexturePlacedByLayer5(false);
			const bool placedByLayer5Y = layer.Description.IsTexturePlacedByLayer5(true);
			if (placedByLayer5X || placedByLayer5Y) {
				// LAYERSPEEDMODEL::LAYER8 (the default of the sky layer of the original game) places it on an axis where layer 5
				// of the original game is drawn instead, or at the view without one, moved by its own auto speed (see OnUpdate())
				// but not by its offset - measured in JJ2+
				Vector2f originCoords = layerCoords;
				for (const TileMapLayer& otherLayer : _layers) {
					if (otherLayer.OriginalNumber == 5) {
						Vector2f otherOffsets = otherLayer.Description.GetDrawOffsets(placementRect.H);
						originCoords = otherLayer.Description.GetViewCoordinates(otherLayer.LayoutSize, _layers[_sprLayerIndex].LayoutSize,
							placementRect, otherOffsets.X, otherOffsets.Y - 1.0f);
						break;
					}
				}
				if (placedByLayer5X) {
					layerCoords.X = originCoords.X + layer.TextureOffset.X;
				}
				if (placedByLayer5Y) {
					layerCoords.Y = originCoords.Y + layer.TextureOffset.Y;
				}
			}
			shift = Vector2f(layerCoords.X + HardcodedOffset, layerCoords.Y + HardcodedOffset);
		} else {
			// A sky or a tunnel as the sky layers always were here (the Resurrection model)
			constexpr float PerspectiveSpeedX = 0.4f;
			constexpr float PerspectiveSpeedY = 0.16f;
			shift = Vector2f(px1 * PerspectiveSpeedX + loX, py1 * PerspectiveSpeedY + loY);
		}
		RenderTexturedBackground(renderQueue, cullingRect, viewCenter, layer, *passTarget, area, reference, shift.X, shift.Y);
		return true;
	}

	bool TileMap::DrawLayerImages(RenderQueue& renderQueue, const TileMapLayer& layer, LayerRendererType rendererType, const Vector4f& layerColor,
		const Rectf& cullingRect, Vector2f layerOrigin)
	{
		// Images in place of the tiles, a layer pixel is a world pixel, so the tile (0, 0) is where its coordinate is 0
		_layerImages.clear();
		_layerTileAreas.clear();
		if (!_viewOptions.LayerOverride->OverrideLayer((std::int32_t)(&layer - _layers.data()), layerOrigin, cullingRect,
			_layerImages, _layerTileAreas)) {
			return false;
		}

		// The images are in true color, drawn by the renderer of the layer as its tiles in true color are, its color
		// a multiplier, the tint or the single color; a layer whose colors come from the palette isn't drawn as
		// images at all
		LayerRendererType imageRenderer = (rendererType == LayerRendererType::Tinted || rendererType == LayerRendererType::SingleColor
			? rendererType : LayerRendererType::Default);
		for (const TileMapLayerImage& image : _layerImages) {
			if (image.Image == nullptr) {
				continue;
			}
			TileCommandUniforms* commandUniforms;
			auto command = RentRenderCommand(imageRenderer, false, &commandUniforms);
			command->SetType(RenderCommand::Type::TileMap);
			command->GetMaterial().SetBlendingFactors(BlendingFactor::SrcAlpha, BlendingFactor::OneMinusSrcAlpha);
			if (commandUniforms->TexRect != nullptr) {
				commandUniforms->TexRect->SetFloatValue(image.TexScale.X, 0.0f, image.TexScale.Y, 0.0f);
			}
			if (commandUniforms->SpriteSize != nullptr) {
				commandUniforms->SpriteSize->SetFloatValue(image.Rect.W, image.Rect.H);
			}
			if (commandUniforms->Color != nullptr) {
				commandUniforms->Color->SetFloatVector(layerColor.Data());
			}
			command->SetTransformation(Matrix4x4f::Translation(image.Rect.X, image.Rect.Y, 0.0f));
			// Just behind what is at the depth of the layer, as the opaque tiles are, which are drawn before the
			// blended commands - the sprites at the depth of the sprite layer stay in front of it
			command->SetLayer(layer.Description.Depth > 0 ? std::uint16_t(layer.Description.Depth - 1) : 0);
			command->GetMaterial().SetTexture(0, *image.Image);
			renderQueue.AddCommand(command);
		}
		return true;
	}

	Vector2f TileMap::GetLayerOrigin(std::int32_t layerIndex, Vector2f viewCenter, Vector2f viewSize) const
	{
		if (layerIndex < 0 || layerIndex >= (std::int32_t)_layers.size() || _sprLayerIndex < 0) {
			return Vector2f::Zero;
		}

		// The same placement as in OnDraw() and DrawLayer()
		Rectf placementRect(viewCenter.X - viewSize.X * 0.5f, viewCenter.Y - viewSize.Y * 0.5f, viewSize.X, viewSize.Y);
		if (_viewOptions.Placement == LayerPlacement::ReferenceView && _viewOptions.ReferenceViewSize.X > 0 && _viewOptions.ReferenceViewSize.Y > 0) {
			placementRect = Rectf(viewCenter.X - _viewOptions.ReferenceViewSize.X * 0.5f, viewCenter.Y - _viewOptions.ReferenceViewSize.Y * 0.5f,
				(float)_viewOptions.ReferenceViewSize.X, (float)_viewOptions.ReferenceViewSize.Y);
		}
		return GetGameplayLayerOrigin(layerIndex, placementRect) * (1.0f - _viewOptions.FlatAmount);
	}

	Vector2f TileMap::GetGameplayLayerOrigin(std::int32_t layerIndex, const Rectf& view) const
	{
		if (layerIndex < 0 || layerIndex >= (std::int32_t)_layers.size() || _sprLayerIndex < 0) {
			return Vector2f::Zero;
		}

		const TileMapLayer& layer = _layers[layerIndex];
		return layer.Description.GetGameplayOrigin(layer.LayoutSize, _layers[_sprLayerIndex].LayoutSize, view);
	}

	RenderCommand* TileMap::RentRenderCommand(LayerRendererType type, bool indexed, TileCommandUniforms** uniforms, bool keyed)
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

		Shader* keyedShader = nullptr;
#if defined(TILEMAP_LAYER_EFFECTS)
		if (keyed) {
			switch (type) {
				case LayerRendererType::Sky: keyedShader = ContentResolver::Get().GetShader(PrecompiledShader::TexturedBackgroundKeyed); break;
				case LayerRendererType::Tunnel: keyedShader = ContentResolver::Get().GetShader(PrecompiledShader::TexturedBackgroundTunnelKeyed); break;
				case LayerRendererType::Cylinder: keyedShader = ContentResolver::Get().GetShader(PrecompiledShader::TexturedBackgroundCylinderKeyed); break;
				case LayerRendererType::Wave: keyedShader = ContentResolver::Get().GetShader(PrecompiledShader::TexturedBackgroundWaveKeyed); break;
				default: break;
			}
		}
#endif

		// Not with a key in the level, whose colors have to stay exact (see UpdateTexturedLayers())
		const bool dither = (PreferencesCache::BackgroundDithering && !_hasKeyedLayers);

		bool shaderChanged;
		switch (keyedShader != nullptr ? LayerRendererType(-1) : type) {
			case LayerRendererType(-1): shaderChanged = command->GetMaterial().SetShader(keyedShader); break;
			case LayerRendererType::Solid: shaderChanged = command->GetMaterial().SetShaderProgramType(Material::ShaderProgramType::SpriteNoTexture); break;
			case LayerRendererType::Tinted: shaderChanged = command->GetMaterial().SetShader(ContentResolver::Get().GetShader(indexed ? PrecompiledShader::TintedPalette : PrecompiledShader::Tinted)); break;
			case LayerRendererType::SingleColor: {
				// Tinted where the backend has no such shader (see ContentResolver::CompileShaders())
				Shader* shader = ContentResolver::Get().GetShader(indexed ? PrecompiledShader::SingleColorPalette : PrecompiledShader::SingleColor);
				if (shader == nullptr) {
					shader = ContentResolver::Get().GetShader(indexed ? PrecompiledShader::TintedPalette : PrecompiledShader::Tinted);
				}
				shaderChanged = command->GetMaterial().SetShader(shader);
				break;
			}
			case LayerRendererType::Sky: shaderChanged = command->GetMaterial().SetShader(ContentResolver::Get().GetShader(dither ? PrecompiledShader::TexturedBackgroundDither : PrecompiledShader::TexturedBackground)); break;
			case LayerRendererType::Tunnel: shaderChanged = command->GetMaterial().SetShader(ContentResolver::Get().GetShader(dither ? PrecompiledShader::TexturedBackgroundTunnelDither : PrecompiledShader::TexturedBackgroundTunnel)); break;
			case LayerRendererType::Cylinder: shaderChanged = command->GetMaterial().SetShader(ContentResolver::Get().GetShader(dither ? PrecompiledShader::TexturedBackgroundCylinderDither : PrecompiledShader::TexturedBackgroundCylinder)); break;
			// Whole pixels of the texture, nothing to dither
			case LayerRendererType::Wave: shaderChanged = command->GetMaterial().SetShader(ContentResolver::Get().GetShader(PrecompiledShader::TexturedBackgroundWave)); break;
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
			// A keyed textured background samples the copy of what is behind it on unit 2, which the caller binds - cleared for
			// any other, the copy may be released while the slot keeps it
			auto* behindUniform = command->GetMaterial().Uniform("uTextureBehind");
			if (behindUniform != nullptr) {
				behindUniform->SetIntValue(2); // GL_TEXTURE2
			}
			command->GetMaterial().SetTexture(2, nullptr);
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
		float s, c;
		sincosApprox(debris.Angle, s, c);
#if defined(DEATH_TARGET_DREAMCAST)
		const float ns = sinApprox(-debris.Angle);	// Never "-s", see the note in Matrix4x4::RotationZ()
#else
		const float ns = -s;
#endif
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
		std::uint16_t paletteOffset, const Vector4f& color, std::uint16_t depth, RenderCommand::Type type, bool additiveBlending,
		Vector2f translation)
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

			// Vertex positions are usually in world space already, so the model matrix is identity then
			command->SetTransformation(Matrix4x4f::Translation(translation.X, translation.Y, 0.0f));
			command->SetLayer(depth);
			// Binds diffuse on unit 0 and, when the mesh is recolored at draw time, the palette on unit 1
			ContentResolver::Get().BindSpritePalette(*command, texture, indexed, paletteOffset);

			renderQueue.AddCommand(command);
		}
	}
#endif

#if defined(TILEMAP_HEAT_TILES)
	bool TileMap::IsHeatTile(const LayerTile& tile) const
	{
		if ((tile.Flags & LayerTileFlags::Heat) == LayerTileFlags::Heat) {
			return true;
		}
		// An animated tile by the frame it shows now
		if (tile.TileID >= _animatedTilesOffset) {
			const std::int32_t animIndex = std::int32_t(tile.TileID) - std::int32_t(_animatedTilesOffset);
			if (animIndex < (std::int32_t)_animatedTiles.size()) {
				const AnimatedTile& animTile = _animatedTiles[animIndex];
				return ((animTile.Tiles[animTile.CurrentTileIdx].Flags & LayerTileFlags::Heat) == LayerTileFlags::Heat);
			}
		}
		return false;
	}

	bool TileMap::AppendHeatTile(std::int32_t tileId, LayerTileFlags flags, float alpha, float x, float y)
	{
		// How far a row of the tile can move, the largest value of the table of the shader (see TileMapMeshHeat.shader)
		constexpr float MaxShift = 2.0f;
		constexpr std::uint32_t HeatFloatsPerVertex = 12;

		Texture* texture;
		Vector4f texCoords;
		bool indexed;
		if (!GetStaticTileQuad(tileId, flags, texture, texCoords, indexed)) {
			return false;
		}

		std::int32_t verticesIndex = -1;
		for (const auto& group : _heatMeshGroups) {
			if (group.DiffuseTexture == texture && group.Indexed == indexed) {
				verticesIndex = group.VerticesIndex;
				break;
			}
		}
		if (verticesIndex < 0) {
			verticesIndex = RentMeshVertices();
			_heatMeshGroups.push_back({ texture, indexed, verticesIndex });
		}

		// The texture coordinates of a pixel of the tile, flipped as it's flipped, linear across the whole quad - which
		// reaches past the tile on either side, as far as a row can move
		const float size = (float)TileSet::DefaultTileSize;
		const float du = texCoords.X / size;
		const float u0 = texCoords.Y - MaxShift * du;
		const float u1 = texCoords.Y + texCoords.X + MaxShift * du;
		const float v0 = texCoords.W, v1 = texCoords.W + texCoords.Z;
		const float xl = x - MaxShift, xr = x + size + MaxShift, yb = y + size;

		SmallVector<float, 0>& vertices = _meshVertices[verticesIndex];
		const std::size_t base = vertices.size();
		vertices.resize_for_overwrite(base + VerticesPerQuad * HeatFloatsPerVertex);
		float* v = vertices.data() + base;
		// The 12-float vertex of TileMapMeshHeat: position.xy, texcoords, the position in the tile in its pixels and how far
		// the texture coordinates move for a pixel to the right, and the color, whose alpha is the tile's own (the layer tint
		// is applied through the instance color)
		auto put = [&](float px, float py, float pu, float pv, float tx, float ty) {
			*v++ = px; *v++ = py; *v++ = pu; *v++ = pv;
			*v++ = tx; *v++ = ty; *v++ = du; *v++ = 0.0f;
			*v++ = 1.0f; *v++ = 1.0f; *v++ = 1.0f; *v++ = alpha;
		};
		// The four corners in the order of the shared quad index pattern, as in AppendTileQuad()
		put(xl, y,  u0, v0, -MaxShift, 0.0f);
		put(xr, y,  u1, v0, size + MaxShift, 0.0f);
		put(xr, yb, u1, v1, size + MaxShift, size);
		put(xl, yb, u0, v1, -MaxShift, size);
		return true;
	}

	void TileMap::EmitHeatMeshes(RenderQueue& renderQueue, const Vector4f& color, std::uint16_t depth)
	{
		constexpr std::uint32_t HeatFloatsPerVertex = 12;
		constexpr std::uint32_t HeatFloatsPerQuad = VerticesPerQuad * HeatFloatsPerVertex;
		const std::uint32_t maxQuadsPerChunk = RenderResources::GetMaxQuadsPerDraw(HeatFloatsPerVertex);
		const std::uint16_t* quadIndices = RenderResources::GetQuadIndices();
		// A row of the table every 4 ticks
		const float phase = std::floor(_heatTicks * 0.25f);

		for (const auto& group : _heatMeshGroups) {
			SmallVector<float, 0>& vertices = _meshVertices[group.VerticesIndex];
			Shader* shader = ContentResolver::Get().GetShader(group.Indexed ? PrecompiledShader::TileMapMeshHeatPalette : PrecompiledShader::TileMapMeshHeat);
			if (vertices.empty() || shader == nullptr) {
				continue;
			}

			const std::uint32_t totalQuads = (std::uint32_t)(vertices.size() / HeatFloatsPerQuad);
			for (std::uint32_t firstQuad = 0; firstQuad < totalQuads; firstQuad += maxQuadsPerChunk) {
				const std::uint32_t count = std::min(maxQuadsPerChunk, totalQuads - firstQuad);

				if (_heatMeshCommandCount >= (std::int32_t)_heatMeshCommands.size()) {
					auto& newCommand = _heatMeshCommands.emplace_back(std::make_unique<RenderCommand>());
					newCommand->GetMaterial().SetBlendingEnabled(true);
				}
				RenderCommand* command = _heatMeshCommands[_heatMeshCommandCount++].get();

				command->SetType(RenderCommand::Type::TileMap);
				command->GetMaterial().SetBlendingFactors(BlendingFactor::SrcAlpha, BlendingFactor::OneMinusSrcAlpha);
				if (command->GetMaterial().SetShader(shader)) {
					command->GetMaterial().ReserveUniformsDataMemory();
					auto* textureUniform = command->GetMaterial().Uniform(Material::TextureUniformName);
					if (textureUniform != nullptr) {
						textureUniform->SetIntValue(0); // GL_TEXTURE0
					}
					auto* paletteUniform = command->GetMaterial().Uniform("uTexturePalette");
					if (paletteUniform != nullptr) {
						paletteUniform->SetIntValue(1); // GL_TEXTURE1
					}
				}
				if (auto* phaseUniform = command->GetMaterial().Uniform("uHeatPhase")) {
					phaseUniform->SetFloatValue(phase);
				}
				if (auto* instanceBlock = command->GetInstanceBlock()) {
					if (auto* colorUniform = instanceBlock->GetUniform(Material::ColorUniformName)) {
						colorUniform->SetFloatVector(color.Data());
					}
				}

				auto& geometry = command->GetGeometry();
				geometry.SetElementsPerVertex(HeatFloatsPerVertex);
				geometry.SetHostVertexPointer(vertices.data() + firstQuad * HeatFloatsPerQuad);
				geometry.SetIndexCount(count * RenderResources::IndicesPerQuad);
				geometry.SetHostIndexPointer(quadIndices);
				geometry.SetDrawParameters(PrimitiveType::Triangles, 0, count * VerticesPerQuad);

				command->SetTransformation(Matrix4x4f::Translation(0.0f, 0.0f, 0.0f));
				command->SetLayer(depth);
				// Diffuse on unit 0 and, when indexed, the palette on unit 1
				ContentResolver::Get().BindSpritePalette(*command, *group.DiffuseTexture, group.Indexed, 0);

				renderQueue.AddCommand(command);
			}
		}
		_heatMeshGroups.clear();
	}
#endif

#if defined(TILEMAP_PACKED_TILE_RECORDS)
	void TileMap::EmitTileRecords(RenderQueue& renderQueue, SmallVector<float, 0>& records, const Texture& texture, bool indexed,
		const Vector4f& color, std::uint16_t depth)
	{
		// The limit of the float quads is kept, a record is only an eighth of one, so it is a safe bound
		const std::uint32_t maxRecordsPerChunk = RenderResources::GetMaxQuadsPerDraw(FloatsPerVertex);
		const std::uint32_t totalRecords = std::uint32_t(records.size() / TileRecordFloats);
		for (std::uint32_t firstRecord = 0; firstRecord < totalRecords; firstRecord += maxRecordsPerChunk) {
			const std::uint32_t count = std::min(maxRecordsPerChunk, totalRecords - firstRecord);

			if (_meshCommandCount >= (std::int32_t)_meshCommands.size()) {
				auto& newCommand = _meshCommands.emplace_back(std::make_unique<RenderCommand>());
				newCommand->GetMaterial().SetBlendingEnabled(true);
			}
			RenderCommand* command = _meshCommands[_meshCommandCount++].get();

			command->SetType(RenderCommand::Type::TileMap);
			command->GetMaterial().SetBlendingFactors(BlendingFactor::SrcAlpha, BlendingFactor::OneMinusSrcAlpha);

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

			auto instanceBlock = command->GetInstanceBlock();
			instanceBlock->GetUniform(Material::ColorUniformName)->SetFloatVector(color.Data());

			auto& geometry = command->GetGeometry();
			geometry.SetElementsPerVertex(TileRecordFloats);
			geometry.SetHostVertexPointer(records.data() + std::size_t(firstRecord) * TileRecordFloats);
			// The command pool is shared with EmitMesh(), which leaves its indices set up
			geometry.SetIndexCount(0);
			geometry.SetHostIndexPointer(nullptr);
			geometry.SetDrawParameters(PrimitiveType::Points, 0, count);

			command->SetTransformation(Matrix4x4f::Translation(0.0f, 0.0f, 0.0f));
			command->SetLayer(depth);
			ContentResolver::Get().BindSpritePalette(*command, texture, indexed, 0);

			renderQueue.AddCommand(command);
		}
	}
#endif

#if defined(TILEMAP_CACHE_LAYER_WINDOW)
	TileMap::LayerWindowCache& TileMap::GetLayerWindowCache(std::int32_t layerIndex)
	{
		const Viewport* viewport = RenderResources::GetCurrentViewport();
		LayerWindowCache* unused = nullptr;
		for (auto& cache : _layerWindowCaches) {
			if (cache->Owner == viewport && cache->LayerIndex == layerIndex) {
				cache->LastUsedFrame = _layerWindowFrame;
				return *cache;
			}
			// Not drawn for a while - its viewport is gone or the layer was hidden - so free for another one
			if (unused == nullptr && _layerWindowFrame - cache->LastUsedFrame > 2) {
				unused = cache.get();
			}
		}
		if (unused == nullptr) {
			unused = _layerWindowCaches.emplace_back(std::make_unique<LayerWindowCache>()).get();
		}
		unused->Owner = viewport;
		unused->LayerIndex = layerIndex;
		unused->LastUsedFrame = _layerWindowFrame;
		unused->Valid = false;
		return *unused;
	}

	bool TileMap::LayerWindowUnchanged(const TileMapLayer& layer, const LayerWindowCache& cache, std::int32_t tileAbsY,
		std::int32_t tileXs, std::int32_t tileYs) const
	{
		// The same cells in the same order as the walk in DrawLayer() records them
		if ((std::int32_t)cache.Window.size() != cache.Cols * cache.Rows) {
			return false;
		}
		const Vector2i tileCount = layer.LayoutSize;
		const std::uint32_t* cells = cache.Window.data();
		std::int32_t tileY = tileYs;
		for (std::int32_t yo = 0; yo < cache.Rows; yo++) {
			if (++tileY >= tileCount.Y) { tileY = 0; }
			if (!layer.Description.RepeatY && (tileAbsY + yo + 1 < 0 || tileAbsY + yo + 1 >= tileCount.Y)) {
				// A skipped row depends only on the first tile, which is part of the key already
				cells += cache.Cols;
				continue;
			}
			const std::size_t rowBase = std::size_t(tileY) * tileCount.X;
			std::int32_t tileX = tileXs;
			for (std::int32_t xo = 0; xo < cache.Cols; xo++) {
				if (++tileX >= tileCount.X) { tileX = 0; }
				if (*cells++ != PackWindowTile(layer.Layout[rowBase + tileX])) {
					return false;
				}
			}
		}
		return true;
	}

	void TileMap::UploadLayerWindowCache(LayerWindowCache& cache)
	{
		// The indices come from the pattern every quad mesh shares, so a piece can't be longer than one draw of it
		const std::uint32_t maxQuads = RenderResources::GetMaxQuadsPerDraw(FloatsPerVertex);
		// Without buffer mapping the whole VBO is uploaded from the host copy (see LayerWindowCache::Chunk); with
		// it, only the quads the draw uses are copied, and the rest of the copy would be held for nothing
		const bool uploadsWholeBuffer = (RenderResources::GetBuffersManager().Specs(RenderBuffersManager::BufferTypes::Array).mapFlags == MapFlags::None);
#if !defined(WITH_RHI_GX) && !defined(WITH_RHI_PICA) && !defined(WITH_RHI_GS) && !defined(WITH_RHI_GU)
		const std::uint32_t windowQuads = std::uint32_t(std::max<std::int32_t>(cache.Cols, 1) * std::max<std::int32_t>(cache.Rows, 1));
#endif
		const std::uint16_t* quadIndices = RenderResources::GetQuadIndices();
		for (std::int32_t c = 0; c < (std::int32_t)cache.Chunks.size(); c++) {
			auto& chunk = cache.Chunks[c];
			chunk.QuadCount = std::uint32_t(chunk.Vertices.size() / FloatsPerQuad);
			if (chunk.QuadCount == 0 || c >= (std::int32_t)_meshSlots.size() || maxQuads == 0) {
				chunk.QuadCount = 0;
				for (auto& piece : chunk.Pieces) {
					piece.QuadCount = 0;
				}
				continue;
			}
			const MeshSlot& meshSlot = _meshSlots[c];
			const bool indexed = meshSlot.Set->IsIndexed;

			// As many pieces of one draw as the quads need, a window seen from far away has many more of them than
			// one draw takes; those left from a larger window keep their buffers for the next one
			const std::uint32_t pieceCount = (chunk.QuadCount + maxQuads - 1) / maxQuads;
			if (chunk.Pieces.size() < pieceCount) {
				chunk.Pieces.resize(pieceCount);
			}
			std::size_t hostQuads = chunk.QuadCount;
			for (std::uint32_t p = 0; p < (std::uint32_t)chunk.Pieces.size(); p++) {
				auto& piece = chunk.Pieces[p];
				const std::uint32_t firstQuad = p * maxQuads;
				piece.QuadCount = (p < pieceCount ? std::min<std::uint32_t>(maxQuads, chunk.QuadCount - firstQuad) : 0);
				if (piece.QuadCount == 0) {
					continue;
				}
				if (piece.Command == nullptr) {
					piece.Command = std::make_unique<RenderCommand>(RenderCommand::Type::TileMap);
					piece.Command->GetMaterial().SetBlendingEnabled(true);
				}
				if (piece.QuadCount > piece.CapacityQuads) {
#if defined(WITH_RHI_GX) || defined(WITH_RHI_PICA) || defined(WITH_RHI_GS) || defined(WITH_RHI_GU)
					// A buffer of the GX, PICA200, GS and GE backends is a plain allocation with no driver behind it to wait
					// on, so a quarter of room to grow is enough. Sized for the whole window, a castle level's eight chunks -
					// most of which hold only a few tiles - would keep about 0.45 MB of the heap, for nothing.
					piece.CapacityQuads = std::min<std::uint32_t>(piece.QuadCount + piece.QuadCount / 4, maxQuads);
#else
					// Sized for the rest of the window at once (a cell holds one quad at most), because creating a buffer
					// makes the driver wait for its own worker thread - a whole frame with NVIDIA's threaded
					// optimization. Growing by a quarter instead recreated the buffers again and again while a
					// layer scrolled into denser parts of the level, at about 7 ms each.
					const std::uint32_t restOfWindow = (windowQuads > firstQuad ? windowQuads - firstQuad : 0);
					piece.CapacityQuads = std::min<std::uint32_t>(std::max<std::uint32_t>(restOfWindow, piece.QuadCount), maxQuads);
#endif
					auto& geometry = piece.Command->GetGeometry();
					geometry.CreateCustomVbo(piece.CapacityQuads * FloatsPerQuad, BufferUsage::DynamicDraw);
					geometry.CreateCustomIbo(piece.CapacityQuads * RenderResources::IndicesPerQuad, BufferUsage::StaticDraw);
				}
				hostQuads = std::max<std::size_t>(hostQuads, std::size_t(firstQuad) + piece.CapacityQuads);
			}
			if (uploadsWholeBuffer) {
				chunk.Vertices.resize(hostQuads * FloatsPerQuad);
			}

			chunk.DiffuseTexture = meshSlot.GetTexture();
			for (std::uint32_t p = 0; p < pieceCount; p++) {
				auto& piece = chunk.Pieces[p];
				RenderCommand* command = piece.Command.get();
				auto& geometry = command->GetGeometry();
				geometry.SetElementsPerVertex(FloatsPerVertex);
				// Both mark the data dirty, so it is uploaded by the next commit and never again until the next rebuild.
				// The indices too, as only as many of them as the draw uses are copied (see Geometry::CommitIndices()).
				geometry.SetHostVertexPointer(chunk.Vertices.data() + std::size_t(p) * maxQuads * FloatsPerQuad);
				geometry.SetHostIndexPointer(quadIndices);
				geometry.SetIndexCount(piece.QuadCount * RenderResources::IndicesPerQuad);
				geometry.SetDrawParameters(PrimitiveType::Triangles, 0, std::int32_t(piece.QuadCount * VerticesPerQuad));

				command->SetType(RenderCommand::Type::TileMap);
				command->GetMaterial().SetBlendingFactors(BlendingFactor::SrcAlpha, BlendingFactor::OneMinusSrcAlpha);
				bool shaderChanged = command->GetMaterial().SetShader(ContentResolver::Get().GetShader(
					indexed ? PrecompiledShader::TileMapMeshPalette : PrecompiledShader::TileMapMesh));
				if (shaderChanged) {
					command->GetMaterial().ReserveUniformsDataMemory();
					auto* textureUniform = command->GetMaterial().Uniform(Material::TextureUniformName);
					if (textureUniform != nullptr && textureUniform->GetIntValue(0) != 0) {
						textureUniform->SetIntValue(0); // GL_TEXTURE0
					}
					auto* paletteUniform = command->GetMaterial().Uniform("uTexturePalette");
					if (paletteUniform != nullptr) {
						paletteUniform->SetIntValue(1); // GL_TEXTURE1
					}
				}
				ContentResolver::Get().BindSpritePalette(*command, *chunk.DiffuseTexture, indexed, 0);
			}
		}
	}

	void TileMap::AppendAnimatedWindowTiles(const TileMapLayer& layer, const LayerWindowCache& cache,
		float originX, float originY, SmallVectorImpl<std::int32_t>& chunkVertices)
	{
		// What the walk in DrawLayer() does for a tile, for just the animated cells of the window (and those of the heat effect)
#if defined(TILEMAP_HEAT_TILES)
		const bool heatTiles = (ContentResolver::Get().GetShader(PrecompiledShader::TileMapMeshHeat) != nullptr);
#endif
		for (const auto& cell : cache.Animated) {
			const LayerTile tile = layer.Layout[cell.LayoutIndex];
#if defined(TILEMAP_LAYER_EFFECTS)
			if (_effectLayer.Type == LayerRendererType::XRay && tile.Alpha != 0 &&
				AppendXRayTile(tile, originX + float(cell.Xo * (std::int32_t)TileSet::DefaultTileSize),
					originY + float(cell.Yo * (std::int32_t)TileSet::DefaultTileSize))) {
				continue;
			}
#endif
			std::int32_t tileId = ResolveTileID(tile);
			if (tileId == 0 || tile.Alpha == 0) {
				continue;
			}
#if defined(TILEMAP_HEAT_TILES)
			if (heatTiles && IsHeatTile(tile) && AppendHeatTile(tileId, tile.Flags, tile.Alpha / 255.0f,
				originX + float(cell.Xo * (std::int32_t)TileSet::DefaultTileSize), originY + float(cell.Yo * (std::int32_t)TileSet::DefaultTileSize))) {
				continue;
			}
#endif
			std::int32_t meshSlotBase = 0;
			TileSet* tileSetPtr = ResolveTileSet(tileId, meshSlotBase);
			if (tileSetPtr == nullptr) {
				continue;
			}
			const TileSet& tileSet = *tileSetPtr;
			// Read before ResolveTextureDiffuse(), which rebases the ID into its chunk
			const std::int32_t tileSlot = tileSet.MapToAtlasSlot(tileId);
			const std::int32_t tileChunk = (tileSet.TilesPerTexture > 0 && tileSlot >= tileSet.TilesPerTexture
				? tileSlot / tileSet.TilesPerTexture : 0);
			const std::int32_t meshSlot = meshSlotBase + tileChunk;
			Texture* tileTexture = tileSet.ResolveTextureDiffuse(tileId);
			if (tileTexture == nullptr || meshSlot >= (std::int32_t)chunkVertices.size()) {
				continue;
			}

			const Vector2i texSize = tileTexture->GetSize();
			const float texInvW = (texSize.X > 0 ? 1.0f / float(texSize.X) : 0.0f);
			const float texInvH = (texSize.Y > 0 ? 1.0f / float(texSize.Y) : 0.0f);
			const std::int32_t tileRow = tileId / tileSet.TilesPerRow;
			const std::int32_t tileCol = tileId - tileRow * tileSet.TilesPerRow;
			float texScaleX = TileSet::DefaultTileSize * texInvW;
			float texBiasX = (tileCol * float(TileSet::PaddedTileSize) + TileSet::TilePadding) * texInvW;
			float texScaleY = TileSet::DefaultTileSize * texInvH;
			float texBiasY = (tileRow * float(TileSet::PaddedTileSize) + TileSet::TilePadding) * texInvH;
			if ((tile.Flags & LayerTileFlags::FlipX) == LayerTileFlags::FlipX) {
				texBiasX += texScaleX;
				texScaleX *= -1;
			}
			if ((tile.Flags & LayerTileFlags::FlipY) == LayerTileFlags::FlipY) {
				texBiasY += texScaleY;
				texScaleY *= -1;
			}

			std::int32_t& verticesIndex = chunkVertices[meshSlot];
			if (verticesIndex < 0) {
				verticesIndex = RentMeshVertices();
			}
			AppendTileQuad(_meshVertices[verticesIndex], originX + float(cell.Xo * (std::int32_t)TileSet::DefaultTileSize),
				originY + float(cell.Yo * (std::int32_t)TileSet::DefaultTileSize), (float)TileSet::DefaultTileSize,
				texScaleX, texBiasX, texScaleY, texBiasY, tile.Alpha / 255.0f);
		}
	}
#endif

#if defined(TILEMAP_GROUP_MESH_BY_TILE)
	void TileMap::EmitTileRuns(RenderQueue& renderQueue, const Texture& texture, bool indexed, const Vector4f& color,
		std::uint16_t depth, std::uint32_t firstEntry, std::uint32_t entryCount)
	{
		/*
			The grouped tile entries of one atlas chunk, written STRAIGHT into the streaming vertex buffer
			the draw reads from. The generic EmitMesh() path appends every quad to a host array first and
			has RenderCommand::CommitAll() copy the array into the buffer at commit time; on this machine a
			copy costs about 5.5 cycles per byte in cache-line misses alone (16-byte lines, ~44 cycles each,
			read and write-allocate), and a frame's ~370 tiles at 128 bytes per quad made that copy the
			single largest part of the commit phase. Here the quad is written once, where it will be read.

			Each tile is one packed record, drawn as a point (see RHI::RDP::TileRecord) - the form the layer
			cache uses too (see BuildLayerMeshCache). The consumer of this mesh is the RDP dispatch, which
			takes the tile's texels as the integers they are instead of scaling normalized coordinates back
			up (see DispatchTileMesh), from 16 bytes instead of the 64 of two float corners. It must not be
			the indexed form: that one carries four real corners, which the particles in the same kind of mesh
			need (their spin is folded into the corners, see AppendDebrisQuad).
		*/
		// The limit of the float quads is kept, a record is only an eighth of one, so it is a safe bound
		const std::uint32_t maxQuadsPerChunk = RenderResources::GetMaxQuadsPerDraw(FloatsPerVertex);

		for (std::uint32_t firstQuad = 0; firstQuad < entryCount; firstQuad += maxQuadsPerChunk) {
			const std::uint32_t count = std::min(maxQuadsPerChunk, entryCount - firstQuad);

			if (_meshCommandCount >= (std::int32_t)_meshCommands.size()) {
				auto& newCommand = _meshCommands.emplace_back(std::make_unique<RenderCommand>());
				newCommand->GetMaterial().SetBlendingEnabled(true);
			}
			RenderCommand* command = _meshCommands[_meshCommandCount++].get();

			command->SetType(RenderCommand::Type::TileMap);
			command->GetMaterial().SetBlendingFactors(BlendingFactor::SrcAlpha, BlendingFactor::OneMinusSrcAlpha);

			bool shaderChanged = command->GetMaterial().SetShader(ContentResolver::Get().GetShader(
				indexed ? PrecompiledShader::TileMapMeshPalette : PrecompiledShader::TileMapMesh));
			if (shaderChanged) {
				command->GetMaterial().ReserveUniformsDataMemory();

				auto* textureUniform = command->GetMaterial().Uniform(Material::TextureUniformName);
				if (textureUniform != nullptr && textureUniform->GetIntValue(0) != 0) {
					textureUniform->SetIntValue(0);
				}
				auto* paletteUniform = command->GetMaterial().Uniform("uTexturePalette");
				if (paletteUniform != nullptr) {
					paletteUniform->SetIntValue(1);
				}
			}

			auto instanceBlock = command->GetInstanceBlock();
			instanceBlock->GetUniform(Material::ColorUniformName)->SetFloatVector(color.Data());

			auto& geometry = command->GetGeometry();
			geometry.SetElementsPerVertex(TileRecordFloats);
			geometry.SetHostVertexPointer(nullptr);
			float* v = geometry.AcquireVertexPointer(count * TileRecordFloats, TileRecordFloats);
			if (v == nullptr) {
				geometry.ReleaseVertexPointer();
				_meshCommandCount--;
				LOGW("Streaming vertex buffer exhausted, {} tiles of a layer are not drawn", entryCount - firstQuad);
				return;
			}
			for (std::uint32_t k = 0; k < count; k++) {
				const MeshTileEntry& e = _meshTileEntries[_meshTileOrder[firstEntry + firstQuad + k]];
				WriteTileRecord(v + std::size_t(k) * TileRecordFloats, e, e.X, e.Y);
			}
			geometry.ReleaseVertexPointer();
			// The command pool is shared with EmitMesh(), which leaves its indices set up
			geometry.SetIndexCount(0);
			geometry.SetHostIndexPointer(nullptr);
			geometry.SetDrawParameters(PrimitiveType::Points, 0, count);

			command->SetTransformation(Matrix4x4f::Translation(0.0f, 0.0f, 0.0f));
			command->SetLayer(depth);
			ContentResolver::Get().BindSpritePalette(*command, texture, indexed, 0);

			renderQueue.AddCommand(command);
		}
	}
#endif

#if defined(DEATH_TARGET_N64)
	bool TileMap::LayerMeshWindowUnchanged(const TileMapLayer& layer, const LayerMeshCache& cache, std::int32_t tileAbsX, std::int32_t tileAbsY,
		std::int32_t tileXs, std::int32_t tileYs) const
	{
		// The same cells in the same order as the walk in DrawLayer() records them
		static_cast<void>(tileAbsX);
		if ((std::int32_t)cache.WindowIds.size() != cache.Cols * cache.Rows) {
			return false;
		}
		const Vector2i tileCount = layer.LayoutSize;
		const std::uint16_t* ids = cache.WindowIds.data();
		std::int32_t tileY = tileYs;
		for (std::int32_t yo = 0; yo < cache.Rows; yo++) {
			if (++tileY >= tileCount.Y) { tileY = 0; }
			if (!layer.Description.RepeatY && (tileAbsY + yo + 1 < 0 || tileAbsY + yo + 1 >= tileCount.Y)) {
				for (std::int32_t xo = 0; xo < cache.Cols; xo++) {
					if (*ids++ != 0) { return false; }
				}
				continue;
			}
			const std::size_t rowBase = std::size_t(tileY) * tileCount.X;
			std::int32_t tileX = tileXs;
			for (std::int32_t xo = 0; xo < cache.Cols; xo++) {
				if (++tileX >= tileCount.X) { tileX = 0; }
				if (*ids++ != layer.Layout.TileIDAt(rowBase + tileX)) { return false; }
			}
		}
		return true;
	}

	void TileMap::BuildLayerMeshCache(LayerMeshCache& cache, TileSet& tileSet, std::int32_t x1i, std::int32_t y1i)
	{
		const std::int32_t chunkCount = std::max<std::int32_t>(tileSet.GetTextureCount(), 1);
		if ((std::int32_t)cache.Chunks.size() != chunkCount) {
			cache.Chunks.clear();
			cache.Chunks.resize(chunkCount);
		}
		for (auto& chunk : cache.Chunks) {
			chunk.QuadCount = 0;
		}

		// The static tiles are counted per chunk first; the order is only compacted (animated entries to
		// the front, for the streaming emission) AFTER the static quads are written below - compacting in
		// place before that overwrote the static indices at the front of the order, and the quads they
		// stood for were never written, which showed as tiles missing or showing an older window's tile
		// wherever the window held an animated tile
		const std::uint32_t orderedCount = (std::uint32_t)_meshTileOrder.size();
		for (std::uint32_t i = 0; i < orderedCount; i++) {
			const MeshTileEntry& e = _meshTileEntries[_meshTileOrder[i]];
			if (!e.Animated && e.Chunk < chunkCount) {
				cache.Chunks[e.Chunk].QuadCount++;
			}
		}

		for (std::int32_t c = 0; c < chunkCount; c++) {
			auto& chunk = cache.Chunks[c];
			if (chunk.QuadCount == 0) {
				continue;
			}
			if (chunk.Command == nullptr) {
				chunk.Command = std::make_unique<RenderCommand>(RenderCommand::Type::TileMap);
				chunk.Command->GetMaterial().SetBlendingEnabled(true);
			}
			const std::uint32_t floats = std::uint32_t(chunk.QuadCount) * TileRecordFloats;
			if (floats > chunk.VboFloats) {
				// With a little room to grow, so a window with a few more tiles does not reallocate
				chunk.VboFloats = floats + (floats / 4);
				chunk.Command->GetGeometry().CreateCustomVbo(chunk.VboFloats, BufferUsage::DynamicDraw);
			}
		}

		// The tiles, as the packed records the RDP dispatch reads (see RHI::RDP::TileRecord), relative to the
		// window's origin so that the same VBO serves every frame of the window. Written straight into the
		// mapped VBO (the backend maps its buffers, and never re-copies a custom one)
		SmallVector<std::uint32_t, 4> fill;
		SmallVector<float*, 4> mapped;
		fill.resize(std::size_t(chunkCount), 0u);
		mapped.resize(std::size_t(chunkCount), nullptr);
		for (std::int32_t c = 0; c < chunkCount; c++) {
			if (cache.Chunks[c].QuadCount > 0) {
				mapped[c] = cache.Chunks[c].Command->GetGeometry().AcquireVertexPointer();
			}
		}
		for (std::uint32_t i = 0; i < orderedCount; i++) {
			const MeshTileEntry& e = _meshTileEntries[_meshTileOrder[i]];
			if (e.Animated || e.Chunk >= chunkCount || mapped[e.Chunk] == nullptr) {
				continue;
			}
			WriteTileRecord(mapped[e.Chunk] + fill[e.Chunk], e, e.X - float(x1i), e.Y - float(y1i));
			fill[e.Chunk] += TileRecordFloats;
		}
		for (std::int32_t c = 0; c < chunkCount; c++) {
			if (mapped[c] != nullptr) {
				cache.Chunks[c].Command->GetGeometry().ReleaseVertexPointer();
			}
		}
		std::uint32_t streamed = 0;
		for (std::uint32_t i = 0; i < orderedCount; i++) {
			if (_meshTileEntries[_meshTileOrder[i]].Animated) {
				_meshTileOrder[streamed++] = _meshTileOrder[i];
			}
		}
		_meshTileOrder.resize(streamed);

		const bool indexed = tileSet.IsIndexed;
		for (std::int32_t c = 0; c < chunkCount; c++) {
			auto& chunk = cache.Chunks[c];
			if (chunk.QuadCount == 0 || c >= (std::int32_t)tileSet.GetTextureCount()) {
				chunk.QuadCount = 0;
				continue;
			}
			RenderCommand* command = chunk.Command.get();
			command->SetType(RenderCommand::Type::TileMap);
			command->GetMaterial().SetBlendingFactors(BlendingFactor::SrcAlpha, BlendingFactor::OneMinusSrcAlpha);
			bool shaderChanged = command->GetMaterial().SetShader(ContentResolver::Get().GetShader(
				indexed ? PrecompiledShader::TileMapMeshPalette : PrecompiledShader::TileMapMesh));
			if (shaderChanged) {
				command->GetMaterial().ReserveUniformsDataMemory();
				auto* textureUniform = command->GetMaterial().Uniform(Material::TextureUniformName);
				if (textureUniform != nullptr && textureUniform->GetIntValue(0) != 0) {
					textureUniform->SetIntValue(0);
				}
				auto* paletteUniform = command->GetMaterial().Uniform("uTexturePalette");
				if (paletteUniform != nullptr) {
					paletteUniform->SetIntValue(1);
				}
			}
			auto& geometry = command->GetGeometry();
			geometry.SetElementsPerVertex(TileRecordFloats);
			geometry.SetHostVertexPointer(nullptr);
			geometry.SetIndexCount(0);
			geometry.SetHostIndexPointer(nullptr);
			geometry.SetDrawParameters(PrimitiveType::Points, 0, chunk.QuadCount);
			ContentResolver::Get().BindSpritePalette(*command, *tileSet.TextureDiffuse[c], indexed, 0);
		}
	}

	void TileMap::AddCachedLayerMesh(RenderQueue& renderQueue, LayerMeshCache& cache, std::int32_t x1i, std::int32_t y1i, const Vector4f& color, std::uint16_t depth)
	{
		for (auto& chunk : cache.Chunks) {
			if (chunk.QuadCount == 0 || chunk.Command == nullptr) {
				continue;
			}
			RenderCommand* command = chunk.Command.get();
			command->GetInstanceBlock()->GetUniform(Material::ColorUniformName)->SetFloatVector(color.Data());
			command->SetTransformation(Matrix4x4f::Translation(float(x1i), float(y1i), 0.0f));
			command->SetLayer(depth);
			renderQueue.AddCommand(command);
		}
	}

	void TileMap::PushAnimatedMeshEntries(const TileMapLayer& layer, const LayerMeshCache& cache, TileSet& tileSet, std::int32_t x1i, std::int32_t y1i)
	{
		for (const auto& cell : cache.Animated) {
			const LayerTile tile = layer.Layout[cell.LayoutIndex];
			std::int32_t tileId = ResolveTileID(tile);
			if (tileId == 0 || tile.Alpha == 0) {
				continue;
			}
			// The same rebasing the walk does (ResolveTileSet may shift the id by the tile set's offset)
			TileSet* resolvedSet = ResolveTileSet(tileId);
			if (resolvedSet != &tileSet) {
				continue;
			}
			const std::int32_t tileSlot = tileSet.MapToAtlasSlot(tileId);
			Texture* tileTexture = tileSet.ResolveTextureDiffuse(tileId);
			if (tileTexture == nullptr) {
				continue;
			}
			const std::int32_t tileChunk = (tileSet.TilesPerTexture > 0 && tileSlot >= tileSet.TilesPerTexture
				? tileSlot / tileSet.TilesPerTexture : 0);
			const std::int32_t tileRow = tileId / tileSet.TilesPerRow;
			const std::int32_t tileCol = tileId - tileRow * tileSet.TilesPerRow;
			_meshTileEntries.push_back({ float(x1i + cell.Xo * (std::int32_t)TileSet::DefaultTileSize), float(y1i + cell.Yo * (std::int32_t)TileSet::DefaultTileSize),
				(std::uint16_t)(tileCol * TileSet::PaddedTileSize + TileSet::TilePadding),
				(std::uint16_t)(tileRow * TileSet::PaddedTileSize + TileSet::TilePadding),
				tile.Alpha, MeshTileFlags(tile.Flags), (std::uint16_t)tileSlot, (std::uint16_t)tileChunk,
				cell.Xo, cell.Yo, cell.LayoutIndex, true });
		}
	}
#endif

	LayerTile* LayerLayout::_sharedFull = nullptr;
	std::uint32_t* LayerLayout::_sharedPacked = nullptr;

	namespace
	{
		// The tile a level stores as tile 0 without any flags, the same as TileMap::MakeLayerTile(0, 0)
		LayerTile MakeEmptyLayerTile()
		{
			LayerTile tile = {};
			tile.DestructAnimation = -1;
			tile.Alpha = 255;
			return tile;
		}

		// Encode() of the empty tile
		constexpr std::uint32_t EmptyPackedTile = std::uint32_t(255) << 24;

		LayerTile* NewFullBlock(std::size_t count)
		{
			LayerTile* block = new (std::nothrow) LayerTile[count];
			if (block != nullptr) {
				std::fill(block, block + count, MakeEmptyLayerTile());
			}
			return block;
		}

		std::uint32_t* NewPackedBlock(std::size_t count)
		{
			std::uint32_t* block = new (std::nothrow) std::uint32_t[count];
			if (block != nullptr) {
				std::fill(block, block + count, EmptyPackedTile);
			}
			return block;
		}
	}

	LayerLayout::~LayerLayout()
	{
		Free();
	}

	LayerLayout::LayerLayout(LayerLayout&& other) noexcept
		: _compact(other._compact), _version(other._version), _full(Death::move(other._full)), _packed(Death::move(other._packed)),
			_count(other._count)
	{
		other._full.clear();
		other._packed.clear();
		other._count = 0;
	}

	LayerLayout& LayerLayout::operator=(LayerLayout&& other) noexcept
	{
		if (this != &other) {
			Free();
			_compact = other._compact;
			_version = other._version + 1;
			_full = Death::move(other._full);
			_packed = Death::move(other._packed);
			_count = other._count;
			other._full.clear();
			other._packed.clear();
			other._count = 0;
		}
		return *this;
	}

	void LayerLayout::Free()
	{
		for (LayerTile* block : _full) {
			if (block != _sharedFull) {
				delete[] block;
			}
		}
		for (std::uint32_t* block : _packed) {
			if (block != _sharedPacked) {
				delete[] block;
			}
		}
		_full.clear();
		_packed.clear();
		_count = 0;
	}

	bool LayerLayout::Allocate(std::size_t count, bool compact, bool sparse)
	{
		_version++;
		Free();
		_compact = compact;

		// The shared block is a whole one, so it can stand in for any of them
		if (sparse) {
			if (compact ? (_sharedPacked == nullptr && (_sharedPacked = NewPackedBlock(BlockTiles)) == nullptr)
						: (_sharedFull == nullptr && (_sharedFull = NewFullBlock(BlockTiles)) == nullptr)) {
				return false;
			}
		}
		for (std::size_t first = 0; first < count; first += BlockTiles) {
			const std::size_t n = std::min(BlockTiles, count - first);
			if (compact) {
				std::uint32_t* block = (sparse ? _sharedPacked : NewPackedBlock(n));
				if (block == nullptr) {
					Free();
					return false;
				}
				_packed.push_back(block);
			} else {
				LayerTile* block = (sparse ? _sharedFull : NewFullBlock(n));
				if (block == nullptr) {
					Free();
					return false;
				}
				_full.push_back(block);
			}
		}
		_count = count;
		return true;
	}

	bool LayerLayout::HasAnyTile() const
	{
		for (std::size_t first = 0, i = 0; first < _count; first += BlockTiles, i++) {
			// A block never written has only empty tiles
			if (_compact ? (_packed[i] == _sharedPacked) : (_full[i] == _sharedFull)) {
				continue;
			}
			const std::size_t n = std::min(BlockTiles, _count - first);
			for (std::size_t j = 0; j < n; j++) {
				if ((_compact ? std::uint16_t(_packed[i][j]) : _full[i][j].TileID) != 0) {
					return true;
				}
			}
		}
		return false;
	}

	LayerTile* LayerLayout::OwnFullBlock(std::size_t blockIndex, const LayerTile& tile)
	{
		static const LayerTile EmptyTile = MakeEmptyLayerTile();
		if (std::memcmp(&tile, &EmptyTile, sizeof(LayerTile)) == 0) {
			return nullptr;
		}
		LayerTile* block = NewFullBlock(std::min(BlockTiles, _count - blockIndex * BlockTiles));
		if (block == nullptr) {
			LOGE("Cannot allocate a block of a layer, a tile is left out");
			return nullptr;
		}
		_full[blockIndex] = block;
		return block;
	}

	std::uint32_t* LayerLayout::OwnPackedBlock(std::size_t blockIndex, std::uint32_t packed)
	{
		if (packed == EmptyPackedTile) {
			return nullptr;
		}
		std::uint32_t* block = NewPackedBlock(std::min(BlockTiles, _count - blockIndex * BlockTiles));
		if (block == nullptr) {
			LOGE("Cannot allocate a block of a layer, a tile is left out");
			return nullptr;
		}
		_packed[blockIndex] = block;
		return block;
	}

	void TileMap::AddTileSet(StringView tileSetPath, std::uint16_t offset, std::uint16_t count, const std::uint8_t* paletteRemapping)
	{
		// A level with extra tilesets is never repacked (see PruneTilesetAtlas())
		LoadDeferredDiffuse();

		auto& tileSetPart = _tileSets.emplace_back();
		tileSetPart.Data = ContentResolver::Get().RequestTileSet(tileSetPath, 0, false, paletteRemapping);
		tileSetPart.Offset = offset;
		tileSetPart.Count = count;

		if (tileSetPart.Data == nullptr) {
			LOGE("Cannot load extra tileset \"{}\"", tileSetPath);
		}
	}

	bool TileMap::AppendTileSet(StringView tileSetPath, std::uint16_t offset, std::uint16_t count, const std::uint8_t* paletteRemapping,
		const std::uint32_t* trueColorPalette)
	{
		if (GetLoadedTileCount() + std::int32_t(count) > std::int32_t(_animatedTilesOffset)) {
			return false;
		}

		std::unique_ptr<TileSet> data = ContentResolver::Get().RequestTileSet(tileSetPath, 0, false, paletteRemapping, nullptr, true, trueColorPalette);
		if (data == nullptr) {
			return false;
		}

		// A level with extra tilesets is never repacked (see PruneTilesetAtlas())
		LoadDeferredDiffuse();

		auto& tileSetPart = _tileSets.emplace_back();
		tileSetPart.Data = Death::move(data);
		tileSetPart.Offset = offset;
		tileSetPart.Count = count;
		return true;
	}

	void TileMap::AddTileSet(Stream& s, StringView name, std::uint16_t offset, std::uint16_t count, const std::uint8_t* paletteRemapping)
	{
		LoadDeferredDiffuse();

		auto& tileSetPart = _tileSets.emplace_back();
		tileSetPart.Data = ContentResolver::Get().RequestTileSet(s, name, 0, false, paletteRemapping);
		tileSetPart.Offset = offset;
		tileSetPart.Count = count;
		tileSetPart.Embedded = true;

		if (tileSetPart.Data == nullptr) {
			LOGE("Cannot load tileset \"{}\" of the level", name);
		}
	}

	void TileMap::LoadLayer(Formats::LevelReader& reader)
	{
		Formats::LayerRecord record;
		reader.ReadLayerHeader(record);

		bool isSprite = (record.Kind == Formats::LayerKind::Sprite);
		if (isSprite) {
			_sprLayerIndex = (std::int32_t)_layers.size();
		}

		TileMapLayer& newLayer = _layers.emplace_back();
		newLayer.LayoutSize = Vector2i(record.Width, record.Height);
		newLayer.Visible = ((record.Flags & Formats::LayerRecord::Visible) != 0);
		newLayer.Description = MakeLayerDescription(record);

		// The collision code writes into the tiles of the sprite layer as they are, every other layer is written through
		// LayerLayout::Set(), so it can be sparse (see LayerLayout)
		if (!newLayer.Layout.Allocate(std::size_t(record.Width) * std::size_t(record.Height), LayerLayout::CompactDrawOnlyLayers && !isSprite, !isSprite)) {
			LOGE("Cannot allocate the layout of a {}x{} layer", record.Width, record.Height);
			_loadFailed = true;
			newLayer.Visible = false;
			// The tile data still has to be consumed for the layers after it
			reader.ReadLayerTiles(record, nullptr);
			return;
		}

		// The reader hands the tiles over a block at a time, out of its own buffer - two stream reads per tile,
		// virtual calls into the decompressing stream, were the largest single cost of loading a level on the
		// Nintendo 64, about a second for castle1's layers
		LayerLayout& layout = newLayer.Layout;
		reader.ReadLayerTiles(record, [&layout](std::int32_t first, ArrayView<const Formats::TileRecord> tiles) {
			for (std::size_t j = 0; j < tiles.size(); j++) {
				// Any 16-bit ID is accepted, everything that looks a tile up checks its bounds (see LayerTile)
				layout.Set(std::size_t(first) + j, MakeLayerTile(tiles[j].Flags, tiles[j].TileId));
			}
		});

#if defined(TILEMAP_LAYER_EFFECTS)
		if (newLayer.Description.RendererType == LayerRendererType::XRay) {
			PrepareXRayLayer(newLayer);
		}
		// The textured and the reflection layers get their passes with the viewports (see OnInitializeViewport())
#else
		// Without the effects, the special layers are reduced to what a level looks like without them (see
		// LayerRendererType): an x-ray layer shows its animated tiles as they are outside the mask, a blur, a
		// reflection or a chroma key layer nothing
		if (newLayer.Description.RendererType == LayerRendererType::XRay) {
			ReduceXRayLayer(newLayer);
			newLayer.Description.RendererType = LayerRendererType::Default;
		} else if (newLayer.Description.RendererType == LayerRendererType::BlurBehind ||
			newLayer.Description.RendererType == LayerRendererType::Reflection ||
			newLayer.Description.RendererType == LayerRendererType::ChromaKey) {
			newLayer.Visible = false;
			newLayer.Description.RendererType = LayerRendererType::Default;
		}
#endif
	}

	LayerDescription TileMap::MakeLayerDescription(const Formats::LayerRecord& record)
	{
		return LayerDescription::FromRecord(record, ILevelHandler::MainPlaneZ);
	}

	bool TileMap::ReplaceLayerTile(std::int32_t layerIndex, std::int32_t x, std::int32_t y, const LayerTile& tile)
	{
		if (layerIndex < 0 || layerIndex >= (std::int32_t)_layers.size()) {
			return false;
		}

		TileMapLayer& layer = _layers[layerIndex];
		if (x < 0 || y < 0 || x >= layer.LayoutSize.X || y >= layer.LayoutSize.Y) {
			return false;
		}

		layer.Layout.Set(std::size_t(y) * std::size_t(layer.LayoutSize.X) + std::size_t(x), tile);
		return true;
	}

	const LayerDescription* TileMap::GetLayerDescription(std::int32_t layerIndex) const
	{
		if (layerIndex < 0 || layerIndex >= (std::int32_t)_layers.size()) {
			return nullptr;
		}
		return &_layers[layerIndex].Description;
	}

	bool TileMap::SetLayerDescription(std::int32_t layerIndex, const LayerDescription& description, bool visible)
	{
		if (layerIndex < 0 || layerIndex >= (std::int32_t)_layers.size()) {
			return false;
		}

		TileMapLayer& layer = _layers[layerIndex];
		layer.Visible = visible;
		if (layerIndex == _sprLayerIndex) {
			// The sprite layer is always placed with its defaults, and drawn as SetSpriteLayerRenderer() set it, which a
			// script switching the sprite mode of the layer doesn't change
			return true;
		}

#if defined(TILEMAP_LAYER_EFFECTS)
		// A textured layer drawn translucent or keyed needs the copy of what is behind it, the owner sets it up (see
		// HasChromaKeyLayers())
		auto needsBehindCopy = [](const LayerDescription& d) {
			return (LayerDescription::IsTexturedBackground(d.RendererType) &&
				(d.TextureBlend == TexturedLayerBlend::Translucent || d.TextureBlend == TexturedLayerBlend::ChromaKey));
		};
		const bool neededBehindCopy = needsBehindCopy(layer.Description);
#endif
		LayerRendererType rendererType = layer.Description.RendererType;
		layer.Description = description;
		layer.Description.RendererType = rendererType;
		SetLayerRendererType(layerIndex, description.RendererType);
		// Also when the renderer type stays, then the call above returns early
		UpdateTexturedLayers();
#if defined(TILEMAP_LAYER_EFFECTS)
		if (neededBehindCopy != needsBehindCopy(layer.Description) && _owner != nullptr) {
			_owner->OnLayerEffectsChanged();
		}
#endif
		return true;
	}

	bool TileMap::SetLayerTexture(std::int32_t layerIndex, ArrayView<const std::uint8_t> pixels)
	{
		constexpr std::size_t PixelCount = std::size_t(Formats::TexturedLayerChunk::TextureSize) * std::size_t(Formats::TexturedLayerChunk::TextureSize);
		if (layerIndex < 0 || layerIndex >= (std::int32_t)_layers.size() || (!pixels.empty() && pixels.size() != PixelCount)) {
			return false;
		}

		TileMapLayer& layer = _layers[layerIndex];
		layer.TexturePixels.assign(pixels.begin(), pixels.end());
		layer.TextureRevision = ++_lastTextureRevision;
		// Drawn once into the texture of the pass, which has to be drawn again
		InvalidateTexturedBackground();
		return true;
	}

	Vector2i TileMap::GetLayerTextureSize(const TileMapLayer& layer)
	{
		if (!layer.TexturePixels.empty()) {
			return Vector2i(Formats::TexturedLayerChunk::TextureSize, Formats::TexturedLayerChunk::TextureSize);
		}
		// No more than the textured backgrounds take, the top left of a larger layer
		return Vector2i(std::clamp<std::int32_t>(layer.LayoutSize.X, 1, TexturedBackgroundTiles) * TileSet::DefaultTileSize,
			std::clamp<std::int32_t>(layer.LayoutSize.Y, 1, TexturedBackgroundTiles) * TileSet::DefaultTileSize);
	}

	bool TileMap::SetSpriteLayerRenderer(LayerRendererType type, const Vector4f& color)
	{
		if (_sprLayerIndex < 0 || _sprLayerIndex >= (std::int32_t)_layers.size()) {
			return false;
		}

		// None of these needs a pass of its own, so nothing else changes with them
		LayerDescription& description = _layers[_sprLayerIndex].Description;
		if (LayerDescription::IsSpriteLayerRenderer(type)) {
			description.RendererType = type;
			description.Color = color;
		} else {
			description.RendererType = LayerRendererType::Default;
			description.Color = Vector4f(1.0f, 1.0f, 1.0f, 1.0f);
		}
		return true;
	}

	bool TileMap::ReorderLayers(ArrayView<const std::int32_t> order)
	{
		const std::int32_t count = (std::int32_t)_layers.size();
		if ((std::int32_t)order.size() != count) {
			return false;
		}
		SmallVector<std::int32_t, 16> newIndices;
		newIndices.resize(std::size_t(count), -1);
		for (std::int32_t i = 0; i < count; i++) {
			const std::int32_t from = order[i];
			if (from < 0 || from >= count || newIndices[from] >= 0) {
				return false;
			}
			newIndices[from] = i;
		}

		SmallVector<TileMapLayer, 0> layers;
		layers.reserve(std::size_t(count));
		for (std::int32_t i = 0; i < count; i++) {
			layers.push_back(Death::move(_layers[order[i]]));
		}
		_layers = Death::move(layers);

		if (_sprLayerIndex >= 0) {
			_sprLayerIndex = newIndices[_sprLayerIndex];
		}
		for (LayerPass& entry : _layerPasses) {
			entry.LayerIndex = newIndices[entry.LayerIndex];
			entry.Pass->_layerIndex = entry.LayerIndex;
		}
		InvalidateLayerCaches();
		InvalidateTexturedBackground();
		return true;
	}

	std::int32_t TileMap::AddLayer(Vector2i size, const LayerDescription& description)
	{
		TileMapLayer layer;
		if (!InitializeLayer(layer, size, description)) {
			return -1;
		}
		_layers.push_back(Death::move(layer));
		return std::int32_t(_layers.size()) - 1;
	}

	bool TileMap::ResetLayer(std::int32_t layerIndex, Vector2i size, const LayerDescription& description)
	{
		// Only a layer drawn as tiles, the others have state of their own
		if (layerIndex < 0 || layerIndex >= (std::int32_t)_layers.size() || layerIndex == _sprLayerIndex ||
			!LayerDescription::IsSpriteLayerRenderer(_layers[layerIndex].Description.RendererType)) {
			return false;
		}

		TileMapLayer layer;
		if (!InitializeLayer(layer, size, description)) {
			return false;
		}
		_layers[layerIndex] = Death::move(layer);
		InvalidateLayerCaches();
		return true;
	}

	bool TileMap::InitializeLayer(TileMapLayer& layer, Vector2i size, const LayerDescription& description)
	{
		// A layer of no size would leave nothing to repeat
		constexpr std::int32_t MaxSize = 32767;
		if (size.X < 1 || size.Y < 1 || size.X > MaxSize || size.Y > MaxSize) {
			return false;
		}

		layer.LayoutSize = size;
		layer.Visible = false;
		layer.Description = description;
		if (!LayerDescription::IsSpriteLayerRenderer(description.RendererType)) {
			layer.Description.RendererType = LayerRendererType::Default;
			layer.Description.Color = Vector4f(1.0f, 1.0f, 1.0f, 1.0f);
		}
		// Written only through SetTile(), so it can be sparse (see LayerLayout)
		return layer.Layout.Allocate(std::size_t(size.X) * std::size_t(size.Y), LayerLayout::CompactDrawOnlyLayers, true);
	}

	void TileMap::InvalidateLayerCaches()
	{
#if defined(TILEMAP_CACHE_LAYER_WINDOW)
		for (auto& cache : _layerWindowCaches) {
			cache->Valid = false;
		}
#endif
#if defined(DEATH_TARGET_N64)
		for (auto& cache : _layerMeshCaches) {
			cache.Valid = false;
		}
#endif
	}

	bool TileMap::IsLayerVisible(std::int32_t layerIndex) const
	{
		return (layerIndex >= 0 && layerIndex < (std::int32_t)_layers.size() && _layers[layerIndex].Visible);
	}

	StringView TileMap::GetLayerName(std::int32_t layerIndex) const
	{
		return (layerIndex >= 0 && layerIndex < (std::int32_t)_layers.size() ? StringView(_layers[layerIndex].Name) : StringView());
	}

	std::uint8_t TileMap::GetLayerOriginalNumber(std::int32_t layerIndex) const
	{
		return (layerIndex >= 0 && layerIndex < (std::int32_t)_layers.size() ? _layers[layerIndex].OriginalNumber : 0);
	}

	void TileMap::SetLayerIdentity(std::int32_t layerIndex, StringView name, std::uint8_t originalNumber)
	{
		if (layerIndex >= 0 && layerIndex < (std::int32_t)_layers.size()) {
			_layers[layerIndex].Name = name;
			_layers[layerIndex].OriginalNumber = originalNumber;
		}
	}

	bool TileMap::GetTileQuad(std::int32_t tileId, LayerTileFlags flags, Texture*& texture, Vector4f& texCoords, bool& indexed)
	{
		if (tileId >= std::int32_t(_animatedTilesOffset)) {
			// The frame the animated tile shows now
			std::int32_t animIndex = tileId - std::int32_t(_animatedTilesOffset);
			if (animIndex >= std::int32_t(_animatedTiles.size()) || _animatedTiles[animIndex].Tiles.empty()) {
				return false;
			}
			const AnimatedTile& animTile = _animatedTiles[animIndex];
			tileId = animTile.Tiles[std::clamp<std::int32_t>(animTile.CurrentTileIdx, 0, std::int32_t(animTile.Tiles.size()) - 1)].TileID;
			if (tileId >= std::int32_t(_animatedTilesOffset)) {
				return false;
			}
		}
		return GetStaticTileQuad(tileId, flags, texture, texCoords, indexed);
	}

	std::int32_t TileMap::ResolveStaticTileId(std::int32_t tileId) const
	{
		if (tileId < std::int32_t(_animatedTilesOffset)) {
			return tileId;
		}
		std::int32_t animIndex = tileId - std::int32_t(_animatedTilesOffset);
		if (animIndex >= std::int32_t(_animatedTiles.size()) || _animatedTiles[animIndex].Tiles.empty()) {
			return -1;
		}
		const AnimatedTile& animTile = _animatedTiles[animIndex];
		std::int32_t frameTileId = animTile.Tiles[std::clamp<std::int32_t>(animTile.CurrentTileIdx, 0, std::int32_t(animTile.Tiles.size()) - 1)].TileID;
		return (frameTileId < std::int32_t(_animatedTilesOffset) ? frameTileId : -1);
	}

	const std::uint8_t* TileMap::GetStaticTileMask(std::int32_t tileId)
	{
		if (tileId < 0 || tileId >= std::int32_t(_animatedTilesOffset)) {
			return nullptr;
		}
		TileSet* tileSet = ResolveTileSet(tileId);
		return (tileSet != nullptr ? tileSet->GetTileMask(tileId) : nullptr);
	}

	bool TileMap::GetStaticTileQuad(std::int32_t tileId, LayerTileFlags flags, Texture*& texture, Vector4f& texCoords, bool& indexed)
	{
		if (tileId < 0) {
			return false;
		}
		TileSet* tileSet = ResolveTileSet(tileId);
		if (tileSet == nullptr) {
			return false;
		}
		indexed = tileSet->IsIndexed;
		// Rebases the ID into its texture chunk
		texture = tileSet->ResolveTextureDiffuse(tileId);
		if (texture == nullptr) {
			return false;
		}

		const Vector2i texSize = texture->GetSize();
		const float texInvW = (texSize.X > 0 ? 1.0f / float(texSize.X) : 0.0f);
		const float texInvH = (texSize.Y > 0 ? 1.0f / float(texSize.Y) : 0.0f);
		const std::int32_t tileRow = tileId / tileSet->TilesPerRow;
		const std::int32_t tileCol = tileId - tileRow * tileSet->TilesPerRow;
		float texScaleX = TileSet::DefaultTileSize * texInvW;
		float texBiasX = (tileCol * float(TileSet::PaddedTileSize) + TileSet::TilePadding) * texInvW;
		float texScaleY = TileSet::DefaultTileSize * texInvH;
		float texBiasY = (tileRow * float(TileSet::PaddedTileSize) + TileSet::TilePadding) * texInvH;
		if ((flags & LayerTileFlags::FlipX) == LayerTileFlags::FlipX) {
			texBiasX += texScaleX;
			texScaleX *= -1;
		}
		if ((flags & LayerTileFlags::FlipY) == LayerTileFlags::FlipY) {
			texBiasY += texScaleY;
			texScaleY *= -1;
		}
		texCoords = Vector4f(texScaleX, texBiasX, texScaleY, texBiasY);
		return true;
	}

	std::int32_t TileMap::GetTileSetCount() const
	{
		return std::int32_t(_tileSets.size());
	}

	std::int32_t TileMap::GetTileSetTileCount(std::int32_t index) const
	{
		return (index >= 0 && index < std::int32_t(_tileSets.size()) ? _tileSets[index].Count : 0);
	}

	std::int32_t TileMap::GetLoadedTileCount() const
	{
		std::int32_t count = 0;
		for (const auto& tileSetPart : _tileSets) {
			count += tileSetPart.Count;
		}
		return count;
	}
	Vector4f TileMap::ResolveTintColor(const Vector4f& description)
	{
		// Red is the palette index, alpha the strength of the tint
		const std::uint32_t* palettes = ContentResolver::Get().GetPalettes();
		std::uint32_t color = palettes[std::uint8_t(description.X * 255.0f + 0.5f)];
		return Vector4f((color & 0x000000ff) / 255.0f, ((color >> 8) & 0x000000ff) / 255.0f, ((color >> 16) & 0x000000ff) / 255.0f,
			description.W * (((color >> 24) & 0x000000ff) / 255.0f));
	}

	std::int32_t TileMap::AcquireShiftedPalette(std::uint8_t shift)
	{
		if (shift == 0) {
			return 0;
		}

		const std::uint32_t frame = theApplication().GetFrameCount();
		for (ShiftedPaletteRow& row : _shiftedPaletteRows) {
			if (row.Shift == shift) {
				row.LastUsedFrame = frame;
				return row.PaletteOffset;
			}
		}

		// A shift that changes every frame (a script can animate it) would take a row for each value otherwise
		ReleaseShiftedPalettes(false);
		std::int32_t paletteOffset = ContentResolver::Get().AcquireShiftedPaletteOffset(shift);
		if (paletteOffset < 0) {
			// All rows are taken, the layer is drawn unshifted
			return 0;
		}
		_shiftedPaletteRows.push_back({ paletteOffset, frame, shift });
		return paletteOffset;
	}

	void TileMap::ReleaseShiftedPalettes(bool all)
	{
		const std::uint32_t frame = theApplication().GetFrameCount();
		for (std::size_t i = 0; i < _shiftedPaletteRows.size(); ) {
			const ShiftedPaletteRow& row = _shiftedPaletteRows[i];
			if (all || row.LastUsedFrame + 1 < frame) {
				ContentResolver::Get().ReleasePaletteOffset(row.PaletteOffset);
				_shiftedPaletteRows.eraseUnordered(i);
			} else {
				i++;
			}
		}
	}

	LayerTile TileMap::MakeLayerTile(std::uint8_t fileFlags, std::uint16_t tileId)
	{
		LayerTile tile = {};
		tile.TileID = tileId;
		tile.DestructAnimation = -1;
		// Only the flips, the other bits of the low nibble mean nothing in the file
		tile.Flags = (LayerTileFlags)(fileFlags & Formats::TileRecord::KnownFlipFlags);

		switch (fileFlags & Formats::TileRecord::ModifierMask) {
			case Formats::TileRecord::ModifierTranslucent: tile.Alpha = TranslucentTileAlpha; break;
			case Formats::TileRecord::ModifierInvisible: tile.Alpha = 0; break;
			case Formats::TileRecord::ModifierHeat: tile.Alpha = 255; tile.Flags |= LayerTileFlags::Heat; break;
			default: tile.Alpha = 255; break;
		}
		return tile;
	}

	void TileMap::SetAnimatedTiles(std::uint16_t animatedTilesOffset, ArrayView<const Formats::AnimatedTileRecord> animatedTiles)
	{
		_animatedTilesOffset = animatedTilesOffset;
		_animatedTiles.clear();
		_animatedTiles.reserve(animatedTiles.size());

		for (const Formats::AnimatedTileRecord& record : animatedTiles) {
			FillAnimatedTile(_animatedTiles.emplace_back(), record);
		}
	}

	void TileMap::SetAnimatedTile(std::int32_t index, const Formats::AnimatedTileRecord& animatedTile)
	{
		if (index < 0 || index >= std::int32_t(_animatedTiles.size())) {
			return;
		}
		AnimatedTile& animTile = _animatedTiles[index];
		std::int32_t currentFrame = animTile.CurrentTileIdx;
		animTile.Tiles.clear();
		FillAnimatedTile(animTile, animatedTile);
		animTile.CurrentTileIdx = std::clamp<std::int32_t>(currentFrame, 0, std::int32_t(animTile.Tiles.size()) - 1);
		animTile.FramesLeft = std::min(animTile.FramesLeft, animTile.FrameDuration);
	}

	void TileMap::FillAnimatedTile(AnimatedTile& animTile, const Formats::AnimatedTileRecord& record)
	{
		// FrameDuration is multiplied by 16 before saving, so divide it here back
		animTile.FrameDuration = record.FrameDuration16 / 16.0f;
		// Kept non-negative, a negative delay would make a loop of the animation take no time at all, and the update
		// would never get out of it
		animTile.Delay = std::int16_t(std::min<std::uint16_t>(record.Delay, INT16_MAX));
		animTile.DelayJitter = std::int16_t(std::min<std::uint16_t>(record.DelayJitter, INT16_MAX));
		animTile.IsPingPong = (record.IsPingPong != 0);
		animTile.PingPongDelay = record.PingPongDelay;

		if (record.Frames.empty()) {
			// It's still added, so the animated tiles after it keep their IDs
			animTile.Tiles.emplace_back().TileID = 0;
			return;
		}

		animTile.Tiles.reserve(record.Frames.size());
		for (const Formats::TileRecord& frame : record.Frames) {
			// TODO: Frame flags (flips and the other modifiers) are not applied yet
			AnimatedTileFrame& tileFrame = animTile.Tiles.emplace_back();
			tileFrame.TileID = frame.TileId;
			// A frame of a tile of the heat effect is drawn so, wherever the animation is (the tile type of JJ2+ is a property
			// of the tile, which the converter carries over to each frame)
			if ((frame.Flags & Formats::TileRecord::ModifierMask) == Formats::TileRecord::ModifierHeat) {
				tileFrame.Flags = LayerTileFlags::Heat;
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
				// The delay takes 16 bits, a wait of the original longer than 10 would wrap around in one byte
				// TODO: Framerate (tileParams[2]) not used
				SetTileDestructibleEventParams(tile, TileDestructType::Collapse, std::uint16_t(tileParams[0] | (tileParams[1] << 8)));
				break;
		}
	}

	/** @brief Overrides the diffuse texture of the specified tile */
	bool TileMap::OverrideTileDiffuse(std::int32_t tileId, StaticArrayView<TileSet::PaddedTileSize * TileSet::PaddedTileSize, std::uint32_t> tileDiffuse)
	{
		// An overridden tile is patched into the whole atlas, which is then never repacked
		LoadDeferredDiffuse();

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
		// Only known once the atlas is built; asked for the tiles about to be overridden
		LoadDeferredDiffuse();

		TileSet* tileSet = ResolveTileSet(tileId);
		return (tileSet != nullptr && tileSet->IsIndexed);
	}

	/** @brief Overrides the collision mask of the specified tile */
	bool TileMap::OverrideTileMask(std::int32_t tileId, StaticArrayView<TileSet::DefaultTileSize * TileSet::DefaultTileSize, const std::uint8_t> tileMask)
	{
		// Loading the atlas later would replace the tileset and with it the patched mask
		LoadDeferredDiffuse();

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
		// The ID is kept in the signed 16-bit destruct field, and an animated one must name an animation the level has;
		// a level from anywhere can place any ID, such a tile simply isn't destructible (see LayerTile)
		if (tile.TileID > INT16_MAX || (tile.TileID >= _animatedTilesOffset &&
			std::int32_t(tile.TileID) - std::int32_t(_animatedTilesOffset) >= std::int32_t(_animatedTiles.size()))) {
			return;
		}

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
			const std::int32_t animIndex = tile.DestructAnimation - std::int32_t(_animatedTilesOffset);
			if (animIndex >= std::int32_t(_animatedTiles.size())) {
				return 1;
			}
			return (std::int32_t)_animatedTiles[animIndex].Tiles.size() - 2;
		}
		return 1;
	}

	Array<StringView> TileMap::GetUsedTileSetPaths() const
	{
		Array<StringView> result;
		arrayReserve(result, _tileSets.size());

		for (const auto& tileSetPart : _tileSets) {
			// A tile set stored in the level has only a name, a file of the same name would be the wrong one
			if (tileSetPart.Data != nullptr && !tileSetPart.Embedded && !tileSetPart.Data->FilePath.empty()) {
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
		float texBiasX = ((tileId % tileSet->TilesPerRow) * float(TileSet::PaddedTileSize) + TileSet::TilePadding) / float(texSize.X);
		float texScaleY = float(QuarterSize) / float(texSize.Y);
		float texBiasY = ((tileId / tileSet->TilesPerRow) * float(TileSet::PaddedTileSize) + TileSet::TilePadding) / float(texSize.Y);

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

	void TileMap::CreateSpriteDebris(const GraphicResource* res, Vector3f pos, std::int32_t count, float elasticity)
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
			debris.Elasticity = elasticity;
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
			float s, c;
			sincosApprox(debris.Angle, s, c);
#if defined(DEATH_TARGET_DREAMCAST)
			const float ns = sinApprox(-debris.Angle);	// Never "-s", see the note in Matrix4x4::RotationZ()
#else
			const float ns = -s;
#endif
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
		// An event parameter or a script can name any trigger up to 255, only the first ones exist
		if (triggerId >= TriggerCount) {
			return false;
		}
		return _triggerState[triggerId];
	}

	void TileMap::SetTrigger(std::uint8_t triggerId, bool newState)
	{
		if (triggerId >= TriggerCount || _triggerState[triggerId] == newState) {
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
		LayerTile tile = layer.Layout[layoutIndex];

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
		layer.Layout.Set(layoutIndex, tile);
		return true;
	}

	LayerRendererType TileMap::GetLayerRendererType(std::int32_t layerIndex) const
	{
		if (layerIndex < 0 || layerIndex >= (std::int32_t)_layers.size()) {
			return LayerRendererType::Default;
		}
		return _layers[layerIndex].Description.RendererType;
	}

	bool TileMap::SetLayerRendererType(std::int32_t layerIndex, LayerRendererType type)
	{
		if (layerIndex < 0 || layerIndex >= (std::int32_t)_layers.size() || layerIndex == _sprLayerIndex) {
			return false;
		}

		TileMapLayer& layer = _layers[layerIndex];
		if (layer.Description.RendererType == type) {
			return true;
		}

#if defined(DEATH_DEBUG)
		{
			// Only the animated tiles change in an x-ray layer, so tell whether there are any
			std::int32_t tileCount = 0, xrayTiles = 0;
			for (std::size_t i = 0; i < layer.Layout.size(); i++) {
				const std::int32_t tileId = layer.Layout.TileIDAt(i);
				if (tileId != 0) {
					tileCount++;
				}
				const std::int32_t animIndex = tileId - std::int32_t(_animatedTilesOffset);
				if (animIndex >= 0 && animIndex < (std::int32_t)_animatedTiles.size()) {
					xrayTiles++;
				}
			}
			LOGI("Layer {} ({}x{}, depth {}) changed to renderer type {} - {} tile(s), {} animated tile(s)",
				layerIndex, layer.LayoutSize.X, layer.LayoutSize.Y, layer.Description.Depth, (std::int32_t)type, tileCount, xrayTiles);
		}
#endif

#if defined(TILEMAP_LAYER_EFFECTS)
		auto isEffect = [](LayerRendererType type) {
			return (type == LayerRendererType::XRay || type == LayerRendererType::BlurBehind || type == LayerRendererType::Reflection ||
				type == LayerRendererType::ChromaKey);
		};
		const bool hadEffect = isEffect(layer.Description.RendererType);
		const bool hasEffect = isEffect(type);
		layer.Description.RendererType = type;
		if (type == LayerRendererType::XRay) {
			PrepareXRayLayer(layer);
		}
		// Before the owner is told, the passes it sets up depend on it
		UpdateTexturedLayers();
#	if defined(TILEMAP_CACHE_LAYER_WINDOW)
		// A blur layer bypasses the window cache, so whatever it held before is stale once it's an ordinary one again
		for (auto& cache : _layerWindowCaches) {
			if (cache->LayerIndex == layerIndex) {
				cache->Valid = false;
			}
		}
#	endif
		if ((hadEffect || hasEffect) && _owner != nullptr) {
			_owner->OnLayerEffectsChanged();
		}
#else
		// Reduced right away, as when a level is loaded (see LoadLayer())
		if (type == LayerRendererType::XRay) {
			ReduceXRayLayer(layer);
			type = LayerRendererType::Default;
		} else if (type == LayerRendererType::BlurBehind || type == LayerRendererType::Reflection || type == LayerRendererType::ChromaKey) {
			layer.Visible = false;
			type = LayerRendererType::Default;
		}
		layer.Description.RendererType = type;
		UpdateTexturedLayers();
#endif
		return true;
	}

	void TileMap::UpdateTexturedLayers()
	{
		// The passes the layers need now: the texture of each textured layer, the palette indices of each reflection layer
		// (only where reflections are drawn, see LoadLayer()), and the two parts of the sprite layer the tile menus take
		struct WantedPass {
			std::int32_t LayerIndex;
			Recti Source;
			bool DrawsIndices;
			bool WithAlpha;
		};
		SmallVector<WantedPass, 8> wanted;
		bool hasTileMenu = false;
		bool hasKeyedLayers = false;
		for (std::int32_t i = 0; i < (std::int32_t)_layers.size(); i++) {
			if (i == _sprLayerIndex) {
				continue;
			}
			LayerRendererType type = _layers[i].Description.RendererType;
			hasKeyedLayers |= (type == LayerRendererType::ChromaKey || (LayerDescription::IsTexturedBackground(type) &&
				_layers[i].Description.TextureBlend == TexturedLayerBlend::ChromaKey));
			if (LayerDescription::IsTexturedBackground(type)) {
				if (SupportsTexturedBackground || SupportsTexturedBackgroundTunnel || SupportsTexturedBackgroundCylinder) {
					wanted.push_back({ i, Recti(), false, type == LayerRendererType::TileMenu });
				}
				hasTileMenu |= (type == LayerRendererType::TileMenu);
			} else if (type == LayerRendererType::Reflection) {
				wanted.push_back({ i, Recti(), true, false });
			}
		}
		if (hasTileMenu && _sprLayerIndex >= 0) {
			wanted.push_back({ _sprLayerIndex, Recti(0, 0, 2, 2), false, true });
			wanted.push_back({ _sprLayerIndex, Recti(0, 2, 1, 1), false, true });
		}

		// A key is compared with what the layers behind it drew, a dithered or filtered textured layer blends the color of
		// the key with its neighbours, which then isn't the key anymore - so they're drawn as their pixels are
		if (_hasKeyedLayers != hasKeyedLayers) {
			_hasKeyedLayers = hasKeyedLayers;
			for (LayerPass& entry : _layerPasses) {
				if (entry.Pass->_target != nullptr && !entry.DrawsIndices) {
					entry.Pass->_target->SetMinFiltering(hasKeyedLayers ? SamplerFilter::Nearest : SamplerFilter::Linear);
					entry.Pass->_target->SetMagFiltering(hasKeyedLayers ? SamplerFilter::Nearest : SamplerFilter::Linear);
				}
			}
		}

		// The passes no layer needs anymore go, those of the layers that changed are drawn again
		auto isWanted = [&wanted](const LayerPass& entry) {
			for (const WantedPass& item : wanted) {
				if (item.LayerIndex == entry.LayerIndex && item.DrawsIndices == entry.DrawsIndices && item.Source == entry.Source &&
					item.WithAlpha == entry.WithAlpha) {
					return true;
				}
			}
			return false;
		};
		for (std::size_t i = 0; i < _layerPasses.size(); ) {
			if (isWanted(_layerPasses[i])) {
				i++;
			} else {
				_layerPasses.erase(_layerPasses.begin() + i);
			}
		}

		// The new ones are drawn as soon as they're made, or with the viewports, if they aren't set up yet
		for (const WantedPass& item : wanted) {
			bool exists = false;
			for (const LayerPass& entry : _layerPasses) {
				if (entry.LayerIndex == item.LayerIndex && entry.DrawsIndices == item.DrawsIndices && entry.Source == item.Source &&
					entry.WithAlpha == item.WithAlpha) {
					exists = true;
					break;
				}
			}
			if (exists) {
				continue;
			}
			LayerPass& entry = _layerPasses.emplace_back();
			entry.LayerIndex = item.LayerIndex;
			entry.Source = item.Source;
			entry.DrawsIndices = item.DrawsIndices;
			entry.WithAlpha = item.WithAlpha;
			entry.Pass = std::make_unique<TexturedBackgroundPass>(this, item.LayerIndex, item.DrawsIndices, item.Source, item.WithAlpha);
			if (_viewportsInitialized) {
				entry.Pass->Initialize();
			}
		}
	}

	Texture* TileMap::GetLayerPassTarget(std::int32_t layerIndex, bool drawsIndices, const Recti& source) const
	{
		for (const LayerPass& entry : _layerPasses) {
			if (entry.LayerIndex == layerIndex && entry.DrawsIndices == drawsIndices && entry.Source == source) {
				return entry.Pass->_target.get();
			}
		}
		return nullptr;
	}

#if defined(TILEMAP_LAYER_EFFECTS)
	bool TileMap::HasXRayLayers() const
	{
		for (const auto& layer : _layers) {
			if (layer.Description.RendererType == LayerRendererType::XRay) {
				return true;
			}
		}
		return false;
	}

	bool TileMap::HasBlurBehindLayers() const
	{
		for (const auto& layer : _layers) {
			if (layer.Description.RendererType == LayerRendererType::BlurBehind) {
				return true;
			}
		}
		return false;
	}

	bool TileMap::HasReflectionLayers() const
	{
		for (const auto& layer : _layers) {
			if (layer.Description.RendererType == LayerRendererType::Reflection) {
				return true;
			}
		}
		return false;
	}

	bool TileMap::HasChromaKeyLayers() const
	{
		for (const auto& layer : _layers) {
			if (layer.Description.RendererType == LayerRendererType::ChromaKey || (LayerDescription::IsTexturedBackground(layer.Description.RendererType) &&
				(layer.Description.TextureBlend == TexturedLayerBlend::Translucent || layer.Description.TextureBlend == TexturedLayerBlend::ChromaKey))) {
				return true;
			}
		}
		return false;
	}

	bool TileMap::BlurBehindLayersNeedSharpCopy()
	{
		auto needsCopy = [this](std::int32_t tileId) {
			TileSet* tileSet = ResolveTileSet(tileId);
			return (tileSet != nullptr && (tileSet->GetTileEffectInfo(tileId) & (TileSet::TileEffectReflective | TileSet::TileEffectDisplaces)) != 0);
		};

		for (const auto& layer : _layers) {
			if (layer.Description.RendererType != LayerRendererType::BlurBehind) {
				continue;
			}
			for (std::size_t i = 0; i < layer.Layout.size(); i++) {
				const std::int32_t tileId = layer.Layout.TileIDAt(i);
				if (tileId == 0) {
					continue;
				}
				const std::int32_t animIndex = tileId - std::int32_t(_animatedTilesOffset);
				if (animIndex >= 0) {
					// Any frame of an animated tile can be the one shown
					if (animIndex < (std::int32_t)_animatedTiles.size()) {
						for (const auto& frame : _animatedTiles[animIndex].Tiles) {
							if (frame.TileID != 0 && needsCopy(frame.TileID)) {
								return true;
							}
						}
					}
				} else if (needsCopy(tileId)) {
					return true;
				}
			}
		}
		return false;
	}

	bool TileMap::BeginEffectLayer(const TileMapLayer& layer, LayerRendererType type)
	{
		_effectMeshGroups.clear();
		_effectLayer.Type = type;
		_effectLayer.Depth = layer.Description.Depth;
		_effectLayer.Target = (_owner != nullptr ? _owner->GetLayerEffectsTarget(RenderResources::GetCurrentViewport()) : nullptr);

		auto& resolver = ContentResolver::Get();
		if (type == LayerRendererType::XRay) {
			// Without a mask, an x-ray layer still shows its tiles as they are outside the mask
			_effectLayer.MaskAvailable = (_effectLayer.Target != nullptr && _effectLayer.Target->XRayMask != nullptr &&
				resolver.GetShader(PrecompiledShader::TileMapMeshXRay) != nullptr);
			return true;
		}

		_effectLayer.MaskAvailable = false;
		if (type == LayerRendererType::ChromaKey) {
			// The colors of what is behind the layer are compared with the key, a blur of them would key only the middle of an
			// area of the color (see PlayerViewport::InitializeLayerEffects())
			return (_effectLayer.Target != nullptr && _effectLayer.Target->View != nullptr && _effectLayer.Target->BehindCopy != nullptr &&
				resolver.GetShader(PrecompiledShader::TileMapMeshChromaKey) != nullptr);
		}

		// Neither a blur nor a reflection of what is behind a blur layer (the blur effects are off and none of its
		// tiles reflects, or the viewport doesn't render the level at all), so there is nothing to draw either: the
		// layer itself is never visible
		return (_effectLayer.Target != nullptr && _effectLayer.Target->View != nullptr &&
			(_effectLayer.Target->BehindBlur != nullptr || _effectLayer.Target->BehindCopy != nullptr) &&
			resolver.GetShader(PrecompiledShader::TileMapMeshBlur) != nullptr);
	}

	bool TileMap::AppendXRayTile(const LayerTile& tile, float x, float y)
	{
		const std::int32_t animIndex = std::int32_t(tile.TileID) - std::int32_t(_animatedTilesOffset);
		if (animIndex < 0 || animIndex >= (std::int32_t)_animatedTiles.size()) {
			return false;
		}
		const std::size_t frameCount = _animatedTiles[animIndex].Tiles.size();
		if (frameCount == 0) {
			return false;
		}

		// Tells the mask pass of the next frame that it's needed, and for the actors behind which layer
		if (_effectLayer.Target != nullptr && _effectLayer.Target->XRayDepth < _effectLayer.Depth) {
			_effectLayer.Target->XRayDepth = _effectLayer.Depth;
		}

		// The last frame is the mask frame, the ones before it animate outside the mask as usual. A single frame is
		// only shown in the mask, nothing outside it
		std::int32_t frame0;
		if (frameCount == 1) {
			frame0 = 0;
		} else if (frameCount == 2) {
			frame0 = _animatedTiles[animIndex].Tiles[0].TileID;
		} else {
			// Can append to _animatedTiles, so no reference to it is held across
			const std::int32_t unmaskedIndex = GetXRayUnmaskedAnimation(animIndex);
			const AnimatedTile& unmasked = _animatedTiles[unmaskedIndex >= 0 ? unmaskedIndex : animIndex];
			frame0 = (unmaskedIndex >= 0 ? unmasked.Tiles[unmasked.CurrentTileIdx].TileID : unmasked.Tiles[0].TileID);
		}
		const std::int32_t frame1 = _animatedTiles[animIndex].Tiles[frameCount - 1].TileID;

		const float alpha = tile.Alpha / 255.0f;
		if (!_effectLayer.MaskAvailable) {
			if (frame0 != 0) {
				AppendEffectTile(EffectMeshKind::Unmasked, frame0, tile.Flags, alpha, x, y);
			}
			return true;
		}

		Texture* texture0 = nullptr;
		Texture* texture1 = nullptr;
		bool indexed0 = false, indexed1 = false;
		float rect0[4] = {}, rect1[4] = {};
		const bool has0 = (frame0 != 0 && ResolveTileQuad(frame0, tile.Flags, texture0, indexed0, rect0[0], rect0[1], rect0[2], rect0[3]));
		const bool has1 = (frame1 != 0 && ResolveTileQuad(frame1, tile.Flags, texture1, indexed1, rect1[0], rect1[1], rect1[2], rect1[3]));
		if (!has0 && !has1) {
			return true;
		}
		if (has0 && has1 && (texture0 != texture1 || indexed0 != indexed1)) {
			// One draw samples one texture, so frames from two of them (two tile sets, or two chunks of an atlas too
			// large for one texture) can't be swapped per pixel - the tile keeps showing what it shows outside the mask
			AppendEffectTile(EffectMeshKind::Unmasked, frame0, tile.Flags, alpha, x, y);
			return true;
		}

		// An empty frame samples the other one under a weight of zero, so both stay inside the same tile
		const float* r0 = (has0 ? rect0 : rect1);
		const float* r1 = (has1 ? rect1 : rect0);
		const float w0 = (has0 ? 1.0f : 0.0f);
		const float w1 = (has1 ? 1.0f : 0.0f);

		constexpr std::uint32_t XRayFloatsPerVertex = 12;
		SmallVector<float, 0>& vertices = _meshVertices[GetEffectMeshGroup(has0 ? texture0 : texture1, has0 ? indexed0 : indexed1, EffectMeshKind::XRay)];
		const std::size_t base = vertices.size();
		vertices.resize_for_overwrite(base + RenderResources::VerticesPerQuad * XRayFloatsPerVertex);
		float* v = vertices.data() + base;
		const float xr = x + (float)TileSet::DefaultTileSize, yr = y + (float)TileSet::DefaultTileSize;
		// The 12-float vertex of TileMapMeshXRay: position.xy, texcoords of both frames, their weights and the
		// color, whose alpha is the tile's own (the layer tint is applied through the instance color)
		auto put = [&](float px, float py, float cx, float cy) {
			*v++ = px; *v++ = py;
			*v++ = cx * r0[0] + r0[1]; *v++ = cy * r0[2] + r0[3];
			*v++ = cx * r1[0] + r1[1]; *v++ = cy * r1[2] + r1[3];
			*v++ = w0; *v++ = w1;
			*v++ = 1.0f; *v++ = 1.0f; *v++ = 1.0f; *v++ = alpha;
		};
		// The four corners in the order of the shared quad index pattern, as in AppendTileQuad()
		put(x,  y,  0.0f, 0.0f);
		put(xr, y,  1.0f, 0.0f);
		put(xr, yr, 1.0f, 1.0f);
		put(x,  yr, 0.0f, 1.0f);
		return true;
	}

	void TileMap::AppendEffectTile(EffectMeshKind kind, std::int32_t tileId, LayerTileFlags flags, float alpha, float x, float y, float axisY)
	{
		Texture* texture;
		bool indexed;
		float texScaleX, texBiasX, texScaleY, texBiasY;
		if (!ResolveTileQuad(tileId, flags, texture, indexed, texScaleX, texBiasX, texScaleY, texBiasY)) {
			return;
		}
		if (kind == EffectMeshKind::Blur) {
			AppendBlurTileQuad(_meshVertices[GetEffectMeshGroup(texture, indexed, kind)], x, y, (float)TileSet::DefaultTileSize,
				texScaleX, texBiasX, texScaleY, texBiasY, alpha, axisY);
		} else {
			AppendTileQuad(_meshVertices[GetEffectMeshGroup(texture, indexed, kind)], x, y, (float)TileSet::DefaultTileSize,
				texScaleX, texBiasX, texScaleY, texBiasY, alpha);
		}
	}

	void TileMap::AppendBlurTileQuad(SmallVector<float, 0>& vertices, float x, float y, float size,
		float texScaleX, float texBiasX, float texScaleY, float texBiasY, float alpha, float axisY)
	{
		constexpr std::uint32_t BlurFloatsPerVertex = 9;
		const float u0 = texBiasX, v0 = texBiasY;
		const float u1 = texScaleX + texBiasX, v1 = texScaleY + texBiasY;
		const float xr = x + size, yr = y + size;

		const std::size_t base = vertices.size();
		vertices.resize_for_overwrite(base + VerticesPerQuad * BlurFloatsPerVertex);
		float* v = vertices.data() + base;
		auto put = [&](float px, float py, float pu, float pv) {
			*v++ = px; *v++ = py; *v++ = pu; *v++ = pv;
			*v++ = 1.0f; *v++ = 1.0f; *v++ = 1.0f; *v++ = alpha;
			*v++ = axisY;
		};
		// The four corners in the order of the shared quad index pattern, as in AppendTileQuad()
		put(x,  y,  u0, v0);
		put(xr, y,  u1, v0);
		put(xr, yr, u1, v1);
		put(x,  yr, u0, v1);
	}

	bool TileMap::IsTileReflective(std::int32_t tileId, std::int32_t& topRow)
	{
		TileSet* tileSet = ResolveTileSet(tileId);
		if (tileSet == nullptr) {
			return false;
		}
		const std::uint8_t info = tileSet->GetTileEffectInfo(tileId);
		topRow = (info & TileSet::TileEffectTopRowMask);
		return ((info & TileSet::TileEffectReflective) != 0);
	}

	float TileMap::GetReflectionAxis(const TileMapLayer& layer, std::int32_t tileX, std::int32_t tileY, std::int32_t column,
		std::int32_t row, std::int32_t tileId, float tileTopY)
	{
		// How far up the layout the start of a run is looked for, which is also the tallest reflection there can be
		constexpr std::int32_t MaxReflectionRun = 64;

		std::int32_t topRow;
		if (!IsTileReflective(tileId, topRow)) {
			// Mirrors nothing, the line is never used
			return tileTopY;
		}

		if (column >= (std::int32_t)_reflectionColumns.size()) {
			_reflectionColumns.resize(column + 1, ReflectionColumn { -2, 0.0f });
		}
		ReflectionColumn& state = _reflectionColumns[column];
		if (state.Row == row - 1) {
			// The run goes on from the row above
			state.Row = row;
			return state.AxisY;
		}

		// The first tile of a run in this window, which may have started above it - the rows above are looked up in
		// the layout itself, wrapping around a layer that repeats vertically
		std::int32_t rowsAbove = 0;
		std::int32_t layoutY = tileY;
		for (std::int32_t k = 1; k <= MaxReflectionRun; k++) {
			if (--layoutY < 0) {
				if (!layer.Description.RepeatY) {
					break;
				}
				layoutY += layer.LayoutSize.Y;
			}
			const LayerTile& above = layer.Layout[tileX + layoutY * layer.LayoutSize.X];
			const std::int32_t aboveId = ResolveTileID(above);
			std::int32_t aboveTopRow;
			if (aboveId == 0 || above.Alpha == 0 || !IsTileReflective(aboveId, aboveTopRow)) {
				break;
			}
			rowsAbove = k;
			topRow = aboveTopRow;
		}

		state.Row = row;
		state.AxisY = tileTopY - float(rowsAbove * TileSet::DefaultTileSize) + float(topRow);
		return state.AxisY;
	}

	std::int32_t TileMap::GetEffectMeshGroup(const Texture* texture, bool indexed, EffectMeshKind kind)
	{
		for (const auto& group : _effectMeshGroups) {
			if (group.DiffuseTexture == texture && group.Indexed == indexed && group.Kind == kind) {
				return group.VerticesIndex;
			}
		}
		const std::int32_t verticesIndex = RentMeshVertices();
		_effectMeshGroups.push_back({ texture, indexed, kind, verticesIndex });
		return verticesIndex;
	}

	void TileMap::EmitEffectMeshes(RenderQueue& renderQueue, const Vector4f& color, std::uint16_t depth)
	{
		bool sceneSampled = false;
		for (const auto& group : _effectMeshGroups) {
			auto& vertices = _meshVertices[group.VerticesIndex];
			if (vertices.empty()) {
				continue;
			}
			if (group.Kind == EffectMeshKind::Unmasked) {
				EmitMesh(renderQueue, vertices, *group.DiffuseTexture, group.Indexed, 0, color, depth, RenderCommand::Type::TileMap, false);
			} else {
				EmitEffectMesh(renderQueue, vertices, *group.DiffuseTexture, group.Indexed, group.Kind, color, depth);
				sceneSampled |= (group.Kind == EffectMeshKind::Blur || group.Kind == EffectMeshKind::ChromaKey);
			}
		}
		_effectMeshGroups.clear();

		if (sceneSampled) {
			// Everything below the layer is drawn first and copied (and blurred) before the rest of the scene is drawn over it
			_effectLayer.Target->View->RequestSplit(depth);
		}
	}

	void TileMap::EmitEffectMesh(RenderQueue& renderQueue, SmallVector<float, 0>& vertices, const Texture& texture, bool indexed,
		EffectMeshKind kind, const Vector4f& color, std::uint16_t depth)
	{
		const bool isXRay = (kind == EffectMeshKind::XRay);
		const bool isChromaKey = (kind == EffectMeshKind::ChromaKey);
		// A blur layer shows the blurred scene by alpha and the sharp copy moved and mirrored by the other channels.
		// Each stands in for the other where only one of them exists: without the blur effects the blur mixes the scene
		// with itself, and without any tile that reflects or displaces the sharp copy is never used. A chroma key layer
		// compares the sharp copy with its key.
		Texture* sampledTarget;
		Texture* reflectionTarget = nullptr;
		PrecompiledShader shaderType;
		std::uint32_t floatsPerVertex;
		if (isXRay) {
			sampledTarget = _effectLayer.Target->XRayMask;
			shaderType = (indexed ? PrecompiledShader::TileMapMeshXRayPalette : PrecompiledShader::TileMapMeshXRay);
			floatsPerVertex = 12;
		} else if (isChromaKey) {
			sampledTarget = _effectLayer.Target->BehindCopy;
			shaderType = (indexed ? PrecompiledShader::TileMapMeshChromaKeyPalette : PrecompiledShader::TileMapMeshChromaKey);
			floatsPerVertex = 8;
		} else {
			sampledTarget = (_effectLayer.Target->BehindBlur != nullptr ? _effectLayer.Target->BehindBlur : _effectLayer.Target->BehindCopy);
			reflectionTarget = (_effectLayer.Target->BehindCopy != nullptr ? _effectLayer.Target->BehindCopy : _effectLayer.Target->BehindBlur);
			shaderType = (indexed ? PrecompiledShader::TileMapMeshBlurPalette : PrecompiledShader::TileMapMeshBlur);
			floatsPerVertex = 9;
		}
		Shader* shader = ContentResolver::Get().GetShader(shaderType);
		if (sampledTarget == nullptr || shader == nullptr) {
			return;
		}

		const std::uint32_t floatsPerQuad = VerticesPerQuad * floatsPerVertex;
		const std::uint32_t maxQuadsPerChunk = RenderResources::GetMaxQuadsPerDraw(floatsPerVertex);
		const std::uint16_t* quadIndices = RenderResources::GetQuadIndices();
		// The size of the target being drawn into, the mask has it too and the blurred scene covers the same view
		const Vector2i targetSize = RenderResources::GetCurrentViewport()->GetSize();
		const Vector2f targetSizeInv(targetSize.X > 0 ? 1.0f / targetSize.X : 0.0f, targetSize.Y > 0 ? 1.0f / targetSize.Y : 0.0f);

		const std::uint32_t totalQuads = (std::uint32_t)(vertices.size() / floatsPerQuad);
		for (std::uint32_t firstQuad = 0; firstQuad < totalQuads; firstQuad += maxQuadsPerChunk) {
			const std::uint32_t count = std::min(maxQuadsPerChunk, totalQuads - firstQuad);

			if (_effectMeshCommandCount >= (std::int32_t)_effectMeshCommands.size()) {
				auto& newCommand = _effectMeshCommands.emplace_back(std::make_unique<RenderCommand>());
				newCommand->GetMaterial().SetBlendingEnabled(true);
			}
			RenderCommand* command = _effectMeshCommands[_effectMeshCommandCount++].get();

			command->SetType(RenderCommand::Type::TileMap);
			// The x-ray tiles come out premultiplied (see TileMapMeshXRay.shader), the blur is a plain mix by the amount
			command->GetMaterial().SetBlendingFactors(isXRay ? BlendingFactor::One : BlendingFactor::SrcAlpha, BlendingFactor::OneMinusSrcAlpha);

			if (command->GetMaterial().SetShader(shader)) {
				command->GetMaterial().ReserveUniformsDataMemory();
				auto* textureUniform = command->GetMaterial().Uniform(Material::TextureUniformName);
				if (textureUniform != nullptr) {
					textureUniform->SetIntValue(0);
				}
				auto* paletteUniform = command->GetMaterial().Uniform("uTexturePalette");
				if (paletteUniform != nullptr) {
					paletteUniform->SetIntValue(1);
				}
				auto* sampledUniform = command->GetMaterial().Uniform(isXRay ? "uTextureMask" : "uTextureBehind");
				if (sampledUniform != nullptr) {
					sampledUniform->SetIntValue(2);
				}
				auto* reflectionUniform = command->GetMaterial().Uniform("uTextureSharp");
				if (reflectionUniform != nullptr) {
					reflectionUniform->SetIntValue(3);
				}
			}

			auto* instanceBlock = command->GetInstanceBlock();
			if (instanceBlock != nullptr) {
				auto* colorUniform = instanceBlock->GetUniform(Material::ColorUniformName);
				if (colorUniform != nullptr) {
					colorUniform->SetFloatVector(color.Data());
				}
			}
			auto* targetSizeUniform = command->GetMaterial().Uniform("uTargetSizeInv");
			if (targetSizeUniform != nullptr) {
				targetSizeUniform->SetFloatValue(targetSizeInv.X, targetSizeInv.Y);
			}
			if (isChromaKey) {
				auto* keyColorUniform = command->GetMaterial().Uniform("uKeyColor");
				if (keyColorUniform != nullptr) {
					// The copy of the scene may have fewer bits per channel than the palette (see Texture::ColorTargetFormat),
					// a channel within a step of them is the same color
					constexpr float KeyTolerance = (Texture::ColorTargetFormat == Texture::Format::RGB565 ? 1.5f / 31.0f : 1.5f / 255.0f);
					keyColorUniform->SetFloatValue(_effectLayer.KeyColor.X, _effectLayer.KeyColor.Y, _effectLayer.KeyColor.Z, KeyTolerance);
				}
			}

			auto& geometry = command->GetGeometry();
			geometry.SetElementsPerVertex(floatsPerVertex);
			geometry.SetHostVertexPointer(vertices.data() + firstQuad * floatsPerQuad);
			geometry.SetIndexCount(count * RenderResources::IndicesPerQuad);
			geometry.SetHostIndexPointer(quadIndices);
			geometry.SetDrawParameters(PrimitiveType::Triangles, 0, count * VerticesPerQuad);

			command->SetTransformation(Matrix4x4f::Translation(0.0f, 0.0f, 0.0f));
			command->SetLayer(depth);
			// Diffuse on unit 0 and the palette on unit 1 when indexed, the sampled target on unit 2 and the sharp copy a
			// blur layer reflects and displaces on unit 3
			ContentResolver::Get().BindSpritePalette(*command, texture, indexed, 0);
			command->GetMaterial().SetTexture(2, *sampledTarget);
			// Cleared otherwise - the pool is shared by both kinds, and a texture of a pass that was released since must
			// not stay bound to a command an x-ray layer reuses
			if (reflectionTarget != nullptr) {
				command->GetMaterial().SetTexture(3, *reflectionTarget);
			} else {
				command->GetMaterial().SetTexture(3, nullptr);
			}

			renderQueue.AddCommand(command);
		}
	}

	bool TileMap::ResolveTileQuad(std::int32_t tileId, LayerTileFlags flags, Texture*& texture, bool& indexed,
		float& texScaleX, float& texBiasX, float& texScaleY, float& texBiasY)
	{
		Vector4f texCoords;
		if (!GetStaticTileQuad(tileId, flags, texture, texCoords, indexed)) {
			return false;
		}
		texScaleX = texCoords.X;
		texBiasX = texCoords.Y;
		texScaleY = texCoords.Z;
		texBiasY = texCoords.W;
		return true;
	}

	void TileMap::PrepareXRayLayer(const TileMapLayer& layer)
	{
		// Derived right away rather than when first drawn, so they play in step with the animations they come from
		for (std::size_t i = 0; i < layer.Layout.size(); i++) {
			const std::int32_t animIndex = layer.Layout.TileIDAt(i) - std::int32_t(_animatedTilesOffset);
			if (animIndex >= 0 && animIndex < (std::int32_t)_animatedTiles.size() && _animatedTiles[animIndex].Tiles.size() >= 3) {
				GetXRayUnmaskedAnimation(animIndex);
			}
		}
	}
#else
	void TileMap::ReduceXRayLayer(TileMapLayer& layer)
	{
		for (std::size_t i = 0; i < layer.Layout.size(); i++) {
			LayerTile tile = layer.Layout[i];
			const std::int32_t animIndex = std::int32_t(tile.TileID) - std::int32_t(_animatedTilesOffset);
			if (animIndex < 0 || animIndex >= (std::int32_t)_animatedTiles.size()) {
				continue;
			}
			// What the tile shows outside the mask: nothing for a single frame, the frame before the mask frame for two,
			// and the animation of the frames before it for more
			const std::size_t frameCount = _animatedTiles[animIndex].Tiles.size();
			if (frameCount <= 1) {
				tile.TileID = 0;
			} else if (frameCount == 2) {
				tile.TileID = _animatedTiles[animIndex].Tiles[0].TileID;
			} else {
				const std::int32_t unmaskedIndex = GetXRayUnmaskedAnimation(animIndex);
				tile.TileID = (unmaskedIndex >= 0 && _animatedTilesOffset + unmaskedIndex <= UINT16_MAX
					? std::uint16_t(_animatedTilesOffset + unmaskedIndex)
					: _animatedTiles[animIndex].Tiles[0].TileID);
			}
			layer.Layout.Set(i, tile);
		}
	}
#endif

	std::int32_t TileMap::GetXRayUnmaskedAnimation(std::int32_t animIndex)
	{
		// Sized by the animations of the level itself, the derived ones are appended behind them
		if (_xrayUnmaskedAnims.empty()) {
			_xrayUnmaskedAnims.resize(_animatedTiles.size(), 0);
		}
		if (animIndex >= (std::int32_t)_xrayUnmaskedAnims.size()) {
			return -1;
		}
		if (_xrayUnmaskedAnims[animIndex] == 0) {
			AnimatedTile unmasked = _animatedTiles[animIndex];
			unmasked.Tiles.pop_back();
			// Starts as the level's own animations do when it is loaded
			unmasked.CurrentTileIdx = 0;
			unmasked.FramesLeft = 0.0f;
			unmasked.Forwards = false;
			_xrayUnmaskedAnims[animIndex] = (std::int32_t)_animatedTiles.size();
			_animatedTiles.push_back(Death::move(unmasked));
		}
		return _xrayUnmaskedAnims[animIndex];
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
		const auto& layout = sprLayer.Layout;

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

		auto& layout = _layers[_sprLayerIndex].Layout;
		for (const auto& saved : _sprLayerForRollback) {
			layout[saved.TileIndex] = saved.Tile;
		}

		// Tiles that started collapsing only after the checkpoint are whole again and must not continue
		const Vector2i& layoutSize = _layers[_sprLayerIndex].LayoutSize;
		auto it = _activeCollapsingTiles.begin();
		while (it != _activeCollapsingTiles.end()) {
			const auto& tile = layout[it->X + it->Y * layoutSize.X];
			if ((tile.Flags & LayerTileFlags::Collapsing) != LayerTileFlags::Collapsing) {
				it = _activeCollapsingTiles.eraseUnordered(it);
				continue;
			}
			++it;
		}

		std::memcpy(_triggerState.data(), _triggerStateForRollback.data(), _triggerState.sizeInBytes());
	}

	bool TileMap::InitializeFromStream(Stream& src)
	{
		std::int32_t layoutSize = src.ReadVariableInt32();
		if (layoutSize == -1) {
			return (_sprLayerIndex == -1);
		}

		// The state can come from a save of a level changed since (the user content shadows the shipped levels), or
		// from a server, so the size is a real check rather than an assumption
		if (_sprLayerIndex == -1) {
			LOGE("Layout size mismatch ({} != no sprite layer)", layoutSize);
			return false;
		}
		auto& spriteLayer = _layers[_sprLayerIndex];
		std::int32_t realLayoutSize = spriteLayer.LayoutSize.X * spriteLayer.LayoutSize.Y;
		if (layoutSize != realLayoutSize || std::size_t(realLayoutSize) > spriteLayer.Layout.size()) {
			LOGE("Layout size mismatch ({} != {})", layoutSize, realLayoutSize);
			return false;
		}

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
		return true;
	}

	void TileMap::SerializeResumableToStream(Stream& dest, bool fromCheckpoint)
	{
		if (_sprLayerIndex == -1) {
			// The same encoding InitializeFromStream() reads, a raw value used to put the rest of the state out of step
			dest.WriteVariableInt32(-1);
			return;
		}

		auto& spriteLayer = _layers[_sprLayerIndex];
		std::int32_t layoutSize = spriteLayer.LayoutSize.X * spriteLayer.LayoutSize.Y;
		const auto& layout = spriteLayer.Layout;
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

	bool TileMap::InitializeSnapshotFromStream(Stream& src)
	{
		std::int32_t layoutSize = src.ReadVariableInt32();
		std::int32_t realLayoutSize = (_sprLayerIndex != -1 ? _layers[_sprLayerIndex].LayoutSize.X * _layers[_sprLayerIndex].LayoutSize.Y : -1);
		if (layoutSize != realLayoutSize) {
			LOGE("Layout size mismatch ({} != {})", layoutSize, realLayoutSize);
			return false;
		}
		if (layoutSize == -1) {
			return true;
		}

		auto& layout = _layers[_sprLayerIndex].Layout;
		std::uint32_t tileCount = src.ReadVariableUint32();
		if (tileCount > std::uint32_t(layoutSize)) {
			LOGE("Snapshot has more tiles than the layout ({} > {})", tileCount, layoutSize);
			return false;
		}
		for (std::uint32_t i = 0; i < tileCount; i++) {
			std::uint32_t tileIndex = src.ReadVariableUint32();
			LayerTile tile;
			ReadLayerTile(src, tile);
			if (tileIndex < (std::uint32_t)layoutSize) {
				layout[tileIndex] = tile;
			}
		}

		// Read outside of an assert, which isn't evaluated at all in builds without them, a larger size would
		// overflow the state
		std::uint32_t triggerSize = src.ReadVariableUint32();
		if (triggerSize != _triggerState.sizeInBytes()) {
			LOGE("Trigger count mismatch ({} != {})", triggerSize, _triggerState.sizeInBytes());
			return false;
		}
		src.Read(_triggerState.data(), triggerSize);

		_activeCollapsingTiles.clear();
		std::uint32_t collapsingCount = src.ReadVariableUint32();
		if (collapsingCount > std::uint32_t(layoutSize)) {
			LOGE("Snapshot has more collapsing tiles than the layout ({} > {})", collapsingCount, layoutSize);
			return false;
		}
		const Vector2i layoutExtent = _layers[_sprLayerIndex].LayoutSize;
		for (std::uint32_t i = 0; i < collapsingCount; i++) {
			std::int32_t x = src.ReadVariableInt32();
			std::int32_t y = src.ReadVariableInt32();
			// The timers index the layout with them
			if (x >= 0 && y >= 0 && x < layoutExtent.X && y < layoutExtent.Y) {
				_activeCollapsingTiles.emplace_back(x, y);
			}
		}
		_collapsingTimer = src.ReadValueAsLE<float>();

		_hasRollbackCheckpoint = (src.ReadValue<std::uint8_t>() != 0);
		_sprLayerForRollback.clear();
		if (_hasRollbackCheckpoint) {
			std::uint32_t rollbackCount = src.ReadVariableUint32();
			if (rollbackCount > std::uint32_t(layoutSize)) {
				LOGE("Snapshot has more saved tiles than the layout ({} > {})", rollbackCount, layoutSize);
				_hasRollbackCheckpoint = false;
				return false;
			}
			_sprLayerForRollback.reserve(rollbackCount);
			for (std::uint32_t i = 0; i < rollbackCount; i++) {
				RollbackTile entry;
				entry.TileIndex = src.ReadVariableUint32();
				ReadLayerTile(src, entry.Tile);
				// The rollback writes the tile back by this index, and the list has to stay sorted by it
				if (entry.TileIndex < std::uint32_t(layoutSize) &&
					(_sprLayerForRollback.empty() || _sprLayerForRollback.back().TileIndex < entry.TileIndex)) {
					_sprLayerForRollback.push_back(entry);
				}
			}
			src.Read(_triggerStateForRollback.data(), triggerSize);
		}

		return true;
	}

	void TileMap::SerializeSnapshotToStream(Stream& dest)
	{
		if (_sprLayerIndex == -1) {
			dest.WriteVariableInt32(-1);
			return;
		}

		auto& spriteLayer = _layers[_sprLayerIndex];
		std::int32_t layoutSize = spriteLayer.LayoutSize.X * spriteLayer.LayoutSize.Y;
		const auto& layout = spriteLayer.Layout;
		dest.WriteVariableInt32(layoutSize);

		// The level is loaded again from the same file before the snapshot is applied, so only the tiles that
		// can change are stored - destructible ones (DestructAnimation stays set even after they're destroyed)
		// and those a script overwrote since the checkpoint (they're in the rollback list)
		auto isMutable = [&](std::int32_t i) {
			const auto& tile = layout[i];
			if (tile.DestructAnimation >= 0 || tile.DestructType != TileDestructType::None) {
				return true;
			}
			auto it = std::lower_bound(_sprLayerForRollback.begin(), _sprLayerForRollback.end(), (std::uint32_t)i,
				[](const RollbackTile& entry, std::uint32_t index) { return entry.TileIndex < index; });
			return (it != _sprLayerForRollback.end() && it->TileIndex == (std::uint32_t)i);
		};

		std::uint32_t tileCount = 0;
		for (std::int32_t i = 0; i < layoutSize; i++) {
			if (isMutable(i)) {
				tileCount++;
			}
		}
		dest.WriteVariableUint32(tileCount);
		for (std::int32_t i = 0; i < layoutSize; i++) {
			if (isMutable(i)) {
				dest.WriteVariableUint32((std::uint32_t)i);
				WriteLayerTile(dest, layout[i]);
			}
		}

		dest.WriteVariableUint32((std::uint32_t)_triggerState.sizeInBytes());
		dest.Write(_triggerState.data(), (std::int64_t)_triggerState.sizeInBytes());

		dest.WriteVariableUint32((std::uint32_t)_activeCollapsingTiles.size());
		for (const auto& tilePos : _activeCollapsingTiles) {
			dest.WriteVariableInt32(tilePos.X);
			dest.WriteVariableInt32(tilePos.Y);
		}
		dest.WriteValueAsLE<float>(_collapsingTimer);

		dest.WriteValue<std::uint8_t>(_hasRollbackCheckpoint ? 1 : 0);
		if (_hasRollbackCheckpoint) {
			dest.WriteVariableUint32((std::uint32_t)_sprLayerForRollback.size());
			for (const auto& entry : _sprLayerForRollback) {
				dest.WriteVariableUint32(entry.TileIndex);
				WriteLayerTile(dest, entry.Tile);
			}
			dest.Write(_triggerStateForRollback.data(), (std::int64_t)_triggerStateForRollback.sizeInBytes());
		}
	}

	void TileMap::WriteLayerTile(Stream& dest, const LayerTile& tile)
	{
		dest.WriteValueAsLE<std::uint16_t>(tile.TileID);
		dest.WriteValueAsLE<std::uint16_t>(tile.TileParams);
		dest.WriteValueAsLE<std::int16_t>(tile.DestructAnimation);
		dest.WriteValueAsLE<std::int16_t>(tile.DestructFrameIndex);
		dest.WriteValue<std::uint8_t>((std::uint8_t)tile.Flags);
		dest.WriteValue<std::uint8_t>(tile.Alpha);
		dest.WriteValue<std::uint8_t>((std::uint8_t)tile.HasSuspendType);
		dest.WriteValue<std::uint8_t>((std::uint8_t)tile.DestructType);
	}

	void TileMap::ReadLayerTile(Stream& src, LayerTile& tile)
	{
		tile.TileID = src.ReadValueAsLE<std::uint16_t>();
		tile.TileParams = src.ReadValueAsLE<std::uint16_t>();
		tile.DestructAnimation = src.ReadValueAsLE<std::int16_t>();
		tile.DestructFrameIndex = src.ReadValueAsLE<std::int16_t>();
		tile.Flags = (LayerTileFlags)src.ReadValue<std::uint8_t>();
		tile.Alpha = src.ReadValue<std::uint8_t>();
		tile.HasSuspendType = (SuspendType)src.ReadValue<std::uint8_t>();
		tile.DestructType = (TileDestructType)src.ReadValue<std::uint8_t>();
	}

	void TileMap::RenderTexturedBackground(RenderQueue& renderQueue, const Rectf& cullingRect, Vector2f viewCenter, TileMapLayer& layer,
		Texture& texture, const Rectf& area, const Rectf& reference, float x, float y)
	{
		// Only the part of the area in the view is drawn, the shaders take the coordinates of its pixels within the reference
		const float left = std::max(area.X, 0.0f);
		const float top = std::max(area.Y, 0.0f);
		const float right = std::min(area.X + area.W, cullingRect.W);
		const float bottom = std::min(area.Y + area.H, cullingRect.H);
		if (right <= left || bottom <= top || reference.W <= 0.0f || reference.H <= 0.0f) {
			return;
		}

		// Drawn translucent by the variant of its shader, and keyed only with the copy of what is behind it (see
		// HasChromaKeyLayers()), opaque otherwise
		const TexturedLayerBlend blend = layer.Description.TextureBlend;
		bool keyed = false;
#if defined(TILEMAP_LAYER_EFFECTS)
		LayerEffectsTarget* effectsTarget = nullptr;
		if (blend == TexturedLayerBlend::Translucent || blend == TexturedLayerBlend::ChromaKey) {
			effectsTarget = (_owner != nullptr ? _owner->GetLayerEffectsTarget(RenderResources::GetCurrentViewport()) : nullptr);
			keyed = (blend == TexturedLayerBlend::Translucent ||
				(effectsTarget != nullptr && effectsTarget->View != nullptr && effectsTarget->BehindCopy != nullptr));
		}
#endif

		auto* command = RentRenderCommand(layer.Description.RendererType, false, nullptr, keyed);

		auto* instanceBlock = command->GetInstanceBlock();
		instanceBlock->GetUniform(Material::TexRectUniformName)->SetFloatValue((right - left) / reference.W, (left - reference.X) / reference.W,
			(bottom - top) / reference.H, (top - reference.Y) / reference.H);
		instanceBlock->GetUniform(Material::SpriteSizeUniformName)->SetFloatValue(right - left, bottom - top);
		// Screened or multiplied, as opaque as the parameter of the sprite mode is, as in JJ2+ - the shaders premultiply by its
		// alpha, and the fixed-function passes that modulate the texture by it get it premultiplied already
		const bool isScreenOrMultiply = (blend == TexturedLayerBlend::Screen || blend == TexturedLayerBlend::Multiply);
		const float opacity = (isScreenOrMultiply ? layer.Description.TextureBlendParam / 255.0f : 1.0f);
		instanceBlock->GetUniform(Material::ColorUniformName)->SetFloatVector(Colorf(opacity, opacity, opacity, opacity).Data());

		command->GetMaterial().Uniform("uViewSize")->SetFloatValue(reference.W, reference.H);
		command->GetMaterial().Uniform("uShift")->SetFloatValue(x, y);
		if (auto* modelUniform = command->GetMaterial().Uniform("uModel")) {
			// The perspective of JJ2+, any other sky layer is drawn as the sky layers always were here (see LayerDescription::HasPlusPerspective())
			modelUniform->SetFloatValue(layer.Description.HasPlusPerspective() ? 1.0f : 0.0f);
		}
		// A wave has neither, its color holds its parameters
		if (auto* cameraUniform = command->GetMaterial().Uniform("uCameraPos")) {
			cameraUniform->SetFloatVector(viewCenter.Data());
		}
		if (auto* horizonUniform = command->GetMaterial().Uniform("uHorizonColor")) {
			Vector4f horizonColor = layer.Description.Color;
			if (layer.Description.TextureBlend != TexturedLayerBlend::Normal) {
				// In a sprite mode, JJ2+ fades the texture as the 8-bit game does, towards the last entry of the gradient the
				// textures are drawn in (207) instead of the fade color, measured in JJ2+ - resolved every frame so it follows
				// the palette, the gradient is near enough linear for the colors to fade as the entries would
				Vector4f entry = ResolveTintColor(Vector4f(207.0f / 255.0f, 0.0f, 0.0f, 1.0f));
				horizonColor = Vector4f(entry.X, entry.Y, entry.Z, horizonColor.W);
			}
			horizonUniform->SetFloatVector(horizonColor.Data());
		}
		if (auto* waveUniform = command->GetMaterial().Uniform("uWave")) {
			// The wavelengths in red and green, a period of one more pixel than each, 0 for no waves; the amplitudes in units
			// of 128 pixels, measured in the original (not of the size of the texture, as JJ2+ documents them)
			std::int32_t wavelengthX = std::int32_t(layer.Description.Color.X * 255.0f + 0.5f);
			std::int32_t wavelengthY = std::int32_t(layer.Description.Color.Y * 255.0f + 0.5f);
			waveUniform->SetFloatValue(wavelengthX != 0 ? layer.Description.TextureFadePosition.X * 128.0f : 0.0f,
				wavelengthY != 0 ? layer.Description.TextureFadePosition.Y * 128.0f : 0.0f,
				1.0f / float(wavelengthX + 1), 1.0f / float(wavelengthY + 1));
			// The vertical waves move the other way with the distortion angle (see OnUpdate())
			float phase = layer.TextureOffset.X;
			bool distortionAngle = ((layer.Description.TextureFlags & Formats::TexturedLayerParams::DistortionAngle) != 0);
			command->GetMaterial().Uniform("uWavePhase")->SetFloatValue(phase, distortionAngle ? -phase : phase);
		}
		if (auto* tunnelUniform = command->GetMaterial().Uniform("uTunnel")) {
			// The center is kept in the view, as in JJ2+, the defaults draw it as it always was
			tunnelUniform->SetFloatValue(std::clamp<float>(layer.Description.TextureFadePosition.X, 0.0f, 1.0f),
				std::clamp<float>(layer.Description.TextureFadePosition.Y, 0.0f, 1.0f),
				layer.Description.TextureFade != 0 ? 1.0f : 0.0f, (layer.Description.TextureFlags & Formats::TexturedLayerParams::Spiral) != 0 ? 1.0f : 0.0f);
		} else if (auto* cylinderUniform = command->GetMaterial().Uniform("uCylinder")) {
			// The horizon may be anywhere, as in JJ2+, the shader keeps the angle around the tube within a quarter of a turn
			cylinderUniform->SetFloatValue(layer.Description.TextureFadePosition.X, layer.Description.TextureFadePosition.Y,
				layer.Description.TextureFade != 0 ? 1.0f : 0.0f, (layer.Description.TextureFlags & Formats::TexturedLayerParams::HalfSize) != 0 ? 1.0f : 0.0f);
		} else if (auto* horizonOffsetUniform = command->GetMaterial().Uniform("uHorizonOffset")) {
			// The vanishing point and the horizon of a sky may be anywhere too, the defaults draw it as it always was (a
			// backend whose offline tables predate it has none, it draws them in the middle)
			horizonOffsetUniform->SetFloatValue(layer.Description.TextureFadePosition.X - 0.5f, layer.Description.TextureFadePosition.Y - 0.5f,
				layer.Description.TextureFade != 0 ? 0.0f : 1.0f, 0.0f);
		}

		// A sprite mode a fixed blending can do, a pooled command keeps the factors it was given last. With the color premultiplied
		// by the opacity a, screen is a * src + dst * (1 - a * src) and multiply a * src * dst + dst * (1 - a), what is behind
		// mixed with the result by a.
		switch (layer.Description.TextureBlend) {
			case TexturedLayerBlend::Screen: command->GetMaterial().SetBlendingFactors(BlendingFactor::One, BlendingFactor::OneMinusSrcColor); break;
			case TexturedLayerBlend::Multiply: command->GetMaterial().SetBlendingFactors(BlendingFactor::DstColor, BlendingFactor::OneMinusSrcAlpha); break;
			default: command->GetMaterial().SetBlendingFactors(BlendingFactor::SrcAlpha, BlendingFactor::OneMinusSrcAlpha); break;
		}

		command->SetTransformation(Matrix4x4f::Translation(cullingRect.X + left, cullingRect.Y + top, 0.0f));
		command->SetLayer(layer.Description.Depth);
		command->GetMaterial().SetTexture(texture);

#if defined(TILEMAP_LAYER_EFFECTS)
		// Only if the variant was there (see RentRenderCommand())
		if (auto* keyUniform = (keyed ? command->GetMaterial().Uniform("uKeyColor") : nullptr)) {
			const bool isChromaKey = (blend == TexturedLayerBlend::ChromaKey);
			if (isChromaKey) {
				// The key is an entry of the palette, resolved every frame so it follows the palette, with the tolerance of the
				// chroma key layers (see EmitEffectMesh())
				constexpr float KeyTolerance = (Texture::ColorTargetFormat == Texture::Format::RGB565 ? 1.5f / 31.0f : 1.5f / 255.0f);
				Vector4f keyColor = ResolveTintColor(Vector4f(layer.Description.TextureBlendParam / 255.0f, 0.0f, 0.0f, 1.0f));
				keyUniform->SetFloatValue(keyColor.X, keyColor.Y, keyColor.Z, KeyTolerance);
			} else {
				keyUniform->SetFloatValue(0.0f, 0.0f, 0.0f, -1.0f);
			}
			if (auto* opacityUniform = command->GetMaterial().Uniform("uOpacity")) {
				opacityUniform->SetFloatValue(isChromaKey ? 1.0f : layer.Description.TextureBlendParam / 255.0f);
			}
			if (auto* targetSizeUniform = command->GetMaterial().Uniform("uTargetSizeInv")) {
				const Vector2i targetSize = RenderResources::GetCurrentViewport()->GetSize();
				targetSizeUniform->SetFloatValue(targetSize.X > 0 ? 1.0f / targetSize.X : 0.0f, targetSize.Y > 0 ? 1.0f / targetSize.Y : 0.0f);
			}
			// Without a key it's never read, but something has to be bound there
			command->GetMaterial().SetTexture(2, isChromaKey ? *effectsTarget->BehindCopy : texture);
			if (isChromaKey) {
				// Everything below the layer is drawn and copied first, before the layer itself
				effectsTarget->View->RequestSplit(layer.Description.Depth);
			}
		}
#endif

		renderQueue.AddCommand(command);
	}

	void TileMap::DrawMenuBackground(RenderQueue& renderQueue, const Rectf& cullingRect, const TileMapLayer& layer, const Rectf& area, const Rectf& reference)
	{
		// The three layers from the back, as the main menu of the game draws them (see UI::Menu::MainMenu): a part of the
		// sprite layer, another one, and the texture of the layer - all colors rendered upside down by the passes (see
		// TexturedBackgroundPass::Initialize())
		const std::int32_t layerIndex = std::int32_t(&layer - _layers.data());
		Texture* images[3] = { GetLayerPassTarget(_sprLayerIndex, false, Recti(0, 2, 1, 1)),
			GetLayerPassTarget(_sprLayerIndex, false, Recti(0, 0, 2, 2)), GetLayerPassTarget(layerIndex, false) };
		const float sizeScale = (layer.Description.Color.W > 0.0f ? 1.0f : 0.5f);

		// Only the part of the area in the view is drawn
		const float left = std::max(area.X, 0.0f);
		const float top = std::max(area.Y, 0.0f);
		const float right = std::min(area.X + area.W, cullingRect.W);
		const float bottom = std::min(area.Y + area.H, cullingRect.H);
		if (right <= left || bottom <= top) {
			return;
		}

		// How far it has turned and zoomed (see OnUpdate()), and the point it turns around
		const float turn = layer.TextureOffset.X;
		const float zoom = layer.TextureOffset.Y;
		const Vector2f pivot(cullingRect.X + reference.X + reference.W * layer.Description.TextureFadePosition.X,
			cullingRect.Y + reference.Y + reference.H * layer.Description.TextureFadePosition.Y);

		// Drawn translucent, or keyed by the chroma key layers' shader where there is the copy of what is behind it
		const float opacity = (layer.Description.TextureBlend == TexturedLayerBlend::Translucent ? layer.Description.TextureBlendParam / 255.0f : 1.0f);
#if defined(TILEMAP_LAYER_EFFECTS)
		bool keyed = false;
		if (layer.Description.TextureBlend == TexturedLayerBlend::ChromaKey) {
			_effectLayer.Target = (_owner != nullptr ? _owner->GetLayerEffectsTarget(RenderResources::GetCurrentViewport()) : nullptr);
			keyed = (_effectLayer.Target != nullptr && _effectLayer.Target->View != nullptr && _effectLayer.Target->BehindCopy != nullptr &&
				ContentResolver::Get().GetShader(PrecompiledShader::TileMapMeshChromaKey) != nullptr);
			if (keyed) {
				Vector4f keyColor = ResolveTintColor(Vector4f(layer.Description.TextureBlendParam / 255.0f, 0.0f, 0.0f, 1.0f));
				_effectLayer.KeyColor = Vector3f(keyColor.X, keyColor.Y, keyColor.Z);
			}
		}
#endif

		for (std::int32_t i = 0; i < 3; i++) {
			Texture* image = images[i];
			if (image == nullptr) {
				continue;
			}

			// The pace of each, as the main menu has them
			float scale, angle;
			Vector2f center = pivot;
			switch (i) {
				case 0:
					scale = 0.6f + 0.04f * sinf(zoom * 0.2f);
					angle = turn * -0.2f;
					break;
				case 1:
					scale = 0.6f + 0.04f * sinf(zoom * 0.2f);
					angle = turn * 0.4f;
					center += Vector2f(96.0f * sinf(turn * 0.37f), 96.0f * cosf(turn * 0.31f));
					break;
				default:
					// The frontmost one zooms between 0.627 and 0.884 every 1016 ticks of the original, measured in JJ2+
					scale = 0.7553f + 0.1287f * sinf(zoom * 0.6f);
					angle = turn * 0.3f;
					center += Vector2f(64.0f * sinf(turn * 0.25f), 64.0f * cosf(turn * 0.32f));
					break;
			}

			// Each a little further back than the one in front of it, the frontmost where the layer is
			const std::uint16_t depth = std::uint16_t(std::max<std::int32_t>(std::int32_t(layer.Description.Depth) - (2 - i), 0));
			const Vector2f period = image->GetSize().As<float>() * (sizeScale * scale);
			const float sn = sinf(angle);
			const float cs = cosf(angle);

#if defined(TILEMAP_USE_SINGLE_DRAW)
			// One quad over the area, its corners where they are on the turned image repeated from where it turns around
			// (the inverse of the rotation of Matrix4x4f::RotateZ()), the texture repeats in between
			SmallVector<float, 0>& vertices = _meshVertices[RentMeshVertices()];
			vertices.resize_for_overwrite(FloatsPerQuad);
			float* v = vertices.data();
			auto put = [&](float x, float y) {
				const float dx = cullingRect.X + x - center.X;
				const float dy = cullingRect.Y + y - center.Y;
				*v++ = cullingRect.X + x; *v++ = cullingRect.Y + y;
				*v++ = (dx * cs + dy * sn) / period.X; *v++ = -(dy * cs - dx * sn) / period.Y;
				*v++ = 1.0f; *v++ = 1.0f; *v++ = 1.0f; *v++ = opacity;
			};
			// The four corners in the order of the shared quad index pattern, as in AppendTileQuad()
			put(left, top);
			put(right, top);
			put(right, bottom);
			put(left, bottom);
#	if defined(TILEMAP_LAYER_EFFECTS)
			if (keyed) {
				EmitEffectMesh(renderQueue, vertices, *image, false, EffectMeshKind::ChromaKey, Vector4f(1.0f, 1.0f, 1.0f, 1.0f), depth);
				continue;
			}
#	endif
			EmitMesh(renderQueue, vertices, *image, false, 0, Vector4f(1.0f, 1.0f, 1.0f, 1.0f), depth, RenderCommand::Type::TileMap, false);
#else
			// One quad repeating the image, an even number of times so a corner of it is where it turns around, large enough
			// to cover the view however it's turned - over the whole view, wherever the layer is
			float farthest = 0.0f;
			for (std::int32_t k = 0; k < 4; k++) {
				Vector2f corner((k & 1) != 0 ? cullingRect.X + cullingRect.W : cullingRect.X, (k & 2) != 0 ? cullingRect.Y + cullingRect.H : cullingRect.Y);
				farthest = std::max(farthest, (corner - center).Length());
			}
			const float repeats = 2.0f * (std::ceil(farthest / std::min(period.X, period.Y)) + 1.0f);
			const Vector2f size = period * repeats;

			auto* command = RentRenderCommand(LayerRendererType::Default, false);
			auto* instanceBlock = command->GetInstanceBlock();
			instanceBlock->GetUniform(Material::TexRectUniformName)->SetFloatValue(repeats, 0.0f, -repeats, repeats);
			instanceBlock->GetUniform(Material::SpriteSizeUniformName)->SetFloatVector(size.Data());
			instanceBlock->GetUniform(Material::ColorUniformName)->SetFloatValue(1.0f, 1.0f, 1.0f, opacity);

			Matrix4x4f worldMatrix = Matrix4x4f::Translation(center.X, center.Y, 0.0f);
			worldMatrix.RotateZ(angle);
			worldMatrix.Translate(size.X * -0.5f, size.Y * -0.5f, 0.0f);
			command->SetTransformation(worldMatrix);
			command->SetLayer(depth);
			ContentResolver::Get().BindSpritePalette(*command, *image, false, 0);

			renderQueue.AddCommand(command);
#endif
		}

#if defined(TILEMAP_LAYER_EFFECTS)
		if (keyed) {
			// Everything below the layer is drawn and copied first, before the layer itself
			_effectLayer.Target->View->RequestSplit(std::uint16_t(std::max<std::int32_t>(std::int32_t(layer.Description.Depth) - 2, 0)));
		}
#endif
	}

#if defined(TILEMAP_LAYER_EFFECTS)
	void TileMap::DrawReflection(RenderQueue& renderQueue, const Rectf& cullingRect, const Rectf& area, const TileMapLayer& layer, Texture* texture, Vector2f shift)
	{
		// What is drawn behind the layer is copied just before it, which is what it mirrors - without the copy (the
		// viewport doesn't render the level, or the shaders of the special layers aren't there) there is nothing to draw
		LayerEffectsTarget* effectsTarget = (_owner != nullptr ? _owner->GetLayerEffectsTarget(RenderResources::GetCurrentViewport()) : nullptr);
		Shader* shader = ContentResolver::Get().GetShader(PrecompiledShader::TexturedBackgroundReflection);
		if (effectsTarget == nullptr || effectsTarget->View == nullptr || effectsTarget->BehindCopy == nullptr ||
			texture == nullptr || shader == nullptr) {
			return;
		}

		// The horizon and the vanishing point are a fraction of the layer (SURFACE::INNERLAYER of JJ2+) or of the view
		const Rectf reference = ((layer.Description.TextureFlags & Formats::TexturedLayerParams::InnerLayer) != 0
			? area : Rectf(0.0f, 0.0f, cullingRect.W, cullingRect.H));
		const float horizon = reference.Y + std::clamp<float>(layer.Description.TextureFadePosition.Y, 0.0f, 1.0f) * reference.H;
		const float vanishingPoint = reference.X + layer.Description.TextureFadePosition.X * reference.W;
		// Only the part of the area below the horizon that is in the view is drawn, a horizon at the top of the view reflects black
		const float top = std::max(std::max(horizon, area.Y), 0.0f);
		const float bottom = std::min(area.Y + area.H, cullingRect.H);
		const float left = std::max(area.X, 0.0f);
		const float right = std::min(area.X + area.W, cullingRect.W);
		if (bottom <= top || right <= left) {
			return;
		}

		if (_reflectionCommandCount >= (std::int32_t)_reflectionCommands.size()) {
			auto& newCommand = _reflectionCommands.emplace_back(std::make_unique<RenderCommand>());
			newCommand->GetMaterial().SetBlendingEnabled(true);
		}
		RenderCommand* command = _reflectionCommands[_reflectionCommandCount++].get();
		command->SetType(RenderCommand::Type::TileMap);
		command->GetMaterial().SetBlendingFactors(BlendingFactor::SrcAlpha, BlendingFactor::OneMinusSrcAlpha);
		if (command->GetMaterial().SetShader(shader)) {
			command->GetMaterial().ReserveUniformsDataMemory();
			command->GetGeometry().SetDrawParameters(PrimitiveType::TriangleStrip, 0, 4);
			auto* textureUniform = command->GetMaterial().Uniform(Material::TextureUniformName);
			if (textureUniform != nullptr) {
				textureUniform->SetIntValue(0);
			}
			auto* behindUniform = command->GetMaterial().Uniform("uTextureBehind");
			if (behindUniform != nullptr) {
				behindUniform->SetIntValue(2);
			}
		}

		auto* instanceBlock = command->GetInstanceBlock();
		if (instanceBlock != nullptr) {
			instanceBlock->GetUniform(Material::TexRectUniformName)->SetFloatValue(1.0f, 0.0f, 1.0f, 0.0f);
			instanceBlock->GetUniform(Material::SpriteSizeUniformName)->SetFloatValue(right - left, bottom - top);
			instanceBlock->GetUniform(Material::ColorUniformName)->SetFloatVector(Colorf::White.Data());
		}

		// The color holds the tint opacity, the distance, the distortion and the blur, as the textured layers of JJ2+ do
		const Vector4f& color = layer.Description.Color;
		// The height of the camera above the plane, in pixels of the view - a plane farther away shows its texture smaller. The
		// horizon of the plane is half of it above the horizon drawn, 32 rows and one more for each step of the distance, as
		// measured in JJ2+ (at the distance of 0, 10, 40 and 120)
		const float cameraHeight = 2.0f * (32.0f + color.Y * 255.0f);
		const Vector2i textureSize = texture->GetSize();
		const Vector4f tint = ResolveTintColor(Vector4f(layer.Description.TextureFade / 255.0f, 0.0f, 0.0f, color.X));
		const Vector2i targetSize = RenderResources::GetCurrentViewport()->GetSize();
		auto& material = command->GetMaterial();
		if (auto* uniform = material.Uniform("uViewSize")) {
			uniform->SetFloatValue(cullingRect.W, cullingRect.H);
		}
		if (auto* uniform = material.Uniform("uQuad")) {
			uniform->SetFloatValue(left, top, right - left, bottom - top);
		}
		if (auto* uniform = material.Uniform("uHorizon")) {
			uniform->SetFloatValue(horizon, vanishingPoint, cameraHeight, color.Z);
		}
		if (auto* uniform = material.Uniform("uShift")) {
			uniform->SetFloatValue(shift.X, shift.Y, 1.0f / std::max<std::int32_t>(textureSize.X, 1), 1.0f / std::max<std::int32_t>(textureSize.Y, 1));
		}
		if (auto* uniform = material.Uniform("uTint")) {
			uniform->SetFloatVector(tint.Data());
		}
		if (auto* uniform = material.Uniform("uBlur")) {
			uniform->SetFloatValue(color.W > 0.0f ? 1.0f : 0.0f);
		}
		if (auto* uniform = material.Uniform("uTargetSizeInv")) {
			uniform->SetFloatValue(targetSize.X > 0 ? 1.0f / targetSize.X : 0.0f, targetSize.Y > 0 ? 1.0f / targetSize.Y : 0.0f);
		}

		command->SetTransformation(Matrix4x4f::Translation(cullingRect.X + left, cullingRect.Y + top, 0.0f));
		command->SetLayer(layer.Description.Depth);
		// The palette indices of the texture of the layer on unit 0, the copy of what is behind it on unit 2, as for the blur layers
		material.SetTexture(0, *texture);
		material.SetTexture(2, *effectsTarget->BehindCopy);
		renderQueue.AddCommand(command);

		// Everything below the layer is drawn and copied first, before the rest of the scene is drawn over it
		effectsTarget->View->RequestSplit(layer.Description.Depth);
	}
#endif

	void TileMap::PruneTilesetAtlas()
	{
		if (!PruneAtlasToUsedTiles || _tileSets.size() != 1 || _tileSets[0].Data == nullptr) {
			// More than one part means the level brought its own extra tiles (MLLE); the ids then span several
			// sets and the mapping below would not describe them, so those are left whole
			LoadDeferredDiffuse();
			return;
		}
		if (_tilesOverridden) {
			// The level's per-tile overrides (MLLE) were already patched into this atlas and its masks;
			// rebuilding from the source sheet would silently revert them - both the graphics and the
			// collision - so such a level keeps its whole atlas
			LoadDeferredDiffuse();
			return;
		}

		const std::int32_t tileCount = _tileSets[0].Data->TileCount;
		if (tileCount <= 0) {
			LoadDeferredDiffuse();
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
			if (layer.Layout.empty()) {
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
			LoadDeferredDiffuse();
			return;	// Nothing to gain
		}

		// The tileset loaded so far goes before the packed one is built, so only one of the two is ever
		// resident. Normally that is just its masks (the atlas was deferred to here); the tileset file is read
		// again for the atlas, which re-reads the masks too - a few tens of milliseconds, where building the
		// whole atlas up front and throwing it away cost the entire sheet.
		const String path = _tileSetPath;
		const std::uint16_t captionTileId = _captionTileId;
		_tileSets[0].Data = nullptr;
		_diffuseDeferred = false;

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
		_viewportsInitialized = true;
		// The passes of the layers that need them, those made before are put in the chain of the viewports again (see
		// UpdateTexturedLayers())
		UpdateTexturedLayers();
		for (LayerPass& entry : _layerPasses) {
			entry.Pass->Initialize();
		}
	}

	void TileMap::InvalidateTexturedBackground()
	{
		for (LayerPass& entry : _layerPasses) {
			entry.Pass->Invalidate();
		}
	}

	void TileMap::LoadDeferredDiffuse()
	{
		if (!_diffuseDeferred) {
			return;
		}
		_diffuseDeferred = false;

		// Freed first, so the masks are never resident twice; the palette was applied by the first request
		_tileSets[0].Data = nullptr;
		_tileSets[0].Data = ContentResolver::Get().RequestTileSet(_tileSetPath, _captionTileId, false);
		if (_tileSets[0].Data == nullptr) {
			// Every lookup now reports a missing tile instead of walking a null tileset; the level is
			// unplayable either way, but it fails as empty layers rather than as a crash
			LOGE("Cannot load tileset \"{}\", the level has no tiles to draw", _tileSetPath);
			_tileSets[0].Count = 0;
			return;
		}
		_tileSets[0].Count = _tileSets[0].Data->TileCount;
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

	TileSet* TileMap::ResolveTileSet(std::int32_t& tileId, std::int32_t& meshSlotBase)
	{
		for (auto& tileSetPart : _tileSets) {
			if (tileId < tileSetPart.Count) {
				tileId += tileSetPart.Offset;
				meshSlotBase = tileSetPart.MeshSlotBase;
				return tileSetPart.Data.get();
			}

			tileId -= tileSetPart.Count;
		}

		return nullptr;
	}

#if defined(TILEMAP_USE_SINGLE_DRAW)
	void TileMap::UpdateMeshSlots()
	{
		// A few tile sets of a chunk or a few each, the atlas of one can be made again (see LoadDeferredDiffuse())
		_meshSlots.clear();
		for (auto& tileSetPart : _tileSets) {
			tileSetPart.MeshSlotBase = (std::int32_t)_meshSlots.size();
			if (tileSetPart.Data != nullptr) {
				for (std::int32_t i = 0; i < tileSetPart.Data->GetTextureCount(); i++) {
					_meshSlots.push_back(MeshSlot{ tileSetPart.Data.get(), i });
				}
			}
		}
	}
#endif

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
			Vector2i textureSize = GetTargetSize();
			std::int32_t width = textureSize.X;
			std::int32_t height = textureSize.Y;

			_camera = std::make_unique<Camera>();
			// Y up, unlike every other pass: the textured background shaders read this texture with V running up
			// the layer - they were written when a render target came out that way - so the layer is rendered
			// upside down into it, on every backend alike (see RhiFwd.h)
			_camera->SetOrthoProjection(0.0f, (float)width, (float)height, 0.0f);
			_camera->SetView(0, 0, 0, 1);
			// Tile menu images keep their transparent parts, there the images behind show through
			_target = std::make_unique<Texture>(nullptr, _withAlpha ? Texture::Format::RGBA8 : Texture::ColorTargetFormat, width, height);
			_view = std::make_unique<Viewport>(_target.get(), Viewport::DepthStencilFormat::None);
			_view->SetRootNode(this);
			_view->SetCamera(_camera.get());
			//_view->setClearMode(Viewport::ClearMode::Never);
			// Indices are taken as they are, as JJ2+ takes the texels of a reflection, never blended into ones in between,
			// not even where the texture is drawn smaller than it is, towards the horizon
			// Colors of a key stay exact too (see TileMap::UpdateTexturedLayers())
			if (_drawsIndices || _owner->_hasKeyedLayers) {
				_target->SetMinFiltering(SamplerFilter::Nearest);
			}
			_target->SetMagFiltering(_drawsIndices || _owner->_hasKeyedLayers ? SamplerFilter::Nearest : SamplerFilter::Linear);
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

		// Where the tiles of a tile menu are transparent, the layers of it behind show through, the other textured backgrounds
		// have them black as they always had
		_view->SetClearColor(0.0f, 0.0f, 0.0f, _withAlpha ? 0.0f : 1.0f);

		// Once in the chain, the passes are set up again with the viewports
		auto& chain = Viewport::GetChain();
		for (Viewport* view : chain) {
			if (view == _view.get()) {
				return;
			}
		}
		chain.push_back(_view.get());
	}

	TileMap::TexturedBackgroundPass::~TexturedBackgroundPass()
	{
		// A pass that isn't needed anymore leaves the chain before its viewport is destroyed
		if (_view != nullptr) {
			auto& chain = Viewport::GetChain();
			for (std::int32_t i = std::int32_t(chain.size()) - 1; i >= 0; i--) {
				if (chain[i] == _view.get()) {
					chain.erase(&chain[i]);
					break;
				}
			}
		}
	}

	Vector2i TileMap::TexturedBackgroundPass::GetTargetSize() const
	{
		if (_source.W > 0 && _source.H > 0) {
			return Vector2i(_source.W, _source.H) * TileSet::DefaultTileSize;
		}
		return GetLayerTextureSize(_owner->_layers[_layerIndex]);
	}

	void TileMap::TexturedBackgroundPass::Invalidate()
	{
		if (_view == nullptr) {
			return;
		}
		auto& chain = Viewport::GetChain();
		if (_layerIndex >= 0 && _layerIndex < (std::int32_t)_owner->_layers.size() && GetTargetSize() != _target->GetSize()) {
			// Another layer of another size is drawn by the pass now, as when the one before is edited as tiles, so the
			// texture is made again at its size instead of showing a part of it or of the one before
			for (std::int32_t i = std::int32_t(chain.size()) - 1; i >= 0; i--) {
				if (chain[i] == _view.get()) {
					chain.erase(&chain[i]);
					break;
				}
			}
			_view = nullptr;
			_camera = nullptr;
			_target = nullptr;
			_renderCommands.clear();
			_alreadyRendered = false;
			Initialize();
			return;
		}
		// The pass leaves the chain once it has drawn a background that isn't animated, it's put back to draw it again (only
		// once, see Initialize(), which also takes what the layer is drawn as now)
		_alreadyRendered = false;
		Initialize();
	}

	bool TileMap::TexturedBackgroundPass::OnDraw(RenderQueue& renderQueue)
	{
		if (_layerIndex < 0) {
			// The layer isn't drawn as a textured background anymore, such as while it's edited
			return false;
		}
		TileMapLayer& layer = _owner->_layers[_layerIndex];
		// No more than the target was made for, which is the top left of a larger layer, or the area of the layer it's given
		Vector2i targetTiles = _target->GetSize() / TileSet::DefaultTileSize;
		Vector2i origin = (_source.W > 0 ? Vector2i(_source.X, _source.Y) : Vector2i(0, 0));
		Vector2i layoutSize = Vector2i(std::clamp<std::int32_t>(layer.LayoutSize.X - origin.X, 0, targetTiles.X),
			std::clamp<std::int32_t>(layer.LayoutSize.Y - origin.Y, 0, targetTiles.Y));

		std::int32_t renderCommandIndex = 0;
		bool isAnimated = false;

		if (!layer.TexturePixels.empty() && _source.W <= 0) {
			// A texture of its own instead of the tiles, made again only when its pixels change. It stays indexed, it's drawn
			// through the palette as the tiles are, so it follows the palette the same way, or as the indices themselves.
			constexpr std::int32_t Size = Formats::TexturedLayerChunk::TextureSize;
			if (_image == nullptr || _imageRevision != layer.TextureRevision) {
				if (_image == nullptr) {
					_image = std::make_unique<Texture>("TexturedLayer", Texture::Format::R8, Size, Size);
				}
				_image->LoadFromTexels(layer.TexturePixels.data(), 0, 0, Size, Size);
				_imageRevision = layer.TextureRevision;
			}

			auto command = _renderCommands[renderCommandIndex++].get();
			ContentResolver::Get().ConfigureSpriteShader(*command, !_drawsIndices);
			auto instanceBlock = command->GetInstanceBlock();
			instanceBlock->GetUniform(Material::TexRectUniformName)->SetFloatValue(1.0f, 0.0f, 1.0f, 0.0f);
			instanceBlock->GetUniform(Material::SpriteSizeUniformName)->SetFloatValue(float(Size), float(Size));
			// Indices only in red, any other channel of the texture taken as it is would read as a higher index, a backend
			// that keeps an R8 texture as RGBA8 fills green and blue with 255 (GL gives 0)
			instanceBlock->GetUniform(Material::ColorUniformName)->SetFloatVector((_drawsIndices ? Colorf::Red : Colorf::White).Data());
			command->SetTransformation(Matrix4x4f::Translation(0.0f, 0.0f, 0.0f));
			ContentResolver::Get().BindSpritePalette(*command, *_image, !_drawsIndices, 0);
			renderQueue.AddCommand(command);
			// The tiles aren't drawn
			layoutSize = Vector2i(0, 0);
		}

		for (std::int32_t y = 0; y < layoutSize.Y; y++) {
			for (std::int32_t x = 0; x < layoutSize.X; x++) {
				LayerTile& tile = layer.Layout[(origin.X + x) + (origin.Y + y) * layer.LayoutSize.X];

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
				// Indexed tilesets bake their colors into the background render target through the palette shader, unless
				// their indices are drawn, the plain sprite shader takes them as they are then, only the red of them (alpha
				// or a channel a backend filled in would read as a higher index, see the texture of the layer above)
				const bool throughPalette = (tileSet->IsIndexed && !_drawsIndices);
				ContentResolver::Get().ConfigureSpriteShader(*command, throughPalette);

				Vector2i texSize = tileTexture->GetSize();
				float texScaleX = TileSet::DefaultTileSize / float(texSize.X);
				float texBiasX = ((tileId % tileSet->TilesPerRow) * float(TileSet::PaddedTileSize) + TileSet::TilePadding) / float(texSize.X);
				float texScaleY = TileSet::DefaultTileSize / float(texSize.Y);
				float texBiasY = ((tileId / tileSet->TilesPerRow) * float(TileSet::PaddedTileSize) + TileSet::TilePadding) / float(texSize.Y);

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
				instanceBlock->GetUniform(Material::ColorUniformName)->SetFloatVector((_drawsIndices && tileSet->IsIndexed ? Colorf::Red : Colorf::White).Data());

				command->SetTransformation(Matrix4x4f::Translation(x * TileSet::DefaultTileSize, y * TileSet::DefaultTileSize, 0.0f));
				ContentResolver::Get().BindSpritePalette(*command, *tileTexture, throughPalette, 0);

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
