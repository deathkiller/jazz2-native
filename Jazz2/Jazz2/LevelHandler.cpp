#include "LevelHandler.h"

#include "../nCine/PCApplication.h"
#include "../nCine/IAppEventHandler.h"
#include "../nCine/Input/IInputEventHandler.h"
#include "../nCine/Graphics/Camera.h"
#include "../nCine/Graphics/Sprite.h"
#include "../nCine/Graphics/Texture.h"
#include "../nCine/Graphics/Viewport.h"
#include "../nCine/Base/Random.h"

#include "Actors/Player.h"
#include "Actors/SolidObjectBase.h"

#include <float.h>

using namespace nCine;

// TODO: Debug only
#if _DEBUG
int _debugActorDirtyCount = 0;
int _debugCollisionPairCount = 0;
#endif

namespace Jazz2
{
	LevelHandler::LevelHandler(const LevelInitialization& data)
		:
		_eventSpawner(this),
		_levelFileName(data.LevelName),
		_episodeName(data.EpisodeName),
		_difficulty(data.Difficulty),
		_reduxMode(data.ReduxMode),
		_cheatsUsed(data.CheatsUsed),
		_shakeDuration(0.0f),
		_waterLevel(FLT_MAX),
		_ambientLightDefault(1.0f),
		_ambientLightCurrent(1.0f),
		_ambientLightTarget(1.0f),
		_pressedActions(0)
	{
		auto& resolver = Jazz2::ContentResolver::Current();
		resolver.BeginLoading();

		_rootNode = std::make_unique<SceneNode>();

		// Setup rendering scene
		Vector2i res = theApplication().resolutionInt();
		OnRootViewportResized(res.X, res.Y);

		LoadLevel(_levelFileName, _episodeName);

		// Process carry overs
		for (int i = 0; i < _countof(data.PlayerCarryOvers); i++) {
			if (data.PlayerCarryOvers[i].Type == PlayerType::None) {
				continue;
			}

			Vector2 spawnPosition = _eventMap->GetSpawnPosition(data.PlayerCarryOvers[i].Type);
			if (spawnPosition.X < 0.0f && spawnPosition.Y < 0.0f) {
				spawnPosition = _eventMap->GetSpawnPosition(PlayerType::Jazz);
				if (spawnPosition.X < 0.0f && spawnPosition.Y < 0.0f) {
					continue;
				}
			}

			std::shared_ptr<Actors::Player> player = std::make_shared<Actors::Player>();
			uint8_t playerParams[2] = { (uint8_t)data.PlayerCarryOvers[i].Type, (uint8_t)i };
			player->OnActivated({
				.LevelHandler = this,
				.Pos = Vector3i(spawnPosition.X + (i * 30), spawnPosition.Y - (i * 30), PlayerZ - i),
				.Params = playerParams
			});

			Actors::Player* ptr = player.get();
			_players.push_back(ptr);
			AddActor(player);

			// TODO
			/*if (i == 0) {
				player.AttachToHud(hud);
			}*/

			ptr->ReceiveLevelCarryOver(data.ExitType, data.PlayerCarryOvers[i]);
		}

		InitializeCamera();

		// TODO
		//_commonResources = ContentResolver::Current().RequestMetadata("Common/Scenery");

		_eventMap->PreloadEventsAsync();

		resolver.EndLoading();
	}

	LevelHandler::~LevelHandler()
	{
		// TODO
		Viewport::chain().clear();
	}

	Recti LevelHandler::LevelBounds() const
	{
		return _levelBounds;
	}

	float LevelHandler::WaterLevel() const
	{
		return _waterLevel;
	}

	const Death::SmallVectorImpl<Actors::Player*>& LevelHandler::GetPlayers() const
	{
		return _players;
	}

