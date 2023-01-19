#pragma once

#include "../ILevelHandler.h"
#include "../PitType.h"
#include "TileSet.h"

#include "../../nCine/IO/IFileStream.h"

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
		uint16_t Depth;
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
		int32_t TileID;
		uint16_t TileParams;
		LayerTileFlags Flags;
		uint8_t Alpha;
		SuspendType HasSuspendType;
		TileDestructType DestructType;
		int32_t DestructAnimation;		// Animation index for a destructible tile that uses an animation, but doesn't animate normally
		int32_t DestructFrameIndex;		// Denotes the specific frame from the above animation that is currently active
										// Collapsible: delay ("wait" parameter); trigger: trigger id
	};

	struct TileMapLayer {
		bool Visible;

		std::unique_ptr<LayerTile[]> Layout;
		Vector2i LayoutSize;

		LayerDescription Description;
	};

	struct AnimatedTileFrame {
		int TileID;
	};

	struct AnimatedTile {
		SmallVector<AnimatedTileFrame, 0> Tiles;
		int Delay;
		bool PingPong;
		int PingPongDelay;
		int CurrentTileIdx;
		bool Forwards;
		float FrameDuration;
		float FramesLeft;
	};

	class TileMap : public SceneNode
	{
	public:
		static constexpr int TriggerCount = 32;
		static constexpr int AnimatedTileMask = 0x80000000;
		static constexpr int HardcodedOffset = 70;

		enum class DebrisFlags {
			None = 0x00,
			Disappear = 0x01,
			Bounce = 0x02,
			AdditiveBlending = 0x04
		};

		DEFINE_PRIVATE_ENUM_OPERATORS(DebrisFlags);

		struct DestructibleDebris {
			Vector2f Pos;
			uint16_t Depth;

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

		TileMap(LevelHandler* levelHandler, const StringView& tileSetPath, uint16_t captionTileId, PitType pitType, bool applyPalette);

		Vector2i Size();
		Vector2i LevelBounds();

		void OnUpdate(float timeMult) override;
		bool OnDraw(RenderQueue& renderQueue) override;

		bool IsTileEmpty(int tx, int ty);
		bool IsTileEmpty(const AABBf& aabb, TileCollisionParams& params);
		bool CanBeDestroyed(const AABBf& aabb, TileCollisionParams& params);
		bool IsTileHurting(float x, float y);
		SuspendType GetTileSuspendState(float x, float y);

		void AddTileSet(const StringView& tileSetPath, uint16_t offset, uint16_t count, const uint8_t* paletteRemapping = nullptr);
		void ReadLayerConfiguration(IFileStream& s);
		void ReadAnimatedTiles(IFileStream& s);
		void SetTileEventFlags(int x, int y, EventType tileEvent, uint8_t* tileParams);

		Color* GetCaptionTile() const
		{
			return _tileSets[0].Data->GetCaptionTile();
		}

		void CreateDebris(const DestructibleDebris& debris);
		void CreateTileDebris(int tileId, int x, int y);
		void CreateParticleDebris(const GraphicResource* res, Vector3f pos, Vector2f force, int currentFrame, bool isFacingLeft);
		void CreateSpriteDebris(const GraphicResource* res, Vector3f pos, int count);

		bool GetTrigger(uint8_t triggerId);
		void SetTrigger(uint8_t triggerId, bool newState);

		void OnInitializeViewport();

	private:
		enum class LayerType {
			Other,
			Sky,
			Sprite
		};

		struct TileSetPart {
			std::unique_ptr<TileSet> Data;
			int32_t Offset;
			int32_t Count;
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

		LevelHandler* _levelHandler;
		int _sprLayerIndex;
		PitType _pitType;

		SmallVector<TileSetPart, 2> _tileSets;
		SmallVector<TileMapLayer, 0> _layers;
		SmallVector<AnimatedTile, 0> _animatedTiles;
		SmallVector<Vector2i, 0> _activeCollapsingTiles;
		float _collapsingTimer;
		BitArray _triggerState;

		SmallVector<DestructibleDebris, 0> _debrisList;
		SmallVector<std::unique_ptr<RenderCommand>, 0> _renderCommands;
		int _renderCommandsCount;

		int _texturedBackgroundLayer;
		TexturedBackgroundPass _texturedBackgroundPass;

		void DrawLayer(RenderQueue& renderQueue, TileMapLayer& layer);
		static float TranslateCoordinate(float coordinate, float speed, float offset, int viewSize, bool isY);
		RenderCommand* RentRenderCommand(LayerRendererType type);

		bool AdvanceDestructibleTileAnimation(LayerTile& tile, int tx, int ty, int& amount, const StringView& soundName);
		void AdvanceCollapsingTileTimers(float timeMult);
		void SetTileDestructibleEventParams(LayerTile& tile, TileDestructType type, uint16_t tileParams);

		void UpdateDebris(float timeMult);
		void DrawDebris(RenderQueue& renderQueue);

		void RenderTexturedBackground(RenderQueue& renderQueue, TileMapLayer& layer, float x, float y);

		TileSet* ResolveTileSet(int& tileId);

		inline int ResolveTileID(LayerTile& tile)
		{
			int tileId = tile.TileID;
			if ((tile.Flags & LayerTileFlags::Animated) == LayerTileFlags::Animated) {
				if (tileId >= _animatedTiles.size()) {
					return 0;
				}
				auto& animTile = _animatedTiles[tileId];
				tileId = animTile.Tiles[animTile.CurrentTileIdx].TileID;
			}
			return tileId;
		}
	};
}