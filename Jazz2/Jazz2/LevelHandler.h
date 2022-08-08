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

// TODO
#define ENABLE_POSTPROCESSING 1

namespace Jazz2
{
	namespace Actors
	{
		class Player;
	}

	namespace UI
	{
		class HUD;
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

		Recti LevelBounds() const override;

		float WaterLevel() const override;

		const SmallVectorImpl<Actors::Player*>& GetPlayers() const override;

		void SetAmbientLight(float value) override;

		void OnBeginFrame() override;
		void OnEndFrame() override;
		void OnInitializeViewport(int width, int height) override;

		void OnKeyPressed(const nCine::KeyboardEvent& event) override;
		void OnKeyReleased(const nCine::KeyboardEvent& event) override;
		void OnTouchEvent(const nCine::TouchEvent& event) override;

		void AddActor(const std::shared_ptr<ActorBase>& actor) override;

		const std::shared_ptr<AudioBufferPlayer>& PlaySfx(AudioBuffer* buffer, const Vector3f& pos, bool sourceRelative, float gain = 1.0f, float pitch = 1.0f) override;
		const std::shared_ptr<AudioBufferPlayer>& PlayCommonSfx(const StringView& identifier, const Vector3f& pos, float gain = 1.0f, float pitch = 1.0f) override;
		void WarpCameraToTarget(const std::shared_ptr<ActorBase>& actor) override;
		bool IsPositionEmpty(ActorBase* self, const AABBf& aabb, TileCollisionParams& params, __out ActorBase** collider) override;
		void FindCollisionActorsByAABB(ActorBase* self, const AABBf& aabb, const std::function<bool(ActorBase*)>& callback) override;
		void FindCollisionActorsByRadius(float x, float y, float radius, const std::function<bool(ActorBase*)>& callback) override;
		void GetCollidingPlayers(const AABBf& aabb, const std::function<bool(ActorBase*)> callback) override;

		void BeginLevelChange(ExitType exitType, const StringView& nextLevel) override;
		void HandleGameOver() override;
		bool HandlePlayerDied(const std::shared_ptr<ActorBase>& player) override;
		void ShowLevelText(const StringView& text) override;
		void ShowCoins(int count) override;
		void ShowGems(int count) override;
		StringView GetLevelText(int textId, int index = -1, uint32_t delimiter = 0) override;

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
			SmallVector<std::unique_ptr<RenderCommand>, 0> _renderCommands;
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

			void Initialize(int width, int height);

			bool OnDraw(RenderQueue& renderQueue) override;

		private:
			LevelHandler* _owner;
			RenderCommand _renderCommand;
			Vector2f _size;
		};

		class UpscaleRenderPass : public SceneNode
		{
		public:
			UpscaleRenderPass()
			{
			}

			void Initialize(int width, int height, int targetWidth, int targetHeight);
			void Register();

			bool OnDraw(RenderQueue& renderQueue) override;

			SceneNode* GetNode() const {
				return _node.get();
			}

		private:
			std::unique_ptr<SceneNode> _node;
			std::unique_ptr<Texture> _target;
			std::unique_ptr<Viewport> _view;
			std::unique_ptr<Camera> _camera;
			RenderCommand _renderCommand;
			Vector2f _targetSize;
		};

		std::unique_ptr<LightingRenderer> _lightingRenderer;
		std::unique_ptr<CombineRenderer> _viewSprite;
		std::unique_ptr<Viewport> _lightingView;
		std::unique_ptr<Texture> _lightingBuffer;

		Shader* _lightingShader;
		Shader* _blurShader;
		Shader* _downsampleShader;
		Shader* _combineShader;
		
		BlurRenderPass _downsamplePass;
		BlurRenderPass _blurPass2;
		BlurRenderPass _blurPass1;
		BlurRenderPass _blurPass3;
		BlurRenderPass _blurPass4;
		UpscaleRenderPass _upscalePass;
#else
		std::unique_ptr<Sprite> _viewSprite;
#endif

		SmallVector<std::shared_ptr<ActorBase>, 0> _actors;
		SmallVector<Actors::Player*, LevelInitialization::MaxPlayerCount> _players;

		String _levelFileName;
		String _episodeName;
		String _defaultNextLevel;
		String _defaultSecretLevel;
		GameDifficulty _difficulty;
		String _musicPath;
		Recti _levelBounds;
		bool _reduxMode, _cheatsUsed;
		SmallVector<String, 0> _levelTexts;

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
		SmallVector<std::shared_ptr<AudioBufferPlayer>> _playingSounds;
		Metadata* _commonResources;
		std::unique_ptr<UI::HUD> _hud;

		uint32_t _pressedActions;
		uint32_t _overrideActions;
		Vector2f _playerRequiredMovement;

		void OnLevelLoaded(const StringView& name, const StringView& nextLevel, const StringView& secretLevel,
			std::unique_ptr<Tiles::TileMap>& tileMap, std::unique_ptr<Events::EventMap>& eventMap,
			const StringView& musicPath, float ambientLight, SmallVectorImpl<String>& levelTexts);

		void ResolveCollisions(float timeMult);
		void InitializeCamera();
		void UpdateCamera(float timeMult);
		void UpdatePressedActions();
		void UpdateRgbLights();
	};
}