	void LevelHandler::LoadLevel(const std::string& levelFileName, const std::string& episodeName)
	{
		// TODO
		auto& resolver = Jazz2::ContentResolver::Current();
		resolver.ApplyPalette("Content/Tilesets/castle1/Main.palette");

		// TODO
		_tileMap = std::make_unique<Tiles::TileMap>(this, "castle1");
		_tileMap->setParent(_rootNode.get());

		{
			auto layerFile = IFileStream::createFileHandle(FileSystem::joinPath("Content/Episodes", "prince/01_castle1/Sprite.layer").c_str());
			_tileMap->ReadLayerConfiguration(LayerType::Sprite, layerFile, { .SpeedX = 1, .SpeedY = 1 });
		}
		{
			auto layerFile = IFileStream::createFileHandle(FileSystem::joinPath("Content/Episodes", "prince/01_castle1/Sky.layer").c_str());
			_tileMap->ReadLayerConfiguration(LayerType::Other, layerFile, { .Depth = -400, .SpeedX = 0.04998779296875f, .SpeedY = 0.079986572265625f, .RepeatX = true, .RepeatY = true, .OffsetX = 180, .OffsetY = -300 });
		}
		{
			auto layerFile = IFileStream::createFileHandle(FileSystem::joinPath("Content/Episodes", "prince/01_castle1/2.layer").c_str());
			_tileMap->ReadLayerConfiguration(LayerType::Other, layerFile, { .Depth = 200, .SpeedX = 1.5f, .SpeedY = 1.5f });
		}
		{
			auto layerFile = IFileStream::createFileHandle(FileSystem::joinPath("Content/Episodes", "prince/01_castle1/3.layer").c_str());
			_tileMap->ReadLayerConfiguration(LayerType::Other, layerFile, { .Depth = 100, .SpeedX = 1, .SpeedY = 1  });
		}
		{
			auto layerFile = IFileStream::createFileHandle(FileSystem::joinPath("Content/Episodes", "prince/01_castle1/5.layer").c_str());
			_tileMap->ReadLayerConfiguration(LayerType::Other, layerFile, { .Depth = -100, .SpeedX = 0.5f, .SpeedY = 0.102996826171875f, .RepeatX = true });
		}
		{
			auto layerFile = IFileStream::createFileHandle(FileSystem::joinPath("Content/Episodes", "prince/01_castle1/6.layer").c_str());
			_tileMap->ReadLayerConfiguration(LayerType::Other, layerFile, { .Depth = -200, .SpeedX = 0.25f, .SpeedY = 0.05999755859375f, .RepeatX = true, .RepeatY = true });
		}
		{
			auto animTilesFile = IFileStream::createFileHandle(FileSystem::joinPath("Content/Episodes", "prince/01_castle1/Animated.tiles").c_str());
			_tileMap->ReadAnimatedTiles(animTilesFile);
		}

		_eventMap = std::make_unique<Events::EventMap>(this, _tileMap->Size());
		{
			auto layerFile = IFileStream::createFileHandle(FileSystem::joinPath("Content/Episodes", "prince/01_castle1/Events.layer").c_str());
			_eventMap->ReadEvents(layerFile, 1, _difficulty);
		}

		_levelBounds = _tileMap->LevelBounds();
		_viewBounds = Rectf((float)_levelBounds.X, (float)_levelBounds.Y, (float)_levelBounds.W, (float)_levelBounds.H);
		_viewBoundsTarget = _viewBounds;
	}

