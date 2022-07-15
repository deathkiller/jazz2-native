#pragma once

#include "ILevelHandler.h"
#include "LevelInitialization.h"
#include "Events/EventMap.h"
#include "Events/EventSpawner.h"
#include "Tiles/TileMap.h"
#include "Collisions/DynamicTreeBroadPhase.h"

#include <SmallVector.h>

namespace Jazz2
{
	namespace Actors
	{
		class Player;
	}

	class LevelHandler : public ILevelHandler
	{
	public:
		static constexpr int DefaultWidth = 720;
		static constexpr int DefaultHeight = 405;

		static constexpr int LayerFormatVersion = 1;
		static constexpr int EventSetVersion = 2;


		LevelHandler(const LevelInitialization& data);
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

		void LoadLevel(const std::string& levelFileName, const std::string& episodeName);

		void OnFrameStart() override;
		void OnRootViewportResized(int width, int height) override;

		void OnKeyPressed(const nCine::KeyboardEvent& event) override;
		void OnKeyReleased(const nCine::KeyboardEvent& event) override;

		void AddActor(const std::shared_ptr<ActorBase>& actor) override;

		void WarpCameraToTarget(const std::shared_ptr<ActorBase>& actor) override;
		bool IsPositionEmpty(ActorBase* self, const AABBf& aabb, bool downwards, __out ActorBase** collider) override;
		void FindCollisionActorsByAABB(ActorBase* self, const AABBf& aabb, const std::function<bool(ActorBase*)>& callback);
		void FindCollisionActorsByRadius(float x, float y, float radius, const std::function<bool(ActorBase*)>& callback);
		void GetCollidingPlayers(const AABBf& aabb, const std::function<bool(ActorBase*)> callback) override;

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
		std::unique_ptr<SceneNode> _rootNode;
		std::unique_ptr<Viewport> _view;
		std::unique_ptr<Camera> _camera;
		std::unique_ptr<Sprite> _viewSprite;

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

		uint32_t _pressedActions;

		void ResolveCollisions(float timeMult);
		void InitializeCamera();
		void UpdateCamera(float timeMult);
		void UpdatePressedActions();
	};
}