#pragma once

#include "ITileMapOwner.h"
#include "../ILevelHandler.h"
#include "../PitType.h"
#include "../SuspendType.h"
#include "LayerTypes.h"
#include "TileSet.h"

#include "../../nCine/Graphics/Camera.h"
#include "../../nCine/Graphics/Viewport.h"

#include <IO/Stream.h>

using namespace Death::IO;

namespace Jazz2
{
	class LevelHandler;

#if defined(WITH_ANGELSCRIPT)
	namespace Scripting
	{
		class LevelScriptLoader;
	}
#endif
}

/*
	Whether a tile layer's mesh is emitted grouped by the atlas slot its tiles sample instead of in screen
	order.

	Only worth it where the device keeps a single small texel window resident rather than the whole texture:
	the RDP samples out of 4 KB of TMEM, of which a paletted tileset may use 2 KB - one 32x32 CI8 tile - so a
	tile id that recurs at scattered screen positions is re-uploaded at every one of them. Grouping the
	submission collapses those repeats onto the residency cache and takes the uploads down to the number of
	DISTINCT tiles on screen. Every other backend holds the whole atlas in texture memory and would only pay
	the grouping, so it stays off there.
*/
#if defined(TILEMAP_USE_SINGLE_DRAW) && defined(WITH_RHI_RDP)
#	define TILEMAP_GROUP_MESH_BY_TILE
#endif

namespace Jazz2::Tiles
{
	/**
		@brief Describes the configuration of a tile map layer
		
		Holds the parallax and rendering properties of a single layer --- its depth, per-axis scroll and auto-scroll
		speeds, scroll offsets, repeat flags, speed models and the renderer type with its color parameter. One is
		stored in each @ref TileMapLayer and drives how that layer is positioned and drawn relative to the camera.
	*/
	struct LayerDescription {
		/** @brief Layer depth (Z position) */
		std::uint16_t Depth;
		/** @brief Horizontal speed */
		float SpeedX;
		/** @brief Vertical speed */
		float SpeedY;
		/** @brief Horizontal auto speed */
		float AutoSpeedX;
		/** @brief Vertical auto speed */
		float AutoSpeedY;
		/** @brief Horizontal scroll offset */
		float OffsetX;
		/** @brief Vertical scroll offset */
		float OffsetY;
		/** @brief Whether layer should repeat horizontally */
		bool RepeatX;
		/** @brief Whether layer should repeat vertically */
		bool RepeatY;
		/** @brief Whether inherent offset should be used */
		bool UseInherentOffset;
		/** @brief Horizontal speed model */
		LayerSpeedModel SpeedModelX;
		/** @brief Vertical speed model */
		LayerSpeedModel SpeedModelY;

		/** @brief Layer renderer type */
		LayerRendererType RendererType;
		/** @brief Layer color parameter */
		Vector4f Color;
	};

	/**
		@brief Per-tile state flags of a tile placed in a layer
		
		Stored on each @ref LayerTile to mark horizontal/vertical flipping, one-way collision and the runtime-only
		state of a tile already queued in the active collapsing list. Supports a bitwise combination of its member
		values.
	*/
	enum class LayerTileFlags : std::uint8_t {
		None = 0x00,			/**< None */

		FlipX = 0x01,			/**< Flipped horizontally */
		FlipY = 0x02,			/**< Flipped vertically */

		OneWay = 0x10,			/**< One-way collision */

		Collapsing = 0x80		/**< Runtime-only: tile is already queued in the active collapsing list */
	};

	DEATH_ENUM_FLAGS(LayerTileFlags);

	/**
		@brief Represents a single tile placed in a tile map layer
		
		One entry of a layer's layout grid. It references a tile in the tile set (or an animated tile) together with
		its packed parameters, flags, transparency and the suspend and destruct behavior; for destructible tiles it
		also tracks the associated animation and the currently active frame (reused as collapse delay or trigger ID).
	*/
	struct LayerTile {
		// The struct is a dense per-tile grid (layoutWidth * layoutHeight entries per layer), so it is packed
		// to 12 bytes: every field is 16-bit or smaller. The three enums carry explicit 8-bit bases, the two
		// destruct fields are 16-bit (an animated-tile ID fits comfortably, -1 stays the "none" sentinel; the
		// frame index is a small frame/delay/trigger number) and the ID is 16-bit because a tile ID is bounded
		// by the tile set size plus the animated tiles behind it: a J2L level stores it in 16 bits and the
		// converter masks it down to JJ2Tileset::GetMaxSupportedTiles() (at most 4096), animated tiles start at
		// that same bound, and the only other writer - SetTile() from scripting - adds a 12-bit index to it. So
		// the highest reachable value is under 8192 and there is no negative sentinel (tile #0 means "empty").