	void LevelHandler::OnBeginFrame()
	{
		float timeMult = theApplication().timeMult();

		UpdatePressedActions();

		/*if (nextLevelInit.HasValue) {
			bool playersReady = true;
			foreach(Player player in players) {
				// Exit type is already provided
				playersReady &= player.OnLevelChanging(ExitType.None);
			}

			if (playersReady) {
				if (levelChangeTimer > 0) {
					levelChangeTimer -= timeMult;
				} else {
					root.ChangeLevel(nextLevelInit.Value);
					nextLevelInit = null;
					initState = InitState.Disposed;
					return;
				}
			}
		}*/

		if (_difficulty != GameDifficulty::Multiplayer) {
			if (!_players.empty()) {
				auto& pos = _players[0]->GetPos();
				int tx1 = (int)pos.X >> 5;
				int ty1 = (int)pos.Y >> 5;
				int tx2 = tx1;
				int ty2 = ty1;

#if ENABLE_SPLITSCREEN
				for (int i = 1; i < players.Count; i++) {
					Vector3 pos2 = players[i].Transform.Pos;
					int tx = (int)pos2.X >> 5;
					int ty = (int)pos2.Y >> 5;
					if (tx1 > tx) {
						tx1 = tx;
					} else if (tx2 < tx) {
						tx2 = tx;
					}
					if (ty1 > ty) {
						ty1 = ty;
					} else if (ty2 < ty) {
						ty2 = ty;
					}
				}
#endif

				constexpr int ActivateTileRange = 26;
				tx1 -= ActivateTileRange;
				ty1 -= ActivateTileRange;
				tx2 += ActivateTileRange;
				ty2 += ActivateTileRange;

				for (int i = (int)_actors.size() - 1; i >= 0; i--) {
					if (_actors[i]->OnTileDeactivate(tx1 - 2, ty1 - 2, tx2 + 2, ty2 + 2)) {
						// Actor was deactivated
					}
				}

				_eventMap->ActivateEvents(tx1, ty1, tx2, ty2, true);
			}

			_eventMap->ProcessGenerators(timeMult);
		}

		// Weather
		/*if (_weatherType != WeatherType.None && commonResources.Graphics != null) {
			// ToDo: Apply weather effect to all other cameras too
			Vector3 viewPos = cameras[0].Transform.Pos;
			for (int i = 0; i < weatherIntensity; i++) {
				TileMap.DebrisCollisionAction collisionAction;
				if (weatherOutdoors) {
					collisionAction = TileMap.DebrisCollisionAction.Disappear;
				} else {
					collisionAction = (MathF.Rnd.NextFloat() > 0.7f
						? TileMap.DebrisCollisionAction.None
						: TileMap.DebrisCollisionAction.Disappear);
				}

				Vector3 debrisPos = viewPos + MathF.Rnd.NextVector3((LevelRenderSetup.TargetSize.X / -2) - 40,
								  (LevelRenderSetup.TargetSize.Y * -2 / 3), MainPlaneZ,
								  LevelRenderSetup.TargetSize.X + 120, LevelRenderSetup.TargetSize.Y, 0);

				if (weatherType == WeatherType.Rain) {
					GraphicResource res = commonResources.Graphics["Rain"];
					Material material = res.Material.Res;
					Texture texture = material.MainTexture.Res;

					float scale = MathF.Rnd.NextFloat(0.4f, 1.1f);
					float speedX = MathF.Rnd.NextFloat(2.2f, 2.7f) * scale;
					float speedY = MathF.Rnd.NextFloat(7.6f, 8.6f) * scale;

					debrisPos.Z = MainPlaneZ * scale;

					tileMap.CreateDebris(new TileMap.DestructibleDebris {
						Pos = debrisPos,
						Size = res.Base.FrameDimensions,
						Speed = new Vector2(speedX, speedY),

						Scale = scale,
						Angle = MathF.Atan2(speedY, speedX),
						Alpha = 1f,

						Time = 180f,

						Material = material,
						MaterialOffset = texture.LookupAtlas(res.FrameOffset + MathF.Rnd.Next(res.FrameCount)),

						CollisionAction = collisionAction
					});
				} else {
					GraphicResource res = commonResources.Graphics["Snow"];
					Material material = res.Material.Res;
					Texture texture = material.MainTexture.Res;

					float scale = MathF.Rnd.NextFloat(0.4f, 1.1f);
					float speedX = MathF.Rnd.NextFloat(-1.6f, -1.2f) * scale;
					float speedY = MathF.Rnd.NextFloat(3f, 4f) * scale;
					float accel = MathF.Rnd.NextFloat(-0.008f, 0.008f) * scale;

					debrisPos.Z = MainPlaneZ * scale;

					tileMap.CreateDebris(new TileMap.DestructibleDebris {
						Pos = debrisPos,
						Size = res.Base.FrameDimensions,
						Speed = new Vector2(speedX, speedY),
						Acceleration = new Vector2(accel, -MathF.Abs(accel)),

						Scale = scale,
						Angle = MathF.Rnd.NextFloat() * MathF.TwoPi,
						AngleSpeed = speedX * 0.02f,
						Alpha = 1f,

						Time = 180f,

						Material = material,
						MaterialOffset = texture.LookupAtlas(res.FrameOffset + MathF.Rnd.Next(res.FrameCount)),

						CollisionAction = collisionAction
					});
				}
			}
		}

		// Active Boss
		if (activeBoss != null && activeBoss.Scene == null) {
			activeBoss = null;

			Hud hud = rootObject.GetComponent<Hud>();
			if (hud != null) {
				hud.ActiveBoss = null;
			}

			InitLevelChange(ExitType.Normal, null);
		}*/
	}

