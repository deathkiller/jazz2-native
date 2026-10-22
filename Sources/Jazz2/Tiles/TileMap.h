#pragma once

#include "ITileMapOwner.h"
#include "../ILevelHandler.h"
#include "../PitType.h"
#include "../SuspendType.h"
#include "LayerEffects.h"
#include "LayerTypes.h"
#include "TileSet.h"

#include "../../nCine/Graphics/Camera.h"
#include "../../nCine/Graphics/Viewport.h"

#include <IO/Stream.h>

using namespace Death::IO;

namespace Jazz2
{
	class LevelHandler;

	namespace Formats
	{
		class LevelReader;
		struct AnimatedTileRecord;
		struct LayerRecord;
	}

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

/*
	Whether a tile layer's mesh holds one packed record per tile instead of the four corners of a float quad.

	A record is 16 bytes where the quad is 128 bytes of vertices and 12 of indices, and the tile map writes
	every byte of it, the commit copies it into the streaming buffer and a fixed-function dispatch reads it
	back - three passes over some thousand tiles a frame at the PowerVR's 640x480. The PVR dispatch takes the
	tile's position and texels straight from the record (see RHI::PVR::TileRecord). The RDP has records of its
	own, written by the grouped emission above.
*/
#if defined(TILEMAP_USE_SINGLE_DRAW) && defined(WITH_RHI_PVR)
#	define TILEMAP_PACKED_TILE_RECORDS
#endif

/*
	Whether the tiles of a layer behind the sprite layer are skipped where fully opaque sprite-layer tiles cover
	them (see TileMap::OpaqueCoverage). Worth its walk only where a hidden tile costs real work: a rectangle and
	often a texel upload on the Nintendo 64, a submitted polygon and the fill of every one of its pixels on the
	PowerVR, which has no hidden-surface removal in the translucent list everything is drawn in.
*/
#if defined(DEATH_TARGET_N64) || defined(WITH_RHI_PVR)
#	define TILEMAP_OPAQUE_COVERAGE
#endif

/*
	Whether that coverage takes each tile's own opacity (TileSet::IsTileOpaque()) as proof, instead of requiring a
	palette with no translucent colour at all. The content loader works the flag out only where it is defined, as
	it costs a pass over every opaque tile when the atlas is built. Not with scripting, which can recolor the
	palette while the level runs, and not yet on the Nintendo 64 (see the note in TileMap::DrawLayer()).
*/
#if defined(TILEMAP_OPAQUE_COVERAGE) && !defined(DEATH_TARGET_N64) && !defined(WITH_ANGELSCRIPT)
#	define TILEMAP_PER_TILE_OPACITY
#endif

/*
	Whether the static tiles of a layer's visible window are kept in buffers of the layer's own between frames.

	The tiles a layer shows only change when its window moves by a whole tile, every 32 pixels of ITS scroll,
	which the slow parallax layers do rarely. Walking the window and copying its quads into the streaming
	buffer every frame was about an eighth of the main thread's CPU time on desktop. The window is walked once
	instead and re-issued under a new translation until it moves or one of its tiles changes; animated tiles
	stay on the streaming path. The Nintendo 64 has a cache of its own in the RDP's packed tile format (see
	LayerMeshCache). The fixed-function tiers are left out, because their tile-mesh dispatch reads the vertex
	stream by hand and isn't known to apply the command's model matrix - except the GX's, the PICA200's, the GS's
	and the GE's, which do (see `GxDevice::DispatchTileMesh()`, `PicaDevice::DispatchTileMesh()`,
	`GsDevice::DispatchTileMesh()` and `GuDevice::DispatchTileMesh()`). On the PSP the walk and the copy of its
	quads into the streaming buffer were a fifth of the main thread's CPU time in a castle level standing still
	(measured in PPSSPP).
*/
#if defined(TILEMAP_USE_SINGLE_DRAW) && !defined(TILEMAP_GROUP_MESH_BY_TILE) && \
	(defined(WITH_RHI_GL) || defined(WITH_RHI_D3D11) || defined(WITH_RHI_VULKAN) || defined(WITH_RHI_METAL) || \
	 defined(WITH_RHI_GX) || defined(WITH_RHI_PICA) || defined(WITH_RHI_GS) || defined(WITH_RHI_GU))
#	define TILEMAP_CACHE_LAYER_WINDOW
#endif

/*
	Whether the tiles of the heat effect (see LayerTileFlags::Heat) are drawn as such. Each one is a single quad of a mesh
	of their own, drawn after the layer by a shader that moves its rows, so it costs one draw call more for a layer that
	shows any, and nothing for one that doesn't. Only on the stream of the single-draw tile mesh, the packed tile records
	and the grouped emission have no room for it; a backend without the shader (the fixed-function tiers) draws them as
	ordinary tiles.
*/
#if defined(TILEMAP_USE_SINGLE_DRAW) && !defined(TILEMAP_GROUP_MESH_BY_TILE) && !defined(TILEMAP_PACKED_TILE_RECORDS)
#	define TILEMAP_HEAT_TILES
#endif

namespace Jazz2::Tiles
{
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
		/**
			Heat effect, as the tile type of JJ2+ --- every row of pixels of the tile is drawn moved left or right by up to
			2 pixels, into the tiles next to it, by a fixed table of the 32 rows of a tile that scrolls up a row every 4 game
			ticks of the original (70 a second), measured in the original. Drawn so only in an ordinary layer and where the
			shader of it is available, as an ordinary tile otherwise.
		*/
		Heat = 0x04,

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
		// frame index is a small frame/delay/trigger number) and the ID is 16-bit because the level format stores
		// it in 16 bits. A converted J2L level keeps it under 8192, but a level of the game's own format (from the
		// editor, or from a server) can use the whole range, so an ID that doesn't fit the signed destruct field,
		// or names an animated tile the level doesn't have, is never made destructible (see
		// SetTileDestructibleEventParams()). There is no negative sentinel of the ID (tile #0 means "empty").

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
		@brief Tiles of a single @ref TileMapLayer, in blocks and in one of two forms

		One allocation per block of @ref BlockTiles rather than one for the whole layer, so a large layer
		never needs one contiguous run of a heap that levels and menus have fragmented.

		The sprite layer keeps the full @ref LayerTile (12 bytes: the destructible, suspend and parameter
		fields the collision code writes). Every other layer is only ever drawn --- all it needs is the
		tile, its flips and its alpha --- and where @ref CompactDrawOnlyLayers says so it stores four bytes
		a tile instead: the 1023x80 background of a Christmas level is 327 KB that way instead of 982 KB,
		on a console with 5.8 MB of heap for everything.

		A compact layer is still read as a @ref LayerTile --- @ref operator[]() decodes into one of a few
		scratch tiles, so a returned reference stays valid for the next few lookups, which is longer than
		any reader here holds one --- and it is written through @ref Set(), which is the only way to change
		a compact tile.

		A sparse layout allocates a block only once a tile that isn't empty is written into it, every other
		block is one block of empty tiles all sparse layouts share --- a level of the original game has an
		empty layer in five levels of six, which the converter keeps (JJ2+ has the layers without tiles too,
		scripts refer to them), up to 330000 tiles of them in a level. Only a layer whose every writer goes
		through @ref Set() can be sparse, the shared block must never be written through @ref operator[]().
	*/
	class LayerLayout {
	public:
		/** @brief Base-2 logarithm of @ref BlockTiles */
		static constexpr std::size_t BlockShift = 12;
		/**
			@brief Whether the layers that are only drawn take the four-byte form

			Enabled only where RAM is the binding constraint, because the compact form trades the lifetime
			of a returned reference for the memory (see the class description); the platforms with memory
			to spare keep the plain array semantics.
		*/
#if defined(DEATH_TARGET_N64) || defined(DEATH_TARGET_PSP) || defined(DEATH_TARGET_DREAMCAST) || \
		defined(DEATH_TARGET_3DS) || defined(DEATH_TARGET_PS2)
		static constexpr bool CompactDrawOnlyLayers = true;
#else
		static constexpr bool CompactDrawOnlyLayers = false;
#endif
		/** @brief Tiles per allocated block */
		static constexpr std::size_t BlockTiles = std::size_t(1) << BlockShift;