		/** @brief Tile ID */
		std::uint16_t TileID;
		/** @brief Tile parameters */
		std::uint16_t TileParams;
		/** @brief Animation ID for destructible tile (`-1` = none) */
		std::int16_t DestructAnimation;
		/** @brief Denotes the specific frame from the above animation that is currently active --- Collapsible: Delay ("wait" parameter); Trigger: Trigger ID */
		std::int16_t DestructFrameIndex;
		/** @brief Tile flags */
		LayerTileFlags Flags;
		/** @brief Tile transparency */
		std::uint8_t Alpha;
		/** @brief Suspend type of tile */
		SuspendType HasSuspendType;
		/** @brief Destruct type of tile */
		TileDestructType DestructType;
	};

	static_assert(sizeof(LayerTile) == 12, "LayerTile must stay packed, it is allocated per tile of every layer");

	/**
		@brief Represents a single tile map layer
		
		Bundles a layer's grid of @ref LayerTile entries with its dimensions, its @ref LayerDescription and a
		visibility flag. A @ref TileMap owns an ordered list of these layers, of which one is the main sprite layer
		used for collision while the rest provide foreground and parallax background detail.
	*/
	struct TileMapLayer {
		/** @brief Layer layout */
		std::unique_ptr<LayerTile[]> Layout;
		/** @brief Layer layout size */
		Vector2i LayoutSize;
		/** @brief Layer description */
		LayerDescription Description;
		/** @brief Layer visibility */
		bool Visible;
	};

	/**
		@brief Represents a single frame of an animated tile
		
		One entry in the frame sequence of an @ref AnimatedTile, referencing the static tile in the tile set that is
		displayed while this frame is active.
	*/
	struct AnimatedTileFrame {
		/** @brief Tile ID */
		std::uint16_t TileID;
	};

	/**
		@brief Represents an animated tile
		
		Defines a tile whose appearance cycles through a sequence of @ref AnimatedTileFrame frames. Besides the frame
		list it stores the playback timing (frame duration, optional fixed and random extra delays) and the current
		playback state, and supports forward-only as well as ping-pong (forward then backward) animation.
	*/
	struct AnimatedTile {
		/** @brief Individual tiles (frames) */
		SmallVector<AnimatedTileFrame, 0> Tiles;
		/** @brief Fixed number of extra animation frames that will show the last frame */
		std::int16_t Delay;
		/** @brief Maximum random number of extra animation frames that will show the last frame */
		std::int16_t DelayJitter;
		/** @brief Fixed number of extra animation frames that will show the last frame before the animation should start to play backward (if @ref IsPingPong is enabled) */
		std::int32_t PingPongDelay;
		/** @brief Current frame of the animation */
		std::int32_t CurrentTileIdx;
		/** @brief Duration of animation frame */
		float FrameDuration;
		/** @brief Frames left until animation advances */
		float FramesLeft;
		/** @brief Whether animation should play forward and then backward */
		bool IsPingPong;
		/** @brief Whether animation plays forward (if @ref IsPingPong is enabled) */
		bool Forwards;
	};

	/**
		@brief Represents a renderable tile map, consists of multiple layers
		
		Owns the level's tile layers and tile sets and renders them as a scene node. Besides drawing, it advances
		animated tiles, performs tile collision queries, handles destructible/collapsing/trigger tiles and spawns
		debris, notifying its @ref ITileMapOwner of the resulting events.
	*/
	class TileMap : public SceneNode // , public IResumable
	{
#if defined(WITH_ANGELSCRIPT)
		friend class Scripting::LevelScriptLoader;
#endif

	public:
		/** @{ @name Constants */