	void LevelHandler::OnEndFrame()
	{
		float timeMult = theApplication().timeMult();

		ResolveCollisions(timeMult);

		// Ambient Light Transition
		if (_ambientLightCurrent != _ambientLightTarget) {
			float step = timeMult * 0.012f;
			if (std::abs(_ambientLightCurrent - _ambientLightTarget) < step) {
				_ambientLightCurrent = _ambientLightTarget;
			} else {
				_ambientLightCurrent += step * ((_ambientLightTarget < _ambientLightCurrent) ? -1 : 1);
			}
		}

		UpdateCamera(timeMult);

		// TODO: DEBUG
#if _DEBUG
		static float _debugRefresh = 0;
		static int _debugFramesCount = 0;
		_debugFramesCount++;
		if (_debugRefresh <= 0) {
			_debugRefresh = 60;

			std::string text = "Jazz² Resurrection | Time: ";
			text += std::to_string(timeMult);
			text += ", fps: ";
			text += std::to_string((int)((1.0f / timeMult) * 60.0f));
			text += ", camera: {";
			text += std::to_string(_cameraLastPos.X);
			text += ", ";
			text += std::to_string(_cameraLastPos.Y);
			text += "}; player: {";
			text += std::to_string(_players[0]->_pos.X);
			text += ", ";
			text += std::to_string(_players[0]->_pos.Y);
			text += "}; dirty: ";
			text += std::to_string(_debugActorDirtyCount);
			text += ", pairs: ";
			text += std::to_string(_debugCollisionPairCount);
			text += ", AABB: {";
			text += std::to_string(_players[0]->AABBInner.L);
			text += ", ";
			text += std::to_string(_players[0]->AABBInner.T);
			text += ", ";
			text += std::to_string(_players[0]->AABBInner.R);
			text += ", ";
			text += std::to_string(_players[0]->AABBInner.B);
			text += "}, level: {";
			text += std::to_string(_levelBounds.X);
			text += ", ";
			text += std::to_string(_levelBounds.Y);
			text += ", ";
			text += std::to_string(_levelBounds.W);
			text += ", ";
			text += std::to_string(_levelBounds.H);
			text += "}";
			theApplication().gfxDevice().setWindowTitle(text.c_str());

			_debugActorDirtyCount = 0;
			_debugCollisionPairCount = 0;
			_debugFramesCount = 0;
		} else {
			_debugRefresh -= timeMult;
		}
#endif
	}

	void LevelHandler::OnRootViewportResized(int width, int height)
	{
		constexpr float defaultRatio = (float)DefaultWidth / DefaultHeight;
		float currentRatio = (float)width / height;

		int w, h;
		if (currentRatio > defaultRatio) {
			w = std::min(DefaultWidth, width);
			h = (int)(w / currentRatio);
		} else if (currentRatio < defaultRatio) {
			h = std::min(DefaultHeight, height);
			w = (int)(h * currentRatio);
		} else {
			w = std::min(DefaultWidth, width);
			h = std::min(DefaultHeight, height);
		}

		Viewport::chain().clear();

		if (_viewTexture == nullptr) {
			_viewTexture = std::make_unique<Texture>(nullptr, Texture::Format::RGB8, w, h);
		} else {
			_viewTexture->init(nullptr, Texture::Format::RGB8, w, h);
		}
		_viewTexture->setMagFiltering(SamplerFilter::Nearest);

		_view = std::make_unique<Viewport>(_viewTexture.get(), Viewport::DepthStencilFormat::DEPTH24_STENCIL8);
		_view->setViewportRect(0, 0, w, h);

		if (_camera == nullptr) {
			_camera = std::make_unique<Camera>();
		}
		_camera->setOrthoProjection(w * (-0.5f), w * (+0.5f), h * (-0.5f), h * (+0.5f));
		_view->setCamera(_camera.get());
		_view->setRootNode(_rootNode.get());

		Viewport::chain().push_back(_view.get());

		if (_viewSprite == nullptr) {
			SceneNode& rootNode = theApplication().rootNode();
			_viewSprite = std::make_unique<Sprite>(&rootNode, _viewTexture.get(), 0.0f, 0.0f);
		}
		_viewSprite->setTexRect(Recti(0, 0, w, h));
		_viewSprite->setSize((float)width, (float)height);
		_viewSprite->setPosition(0, 0);
		_viewSprite->setBlendingEnabled(false);
	}

