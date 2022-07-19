#pragma once

#include "ILevelHandler.h"
#include "IRootController.h"
#include "LevelInitialization.h"
#include "Events/EventMap.h"
#include "Events/EventSpawner.h"
#include "Tiles/TileMap.h"
#include "Collisions/DynamicTreeBroadPhase.h"

#include "../nCine/Graphics/Shader.h"
#include "../nCine/Graphics/ShaderState.h"
#include "../nCine/Audio/AudioBufferPlayer.h"
#include "../nCine/Audio/AudioStreamPlayer.h"

#include <SmallVector.h>

// TODO
#define ENABLE_POSTPROCESSING 1

namespace Jazz2
{
	namespace Actors
	{
		class Player;
	}

	class LevelHandler : public ILevelHandler
	{
		friend class ContentResolver;

	public:
		static constexpr int DefaultWidth = 720;
		static constexpr int DefaultHeight = 405;

		static constexpr int LayerFormatVersion = 1;
		static constexpr int EventSetVersion = 2;


		LevelHandler(IRootController* root, const LevelInitialization& levelInit);
		~LevelHandler() override;

		Events::EventSpawner* EventSpawner() override {
			return &_eventSpawner;
		}
		Events::EventMap* EventMap() override {
			return _eventMap.get();
		}
		Tiles::TileMap* TileMap() override {
			return _tileMap.get();
		}

		GameDifficulty Difficulty() const override {
			return _difficulty;
		}

		bool ReduxMode() const override {
			// TODO
			return false;
		}

		Recti LevelBounds() const;

		float WaterLevel() const override;

		const Death::SmallVectorImpl<Actors::Player*>& GetPlayers() const override;

		void SetAmbientLight(float value) override;

		void OnBeginFrame() override;
		void OnEndFrame() override;
		void OnInitializeViewport(int width, int height) override;

		void OnKeyPressed(const nCine::KeyboardEvent& event) override;
		void OnKeyReleased(const nCine::KeyboardEvent& event) override;

		void AddActor(const std::shared_ptr<ActorBase>& actor) override;

		void PlaySfx(AudioBuffer* buffer, const Vector3f& pos, float gain, float pitch) override;
		void WarpCameraToTarget(const std::shared_ptr<ActorBase>& actor) override;
		bool IsPositionEmpty(ActorBase* self, const AABBf& aabb, bool downwards, __out ActorBase** collider) override;
		void FindCollisionActorsByAABB(ActorBase* self, const AABBf& aabb, const std::function<bool(ActorBase*)>& callback);
		void FindCollisionActorsByRadius(float x, float y, float radius, const std::function<bool(ActorBase*)>& callback);
		void GetCollidingPlayers(const AABBf& aabb, const std::function<bool(ActorBase*)> callback) override;

		void BeginLevelChange(ExitType exitType, const std::string& nextLevel) override;
		void HandleGameOver() override;
		bool HandlePlayerDied(const std::shared_ptr<ActorBase>& player) override;

		bool PlayerActionPressed(int index, PlayerActions action, bool includeGamepads = true) override;
		__success(return) bool PlayerActionPressed(int index, PlayerActions action, bool includeGamepads, __out bool& isGamepad) override;
		bool PlayerActionHit(int index, PlayerActions action, bool includeGamepads = true) override;
		__success(return) bool PlayerActionHit(int index, PlayerActions action, bool includeGamepads, __out bool& isGamepad) override;
		float PlayerHorizontalMovement(int index) override;
		float PlayerVerticalMovement(int index) override;
		void PlayerFreezeMovement(int index, bool enable) override;

		Vector2f GetCameraPos() {
			return _cameraPos;
		}

		Vector2i GetViewSize() {
			return _view->size();
		}

	private:
		IRootController* _root;

		std::unique_ptr<SceneNode> _rootNode;
		std::unique_ptr<Viewport> _view;
		std::unique_ptr<Texture> _viewTexture;
		std::unique_ptr<Camera> _camera;
#if ENABLE_POSTPROCESSING
		class LightingRenderer : public SceneNode
		{
		public:
			LightingRenderer(LevelHandler* owner)
				: _owner(owner), _renderCommandsCount(0)
			{
			}