		/** @brief Maximum number of triggers */
		static constexpr std::int32_t TriggerCount = 32;
		/** @brief Hardcoded offset for layer positioning */
		static constexpr std::int32_t HardcodedOffset = 70;

		/** @brief Mask of the tile index inside a packed tile value (see @ref GetTile()) */
		static constexpr std::uint16_t TileIndexMask = 0x0FFF;
		/** @brief Flag of a packed tile value that is flipped horizontally */
		static constexpr std::uint16_t TileFlagFlipX = 0x1000;
		/** @brief Flag of a packed tile value that is flipped vertically */
		static constexpr std::uint16_t TileFlagFlipY = 0x2000;
		/** @brief Flag of a packed tile value that refers to an animated tile (index is relative to the first animated tile) */
		static constexpr std::uint16_t TileFlagAnimated = 0x4000;

		/** @} */

		/** @brief Flags that modify behaviour of @ref DestructibleDebris, supports a bitwise combination of its member values */
		enum class DebrisFlags {
			None = 0x00,				/**< None */
			Disappear = 0x01,			/**< Debris disappears over time */
			Bounce = 0x02,				/**< Debris bounces off solid tiles */
			AdditiveBlending = 0x04		/**< Debris is rendered with additive blending */
		};

		DEATH_PRIVATE_ENUM_FLAGS(DebrisFlags);

		/** @brief Describes a visual debris (particle effect) */
		struct DestructibleDebris {
			/** @brief Position */
			Vector2f Pos;
			/** @brief Depth (layer) */
			std::uint16_t Depth;

			/** @brief Size of the drawn area */
			Vector2f Size;
			/**
				@brief Displacement of the drawn area from the centre of its logical frame cell

				Zero for debris that is its own little quad. A trimmed sprite frame covers less than its
				cell, so drawing it needs to know where inside that cell it belongs - otherwise the frame
				is stretched over the whole cell (see @ref GenericGraphicResource::GetFrameOffset()).
			*/
			Vector2f FrameOffset;
			/** @brief Speed */
			Vector2f Speed;
			/** @brief Acceleration */
			Vector2f Acceleration;

			/** @brief Scale */
			float Scale;
			/** @brief Scale change speed */
			float ScaleSpeed;

			/** @brief Angle */
			float Angle;
			/** @brief Angle change speed */
			float AngleSpeed;

			/** @brief Alpha */
			float Alpha;
			/** @brief Alpha change speed */
			float AlphaSpeed;

			/** @brief Time remaining until disposal */
			float Time;

			/** @brief Fraction of speed kept when bouncing off a solid tile (with @ref DebrisFlags::Bounce) */
			float Elasticity = 0.8f;

			/** @brief Texture horizontal scale */
			float TexScaleX;
			/** @brief Texture horizontal bias */
			float TexBiasX;
			/** @brief Texture vertical scale */
			float TexScaleY;
			/** @brief Texture vertical bias */
			float TexBiasY;

			/** @brief Diffuse texture */
			Texture* DiffuseTexture;
			/**
			 * @brief Flat palette offset when @ref DiffuseTexture is an indexed sprite
			 *
			 * The sprite is recolored at draw time. `-1` when the texture holds baked colors (e.g., a tileset texture)
			 * and must use the plain Sprite shader.
			 */
			std::int32_t PaletteOffset = -1;

			/** @brief Behavior flags */
			DebrisFlags Flags;
		};

		/**
		 * @brief Creates a new instance
		 *
		 * @param tileSetPath   Relative path to the main tile set
		 * @param captionTileId  Tile used to render the level-preview caption thumbnail
		 * @param applyPalette   Whether to apply the tile set's palette to the live sprite palette
		 */
		TileMap(StringView tileSetPath, std::uint16_t captionTileId, bool applyPalette);
		~TileMap();

		/** @brief Returns `true` if all used tile sets are loaded */
		bool IsValid() const;

		/** @brief Sets an owner of tile map */
		void SetOwner(ITileMapOwner* owner);
		/** @brief Returns size of tile map in tiles */
		Vector2i GetSize() const;
		/** @brief Returns size of tile map in pixels */
		Vector2i GetLevelBounds() const;
		/** @brief Returns pit type */
		PitType GetPitType() const;
		/** @brief Sets pit type */
		void SetPitType(PitType value);

