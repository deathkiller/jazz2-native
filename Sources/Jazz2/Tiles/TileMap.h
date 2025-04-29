#pragma once

#include "ITileMapOwner.h"
#include "../ILevelHandler.h"
#include "../PitType.h"
#include "../SuspendType.h"
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

namespace Jazz2::Tiles
{
	/** @brief Layer speed model */
	enum class LayerSpeedModel {
		/** @brief Default model */
		Default,			
		/** @brief Ignores all speed and offset settings to be tied to the top/left side of the screen */
		AlwaysOnTop,		
		/** @brief Ignores the speed and auto speed properties, and instead ensures that the full extent of this layer will be visible and no blank space outside of it will be shown */
		FitLevel,
		/** @brief Treats the layer's speed and auto speed properties on this axis as multipliers of the current camera size, rather than camera position */
		SpeedMultipliers
	};

	/** @brief Layer renderer type */
	enum class LayerRendererType {
		Default,				/**< Default rendering */
		Solid,					/**< Solid color rendering */
		Tinted,					/**< Color-tinted rendering */

		Sky = 10,				/**< Textured background --- Sky */
		Circle					/**< Textured background --- Circle */
	};

	/** @brief Description of a tile map layer */
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

	/** @brief Layer tile flags, supports a bitwise combination of its member values */
	enum class LayerTileFlags : std::uint8_t {
		None = 0x00,			/**< None */

		FlipX = 0x01,			/**< Flipped horizontally */
		FlipY = 0x02,			/**< Flipped vertically */
		Animated = 0x04,		/**< Animated tile */

		OneWay = 0x10			/**< One-way collision */
	};

	DEATH_ENUM_FLAGS(LayerTileFlags);

	/** @brief Represents a single tile in a tile map layer */
	struct LayerTile {
		/** @brief Tile ID */
		std::int32_t TileID;
		/** @brief Tile parameters */
		std::uint16_t TileParams;
		/** @brief Tile flags */
		LayerTileFlags Flags;
		/** @brief Tile transparency */
		std::uint8_t Alpha;
		/** @brief Suspend type of tile */
		SuspendType HasSuspendType;
		/** @brief Destruct type of tile */
		TileDestructType DestructType;
		/** @brief Animation ID for destructible tile */
		std::int32_t DestructAnimation;
		/** @brief Denotes the specific frame from the above animation that is currently active --- Collapsible: Delay ("wait" parameter); Trigger: Trigger ID */
		std::int32_t DestructFrameIndex;
	};

	/** @brief Represents a single tile map layer */
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

	/** @brief Represents a single frame of an animated tile */
	struct AnimatedTileFrame {
		/** @brief Tile ID */
		std::int32_t TileID;
	};

	/** @brief Represents an animated tile */
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

	/** @brief Represents a renderable tile map, consists of multiple layers */
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

		/** @} */

		/** @brief Flags that modify behaviour of @ref DestructibleDebris, supports a bitwise combination of its member values */
		enum class DebrisFlags {
			None = 0x00,
			Disappear = 0x01,
			Bounce = 0x02,
			AdditiveBlending = 0x04
		};

		DEATH_PRIVATE_ENUM_FLAGS(DebrisFlags);

		/** @brief Describes a visual debris (particle effect) */
		struct DestructibleDebris {
			/** @brief Position */
			Vector2f Pos;
			/** @brief Depth (layer) */
			std::uint16_t Depth;

			/** @brief Size */
			Vector2f Size;
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

			/** @brief Behavior flags */
			DebrisFlags Flags;
		};

		TileMap(StringView tileSetPath, std::uint16_t captionTileId, bool applyPalette);
		~TileMap();

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
		/** @brief Returns `true` if the mask of tiles on the main (sprite) layer intersecting a given AABB is empty */
		bool IsTileEmpty(const AABBf& aabb, TileCollisionParams& params);
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
		/** @brief Overrides the collision mask of the specified tile */
		bool OverrideTileMask(std::int32_t tileId, StaticArrayView<TileSet::DefaultTileSize * TileSet::DefaultTileSize, std::uint8_t> tileMask);

		/** @brief Returns a caption tile */
		StaticArrayView<TileSet::DefaultTileSize * TileSet::DefaultTileSize, Color> GetCaptionTile() const {
			return _tileSets[0].Data->GetCaptionTile();
		}

		/** @brief Returns relative paths of all used tile sets */
		Array<StringView> GetUsedTileSetPaths() const;
		
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

		/** @brief Creates a checkpoint for eventual rollback */
		void CreateCheckpointForRollback();
		/** @brief Rolls back to the last checkpoint */
		void RollbackToCheckpoint();

		/** @brief Initializes tile map state from a stream */
		void InitializeFromStream(Stream& src);
		/** @brief Serializes tile map state to a stream */
		void SerializeResumableToStream(Stream& dest);

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
		std::unique_ptr<LayerTile[]> _sprLayerForRollback;
		SmallVector<AnimatedTile, 0> _animatedTiles;
		SmallVector<Vector2i, 0> _activeCollapsingTiles;
		float _collapsingTimer;
		BitArray _triggerState;
		BitArray _triggerStateForRollback;

		SmallVector<DestructibleDebris, 0> _debrisList;
		SmallVector<std::unique_ptr<RenderCommand>, 0> _renderCommands;
		std::int32_t _renderCommandsCount;

		std::int32_t _texturedBackgroundLayer;
		TexturedBackgroundPass _texturedBackgroundPass;

		void DrawLayer(RenderQueue& renderQueue, TileMapLayer& layer, const Rectf& cullingRect, Vector2f viewCenter);
		static float TranslateCoordinate(float coordinate, float speed, float offset, std::int32_t viewSize, bool isY);
		RenderCommand* RentRenderCommand(LayerRendererType type);

		bool AdvanceDestructibleTileAnimation(LayerTile& tile, std::int32_t tx, std::int32_t ty, std::int32_t& amount, StringView soundName);
		void AdvanceCollapsingTileTimers(float timeMult);
		void SetTileDestructibleEventParams(LayerTile& tile, TileDestructType type, std::uint16_t tileParams);

		void UpdateDebris(float timeMult);
		void DrawDebris(RenderQueue& renderQueue);

		void RenderTexturedBackground(RenderQueue& renderQueue, const Rectf& cullingRect, Vector2f viewCenter, TileMapLayer& layer, float x, float y);

		TileSet* ResolveTileSet(std::int32_t& tileId);
		std::int32_t ResolveTileID(LayerTile& tile);
	};
}