		LayerLayout() = default;
		~LayerLayout();

		LayerLayout(const LayerLayout&) = delete;
		LayerLayout& operator=(const LayerLayout&) = delete;
		LayerLayout(LayerLayout&& other) noexcept;
		LayerLayout& operator=(LayerLayout&& other) noexcept;

		/**
		 * @brief Allocates @p count empty tiles, in the compact form if @p compact, returning `false` if the memory is not there
		 *
		 * An empty tile is the one a level stores as tile 0 without any flags (see @ref TileMap::MakeLayerTile()). With
		 * @p sparse the blocks are allocated only as tiles that aren't empty are written into them (see the class description).
		 */
		bool Allocate(std::size_t count, bool compact, bool sparse = false);
		/** @brief Returns the number of tiles */
		std::size_t size() const { return _count; }
		/** @brief Returns `true` if no tiles are allocated */
		bool empty() const { return _count == 0; }
		/** @brief Returns `true` if the tiles are stored in the four-byte form */
		bool IsCompact() const { return _compact; }
		/** @brief Returns `true` if any tile has an ID other than 0, which is quick for the blocks of a sparse layout never written */
		bool HasAnyTile() const;
		/**
			@brief Returns a stamp that changes with every write through @ref Set() or @ref Allocate()

			Those are the only writers of the compact form, so an unchanged stamp proves that none of its tiles
			changed. The full form can still be written through the references @ref operator[]() hands out, which
			the stamp cannot see.
		*/
		std::uint32_t GetVersion() const { return _version; }

		/**
			@brief Returns the tile at @p i

			Like the array it replaces, a const layout still hands out mutable tiles --- but writing
			through the returned reference reaches the layer only in the full form, so every writer goes
			through @ref Set() instead.
		*/
		LayerTile& operator[](std::size_t i) const {
			if (!_compact) {
				return _full[i >> BlockShift][i & (BlockTiles - 1)];
			}
			LayerTile& tile = _scratch[_scratchNext++ & (ScratchCount - 1)];
			Decode(_packed[i >> BlockShift][i & (BlockTiles - 1)], tile);
			return tile;
		}
		/**
			@brief Returns the ID of the tile at @p i

			The same value as `operator[](i).TileID`, without decoding the rest of a compact tile into a scratch
			one first - for a reader that compares only the IDs of a whole window of tiles every frame.
		*/
		std::uint16_t TileIDAt(std::size_t i) const {
			if (!_compact) {
				return _full[i >> BlockShift][i & (BlockTiles - 1)].TileID;
			}
			return std::uint16_t(_packed[i >> BlockShift][i & (BlockTiles - 1)]);
		}
		/** @brief Replaces the tile at @p i, in either form */
		void Set(std::size_t i, const LayerTile& tile) {
			_version++;
			if (!_compact) {
				LayerTile* block = _full[i >> BlockShift];
				if (block == _sharedFull && !(block = OwnFullBlock(i >> BlockShift, tile))) {
					return;
				}
				block[i & (BlockTiles - 1)] = tile;
			} else {
				std::uint32_t* block = _packed[i >> BlockShift];
				const std::uint32_t packed = Encode(tile);
				if (block == _sharedPacked && !(block = OwnPackedBlock(i >> BlockShift, packed))) {
					return;
				}
				block[i & (BlockTiles - 1)] = packed;
			}
		}

	private:
		static constexpr std::uint32_t ScratchCount = 8;

		// The blocks of empty tiles all sparse layouts share, made on first use and kept for the whole run
		static LayerTile* _sharedFull;
		static std::uint32_t* _sharedPacked;

		// Gives a shared block of a sparse layout a block of its own before a tile is written into it, returns nullptr if the
		// tile is empty (the block stays shared, there's nothing to write) or the memory isn't there
		LayerTile* OwnFullBlock(std::size_t blockIndex, const LayerTile& tile);
		std::uint32_t* OwnPackedBlock(std::size_t blockIndex, std::uint32_t packed);
		void Free();

		static std::uint32_t Encode(const LayerTile& tile) {
			return std::uint32_t(tile.TileID) | (std::uint32_t(std::uint8_t(tile.Flags)) << 16) | (std::uint32_t(tile.Alpha) << 24);
		}
		static void Decode(std::uint32_t packed, LayerTile& tile) {
			tile = LayerTile {};
			tile.TileID = std::uint16_t(packed);
			tile.Flags = (LayerTileFlags)std::uint8_t(packed >> 16);
			tile.Alpha = std::uint8_t(packed >> 24);
			tile.DestructAnimation = -1;
		}

		bool _compact = false;
		std::uint32_t _version = 0;
		// Blocks of their own (owned) or the shared one of empty tiles (see Allocate())
		SmallVector<LayerTile*, 0> _full;
		SmallVector<std::uint32_t*, 0> _packed;
		std::size_t _count = 0;
		mutable LayerTile _scratch[ScratchCount] = {};
		mutable std::uint32_t _scratchNext = 0;
	};

	/**
		@brief Represents a single tile map layer
		
		Bundles a layer's grid of @ref LayerTile entries with its dimensions, its @ref LayerDescription and a
		visibility flag. A @ref TileMap owns an ordered list of these layers, of which one is the main sprite layer
		used for collision while the rest provide foreground and parallax background detail.
	*/
	struct TileMapLayer {
		/** @brief Layer layout */
		LayerLayout Layout;
		/** @brief Layer layout size */
		Vector2i LayoutSize;
		/** @brief Layer description */
		LayerDescription Description;
		/** @brief Layer visibility */
		bool Visible;
		/**
		 * @brief Texture of its own of a textured layer, palette indices row by row, or empty
		 *
		 * @ref Formats::TexturedLayerChunk::TextureSize pixels wide and high, taken instead of the tiles at the top
		 * left of the layer (see @ref Formats::TexturedLayerChunk::Entry::Texture).
		 */
		SmallVector<std::uint8_t, 0> TexturePixels;
		/** @brief Changes with @ref TexturePixels, unique among the layers, so the texture made of them is made again */
		std::uint32_t TextureRevision = 0;
		/**
		 * @brief How far the texture of a reflection drawn only where the layer is has moved by its inner auto speeds, in its pixels
		 *
		 * For a wave, X is how far its waves have moved, in periods, from 0 to 1.
		 */
		Vector2f TextureOffset;
		/** @brief Name of the layer, which scripts may find it by, or empty (see @ref Formats::LayerNamesChunk) */
		String Name;
		/**
		 * @brief Number of the layer in the original game, 1 to 8 from the front, or 0 for a layer of another origin
		 *
		 * Scripts of JJ2+ refer to the layers by it (see @ref Formats::LayerNumbersChunk).
		 */
		std::uint8_t OriginalNumber = 0;
	};

	/**
		@brief Represents a single frame of an animated tile
		
		One entry in the frame sequence of an @ref AnimatedTile, referencing the static tile in the tile set that is
		displayed while this frame is active.
	*/
	struct AnimatedTileFrame {
		/** @brief Tile ID */
		std::uint16_t TileID;
		/** @brief Only @ref LayerTileFlags::Heat of the frame, the other flags of frames aren't applied */
		LayerTileFlags Flags = LayerTileFlags::None;
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
		@brief How the layers of a tile map are placed relative to the view
	*/
	enum class LayerPlacement : std::uint8_t
	{
		/** @brief As in the game, by the view that is drawn */
		Gameplay,
		/**
			@brief As in the game, by a view of @ref TileMapViewOptions::ReferenceViewSize at the same center

			The layers sit where the game would put them for the view of a player at the center, and are extended across
			a view of another size --- the level editor zoomed out shows more of every layer, not layers moved apart.
		*/
		ReferenceView
	};

	/** @brief An image drawn in place of the tiles of a layer, see @ref ITileMapLayerOverride */
	struct TileMapLayerImage
	{
		/** @brief Image, not indexed */
		Texture* Image;
		/** @brief Where it's drawn in the level */
		Rectf Rect;
		/** @brief Part of the image drawn there, from its top left corner, all of it by default */
		Vector2f TexScale = Vector2f(1.0f, 1.0f);
	};

	/**
		@brief Draws images in place of the tiles of layers, as the level editor does for a view from far away
	*/
	class ITileMapLayerOverride
	{
	public:
		virtual ~ITileMapLayerOverride() = default;