		void OnUpdate(float timeMult) override;
		/** @brief Called at the end of each frame */
		void OnEndFrame();
		bool OnDraw(RenderQueue& renderQueue) override;

		/** @brief Returns `true` if the mask of a tile on the main (sprite) layer is completely empty */
		bool IsTileEmpty(std::int32_t tx, std::int32_t ty);
		/** @brief Returns `true` if the tile on the main (sprite) layer can be destroyed by the player (read-only, no side effects) */
		bool IsTileDestructible(std::int32_t tx, std::int32_t ty);
		/** @brief Returns `true` if the tile on the main (sprite) layer is a one-way platform (passable from below) */
		bool IsTileOneWay(std::int32_t tx, std::int32_t ty);
		/** @brief Returns `true` if the given ~1/3 corner of the tile's collision mask is empty (cornerX/cornerY: -1 = left/top, +1 = right/bottom) */
		bool IsTileCornerEmpty(std::int32_t tx, std::int32_t ty, std::int32_t cornerX, std::int32_t cornerY);
		/** @brief Returns `true` if the tile's collision mask is neither fully empty nor fully filled (e.g., a slope or a thin solid band) */
		bool IsTilePartiallySolid(std::int32_t tx, std::int32_t ty);
		/** @brief Returns `true` if the tile on the main (sprite) layer is controlled by a trigger (toggled solid/empty by a trigger crate) */
		bool IsTileTrigger(std::int32_t tx, std::int32_t ty);
		/** @brief Returns `true` if the mask of tiles on the main (sprite) layer intersecting a given AABB is empty */
		bool IsTileEmpty(const AABBf& aabb, TileCollisionParams& params);
		/**
			@brief Returns `true` if the collision mask of the main (sprite) layer is empty at a single point

			A stripped-down @ref IsTileEmpty() for things small enough that one sample is all the precision
			they need - debris in particular, which is a few pixels across but can be several hundred strong
			after an enemy dies. It resolves one tile and tests one mask bit, skipping the covered-tile
			iteration and the whole destructible/collapsing tile machinery that a point sample cannot use.
		*/
		bool IsTilePointEmpty(std::int32_t x, std::int32_t y, bool downwards);
		/** @brief Returns `true` if tiles on the main (sprite) layer intersecting a given AABB can be destroyed */
		bool CanBeDestroyed(const AABBf& aabb, TileCollisionParams& params);
		/** @brief Returns suspend state of a given position */
		SuspendType GetTileSuspendState(float x, float y);
		/** @brief Advances descructible animation of a given tile */
		bool AdvanceDestructibleTileAnimation(std::int32_t tx, std::int32_t ty, std::int32_t amount);

		/** @brief Adds an additional tile set as a continuation of the previous one */
		void AddTileSet(StringView tileSetPath, std::uint16_t offset, std::uint16_t count, const std::uint8_t* paletteRemapping = nullptr);
		/** @brief Reads layer configuration from a stream */
		void ReadLayerConfiguration(Stream& s);
		/** @brief Reads description of animated tiles from a stream */
		void ReadAnimatedTiles(Stream& s);
		/** @brief Sets tile event flags */
		void SetTileEventFlags(std::int32_t x, std::int32_t y, EventType tileEvent, std::uint8_t* tileParams);
		/** @brief Overrides the diffuse texture of the specified tile */
		bool OverrideTileDiffuse(std::int32_t tileId, StaticArrayView<(TileSet::DefaultTileSize + 2) * (TileSet::DefaultTileSize + 2), std::uint32_t> tileDiffuse);
		/**
		 * @brief Returns `true` if the tileset containing the given tile stores indexed (palette) diffuse
		 *
		 * When indexed, an overridden tile must be supplied as palette indices (red channel) rather than baked colors.
		 */
		bool IsTileSetIndexed(std::int32_t tileId);
		/** @brief Overrides the collision mask of the specified tile */
		bool OverrideTileMask(std::int32_t tileId, StaticArrayView<TileSet::DefaultTileSize * TileSet::DefaultTileSize, std::uint8_t> tileMask);