	void LevelHandler::OnKeyPressed(const KeyboardEvent& event)
	{
		// TODO
	}

	void LevelHandler::OnKeyReleased(const KeyboardEvent& event)
	{
		// TODO
	}

	void LevelHandler::AddActor(const std::shared_ptr<ActorBase>& actor)
	{
		actor->SetParent(_rootNode.get());

		if ((actor->CollisionFlags & CollisionFlags::ForceDisableCollisions) != CollisionFlags::ForceDisableCollisions) {
			actor->UpdateAABB();
			actor->CollisionProxyID = _collisions.CreateProxy(actor->AABB, actor.get());
		}

		_actors.emplace_back(actor);
	}

	void LevelHandler::WarpCameraToTarget(const std::shared_ptr<ActorBase>& actor)
	{
		// TODO: Allow multiple cameras
		if (_players[0] != actor.get()) {
			return;
		}

		Vector2f focusPos = actor->_pos;

		_cameraPos.X = focusPos.X;
		_cameraPos.Y = focusPos.Y;
		_cameraLastPos = _cameraPos;
		_cameraDistanceFactor.X = 0.0f;
		_cameraDistanceFactor.Y = 0.0f;
	}

	bool LevelHandler::IsPositionEmpty(ActorBase* self, const AABBf& aabb, bool downwards, __out ActorBase** collider)
	{
		*collider = nullptr;

		if ((self->CollisionFlags & CollisionFlags::CollideWithTileset) == CollisionFlags::CollideWithTileset) {
			if (_tileMap != nullptr && !_tileMap->IsTileEmpty(aabb, downwards)) {
				return false;
			}
		}

		// Check for solid objects
		if ((self->CollisionFlags & CollisionFlags::CollideWithSolidObjects) == CollisionFlags::CollideWithSolidObjects) {
			ActorBase* colliderActor = nullptr;
			FindCollisionActorsByAABB(self, aabb, [&](ActorBase* actor) -> bool {
				if ((actor->CollisionFlags & CollisionFlags::IsSolidObject) != CollisionFlags::IsSolidObject) {
					return true;
				}

				Actors::SolidObjectBase* solidObject = dynamic_cast<Actors::SolidObjectBase*>(actor);
				if (solidObject == nullptr || !solidObject->IsOneWay || downwards) {
					colliderActor = actor;
					return false;
				}

				return true;
			});

			*collider = colliderActor;
		}

		return (*collider == nullptr);
	}

	void LevelHandler::FindCollisionActorsByAABB(ActorBase* self, const AABBf& aabb, const std::function<bool(ActorBase*)>& callback)
	{
		struct QueryHelper {
			const LevelHandler* LevelHandler;
			const ActorBase* Self;
			const AABBf& AABB;
			const std::function<bool(ActorBase*)>& Callback;

			bool OnCollisionQuery(int32_t nodeId) {
				ActorBase* actor = (ActorBase*)LevelHandler->_collisions.GetUserData(nodeId);
				if (Self == actor || (actor->CollisionFlags & CollisionFlags::CollideWithOtherActors) != CollisionFlags::CollideWithOtherActors) {
					return true;
				}
				if (actor->IsCollidingWith(AABB)) {
					return Callback(actor);
				}
				return true;
			}
		};

		QueryHelper helper = { this, self, aabb, callback };
		_collisions.Query(&helper, aabb);
	}

