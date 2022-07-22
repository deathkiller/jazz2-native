#pragma once

#include "../ILevelHandler.h"
#include "TileSet.h"

#include "../../nCine/IO/IFileStream.h"

#include <SmallVector.h>

namespace Jazz2
{
	class LevelHandler;
}

namespace Jazz2::Tiles
{
	struct LayerTile {
		int TileID;

		//Material* Material;
		//Vector2i MaterialOffset;
		uint8_t MaterialAlpha;

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
		Vector4f BackgroundColor;
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

		TileMap(LevelHandler* levelHandler, const std::string& tileSetPath);

		Vector2i Size();
		Recti LevelBounds();

		void OnUpdate(float timeMult) override;
		bool OnDraw(RenderQueue& renderQueue) override;

		bool IsTileEmpty(int x, int y);
		bool IsTileEmpty(const AABBf& aabb, bool downwards);
		SuspendType GetTileSuspendState(float x, float y);

		int CheckWeaponDestructible(const AABBf& aabb, WeaponType weapon, int strength);
		int CheckSpecialDestructible(const AABBf& aabb);
		int CheckSpecialSpeedDestructible(const AABBf& aabb, float speed);
		int CheckCollapseDestructible(const AABBf& aabb);

		void SetSolidLimit(int tileLeft, int tileWidth);

		void ReadLayerConfiguration(LayerType type, const std::unique_ptr<IFileStream>& s, const LayerDescription& layer);
		void ReadAnimatedTiles(const std::unique_ptr<IFileStream>& s);
		void SetTileEventFlags(int x, int y, EventType tileEvent, uint8_t* tileParams);

		void CreateDebris(const DestructibleDebris& debris);
		void CreateTileDebris(int tileId, int x, int y);
		void CreateParticleDebris(const GraphicResource* res, Vector3f pos, Vector2f force, int currentFrame, bool isFacingLeft);
		void CreateSpriteDebris(const GraphicResource* res, Vector3f pos, int count);

		bool GetTrigger(uint16_t triggerId);
		void SetTrigger(uint16_t triggerId, bool newState);

	private:
		LevelHandler* _levelHandler;
		int _sprLayerIndex;
		bool _hasPit;
		int _limitLeft, _limitRight;

		std::unique_ptr<TileSet> _tileSet;
		SmallVector<TileMapLayer, 0> _layers;
		SmallVector<AnimatedTile, 0> _animatedTiles;
		SmallVector<Vector2i, 0> _activeCollapsingTiles;
		float _collapsingTimer;
		BitArray _triggerState;

		SmallVector<DestructibleDebris, 0> _debrisList;
		SmallVector<std::unique_ptr<RenderCommand>, 0> _renderCommands;
		int _renderCommandsCount;

		void DrawLayer(RenderQueue& renderQueue, TileMapLayer& layer);
		static float TranslateCoordinate(float coordinate, float speed, float offset, bool isY, int viewHeight, int viewWidth);
		RenderCommand* RentRenderCommand();

		bool AdvanceDestructibleTileAnimation(LayerTile& tile, int tx, int ty, int& amount, const std::string& soundName);
		void AdvanceCollapsingTileTimers(float timeMult);

		void SetTileDestructibleEventFlag(LayerTile& tile, TileDestructType type, uint16_t extraData);

		void UpdateDebris(float timeMult);
		void DrawDebris(RenderQueue& renderQueue);
	};
}