		/** @brief Returns a caption tile */
		StaticArrayView<TileSet::DefaultTileSize * TileSet::DefaultTileSize, Color> GetCaptionTile() const {
			// The tileset can be gone after a failed repack (see PruneTilesetAtlas()); the caller
			// already handles the empty view a caption-less tileset returns, so it covers this too
			if (_tileSets.empty() || _tileSets[0].Data == nullptr) {
				return {};
			}
			return _tileSets[0].Data->GetCaptionTile();
		}

		/** @brief Returns relative paths of all used tile sets */
		Array<StringView> GetUsedTileSetPaths() const;

		/** @{ @name Debris budget */

		/**
			@brief Maximum number of live debris particles, or `0` if the effect is unbounded

			A live particle is far more expensive than its 100 bytes in @ref _debrisList: it also rents a pooled
			@ref RenderCommand for every frame it is visible (~840 bytes on the Dreamcast, see @ref
			RentRenderCommand()) and its instance uniforms take a slice of both the render batcher's and the
			streaming uniform buffer's pools, which only ever grow to their high-water mark. Together that is
			roughly 1.3 KB per particle that a burst pins until the level ends, and one boss death emits over a
			thousand of them - more than the whole heap the consoles have left. Desktop targets have the memory,
			so the effect stays unbounded there.
		*/
#if defined(DEATH_TARGET_DREAMCAST) || defined(DEATH_TARGET_WII) || defined(DEATH_TARGET_GAMECUBE)
		static constexpr std::int32_t MaxDebrisCount = 448;
#elif defined(DEATH_TARGET_N64)
		// Half the other consoles' budget, because 8 MB of RDRAM is half the heap the smallest of them has
		static constexpr std::int32_t MaxDebrisCount = 224;
#else
		static constexpr std::int32_t MaxDebrisCount = 0;
#endif
		/**
			@brief Maximum number of live debris particles the weather effect may occupy, or `0` if unbounded

			Weather spawns every frame and its particles live for about three seconds, so it settles at
			`intensity * ~190` live particles. Left alone it would sit at @ref MaxDebrisCount on its own and
			leave nothing for a death burst, which is the effect that actually matters.
		*/
#if defined(DEATH_TARGET_DREAMCAST) || defined(DEATH_TARGET_WII) || defined(DEATH_TARGET_GAMECUBE)
		static constexpr std::int32_t MaxWeatherDebrisCount = 128;
#elif defined(DEATH_TARGET_N64)
		// Half again, see MaxDebrisCount
		static constexpr std::int32_t MaxWeatherDebrisCount = 64;
#else
		static constexpr std::int32_t MaxWeatherDebrisCount = 0;
#endif
		/**
			@brief Maximum number of particles one @ref CreateParticleDebris() burst emits, or `0` if unbounded

			The producers walk the sprite frame in fixed steps, so the count grows with the sprite's area: the
			biggest one that can perish this way (Devan's demon form, 154x115) yields 1131 particles. Widening
			the step instead of dropping the tail keeps the burst covering the whole sprite - the particles just
			get proportionally fewer and bigger (see @ref GetParticleDebrisStep()).
		*/
#if defined(DEATH_TARGET_DREAMCAST) || defined(DEATH_TARGET_WII) || defined(DEATH_TARGET_GAMECUBE)
		static constexpr std::int32_t MaxParticleDebrisPerBurst = 256;
#elif defined(DEATH_TARGET_N64)
		// Half again, see MaxDebrisCount
		static constexpr std::int32_t MaxParticleDebrisPerBurst = 128;
#else
		static constexpr std::int32_t MaxParticleDebrisPerBurst = 0;
#endif

		/** @brief Returns number of live debris particles */
		std::int32_t GetDebrisCount() const {
			return (std::int32_t)_debrisList.size();
		}
		/**
		 * @brief Returns the step in which a particle debris burst walks a sprite frame of the given size
		 *
		 * `debrisSize + 1` (the particle size plus a one pixel gap) unless the frame is big enough for the
		 * burst to exceed @ref MaxParticleDebrisPerBurst; the particle size is then the returned step minus one.
		 */
		static std::int32_t GetParticleDebrisStep(std::int32_t debrisSize, std::int32_t frameWidth, std::int32_t frameHeight);