	void LevelHandler::FindCollisionActorsByRadius(float x, float y, float radius, const std::function<bool(ActorBase*)>& callback)
	{
		AABBf aabb = AABBf(x - radius, y - radius, x + radius, y + radius);
		float radiusSquared = (radius * radius);

		struct QueryHelper {
			const LevelHandler* LevelHandler;
			const float x, y;
			const float RadiusSquared;
			const std::function<bool(ActorBase*)>& Callback;

			bool OnCollisionQuery(int32_t nodeId) {
				ActorBase* actor = (ActorBase*)LevelHandler->_collisions.GetUserData(nodeId);
				if ((actor->CollisionFlags & CollisionFlags::CollideWithOtherActors) != CollisionFlags::CollideWithOtherActors) {
					return true;
				}

				// Find the closest point to the circle within the rectangle
				float closestX = std::clamp(x, actor->AABB.L, actor->AABB.R);
				float closestY = std::clamp(y, actor->AABB.T, actor->AABB.B);

				// Calculate the distance between the circle's center and this closest point
				float distanceX = (x - closestX);
				float distanceY = (y - closestY);

				// If the distance is less than the circle's radius, an intersection occurs
				float distanceSquared = (distanceX * distanceX) + (distanceY * distanceY);
				if (distanceSquared < RadiusSquared) {
					return Callback(actor);
				}

				return true;
			}
		};

		QueryHelper helper = { this, x, y, radiusSquared, callback };
		_collisions.Query(&helper, aabb);
	}

	void LevelHandler::GetCollidingPlayers(const AABBf& aabb, const std::function<bool(ActorBase*)> callback)
	{
		for (auto& player : _players) {
			if (aabb.Overlaps(player->AABB)) {
				if (!callback(player)) {
					break;
				}
			}
		}
	}

	void LevelHandler::HandleGameOver()
	{
		// TODO: Implement Game Over screen
		//root.ShowMainMenu(false);
	}

	bool LevelHandler::HandlePlayerDied(const std::shared_ptr<ActorBase>& player)
	{
		// TODO
		/*if (_activeBoss != null) {
			if (activeBoss.HandlePlayerDied()) {
				activeBoss = null;

				Hud hud = rootObject.GetComponent<Hud>();
				if (hud != null) {
					hud.ActiveBoss = null;
				}

#if !DISABLE_SOUND
				if (music != null) {
					music.FadeOut(1.8f);
				}

				// Load default music again
				music = new OpenMptStream(musicPath, true);
				music.BeginFadeIn(0.4f);
				DualityApp.Sound.PlaySound(music);
#endif
			}
		}*/

		// Single player can respawn immediately
		return true;
	}

	bool LevelHandler::PlayerActionPressed(int index, PlayerActions action, bool includeGamepads)
	{
		// TODO
		if (index != 0) {
			return false;
		}

		return ((_pressedActions & (1 << (int)action)) != 0);
	}

	__success(return) bool LevelHandler::PlayerActionPressed(int index, PlayerActions action, bool includeGamepads, __out bool& isGamepad)
	{
		// TODO
		if (index != 0) {
			return false;
		}

		isGamepad = false;
		return ((_pressedActions & (1 << (int)action)) != 0);
	}

	bool LevelHandler::PlayerActionHit(int index, PlayerActions action, bool includeGamepads)
	{
		// TODO
		if (index != 0) {
			return false;
		}

		return ((_pressedActions & ((1 << (int)action) | (1 << (16 + (int)action)))) == (1 << (int)action));
	}

	__success(return) bool LevelHandler::PlayerActionHit(int index, PlayerActions action, bool includeGamepads, __out bool& isGamepad)
	{
		// TODO
		if (index != 0) {
			return false;
		}

		isGamepad = false;
		return ((_pressedActions & ((1 << (int)action) | (1 << (16 + (int)action)))) == (1 << (int)action));
	}

	float LevelHandler::PlayerHorizontalMovement(int index)
	{
		// TODO
		if (index != 0) {
			return 0;
		}

		if ((_pressedActions & (1 << (int)PlayerActions::Right)) != 0) {
			return 1.0f;
		} else if ((_pressedActions & (1 << (int)PlayerActions::Left)) != 0) {
			return -1.0f;
		}

		return 0.0f;
	}