		/**
		 * @brief Called for each visible tile layer that is drawn, with where the tile (0, 0) of it is in the level
		 *
		 * Returns `true` to draw @p images at the depth of the layer instead of its tiles. The tiles of @p tileAreas
		 * are drawn as usual with them, where there's no image yet --- in tiles of the layer from its tile (0, 0),
		 * past its size where it repeats, not overlapping any image.
		 */
		virtual bool OverrideLayer(std::int32_t layerIndex, Vector2f origin, const Rectf& cullingRect, SmallVectorImpl<TileMapLayerImage>& images,
			SmallVectorImpl<Recti>& tileAreas) = 0;
	};

	/**
		@brief Options of how a tile map is shown

		The defaults show it as the game does. The level editor changes them to look at a level in other ways.
	*/
	struct TileMapViewOptions
	{
		/** @brief Draws images in place of the tiles of layers, not owned */
		ITileMapLayerOverride* LayerOverride = nullptr;
		/** @brief How the layers are placed relative to the view */
		LayerPlacement Placement = LayerPlacement::Gameplay;
		/** @brief Size of the view the layers are placed by, see @ref LayerPlacement::ReferenceView */
		Vector2i ReferenceViewSize;
		/**
			@brief How far the layers are aligned with the sprite layer instead, from 0 to 1

			At 1 the tile (0, 0) of every layer is at the world position (0, 0), whatever its speed and offset, so each
			layer can be edited in the same grid as the sprite layer.
		*/
		float FlatAmount = 0.0f;
		/** @brief Whether black bars are drawn at the sides of a level narrower than the view */
		bool DrawSideBars = true;
		/** @brief Whether layers with an automatic speed stay where their offset puts them, instead of moving */
		bool PauseLayerMovement = false;
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
		static constexpr std::int32_t HardcodedOffset = LayerDescription::HardcodedOffset;

		/** @brief Mask of the tile index inside a packed tile value (see @ref GetTile()) */
		static constexpr std::uint16_t TileIndexMask = 0x0FFF;
		/** @brief Flag of a packed tile value that is flipped horizontally */
		static constexpr std::uint16_t TileFlagFlipX = 0x1000;
		/** @brief Flag of a packed tile value that is flipped vertically */
		static constexpr std::uint16_t TileFlagFlipY = 0x2000;
		/** @brief Flag of a packed tile value that refers to an animated tile (index is relative to the first animated tile) */
		static constexpr std::uint16_t TileFlagAnimated = 0x4000;
		/** @brief Alpha of a tile the file marks as translucent, anything that shows the tiles otherwise uses it too */
		static constexpr std::uint8_t TranslucentTileAlpha = 192;

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
		/**
			@brief Returns suspend state of the closest attachment point around a given position

			Searches the whole tolerance box around `[x, y]` instead of a single point, so that vines and
			hooks spanning only one tile are not too hard to grab. The lowest reachable row wins, and inside
			that row the column closest to `[x, y]` - `snapOffset` receives the offset that needs to be
			applied to the given position to align it with the returned attachment point.

			Either vertical tolerance can be negative to move that edge of the box past `y` to the other side.
		*/
		SuspendType GetTileSuspendState(float x, float y, float toleranceX, float toleranceUp, float toleranceDown, Vector2f& snapOffset);
		/** @brief Advances descructible animation of a given tile */
		bool AdvanceDestructibleTileAnimation(std::int32_t tx, std::int32_t ty, std::int32_t amount);

		/** @brief Adds an additional tile set as a continuation of the previous one */
		void AddTileSet(StringView tileSetPath, std::uint16_t offset, std::uint16_t count, const std::uint8_t* paletteRemapping = nullptr);
		/** @brief Adds an additional tile set read from a stream, as one stored in the level, as a continuation of the previous one */
		void AddTileSet(Stream& s, StringView name, std::uint16_t offset, std::uint16_t count, const std::uint8_t* paletteRemapping = nullptr);
		/**
		 * @brief Adds tiles of a tile set after the tiles of the others while the level is played
		 *
		 * The tiles from @p offset of the tile set get the tile IDs from @ref GetLoadedTileCount() on. Returns `false`
		 * and adds nothing if they don't fit below the animated tiles or the tile set can't be loaded. Meant for scripts.
		 * With @p trueColorPalette, the 8-bit tiles are true color tiles in its 256 colors (see
		 * @ref ContentResolver::RequestTileSet()).
		 */
		bool AppendTileSet(StringView tileSetPath, std::uint16_t offset, std::uint16_t count, const std::uint8_t* paletteRemapping = nullptr,
			const std::uint32_t* trueColorPalette = nullptr);
		/** @brief Reads the next layer of a level, its description and its tiles, and adds it */
		void LoadLayer(Formats::LevelReader& reader);
		/**
		 * @brief Adds a layer of no tiles after the others, which isn't drawn, returns its index or `-1` if the memory isn't there
		 *
		 * A layer drawn other than tile by tile (see @ref LayerDescription::IsSpriteLayerRenderer()) is drawn as ordinary
		 * tiles, the textured background, the x-ray, the blur and the reflection are only the layers of the level. Its tiles
		 * are written through @ref SetTile(). Meant for scripts, which make layers of their own.
		 */
		std::int32_t AddLayer(Vector2i size, const LayerDescription& description);
		/** @brief Replaces a layer added by @ref AddLayer() with one of no tiles, which isn't drawn */
		bool ResetLayer(std::int32_t layerIndex, Vector2i size, const LayerDescription& description);
		/** @brief Whether reading the layers ran out of memory (the level cannot be played) */
		bool HasLoadFailed() const { return _loadFailed; }
		/**
		 * @brief Sets the animated tiles of the level
		 *
		 * Tile IDs from @p animatedTilesOffset up refer to them, in the order they are given. An animated tile without
		 * frames shows the empty tile, so the ones after it keep their IDs.
		 */
		void SetAnimatedTiles(std::uint16_t animatedTilesOffset, ArrayView<const Formats::AnimatedTileRecord> animatedTiles);
		/** @brief Draws the textured background of the level again, after its tiles changed */
		void InvalidateTexturedBackground();
		/** @brief Returns the number of animated tiles */
		std::int32_t GetAnimatedTileCount() const {
			return std::int32_t(_animatedTiles.size());
		}
		/** @brief Changes the timing and the frames of an animated tile, it goes on playing from where it is */
		void SetAnimatedTile(std::int32_t index, const Formats::AnimatedTileRecord& animatedTile);
		/**
		 * @brief Makes a layer tile from a tile of a level file
		 *
		 * The flags carry the flips in the low nibble and a modifier in the high one (see @ref Formats::TileRecord).
		 */
		static LayerTile MakeLayerTile(std::uint8_t fileFlags, std::uint16_t tileId);
		/** @brief Sets tile event flags */
		void SetTileEventFlags(std::int32_t x, std::int32_t y, EventType tileEvent, std::uint8_t* tileParams);
		/** @brief Overrides the diffuse texture of the specified tile */
		bool OverrideTileDiffuse(std::int32_t tileId, StaticArrayView<TileSet::PaddedTileSize * TileSet::PaddedTileSize, std::uint32_t> tileDiffuse);
		/**
		 * @brief Returns `true` if the tileset containing the given tile stores indexed (palette) diffuse
		 *
		 * When indexed, an overridden tile must be supplied as palette indices (red channel) rather than baked colors.
		 */
		bool IsTileSetIndexed(std::int32_t tileId);
		/** @brief Overrides the collision mask of the specified tile */
		bool OverrideTileMask(std::int32_t tileId, StaticArrayView<TileSet::DefaultTileSize * TileSet::DefaultTileSize, const std::uint8_t> tileMask);

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
#if defined(DEATH_TARGET_DREAMCAST) || defined(DEATH_TARGET_WII) || defined(DEATH_TARGET_GAMECUBE) || \
		defined(DEATH_TARGET_3DS) || defined(DEATH_TARGET_AMIGAOS)
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
#if defined(DEATH_TARGET_DREAMCAST) || defined(DEATH_TARGET_WII) || defined(DEATH_TARGET_GAMECUBE) || \
		defined(DEATH_TARGET_3DS) || defined(DEATH_TARGET_AMIGAOS)
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
#if defined(DEATH_TARGET_DREAMCAST) || defined(DEATH_TARGET_WII) || defined(DEATH_TARGET_GAMECUBE) || \
		defined(DEATH_TARGET_3DS) || defined(DEATH_TARGET_AMIGAOS)
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