		/** @} */

		/** @brief Creates a generic debris */
		void CreateDebris(const DestructibleDebris& debris);
		/** @brief Creates a tile debris */
		void CreateTileDebris(std::int32_t tileId, std::int32_t x, std::int32_t y);
		/** @brief Creates a particle debris from a sprite */
		void CreateParticleDebris(const GraphicResource* res, Vector3f pos, Vector2f force, std::int32_t currentFrame, bool isFacingLeft);
		/** @brief Creates a sprite debris */
		void CreateSpriteDebris(const GraphicResource* res, Vector3f pos, std::int32_t count);

		/** @brief Returns state of a given trigger */
		bool GetTrigger(std::uint8_t triggerId);
		/** @brief Sets state of a given trigger */
		void SetTrigger(std::uint8_t triggerId, bool newState);

		/** @brief Returns number of layers */
		std::int32_t GetLayerCount() const {
			return (std::int32_t)_layers.size();
		}
		/** @brief Returns size of a given layer in tiles, or an empty vector if the layer doesn't exist */
		Vector2i GetLayerSize(std::int32_t layerIndex) const;
		/**
		 * @brief Returns the tile at the given coordinates on a given layer as a packed value
		 *
		 * The low 12 bits are the tile index (see @ref TileIndexMask), combined with @ref TileFlagFlipX / @ref
		 * TileFlagFlipY / @ref TileFlagAnimated. Returns `0` if the layer or coordinates are out of range.
		 */
		std::uint16_t GetTile(std::int32_t layerIndex, std::int32_t x, std::int32_t y) const;
		/**
		 * @brief Sets the tile at the given coordinates on a given layer from a packed value
		 *
		 * The value is packed as in @ref GetTile(). Unrelated tile state (transparency, collision flags) is preserved.
		 * Returns `false` if out of range.
		 */
		bool SetTile(std::int32_t layerIndex, std::int32_t x, std::int32_t y, std::uint16_t tileValue);

		/** @brief Creates a checkpoint for eventual rollback */
		void CreateCheckpointForRollback();
		/** @brief Rolls back to the last checkpoint */
		void RollbackToCheckpoint();

		/** @brief Initializes tile map state from a stream */
		void InitializeFromStream(Stream& src);
		/** @brief Serializes tile map state to a stream */
		void SerializeResumableToStream(Stream& dest, bool fromCheckpoint = false);

		/**
			@brief Repacks the tileset's atlas around the tiles this level actually references

			Called once the layers, animated tiles and events have been read, which is the first moment the set
			of referenced tiles is known. Does nothing where the platform can hold the whole atlas.
		*/
		void PruneTilesetAtlas();

		/** @brief Called when the viewport needs to be initialized (e.g., when the resolution is changed) */
		void OnInitializeViewport();

	private:
		enum class LayerType {
			Other,
			Sky,
			Sprite
		};

#ifndef DOXYGEN_GENERATING_OUTPUT
		// Doxygen 1.12.0 outputs also private structs/unions even if it shouldn't
		struct TileSetPart {
			std::unique_ptr<TileSet> Data;
			std::int32_t Offset;
			std::int32_t Count;
		};

		// One sprite-layer tile as it looked when the last checkpoint was taken. The checkpoint used to be a
		// full copy of the layer - a second grid as big as the layer itself, megabytes on a large level - even
		// though only a fraction of a percent of the tiles can ever differ from it: a tile only changes if it
		// is destructible (shot, collapsing or trigger-controlled) or if a script overwrites it via SetTile().
		// The list is kept sorted by index, both to dedupe (only the first saved value of a tile is the
		// checkpoint value) and so that SerializeResumableToStream() can merge-walk it against the live layer.
		struct RollbackTile {
			std::uint32_t TileIndex;
			LayerTile Tile;
		};

		class TexturedBackgroundPass : public SceneNode
		{
			friend class TileMap;

		public:
			TexturedBackgroundPass(TileMap* owner)
				: _owner(owner), _alreadyRendered(false)
			{
			}

			void Initialize();

			bool OnDraw(RenderQueue& renderQueue) override;