	float LevelHandler::PlayerVerticalMovement(int index)
	{
		// TODO
		if (index != 0) {
			return 0;
		}

		if ((_pressedActions & (1 << (int)PlayerActions::Up)) != 0) {
			return -1.0f;
		} else if ((_pressedActions & (1 << (int)PlayerActions::Down)) != 0) {
			return 1.0f;
		}

		return 0.0f;
	}

	void LevelHandler::PlayerFreezeMovement(int index, bool enable)
	{
		// TODO
	}

	void LevelHandler::ResolveCollisions(float timeMult)
	{
		auto actor = _actors.begin();
		while (actor != _actors.end()) {
			if (((*actor)->CollisionFlags & CollisionFlags::IsDestroyed) == CollisionFlags::IsDestroyed) {
				if (((*actor)->CollisionFlags & CollisionFlags::ForceDisableCollisions) != CollisionFlags::ForceDisableCollisions) {
					_collisions.DestroyProxy((*actor)->CollisionProxyID);
					(*actor)->CollisionProxyID = Collisions::NullNode;
				}

				actor = _actors.erase(actor);
				continue;
			}
			
			if (((*actor)->CollisionFlags & CollisionFlags::IsDirty) == CollisionFlags::IsDirty) {
				if ((*actor)->CollisionProxyID == Collisions::NullNode) {
					continue;
				}

				(*actor)->UpdateAABB();
				_collisions.MoveProxy((*actor)->CollisionProxyID, (*actor)->AABB, (*actor)->_speed * timeMult);
				(*actor)->CollisionFlags &= ~CollisionFlags::IsDirty;

#if _DEBUG
				_debugActorDirtyCount++;
#endif
			}
			++actor;
		}

		struct UpdatePairsHelper {
			void OnPairAdded(void* proxyA, void* proxyB) {
				ActorBase* actorA = (ActorBase*)proxyA;
				ActorBase* actorB = (ActorBase*)proxyB;
				if (((actorA->CollisionFlags | actorB->CollisionFlags) & CollisionFlags::CollideWithOtherActors) != CollisionFlags::CollideWithOtherActors) {
					return;
				}
				if (actorA->GetHealth() <= 0 || actorB->GetHealth() <= 0) {
					return;
				}

				if (actorA->IsCollidingWith(actorB)) {
					if (!actorA->OnHandleCollision(actorB)) {
						actorB->OnHandleCollision(actorA);
					}
				}

#if _DEBUG
				_debugCollisionPairCount++;
#endif
			}
		};
		UpdatePairsHelper helper;
		_collisions.UpdatePairs(&helper);
	}

	void LevelHandler::InitializeCamera()
	{
		if (_players.empty()) {
			return;
		}

		auto targetObj = _players[0];

		// The position to focus on
		Vector2f focusPos = targetObj->_pos;
		Vector2i halfView = _view->size() / 2;

		// Clamp camera position to level bounds
		if (_viewBounds.W > halfView.X * 2) {
			_cameraPos.X = std::round(std::clamp(focusPos.X, _viewBounds.X + halfView.X, _viewBounds.X + _viewBounds.W - halfView.X));
		} else {
			_cameraPos.X = std::round(_viewBounds.X + _viewBounds.W * 0.5f);
		}
		if (_viewBounds.H > halfView.Y * 2) {
			_cameraPos.Y = std::round(std::clamp(focusPos.Y, _viewBounds.Y + halfView.Y, _viewBounds.Y + _viewBounds.H - halfView.Y));
		} else {
			_cameraPos.Y = std::round(_viewBounds.Y + _viewBounds.H * 0.5f);
		}

		_cameraLastPos = _cameraPos;
		_camera->setView(_cameraPos, 0.0f, 1.0f);
	}