			bool OnDraw(RenderQueue& renderQueue) override;

		private:
			LevelHandler* _owner;
			Death::SmallVector<std::unique_ptr<RenderCommand>, 0> _renderCommands;
			int _renderCommandsCount;

			RenderCommand* RentRenderCommand();
		};

		class BlurRenderPass : public SceneNode
		{
		public:
			BlurRenderPass(LevelHandler* owner)
				: _owner(owner)
			{
			}

			void Initialize(Texture* source, int width, int height, const Vector2f& direction);
			void Register();

			bool OnDraw(RenderQueue& renderQueue) override;

			Texture* GetTarget() const {
				return _target.get();
			}

		private:
			LevelHandler* _owner;
			std::unique_ptr<Texture> _target;
			std::unique_ptr<Viewport> _view;
			std::unique_ptr<Camera> _camera;
			RenderCommand _renderCommand;

			Texture* _source;
			bool _downsampleOnly;
			Vector2f _direction;
		};

		class CombineRenderer : public SceneNode
		{
		public:
			CombineRenderer(LevelHandler* owner)
				: _owner(owner)
			{
			}

			void Initialize();

			bool OnDraw(RenderQueue& renderQueue) override;

			void setSize(float width, float height) {
				_size.X = width;
				_size.Y = height;
			}

		private:
			LevelHandler* _owner;
			RenderCommand _renderCommand;
			Vector2f _size;
		};

		std::unique_ptr<LightingRenderer> _lightingRenderer;
		std::unique_ptr<CombineRenderer> _viewSprite;
		std::unique_ptr<Viewport> _lightingView;
		std::unique_ptr<Texture> _lightingBuffer;


		std::unique_ptr<Shader> _lightingShader;
		std::unique_ptr<Shader> _blurShader;
		std::unique_ptr<Shader> _downsampleShader;
		std::unique_ptr<Shader> _combineShader;
		
		BlurRenderPass _downsamplePass;
		BlurRenderPass _blurPass2;
		BlurRenderPass _blurPass1;
		BlurRenderPass _blurPass3;
		BlurRenderPass _blurPass4;
#else
		std::unique_ptr<Sprite> _viewSprite;
#endif

		Death::SmallVector<std::shared_ptr<ActorBase>, 0> _actors;
		Death::SmallVector<Actors::Player*, LevelInitialization::MaxPlayerCount> _players;

		std::string _levelFileName;
		std::string _episodeName;
		std::string _defaultNextLevel;
		std::string _defaultSecretLevel;
		GameDifficulty _difficulty;
		std::string _musicPath;
		Recti _levelBounds;
		bool _reduxMode, _cheatsUsed;

		Events::EventSpawner _eventSpawner;
		std::unique_ptr<Events::EventMap> _eventMap;
		std::unique_ptr<Tiles::TileMap> _tileMap;
		Collisions::DynamicTreeBroadPhase _collisions;

		Rectf _viewBounds;
		Rectf _viewBoundsTarget;
		Vector2f _cameraPos;
		Vector2f _cameraLastPos;
		Vector2f _cameraDistanceFactor;
		float _shakeDuration;
		Vector2f _shakeOffset;
		float _waterLevel;
		float _ambientLightDefault, _ambientLightCurrent, _ambientLightTarget;
		std::unique_ptr<AudioStreamPlayer> _music;
		SmallVector<std::unique_ptr<AudioBufferPlayer>> _playingSounds;

		uint32_t _pressedActions;
		Vector2f _playerRequiredMovement;

		void OnLevelLoaded(const std::string& name, const std::string& nextLevel, const std::string& secretLevel,
			std::unique_ptr<Tiles::TileMap>& tileMap, std::unique_ptr<Events::EventMap>& eventMap,
			const std::string& musicPath, float ambientLight);

		void ResolveCollisions(float timeMult);
		void InitializeCamera();
		void UpdateCamera(float timeMult);
		void UpdatePressedActions();
	};
}