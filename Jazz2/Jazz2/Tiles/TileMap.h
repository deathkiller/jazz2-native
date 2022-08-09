#pragma once

#include "../ILevelHandler.h"
#include "TileSet.h"

#include "../../nCine/IO/IFileStream.h"

namespace Jazz2
{
	class LevelHandler;
}

namespace Jazz2::Tiles
{
	struct LayerTile {
		int TileID;

		uint8_t Alpha;
		bool IsFlippedX;
		bool IsFlippedY;
		bool IsAnimated;

		// Collision affecting modifiers
		bool IsOneWay;
		SuspendType SuspendType;
		TileDestructType DestructType;
		int DestructAnimation;   // Animation index for a destructible tile that uses an animation, but doesn't animate normally
		int DestructFrameIndex;  // Denotes the specific frame from the above animation that is currently active
								 // Collapsible: delay ("wait" parameter); trigger: trigger id

		// ToDo: I don't know if it's good solution for this
		unsigned int ExtraData;
		// ToDo: I don't know if it's used at all
		//public bool TilesetDefault;
	};

	struct TileMapLayer {
		bool Visible;

		std::unique_ptr<LayerTile[]> Layout;
		Vector2i LayoutSize;

		uint16_t Depth;
		float SpeedX;
		float SpeedY;
		float AutoSpeedX;
		float AutoSpeedY;
		bool RepeatX;
		bool RepeatY;

		float OffsetX;
		float OffsetY;

		// JJ2's "limit visible area" flag
		bool UseInherentOffset;

		BackgroundStyle BackgroundStyle;
		Vector3f BackgroundColor;
		bool ParallaxStarsEnabled;
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

		struct LayerDescription {
			float SpeedX;
			float SpeedY;
			float AutoSpeedX;
			float AutoSpeedY;
			bool RepeatX;
			bool RepeatY;
			float OffsetX;
			float OffsetY;
			int Depth;

			// JJ2's "limit visible area" flag
			bool UseInherentOffset;

			BackgroundStyle BackgroundStyle;
			Vector3f BackgroundColor;
			bool ParallaxStarsEnabled;
		};

		enum class DebrisCollisionAction {
			None,
			Disappear,
			Bounce
		};

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

			DebrisCollisionAction CollisionAction;
		};

		TileMap(LevelHandler* levelHandler, const StringView& tileSetPath);

		Vector2i Size();
		Recti LevelBounds();

		void OnUpdate(float timeMult) override;
		bool OnDraw(RenderQueue& renderQueue) override;

		bool IsTileEmpty(int x, int y);
		bool IsTileEmpty(const AABBf& aabb, TileCollisionParams& params);
		SuspendType GetTileSuspendState(float x, float y);

		void ReadLayerConfiguration(LayerType type, const std::unique_ptr<IFileStream>& s, const LayerDescription& layer);
		void ReadAnimatedTiles(const std::unique_ptr<IFileStream>& s);
		void SetTileEventFlags(int x, int y, EventType tileEvent, uint8_t* tileParams);

		void CreateDebris(const DestructibleDebris& debris);
		void CreateTileDebris(int tileId, int x, int y);
		void CreateParticleDebris(const GraphicResource* res, Vector3f pos, Vector2f force, int currentFrame, bool isFacingLeft);
		void CreateSpriteDebris(const GraphicResource* res, Vector3f pos, int count);

		bool GetTrigger(uint16_t triggerId);
		void SetTrigger(uint16_t triggerId, bool newState);

		void OnInitializeViewport(int width, int height);

	private:
		class TexturedBackgroundPass : public SceneNode
		{
			friend class TileMap;

		public:
			TexturedBackgroundPass(TileMap* owner)
				: _owner(owner), _alreadyRendered(false)
			{
			}

			void Initialize(int width, int height);

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
		bool _hasPit;

		std::unique_ptr<TileSet> _tileSet;
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
		Shader* _texturedBackgroundShader;

		void DrawLayer(RenderQueue& renderQueue, TileMapLayer& layer);
		static float TranslateCoordinate(float coordinate, float speed, float offset, bool isY, int viewHeight, int viewWidth);
		RenderCommand* RentRenderCommand();

		bool AdvanceDestructibleTileAnimation(LayerTile& tile, int tx, int ty, int& amount, const StringView& soundName);
		void AdvanceCollapsingTileTimers(float timeMult);

		void SetTileDestructibleEventFlag(LayerTile& tile, TileDestructType type, uint16_t extraData);

		void UpdateDebris(float timeMult);
		void DrawDebris(RenderQueue& renderQueue);

		void RenderTexturedBackground(RenderQueue& renderQueue, TileMapLayer& layer, float x, float y);

		inline int ResolveTileID(LayerTile& tile)
		{
			int tileId = tile.TileID;
			if (tile.IsAnimated) {
				tileId = _animatedTiles[tileId].Tiles[_animatedTiles[tileId].CurrentTileIdx].TileID;
			}
			return tileId;
		}
	};
}