	void LevelHandler::UpdateCamera(float timeMult)
	{
		if (_players.empty()) {
			return;
		}

		auto targetObj = _players[0];

		// View Bounds Animation
		if (_viewBounds != _viewBoundsTarget) {
			if (std::abs(_viewBounds.X - _viewBoundsTarget.X) < 2.0f) {
				_viewBounds = _viewBoundsTarget;
			} else {
				constexpr float transitionSpeed = 0.02f;
				float dx = (_viewBoundsTarget.X - _viewBounds.X) * transitionSpeed * timeMult;
				_viewBounds.X += dx;
				_viewBounds.W -= dx;
			}
		}

		// The position to focus on
		Vector2f focusPos = targetObj->_pos;

		_cameraLastPos.X = lerp(_cameraLastPos.X, focusPos.X, 0.5f * timeMult);
		_cameraLastPos.Y = lerp(_cameraLastPos.Y, focusPos.Y, 0.5f * timeMult);

		Vector2i halfView = _view->size() / 2;

		Vector2f speed = targetObj->_speed;
		_cameraDistanceFactor.X = lerp(_cameraDistanceFactor.X, speed.X * 8.0f, 0.2f * timeMult);
		_cameraDistanceFactor.Y = lerp(_cameraDistanceFactor.Y, speed.Y * 5.0f, 0.04f * timeMult);

		if (_shakeDuration > 0.0f) {
			_shakeDuration -= timeMult;

			if (_shakeDuration <= 0.0f) {
				_shakeOffset = Vector2f::Zero;
			} else {
				float shakeFactor = 0.1f * timeMult;
				_shakeOffset.X = lerp(_shakeOffset.X, nCine::Random().NextFloat(-0.2f, 0.2f) * halfView.X, shakeFactor) * std::min(_shakeDuration * 0.1f, 1.0f);
				_shakeOffset.Y = lerp(_shakeOffset.Y, nCine::Random().NextFloat(-0.2f, 0.2f) * halfView.Y, shakeFactor) * std::min(_shakeDuration * 0.1f, 1.0f);
			}
		}

		// Clamp camera position to level bounds
		if (_viewBounds.W > halfView.X * 2) {
			_cameraPos.X = std::round(std::clamp(_cameraLastPos.X + _cameraDistanceFactor.X, _viewBounds.X + halfView.X, _viewBounds.X + _viewBounds.W - halfView.X) + _shakeOffset.X);
		} else {
			_cameraPos.X = std::round(_viewBounds.X + _viewBounds.W * 0.5f + _shakeOffset.X);
		}
		if (_viewBounds.H > halfView.Y * 2) {
			_cameraPos.Y = std::round(std::clamp(_cameraLastPos.Y + _cameraDistanceFactor.Y, _viewBounds.Y + halfView.Y, _viewBounds.Y + _viewBounds.H - halfView.Y) + _shakeOffset.Y);
		} else {
			_cameraPos.Y = std::round(_viewBounds.Y + _viewBounds.H * 0.5f + _shakeOffset.Y);
		}

		_camera->setView(_cameraPos, 0.0f, 1.0f);
	}

	void LevelHandler::UpdatePressedActions()
	{
		auto& keyState = theApplication().inputManager().keyboardState();

		_pressedActions = ((_pressedActions & 0xffff) << 16);

		if (keyState.isKeyDown(KeySym::LEFT)) {
			_pressedActions |= (1 << (int)PlayerActions::Left);
		}
		if (keyState.isKeyDown(KeySym::RIGHT)) {
			_pressedActions |= (1 << (int)PlayerActions::Right);
		}
		if (keyState.isKeyDown(KeySym::UP)) {
			_pressedActions |= (1 << (int)PlayerActions::Up);
		}
		if (keyState.isKeyDown(KeySym::DOWN)) {
			_pressedActions |= (1 << (int)PlayerActions::Down);
		}
		if (keyState.isKeyDown(KeySym::SPACE)) {
			_pressedActions |= (1 << (int)PlayerActions::Fire);
		}
		if (keyState.isKeyDown(KeySym::V)) {
			_pressedActions |= (1 << (int)PlayerActions::Jump);
		}
		if (keyState.isKeyDown(KeySym::C)) {
			_pressedActions |= (1 << (int)PlayerActions::Run);
		}
		if (keyState.isKeyDown(KeySym::X)) {
			_pressedActions |= (1 << (int)PlayerActions::SwitchWeapon);
		}
	}
}