		private:
			TileMap* _owner;
			std::unique_ptr<Texture> _target;
			std::unique_ptr<Viewport> _view;
			std::unique_ptr<Camera> _camera;
			SmallVector<std::unique_ptr<RenderCommand>, 0> _renderCommands;
			bool _alreadyRendered;
		};
#endif

		ITileMapOwner* _owner;
		std::int32_t _sprLayerIndex;
		PitType _pitType;

		SmallVector<TileSetPart, 2> _tileSets;
		SmallVector<TileMapLayer, 0> _layers;
		SmallVector<RollbackTile, 0> _sprLayerForRollback;
		/// Whether a checkpoint was ever taken. Not implied by @ref _sprLayerForRollback being non-empty --- a
		/// level without a single destructible tile has an empty (but valid) checkpoint.
		bool _hasRollbackCheckpoint;
		SmallVector<AnimatedTile, 0> _animatedTiles;
		SmallVector<Vector2i, 0> _activeCollapsingTiles;
		float _collapsingTimer;
		std::uint32_t _animatedTilesOffset;
		// Kept so the atlas can be repacked once the level's tile usage is known (see PruneTilesetAtlas())
		String _tileSetPath;
		std::uint16_t _captionTileId;
		// Whether any tile's graphics or mask was overridden (MLLE) - such an atlas must not be repacked,
		// the rebuild would silently revert the overrides
		bool _tilesOverridden;
		BitArray _triggerState;
		BitArray _triggerStateForRollback;

		/// Cached instance-block uniforms of one pooled per-tile render command
		struct TileCommandUniforms
		{
			RHI::UniformCache* TexRect = nullptr;
			RHI::UniformCache* SpriteSize = nullptr;
			RHI::UniformCache* Color = nullptr;
			RHI::UniformCache* PaletteOffset = nullptr;
		};

		SmallVector<DestructibleDebris, 0> _debrisList;
		SmallVector<std::unique_ptr<RenderCommand>, 0> _renderCommands;
		/// Instance-block uniforms of the correspondingly indexed pooled command. Resolving them by name costs
		/// a linear scan of the block, which at one command per visible tile dominated the layer build - they
		/// only have to be looked up again when a pool slot's shader changes (see @ref RentRenderCommand).
		SmallVector<TileCommandUniforms, 0> _renderCommandUniforms;
		std::int32_t _renderCommandsCount;
		/// Highest number of commands rented in one frame since the pool was last trimmed, and how many frames
		/// ago that was. Only the memory-constrained consoles trim (see @ref OnEndFrame()).
		std::int32_t _renderCommandsPeak;
		std::int32_t _renderCommandsPeakAge;

#if defined(TILEMAP_USE_SINGLE_DRAW)
		// Per-frame pools for aggregated meshes, replacing the per-tile and per-particle commands. One vertex buffer
		// is filled per drawn tile layer and per debris group; each mesh is then split into chunks that individually
		// fit the shared array buffer limit (64 KB), so a mesh emits one command per chunk (usually just one). Both
		// pools grow on demand and reset in OnEndFrame(); host vertex pointers reference the buffers until the render
		// queue is flushed, so a buffer is never reused within a frame (across viewports the counts simply keep growing).
		SmallVector<SmallVector<float, 0>, 0> _meshVertices;
		SmallVector<std::unique_ptr<RenderCommand>, 0> _meshCommands;
		std::int32_t _meshVerticesCount = 0;
		std::int32_t _meshCommandCount = 0;
		/// Highest number of vertex buffers rented in one frame since the pool was last trimmed. Shares the age
		/// counter of the command pool, as both are trimmed together (see @ref OnEndFrame()).
		std::int32_t _meshVerticesPeak = 0;

		/// One accumulated batch of debris quads - everything a particle carries outside the vertex stream, so
		/// particles that agree on all of it share a single draw (a death burst is one sprite at one depth, hence
		/// one group for the whole effect)
		struct DebrisMeshGroup
		{
			Texture* DiffuseTexture;
			std::int32_t PaletteOffset;
			std::uint16_t Depth;
			bool AdditiveBlending;
			std::int32_t VerticesIndex;
		};

