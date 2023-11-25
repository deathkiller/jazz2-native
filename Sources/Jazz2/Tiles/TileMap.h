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
}

namespace Jazz2::Tiles
{
	enum class LayerSpeedModel {
		Default,
		AlwaysOnTop,
		FitLevel,
		SpeedMultipliers
	};

	enum class LayerRendererType {
		Default,
		Tinted,

		Sky,
		Circle
	};

	struct LayerDescription {
		std::uint16_t Depth;
		float SpeedX;
		float SpeedY;
		float AutoSpeedX;
		float AutoSpeedY;
		float OffsetX;
		float OffsetY;
		bool RepeatX;
		bool RepeatY;
		bool UseInherentOffset;
		LayerSpeedModel SpeedModelX;
		LayerSpeedModel SpeedModelY;

		LayerRendererType RendererType;
		Vector4f Color;
	};

	enum class LayerTileFlags : uint8_t {
		None = 0x00,

		FlipX = 0x01,
		FlipY = 0x02,
		Animated = 0x04,

		OneWay = 0x10,
		Hurt = 0x20
	};

	DEFINE_ENUM_OPERATORS(LayerTileFlags);

	struct LayerTile {
		std::int32_t TileID;
		std::uint16_t TileParams;
		LayerTileFlags Flags;
		std::uint8_t Alpha;
		SuspendType HasSuspendType;
		TileDestructType DestructType;
		std::int32_t DestructAnimation;		// Animation index for a destructible tile that uses an animation, but doesn't animate normally
		std::int32_t DestructFrameIndex;	// Denotes the specific frame from the above animation that is currently active
											// Collapsible: Delay ("wait" parameter); Trigger: Trigger ID
	};

	struct TileMapLayer {
		bool Visible;

		std::unique_ptr<LayerTile[]> Layout;
		Vector2i LayoutSize;

		LayerDescription Description;
	};

	struct AnimatedTileFrame {
		std::int32_t TileID;
	};

	struct AnimatedTile {
		SmallVector<AnimatedTileFrame, 0> Tiles;
		std::int16_t Delay;
		std::int16_t DelayJitter;
		std::int32_t PingPongDelay;
		std::int32_t CurrentTileIdx;
		float FrameDuration;
		float FramesLeft;
		bool IsPingPong;
		bool Forwards;
	};

	class TileMap : public SceneNode
	{
	public:
		static constexpr std::int32_t TriggerCount = 32;
		static constexpr std::int32_t AnimatedTileMask = 0x80000000;
		static constexpr std::int32_t HardcodedOffset = 70;

		enum class DebrisFlags {
			None = 0x00,
			Disappear = 0x01,
			Bounce = 0x02,
			AdditiveBlending = 0x04
		};

		DEFINE_PRIVATE_ENUM_OPERATORS(DebrisFlags);

		struct DestructibleDebris {
			Vector2f Pos;
			std::uint16_t Depth;

			Vector2f Size;
			Vector2f Speed;
			Vector2f Acceleration;

			float Scale;
			float ScaleSpeed;

			float Angle;
			float AngleSpeed;

			float Alpha;
			float AlphaSpeed;

			float Time;

			float TexScaleX;
			float TexBiasX;
			float TexScaleY;
			float TexBiasY;

			Texture* DiffuseTexture;

			DebrisFlags Flags;
		};

		TileMap(const StringView& tileSetPath, std::uint16_t captionTileId, bool applyPalette);
		~TileMap();

		void SetOwner(ITileMapOwner* owner);
		Vector2i GetSize() const;
		Vector2i GetLevelBounds() const;
		PitType GetPitType() const;
		void SetPitType(PitType value);

		void OnUpdate(float timeMult) override;
		bool OnDraw(RenderQueue& renderQueue) override;

		bool IsTileEmpty(std::int32_t tx, std::int32_t ty);
		bool IsTileEmpty(const AABBf& aabb, TileCollisionParams& params);
		bool CanBeDestroyed(const AABBf& aabb, TileCollisionParams& params);
		bool IsTileHurting(float x, float y);
		SuspendType GetTileSuspendState(float x, float y);
		bool AdvanceDestructibleTileAnimation(std::int32_t tx, std::int32_t ty, std::int32_t amount);

		void AddTileSet(const StringView& tileSetPath, std::uint16_t offset, std::uint16_t count, const std::uint8_t* paletteRemapping = nullptr);
		void ReadLayerConfiguration(Stream& s);
		void ReadAnimatedTiles(Stream& s);
		void SetTileEventFlags(std::int32_t x, std::int32_t y, EventType tileEvent, std::uint8_t* tileParams);

		Color* GetCaptionTile() const
		{
			return _tileSets[0].Data->GetCaptionTile();
		}

		void CreateDebris(const DestructibleDebris& debris);
		void CreateTileDebris(std::int32_t tileId, std::int32_t x, std::int32_t y);
		void CreateParticleDebris(const GraphicResource* res, Vector3f pos, Vector2f force, std::int32_t currentFrame, bool isFacingLeft);
		void CreateSpriteDebris(const GraphicResource* res, Vector3f pos, std::int32_t count);

		bool GetTrigger(std::uint8_t triggerId);
		void SetTrigger(std::uint8_t triggerId, bool newState);

		void InitializeFromStream(Stream& src);
		void SerializeResumableToStream(Stream& dest);

		void OnInitializeViewport();

	private:
		enum class LayerType {
			Other,
			Sky,
			Sprite
		};

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
			RenderCommand _outputRenderCommand;
			bool _alreadyRendered;
		};

		ITileMapOwner* _owner;
		std::int32_t _sprLayerIndex;
		PitType _pitType;

		SmallVector<TileSetPart, 2> _tileSets;
		SmallVector<TileMapLayer, 0> _layers;
		SmallVector<AnimatedTile, 0> _animatedTiles;
		SmallVector<Vector2i, 0> _activeCollapsingTiles;
		float _collapsingTimer;
		BitArray _triggerState;

		SmallVector<DestructibleDebris, 0> _debrisList;
		SmallVector<std::unique_ptr<RenderCommand>, 0> _renderCommands;
		std::int32_t _renderCommandsCount;

		std::int32_t _texturedBackgroundLayer;
		TexturedBackgroundPass _texturedBackgroundPass;

		void DrawLayer(RenderQueue& renderQueue, TileMapLayer& layer);
		static float TranslateCoordinate(float coordinate, float speed, float offset, std::int32_t viewSize, bool isY);
		RenderCommand* RentRenderCommand(LayerRendererType type);

		bool AdvanceDestructibleTileAnimation(LayerTile& tile, std::int32_t tx, std::int32_t ty, std::int32_t& amount, const StringView& soundName);
		void AdvanceCollapsingTileTimers(float timeMult);
		void SetTileDestructibleEventParams(LayerTile& tile, TileDestructType type, std::uint16_t tileParams);

		void UpdateDebris(float timeMult);
		void DrawDebris(RenderQueue& renderQueue);

		void RenderTexturedBackground(RenderQueue& renderQueue, TileMapLayer& layer, float x, float y);

		TileSet* ResolveTileSet(std::int32_t& tileId);
		std::int32_t ResolveTileID(LayerTile& tile);
	};
}