		/**
		 * @brief Creates a generic debris
		 *
		 * All of the debris producers honour @ref PreferencesCache::Particles: nothing is created when
		 * particles are off, and the bursts (@ref CreateParticleDebris(), @ref CreateSpriteDebris()) are
		 * halved at the low quality. The console budgets above apply on top of that.
		 */
		void CreateDebris(const DestructibleDebris& debris);
		/** @brief Creates a tile debris */
		void CreateTileDebris(std::int32_t tileId, std::int32_t x, std::int32_t y);
		/** @brief Creates a particle debris from a sprite */
		void CreateParticleDebris(const GraphicResource* res, Vector3f pos, Vector2f force, std::int32_t currentFrame, bool isFacingLeft);
		/**
		 * @brief Creates a sprite debris
		 *
		 * The pieces keep @p elasticity of their speed when they bounce off a solid tile, see
		 * @ref DestructibleDebris::Elasticity.
		 */
		void CreateSpriteDebris(const GraphicResource* res, Vector3f pos, std::int32_t count, float elasticity = 0.8f);

		/** @brief Returns state of a given trigger */
		bool GetTrigger(std::uint8_t triggerId);
		/** @brief Sets state of a given trigger */
		void SetTrigger(std::uint8_t triggerId, bool newState);

		/** @brief Returns number of layers */
		std::int32_t GetLayerCount() const {
			return (std::int32_t)_layers.size();
		}
		/** @brief Returns the options of how the tile map is shown */
		const TileMapViewOptions& GetViewOptions() const {
			return _viewOptions;
		}
		/** @brief Sets the options of how the tile map is shown, the defaults show it as the game does */
		void SetViewOptions(const TileMapViewOptions& options) {
			_viewOptions = options;
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

		/** @brief Returns the renderer type of a given layer, or @ref LayerRendererType::Default if the layer doesn't exist */
		LayerRendererType GetLayerRendererType(std::int32_t layerIndex) const;
		/**
			@brief Changes the renderer type of a given layer

			Meant for tools such as an editor. A layer switched to @ref LayerRendererType::XRay,
			@ref LayerRendererType::BlurBehind or @ref LayerRendererType::Reflection can need render passes the level
			didn't need so far, which the owner is told about (see @ref ITileMapOwner::OnLayerEffectsChanged()). Where
			the backend can't show those layers, they are reduced as when a level is loaded, an x-ray layer
			irreversibly. Returns `false` if the layer doesn't exist or is the sprite layer.
		*/
		bool SetLayerRendererType(std::int32_t layerIndex, LayerRendererType type);

		/** @brief Makes the description of a layer from a layer of a level file */
		static LayerDescription MakeLayerDescription(const Formats::LayerRecord& record);
		/**
		 * @brief Replaces a tile of a layer, as if it was loaded so from the level file
		 *
		 * Unlike @ref SetTile(), nothing of the replaced tile is kept --- neither the state it had in the game, nor
		 * what the events of the level made of it. Meant for the level editor. Returns `false` if out of range.
		 */
		bool ReplaceLayerTile(std::int32_t layerIndex, std::int32_t x, std::int32_t y, const LayerTile& tile);
		/** @brief Returns the description of a layer, or `nullptr` if the layer doesn't exist */
		const LayerDescription* GetLayerDescription(std::int32_t layerIndex) const;
		/**
		 * @brief Changes how a layer is placed and drawn, and whether it's drawn at all
		 *
		 * The renderer type is changed as by @ref SetLayerRendererType(). A layer that becomes a @ref LayerRendererType::Sky,
		 * a @ref LayerRendererType::Tunnel, a @ref LayerRendererType::Cylinder or a @ref LayerRendererType::Reflection needs
		 * the viewport to be initialized again.
		 * The description of the sprite layer can't be changed, only how it's drawn by @ref SetSpriteLayerRenderer().
		 * Meant for the level editor. Returns `false` if the layer doesn't exist.
		 */
		bool SetLayerDescription(std::int32_t layerIndex, const LayerDescription& description, bool visible);
		/**
		 * @brief Gives a textured layer a texture of its own, taken instead of the tiles at its top left
		 *
		 * @p pixels are @ref Formats::TexturedLayerChunk::TextureSize × @ref Formats::TexturedLayerChunk::TextureSize
		 * palette indices row by row, they stay indexed, so the texture follows the palette as the tiles do. Empty
		 * @p pixels take the tiles again. Called before the viewport is initialized, as when the level is loaded, the
		 * texture is drawn of its own size. Returns `false` if the layer doesn't exist or the pixels are of another size.
		 */
		bool SetLayerTexture(std::int32_t layerIndex, ArrayView<const std::uint8_t> pixels);
		/** @brief Returns the texture of its own of a layer (see @ref SetLayerTexture()), empty if it has none or the layer doesn't exist */
		ArrayView<const std::uint8_t> GetLayerTexture(std::int32_t layerIndex) const {
			return (layerIndex >= 0 && layerIndex < (std::int32_t)_layers.size() ? ArrayView<const std::uint8_t>(_layers[layerIndex].TexturePixels) : nullptr);
		}
		/**
		 * @brief Changes how the sprite layer is drawn, it's still placed as always
		 *
		 * Only a renderer type the sprite layer can be drawn with is taken (see
		 * @ref LayerDescription::IsSpriteLayerRenderer()), with any other one the layer is drawn as ordinary tiles. The
		 * color means what it does for any other layer. Set by the loader from @ref Formats::SpriteLayerChunk, and by the
		 * level editor. Returns `false` if there's no sprite layer.
		 */
		bool SetSpriteLayerRenderer(LayerRendererType type, const Vector4f& color);
		/**
		 * @brief Puts the layers in another order, as they are with all their state
		 *
		 * @p order has the index each layer had before, for each new index, and has to have every index once. Only the
		 * order of the layers changes, the depths they're drawn at stay. Meant for the level editor. Returns `false`
		 * and changes nothing if @p order isn't such a list.
		 */
		bool ReorderLayers(ArrayView<const std::int32_t> order);
		/** @brief Returns `true` if a layer is drawn */
		bool IsLayerVisible(std::int32_t layerIndex) const;
		/** @brief Returns the name of a layer (see @ref TileMapLayer::Name), empty if it has none or the layer doesn't exist */
		StringView GetLayerName(std::int32_t layerIndex) const;
		/** @brief Returns the number of a layer in the original game (see @ref TileMapLayer::OriginalNumber), 0 if it has none */
		std::uint8_t GetLayerOriginalNumber(std::int32_t layerIndex) const;
		/** @brief Sets the name and the number in the original game of a layer, which only scripts use */
		void SetLayerIdentity(std::int32_t layerIndex, StringView name, std::uint8_t originalNumber);
		/**
		 * @brief Returns the texture and texture coordinates a tile is drawn with
		 *
		 * The tile ID is as the level file stores it, an animated tile resolves to the frame it shows now. The
		 * texture coordinates are in the layout the sprite shaders take (horizontal scale and offset, then vertical).
		 * An indexed texture keeps palette indices, which are recolored by the first palette. Returns `false` if
		 * the tile doesn't exist.
		 */
		bool GetTileQuad(std::int32_t tileId, LayerTileFlags flags, Texture*& texture, Vector4f& texCoords, bool& indexed);
		/**
		 * @brief Returns the texture and texture coordinates a tile of the tile sets is drawn with
		 *
		 * As @ref GetTileQuad(), but the tile ID always counts the tiles of the default tile set and then the extra ones,
		 * even past the first animated tile, so a tile the level can't address yet is drawn as itself, never as an
		 * animated tile. Returns `false` if no tile set has the tile.
		 */
		bool GetStaticTileQuad(std::int32_t tileId, LayerTileFlags flags, Texture*& texture, Vector4f& texCoords, bool& indexed);
		/**
		 * @brief Returns where the tile (0, 0) of a layer is drawn in the level, for a view of a given center and size
		 *
		 * The layer is placed as by the current view options, a position in the level then lies on the tile
		 * @cpp (position - origin) / 32 @ce of the layer, repeated if the layer repeats.
		 */
		Vector2f GetLayerOrigin(std::int32_t layerIndex, Vector2f viewCenter, Vector2f viewSize) const;
		/** @brief Returns where the tile (0, 0) of a layer is drawn in the level, as the game places it for a view, whatever the view options */
		Vector2f GetGameplayLayerOrigin(std::int32_t layerIndex, const Rectf& view) const;
		/** @brief Returns the number of tile sets, the default one and the extra ones */
		std::int32_t GetTileSetCount() const;
		/** @brief Returns the number of tiles taken from a tile set */
		std::int32_t GetTileSetTileCount(std::int32_t index) const;
		/** @brief Returns the number of tiles of all tile sets, which is the first tile ID of the animated tiles */
		std::int32_t GetStaticTileCount() const {
			return std::int32_t(_animatedTilesOffset);
		}
		/** @brief Returns the number of tiles the tile sets give, the IDs from it up to @ref GetStaticTileCount() are empty tiles */
		std::int32_t GetLoadedTileCount() const;
		/** @brief Returns the tile an animated tile shows now, or the tile itself if it isn't animated */
		std::int32_t ResolveStaticTileId(std::int32_t tileId) const;
		/** @brief Returns the packed collision mask of a tile that isn't animated (@ref TileSet::MaskBytesPerTile bytes), or `nullptr` */
		const std::uint8_t* GetStaticTileMask(std::int32_t tileId);

#if defined(TILEMAP_LAYER_EFFECTS) || defined(DOXYGEN_GENERATING_OUTPUT)
		/** @brief Returns `true` if any layer is a @ref LayerRendererType::XRay layer */
		bool HasXRayLayers() const;
		/** @brief Returns `true` if any layer is a @ref LayerRendererType::BlurBehind layer */
		bool HasBlurBehindLayers() const;
		/** @brief Returns `true` if a @ref LayerRendererType::BlurBehind layer has a tile that reflects or displaces, which needs a sharp copy of the scene behind it */
		bool BlurBehindLayersNeedSharpCopy();
		/** @brief Returns `true` if any layer is a @ref LayerRendererType::Reflection layer, which needs a sharp copy of the scene behind it */
		bool HasReflectionLayers() const;
		/** @brief Returns `true` if any layer is a @ref LayerRendererType::ChromaKey layer or a textured one drawn translucent or keyed (see @ref TexturedLayerBlend), which needs a sharp copy of the scene behind it */
		bool HasChromaKeyLayers() const;
#endif

		/** @brief Creates a checkpoint for eventual rollback */
		void CreateCheckpointForRollback();
		/** @brief Rolls back to the last checkpoint */
		void RollbackToCheckpoint();

		/**
		 * @brief Initializes tile map state from a stream
		 *
		 * Returns `false` if the state belongs to a sprite layer of another size, such as of a level changed since,
		 * nothing is changed then and the rest of the state in the stream can't be read either.
		 */
		bool InitializeFromStream(Stream& src);
		/** @brief Serializes tile map state to a stream */
		void SerializeResumableToStream(Stream& dest, bool fromCheckpoint = false);
		/** @brief Restores complete live state of the sprite layer (including the last checkpoint) from a snapshot */
		bool InitializeSnapshotFromStream(Stream& src);
		/** @brief Serializes complete live state of the sprite layer (including the last checkpoint) to a snapshot */
		void SerializeSnapshotToStream(Stream& dest);

		/**
			@brief Repacks the tileset's atlas around the tiles this level actually references

			Called once the layers, animated tiles and events have been read, which is the first moment the set
			of referenced tiles is known. Does nothing where the platform can hold the whole atlas.
		*/
		void PruneTilesetAtlas();

		/** @brief Called when the viewport needs to be initialized (e.g., when the resolution is changed) */
		void OnInitializeViewport();

	private:
#ifndef DOXYGEN_GENERATING_OUTPUT
		// Doxygen 1.12.0 outputs also private structs/unions even if it shouldn't
		struct TileSetPart {
			std::unique_ptr<TileSet> Data;
			std::int32_t Offset;
			std::int32_t Count;
			// First mesh slot of the texture chunks of its atlas, see _meshSlots
			std::int32_t MeshSlotBase = 0;
			// Stored in the level itself, its name isn't a file in "Tilesets/"
			bool Embedded = false;
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

		// Renders the tiles at the top left of a layer, or its texture of its own, into a texture. With `drawsIndices`, the
		// palette indices of the indexed tiles are rendered instead of their colors, as the ripples of a reflection take
		// them. With `source`, the tiles of that area of the layer are rendered instead, never a texture of its own.
		class TexturedBackgroundPass : public SceneNode
		{
			friend class TileMap;

		public:
			// With `withAlpha`, the texture keeps where the tiles are transparent, as a tile menu needs it
			TexturedBackgroundPass(TileMap* owner, std::int32_t layerIndex, bool drawsIndices, const Recti& source = {}, bool withAlpha = false)
				: _owner(owner), _layerIndex(layerIndex), _source(source), _imageRevision(0), _drawsIndices(drawsIndices), _withAlpha(withAlpha),
					_alreadyRendered(false)
			{
			}
			~TexturedBackgroundPass() override;

			void Initialize();
			// Draws the texture again, after the tiles of the layer changed
			void Invalidate();

			bool OnDraw(RenderQueue& renderQueue) override;

		private:
			TileMap* _owner;
			std::int32_t _layerIndex;
			Recti _source;
			std::unique_ptr<Texture> _target;
			std::unique_ptr<Viewport> _view;
			std::unique_ptr<Camera> _camera;
			SmallVector<std::unique_ptr<RenderCommand>, 0> _renderCommands;
			// The texture of its own of the layer, still indexed, and the revision of the pixels it was made of
			std::unique_ptr<Texture> _image;
			std::uint32_t _imageRevision;
			bool _drawsIndices;
			bool _withAlpha;
			bool _alreadyRendered;

			// The size of the texture the pass renders
			Vector2i GetTargetSize() const;
		};
#endif

		ITileMapOwner* _owner;
		std::int32_t _sprLayerIndex;
		bool _loadFailed = false;
		TileMapViewOptions _viewOptions;
		// Images drawn in place of the layer being drawn, see TileMapViewOptions::LayerOverride, and the areas of it drawn
		// as tiles with them
		SmallVector<TileMapLayerImage, 0> _layerImages;
		SmallVector<Recti, 0> _layerTileAreas;
		PitType _pitType;

		SmallVector<TileSetPart, 2> _tileSets;
		SmallVector<TileMapLayer, 0> _layers;
		SmallVector<RollbackTile, 0> _sprLayerForRollback;
		/// Whether a checkpoint was ever taken. Not implied by @ref _sprLayerForRollback being non-empty --- a
		/// level without a single destructible tile has an empty (but valid) checkpoint.
		bool _hasRollbackCheckpoint;
		SmallVector<AnimatedTile, 0> _animatedTiles;
		// Per animation of the level itself, the one derived from it for x-ray layers (see GetXRayUnmaskedAnimation()),
		// `0` if none was derived. Empty until the first one is
		SmallVector<std::int32_t, 0> _xrayUnmaskedAnims;
		SmallVector<Vector2i, 0> _activeCollapsingTiles;
		float _collapsingTimer;
		std::uint32_t _animatedTilesOffset;
		// Kept so the atlas can be repacked once the level's tile usage is known (see PruneTilesetAtlas())
		String _tileSetPath;
		std::uint16_t _captionTileId;
		// Whether any tile's graphics or mask was overridden (MLLE) - such an atlas must not be repacked,
		// the rebuild would silently revert the overrides
		bool _tilesOverridden;
		// Whether the main tileset was loaded without its atlas (masks only), because the atlas is built once
		// the level's tile usage is known (see PruneTilesetAtlas())
		bool _diffuseDeferred;
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
		/// A row of the shared palette that the palette-shifted layers are drawn through, and the frame it was last used in
		struct ShiftedPaletteRow
		{
			std::int32_t PaletteOffset;
			std::uint32_t LastUsedFrame;
			std::uint8_t Shift;
		};
		SmallVector<ShiftedPaletteRow, 0> _shiftedPaletteRows;

#if defined(TILEMAP_USE_SINGLE_DRAW)
		// Per-frame pools for aggregated meshes, replacing the per-tile and per-particle commands. One vertex buffer
		// is filled per drawn tile layer and per debris group; each mesh is then split into chunks that individually
		// fit the shared array buffer limit (64 KB), so a mesh emits one command per chunk (usually just one). Both
		// pools grow on demand and reset in OnEndFrame(); host vertex pointers reference the buffers until the render
		// queue is flushed, so a buffer is never reused within a frame (across viewports the counts simply keep growing).
		SmallVector<SmallVector<float, 0>, 0> _meshVertices;
		SmallVector<std::unique_ptr<RenderCommand>, 0> _meshCommands;

		/// One texture chunk of the atlas of a tile set. The chunks of all tile sets are numbered one after another, the
		/// first one of each at its @ref TileSetPart::MeshSlotBase, a layer mesh has a vertex buffer for each it uses.
		struct MeshSlot
		{
			TileSet* Set;
			std::int32_t Chunk;

			Texture* GetTexture() const {
				return Set->TextureDiffuse[Chunk].get();
			}
		};
		SmallVector<MeshSlot, 8> _meshSlots;

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
		/// the atlas slot its tiles sample instead of in screen order (see @ref DrawLayer()). Everything the
		/// RDP's packed tile record takes (see `RHI::RDP::TileRecord`) plus the grouping key, so nothing has to
		/// be recomputed - the two integer divisions the texel position costs would run again per tile otherwise.
		struct MeshTileEntry
		{
			float X, Y;
			std::uint16_t TexX, TexY;	//< Top-left texel of the tile in its atlas chunk
			std::uint8_t Alpha;			//< The tile's own alpha (LayerTile::Alpha)
			std::uint8_t Flags;			//< `RHI::RDP::TileRecord::FlipX` / `FlipY`
			std::uint16_t Slot;			//< Packed atlas slot the tile samples, the grouping key
			std::uint16_t Chunk;		//< Atlas chunk (texture) that slot lives in
			std::uint16_t Xo, Yo;		//< Cell of the layer's window the tile sits in (see LayerMeshCache)
			std::int32_t LayoutIndex;	//< Index of the tile in the layer's layout
			bool Animated;				//< An animated tile: never cached, resolved every frame
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

		void EmitTileRuns(RenderQueue& renderQueue, const Texture& texture, bool indexed, const Vector4f& color,
			std::uint16_t depth, std::uint32_t firstEntry, std::uint32_t entryCount);
#	endif
#endif

		// The texture a textured layer or a reflection layer is drawn with, or one of the two parts of the sprite layer a tile menu
		// takes (see LayerRendererType::TileMenu), made only for the layers that need one (see UpdateTexturedLayers())
		struct LayerPass {
			std::int32_t LayerIndex;
			Recti Source;
			bool DrawsIndices;
			bool WithAlpha;
			std::unique_ptr<TexturedBackgroundPass> Pass;
		};
		SmallVector<LayerPass, 0> _layerPasses;
		// Whether a layer is keyed (see HasChromaKeyLayers()), the colors of the textured layers have to stay exact then, so
		// they're neither dithered nor filtered (see UpdateTexturedLayers())
		bool _hasKeyedLayers = false;
		// The last revision given to a texture of its own of a layer, see TileMapLayer::TextureRevision
		std::uint32_t _lastTextureRevision = 0;
		// Whether the passes were set up with the viewports already, a layer that becomes the textured background later needs
		// them set up again (see UpdateTexturedLayers())
		bool _viewportsInitialized = false;

#if defined(TILEMAP_OPAQUE_COVERAGE)
		/**
			@brief Screen cells the sprite layer covers with fully opaque tiles this frame

			Rebuilt by DrawLayer() for the sprite layer, which OnDraw() draws first for that reason; the
			layers behind it then skip every tile whose whole screen rectangle falls on covered cells (see
			the note in DrawLayer). One bit per cell, a cell being one sprite-layer tile on screen - a
			640x480 view walks 23x18 of them.
		*/
		struct OpaqueCoverage {
			static constexpr std::int32_t MaxCols = 32;
			static constexpr std::int32_t MaxRows = 24;
			std::int32_t OriginX, OriginY;		//< Screen position of cell (0, 0)
			std::int32_t Cols, Rows;
			std::uint16_t Depth;				//< Depth of the covering layer; only layers behind it are culled
			bool Valid;
			std::uint32_t RowBits[MaxRows];
		};
		OpaqueCoverage _opaqueCoverage = {};
#endif
#if defined(WITH_RHI_SOFTWARE) || defined(DEATH_TARGET_N64) || (defined(TILEMAP_OPAQUE_COVERAGE) && defined(WITH_ANGELSCRIPT))
		/// Whether every non-zero entry of the sprite palette is fully opaque, scanned once per OnDraw() (see DrawLayer())
		bool _spritePaletteOpaque = true;
#endif

#if defined(DEATH_TARGET_N64)
		/// Bumped whenever a rebuilt coverage grid differs from the previous one (keys the culled layers' caches)
		std::uint32_t _coverageStamp = 0;

		/**
			@brief The static tiles of one layer's window, kept between frames

			See the note in DrawLayer(): a layer's visible window changes only every 32 pixels of its own
			scroll, so its tiles are walked and their quads written once per window into a VBO the layer
			owns, and the frames in between re-issue those commands under a new translation. Animated tiles
			are excluded (recorded as cells and re-resolved every frame through the streaming path), and a
			copy of the window's tile IDs detects destroyed or triggered tiles.
		*/
		struct LayerMeshCache {
			struct AnimatedCell {
				std::uint16_t Xo, Yo;
				std::int32_t LayoutIndex;
			};
			struct Chunk {
				std::unique_ptr<RenderCommand> Command;
				std::uint32_t VboFloats = 0;			//< Capacity of the command's own VBO (written through a mapping)
				std::int32_t QuadCount = 0;
			};
			bool Valid = false;
			bool Covering = false, Culled = false, PaletteOpaque = true;
			std::int32_t TileAbsX = 0, TileAbsY = 0, Cols = 0, Rows = 0;
			std::int32_t CullShiftX = 0, CullShiftY = 0;
			std::uint32_t CoverageStamp = 0;
			std::uint32_t LayoutVersion = 0;		//< The layout's stamp (LayerLayout::GetVersion()) WindowIds were last checked against
			SmallVector<std::uint16_t, 0> WindowIds;
			SmallVector<AnimatedCell, 0> Animated;
			SmallVector<Chunk, 1> Chunks;
			OpaqueCoverage Coverage = {};
		};
		SmallVector<LayerMeshCache, 0> _layerMeshCaches;

		bool LayerMeshWindowUnchanged(const TileMapLayer& layer, const LayerMeshCache& cache, std::int32_t tileAbsX, std::int32_t tileAbsY,
			std::int32_t tileXs, std::int32_t tileYs) const;
		void BuildLayerMeshCache(LayerMeshCache& cache, TileSet& tileSet, std::int32_t x1i, std::int32_t y1i);
		void AddCachedLayerMesh(RenderQueue& renderQueue, LayerMeshCache& cache, std::int32_t x1i, std::int32_t y1i, const Vector4f& color, std::uint16_t depth);
		void PushAnimatedMeshEntries(const TileMapLayer& layer, const LayerMeshCache& cache, TileSet& tileSet, std::int32_t x1i, std::int32_t y1i);
#endif

#if defined(TILEMAP_CACHE_LAYER_WINDOW)
		/**
			@brief The static tiles of one layer's window as seen by one viewport, kept between frames

			See the note above `TILEMAP_CACHE_LAYER_WINDOW`. Keyed by the viewport too, because splitscreen
			draws every layer once per viewport, each with a window of its own. The quads are stored relative
			to the window's origin, so the frames in between only re-issue the commands under a new
			translation. A copy of the window's tiles (ID, flags and alpha) detects destroyed, triggered or
			script-placed tiles, and animated tiles are recorded as cells re-resolved every frame.
		*/
		struct LayerWindowCache {
			struct AnimatedCell {
				std::uint16_t Xo, Yo;
				std::int32_t LayoutIndex;
			};
			// A part of a chunk no longer than one draw, with a VBO of its own, a window seen from far away has more
			// quads of a chunk than one draw takes
			struct Piece {
				std::unique_ptr<RenderCommand> Command;
				std::uint32_t CapacityQuads = 0;
				std::uint32_t QuadCount = 0;
			};
			// The quads of one mesh slot, see TileMap::MeshSlot
			struct Chunk {
				SmallVector<Piece, 1> Pieces;
				// Host copy the VBOs of the pieces are uploaded from, one after another. Kept as long as the VBOs
				// themselves, because without buffer mapping a whole VBO is uploaded from it (see Geometry::CommitVertices())
				SmallVector<float, 0> Vertices;
				const Texture* DiffuseTexture = nullptr;
				std::uint32_t QuadCount = 0;
			};
			const Viewport* Owner = nullptr;
			std::int32_t LayerIndex = -1;
			std::uint32_t LastUsedFrame = 0;
			bool Valid = false;
			bool Unaligned = false;
			bool RepeatX = false, RepeatY = false;
			const TileSet* TileSetData = nullptr;
			Vector2i LayoutSize;
			std::int32_t TileAbsX = 0, TileAbsY = 0, Cols = 0, Rows = 0;
			SmallVector<std::uint32_t, 0> Window;
			SmallVector<AnimatedCell, 0> Animated;
			SmallVector<Chunk, 1> Chunks;
		};
		SmallVector<std::unique_ptr<LayerWindowCache>, 0> _layerWindowCaches;
		/// Counts frames for @ref LayerWindowCache::LastUsedFrame, advanced in @ref OnEndFrame()
		std::uint32_t _layerWindowFrame = 0;

		/** @brief Returns the window cache of a layer for the current viewport, reusing one that is no longer drawn */
		LayerWindowCache& GetLayerWindowCache(std::int32_t layerIndex);
		/** @brief Returns `true` if the tiles under the window are the ones the cache was built from */
		bool LayerWindowUnchanged(const TileMapLayer& layer, const LayerWindowCache& cache, std::int32_t tileAbsY,
			std::int32_t tileXs, std::int32_t tileYs) const;
		/** @brief Uploads the window's quads collected in @ref LayerWindowCache::Chunk::Vertices, in pieces of one draw each */
		void UploadLayerWindowCache(LayerWindowCache& cache);
		/** @brief Appends the current frame of the window's animated tiles to the streamed meshes of the mesh slots */
		void AppendAnimatedWindowTiles(const TileMapLayer& layer, const LayerWindowCache& cache,
			float originX, float originY, SmallVectorImpl<std::int32_t>& chunkVertices);

		/** @brief Packs the parts of a layer tile its appearance depends on, for @ref LayerWindowCache::Window */
		static std::uint32_t PackWindowTile(const LayerTile& tile) {
			return std::uint32_t(tile.TileID) | (std::uint32_t(std::uint8_t(tile.Flags)) << 16) | (std::uint32_t(tile.Alpha) << 24);
		}
#endif

		// The layer is placed by `placementRect` (its parallax), and the tiles that cover `cullingRect` are drawn; the two
		// are the same unless the view options say otherwise (see TileMapViewOptions)
		void DrawLayer(RenderQueue& renderQueue, TileMapLayer& layer, const Rectf& cullingRect, const Rectf& placementRect, Vector2f viewCenter);
		// Draws a textured background, a tile menu or a reflection as a whole, returns `false` if the layer is to be drawn as
		// tiles instead (where its pass or its texture isn't there), `layerOffsets` are its draw offsets
		bool DrawTexturedLayer(RenderQueue& renderQueue, TileMapLayer& layer, const Rectf& cullingRect, const Rectf& placementRect,
			Vector2f viewCenter, Vector2f layerOffsets);
		// Draws the images the view options draw in place of tiles of a layer (see TileMapViewOptions::LayerOverride), whose
		// tile (0, 0) is at `layerOrigin` of the view, returns `true` if there are any - the tiles are then drawn only in the
		// areas of `_layerTileAreas`
		bool DrawLayerImages(RenderQueue& renderQueue, const TileMapLayer& layer, LayerRendererType rendererType, const Vector4f& layerColor,
			const Rectf& cullingRect, Vector2f layerOrigin);
		// Returns the color of a tinted layer, whose description keeps a palette index in red and the strength in alpha
		static Vector4f ResolveTintColor(const Vector4f& description);
		// Returns the palette offset of the sprite palette shifted by `shift` entries for a palette-shifted layer drawn now,
		// 0 for no shift or if no row of the palette is free (see LayerRendererType::PaletteShift)
		std::int32_t AcquireShiftedPalette(std::uint8_t shift);
		// Gives back the rows of the shifted palettes no layer was drawn with in this frame or the one before, all of them with `all`
		void ReleaseShiftedPalettes(bool all);
		// With `keyed`, a textured background gets the variant of its shader that's drawn translucent or keyed (see
		// TexturedLayerBlend), if there is one
		RenderCommand* RentRenderCommand(LayerRendererType type, bool indexed = false, TileCommandUniforms** uniforms = nullptr, bool keyed = false);
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
		// Emits an accumulated mesh as one or more render commands (split into <=64 KB chunks), translated by
		// `translation` (the vertices are usually in world space already)
		void EmitMesh(RenderQueue& renderQueue, SmallVector<float, 0>& vertices, const Texture& texture, bool indexed,
			std::uint16_t paletteOffset, const Vector4f& color, std::uint16_t depth, RenderCommand::Type type, bool additiveBlending,
			Vector2f translation = Vector2f(0.0f, 0.0f));
#endif
#if defined(TILEMAP_HEAT_TILES)
		/** @brief Tiles of the heat effect of the layer being drawn, accumulated into one mesh per texture */
		struct HeatMeshGroup {
			const Texture* DiffuseTexture;
			bool Indexed;
			std::int32_t VerticesIndex;
		};

		SmallVector<HeatMeshGroup, 2> _heatMeshGroups;
		/// A pool of their own, so the commands of the ordinary layers never get the shader of the heat
		SmallVector<std::unique_ptr<RenderCommand>, 0> _heatMeshCommands;
		std::int32_t _heatMeshCommandCount = 0;
		/// Game ticks of the original (70 a second) the heat has run for, modulo a whole turn of its table (128)
		float _heatTicks = 0.0f;

		/** @brief Returns `true` if a tile is drawn with the heat effect, an animated one by the frame it shows now */
		bool IsHeatTile(const LayerTile& tile) const;
		/** @brief Appends a tile of the heat effect (resolved to a single frame) to the heat mesh, returns `false` if it can't be drawn so */
		bool AppendHeatTile(std::int32_t tileId, LayerTileFlags flags, float alpha, float x, float y);
		/** @brief Emits the heat meshes accumulated for the layer being drawn */
		void EmitHeatMeshes(RenderQueue& renderQueue, const Vector4f& color, std::uint16_t depth);
#endif
#if defined(TILEMAP_LAYER_EFFECTS)
		/** @brief What a group of the tiles of a special layer is drawn as */
		enum class EffectMeshKind : std::uint8_t {
			Unmasked,		//< The current frame of x-ray tiles outside the mask, as an ordinary layer mesh - with no mask in this frame
			XRay,			//< That frame and the mask frame of x-ray tiles, chosen per pixel by the mask (12-float vertices)
			Blur,			//< Tiles of a blur-behind layer, the amount of blur behind them
			ChromaKey		//< Tiles of a chroma key layer, drawn where the scene behind them has the key color
		};

		/** @brief Tiles of a special layer accumulated into one mesh, one per texture and kind */
		struct EffectMeshGroup {
			const Texture* DiffuseTexture;
			bool Indexed;
			EffectMeshKind Kind;
			std::int32_t VerticesIndex;
		};

		/** @brief The special layer @ref DrawLayer() is drawing, a @ref LayerRendererType::Default type otherwise */
		struct EffectLayerState {
			LayerRendererType Type = LayerRendererType::Default;
			LayerEffectsTarget* Target = nullptr;
			std::uint16_t Depth = 0;
			bool MaskAvailable = false;
			// The color of the palette a chroma key layer is drawn over
			Vector3f KeyColor;
		};

		/** @brief A column of the window of the blur layer being walked: the last row it had a reflective tile in, and that tile's mirror line */
		struct ReflectionColumn {
			std::int32_t Row;
			float AxisY;
		};

		EffectLayerState _effectLayer;
		SmallVector<EffectMeshGroup, 4> _effectMeshGroups;
		SmallVector<ReflectionColumn, 0> _reflectionColumns;
		/// A pool of their own, so the commands of the ordinary layers never see the third texture these bind
		SmallVector<std::unique_ptr<RenderCommand>, 0> _effectMeshCommands;
		std::int32_t _effectMeshCommandCount = 0;
		/// The same for the reflection, one command for each viewport it's drawn in
		SmallVector<std::unique_ptr<RenderCommand>, 0> _reflectionCommands;
		std::int32_t _reflectionCommandCount = 0;

		/** @brief Prepares drawing of a special layer, returns `false` if it can't be drawn in the current viewport at all */
		bool BeginEffectLayer(const TileMapLayer& layer, LayerRendererType type);
		/** @brief Appends an x-ray tile at the given position, returns `false` if it is no x-ray tile and is drawn as usual */
		bool AppendXRayTile(const LayerTile& tile, float x, float y);
		/** @brief Appends a tile (resolved to a single frame) to the mesh of the given kind, a blur one with the mirror line of its reflection */
		void AppendEffectTile(EffectMeshKind kind, std::int32_t tileId, LayerTileFlags flags, float alpha, float x, float y, float axisY = 0.0f);
		/** @brief Appends one tile of a blur layer, the 9-float vertex of `TileMapMeshBlur` (the tile-layer mesh layout plus the mirror line) */
		static void AppendBlurTileQuad(SmallVector<float, 0>& vertices, float x, float y, float size,
			float texScaleX, float texBiasX, float texScaleY, float texBiasY, float alpha, float axisY);
		/** @brief Returns `true` if a tile has a reflection, and the first visible row of it */
		bool IsTileReflective(std::int32_t tileId, std::int32_t& topRow);
		/**
			@brief Returns the mirror line of the reflection a tile of the blur layer being walked belongs to

			The first visible row of the topmost tile of the uninterrupted column of reflective tiles above it, in the
			coordinates of @p tileTopY - which is where the tile is drawn. Inherited from the row above in the window,
			looked up in the layout above it for the first tile of a run in the window.
		*/
		float GetReflectionAxis(const TileMapLayer& layer, std::int32_t tileX, std::int32_t tileY, std::int32_t column,
			std::int32_t row, std::int32_t tileId, float tileTopY);
		/** @brief Returns the vertex buffer of the mesh group for the given texture and kind, renting it if needed */
		std::int32_t GetEffectMeshGroup(const Texture* texture, bool indexed, EffectMeshKind kind);
		/** @brief Emits the meshes accumulated for the current special layer */
		void EmitEffectMeshes(RenderQueue& renderQueue, const Vector4f& color, std::uint16_t depth);
		/** @brief Emits one x-ray or blur mesh, split into as many commands as the shared buffers need */
		void EmitEffectMesh(RenderQueue& renderQueue, SmallVector<float, 0>& vertices, const Texture& texture, bool indexed,
			EffectMeshKind kind, const Vector4f& color, std::uint16_t depth);
		/** @brief Resolves a tile to its texture and the scale and bias of its quad's texture coordinates */
		bool ResolveTileQuad(std::int32_t tileId, LayerTileFlags flags, Texture*& texture, bool& indexed,
			float& texScaleX, float& texBiasX, float& texScaleY, float& texBiasY);
		/** @brief Derives the animations the x-ray tiles of a layer show outside the mask, see @ref GetXRayUnmaskedAnimation() */
		void PrepareXRayLayer(const TileMapLayer& layer);
		/** @brief Draws a reflection layer below its horizon, inside @p area of the view, with its @p texture moved by @p shift of its pixels */
		void DrawReflection(RenderQueue& renderQueue, const Rectf& cullingRect, const Rectf& area, const TileMapLayer& layer, Texture* texture, Vector2f shift);
#else
		/** @brief Turns the animated tiles of an x-ray layer into what they show outside the mask, see @ref LayerRendererType::XRay */
		void ReduceXRayLayer(TileMapLayer& layer);
#endif
		/**
			@brief Returns the animation an x-ray tile shows outside the mask, deriving it if needed

			All frames of a given animation of three or more frames but the last one, which is the mask frame. Appended
			to @ref _animatedTiles, so it plays as the level's own animations do. Returns `-1` for an animation that is
			itself derived.
		*/
		std::int32_t GetXRayUnmaskedAnimation(std::int32_t animIndex);

#if defined(TILEMAP_PACKED_TILE_RECORDS)
		// Emits a layer mesh buffer of packed tile records (see `TILEMAP_PACKED_TILE_RECORDS`) as one or more
		// point-list render commands
		void EmitTileRecords(RenderQueue& renderQueue, SmallVector<float, 0>& records, const Texture& texture, bool indexed,
			const Vector4f& color, std::uint16_t depth);
#endif

		void SaveTileForRollback(std::uint32_t tileIndex, const LayerTile& tile);

		static void WriteLayerTile(Stream& dest, const LayerTile& tile);
		static void ReadLayerTile(Stream& src, LayerTile& tile);

		bool AdvanceDestructibleTileAnimation(LayerTile& tile, std::int32_t tx, std::int32_t ty, std::int32_t& amount, StringView soundName);
		void AdvanceCollapsingTileTimers(float timeMult);
		void SetTileDestructibleEventParams(LayerTile& tile, TileDestructType type, std::uint16_t tileParams);
		std::int32_t GetTileDestructibleFrameCount(const LayerTile& tile);

		void UpdateDebris(float timeMult);
		void DrawDebris(RenderQueue& renderQueue);

		// Draws a textured layer inside `area` of the view, its horizon, center or vanishing point a fraction of `reference`
		// (both from the top left of the view), its texture moved by `x` and `y`
		void RenderTexturedBackground(RenderQueue& renderQueue, const Rectf& cullingRect, Vector2f viewCenter, TileMapLayer& layer,
			Texture& texture, const Rectf& area, const Rectf& reference, float x, float y);
		// Draws a tile menu inside `area` of the view, its three layers turning and zooming around a point of `reference`
		// (see LayerRendererType::TileMenu)
		void DrawMenuBackground(RenderQueue& renderQueue, const Rectf& cullingRect, const TileMapLayer& layer, const Rectf& area, const Rectf& reference);
		// Makes a pass for each layer that needs one and removes those no layer needs anymore, after a renderer type changed
		void UpdateTexturedLayers();
		// Returns the texture of the pass of a layer, or of an area of it, `nullptr` if it has none yet
		Texture* GetLayerPassTarget(std::int32_t layerIndex, bool drawsIndices, const Recti& source = {}) const;
		// Size of the texture a textured layer is drawn with in pixels, its own or the tiles at its top left
		static Vector2i GetLayerTextureSize(const TileMapLayer& layer);

		TileSet* ResolveTileSet(std::int32_t& tileId);
		// The same, with the first mesh slot of the tile set, see MeshSlot
		TileSet* ResolveTileSet(std::int32_t& tileId, std::int32_t& meshSlotBase);
#if defined(TILEMAP_USE_SINGLE_DRAW)
		// Numbers the texture chunks of the tile sets, see MeshSlot
		void UpdateMeshSlots();
#endif
		static void FillAnimatedTile(AnimatedTile& animTile, const Formats::AnimatedTileRecord& record);
		std::int32_t ResolveTileID(const LayerTile& tile) const;
		/** @brief Loads the whole atlas of the main tileset if it was deferred, see @ref _diffuseDeferred */
		void LoadDeferredDiffuse();
		// Makes a layer of no tiles, not drawn, see AddLayer()
		bool InitializeLayer(TileMapLayer& layer, Vector2i size, const LayerDescription& description);
		// The caches are kept by the index of a layer, they'd show another one after the layers changed places or contents
		void InvalidateLayerCaches();
	};
}