		SmallVector<DebrisMeshGroup, 4> _debrisMeshGroups;

#	if defined(TILEMAP_GROUP_MESH_BY_TILE)
		/// One visible tile of the layer currently being built, held back so the layer can be emitted grouped by
		/// the atlas slot its tiles sample instead of in screen order (see @ref DrawLayer()). Exactly the
		/// arguments @ref AppendTileQuad() takes plus the grouping key, so nothing has to be recomputed - the
		/// two integer divisions the UV bias costs would have run again per tile otherwise.
		struct MeshTileEntry
		{
			float X, Y;
			float TexScaleX, TexBiasX, TexScaleY, TexBiasY;
			float Alpha;
			std::uint16_t Slot;			//< Packed atlas slot the tile samples, the grouping key
			std::uint16_t Chunk;		//< Atlas chunk (texture) that slot lives in
		};

		/// Ceiling on the tiles one layer's mesh is grouped over, set by the width of @ref _meshTileOrder. A
		/// viewport of a tile layer is a few hundred cells; beyond this the layer is emitted ungrouped instead.
		constexpr static std::uint32_t MaxGroupedMeshTiles = 65535;

		/// Held-back tiles of the layer being built, reused across layers and frames (a layer's worth of cells,
		/// so a few KB at the peak)
		SmallVector<MeshTileEntry, 0> _meshTileEntries;
		/// Indices into @ref _meshTileEntries in grouped order, and the counting-sort buckets that produce them
		SmallVector<std::uint16_t, 0> _meshTileOrder;
		SmallVector<std::uint16_t, 0> _meshTileBuckets;
#	endif
#endif

		std::int32_t _texturedBackgroundLayer;
		TexturedBackgroundPass _texturedBackgroundPass;

		void DrawLayer(RenderQueue& renderQueue, TileMapLayer& layer, const Rectf& cullingRect, Vector2f viewCenter);
		static float TranslateCoordinate(float coordinate, float speed, float offset, std::int32_t viewSize, bool isY);
		RenderCommand* RentRenderCommand(LayerRendererType type, bool indexed = false, TileCommandUniforms** uniforms = nullptr);
#if defined(TILEMAP_USE_SINGLE_DRAW)
		// Appends one tile's two triangles (6 vertices, 8 floats each: position.xy, texcoords.xy, color.rgba) to a
		// layer mesh buffer. Color is (1,1,1,alpha); the layer tint is applied via the command's instance color.
		static void AppendTileQuad(SmallVector<float, 0>& vertices, float x, float y, float size,
			float texScaleX, float texBiasX, float texScaleY, float texBiasY, float alpha);
		// Appends one particle's two triangles in the same layout, with its rotation, scale and frame offset already
		// folded into the four corners - the quad the sprite shader would have synthesized from its model matrix
		static void AppendDebrisQuad(SmallVector<float, 0>& vertices, const DestructibleDebris& debris);
		// Rents a mesh vertex buffer from the per-frame pool and returns its index (the pool can reallocate, so
		// callers hold indices rather than pointers)
		std::int32_t RentMeshVertices();
		// Emits an accumulated mesh as one or more render commands (split into <=64 KB chunks)
		void EmitMesh(RenderQueue& renderQueue, SmallVector<float, 0>& vertices, const Texture& texture, bool indexed,
			std::uint16_t paletteOffset, const Vector4f& color, std::uint16_t depth, RenderCommand::Type type, bool additiveBlending);
#endif

		void SaveTileForRollback(std::uint32_t tileIndex, const LayerTile& tile);

		bool AdvanceDestructibleTileAnimation(LayerTile& tile, std::int32_t tx, std::int32_t ty, std::int32_t& amount, StringView soundName);
		void AdvanceCollapsingTileTimers(float timeMult);
		void SetTileDestructibleEventParams(LayerTile& tile, TileDestructType type, std::uint16_t tileParams);
		std::int32_t GetTileDestructibleFrameCount(const LayerTile& tile);

		void UpdateDebris(float timeMult);
		void DrawDebris(RenderQueue& renderQueue);

		void RenderTexturedBackground(RenderQueue& renderQueue, const Rectf& cullingRect, Vector2f viewCenter, TileMapLayer& layer, float x, float y);

		TileSet* ResolveTileSet(std::int32_t& tileId);
		std::int32_t ResolveTileID(const LayerTile& tile) const;
	};
}