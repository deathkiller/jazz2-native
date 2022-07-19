#include "LevelHandler.h"
#include "../Common.h"

#include "../nCine/PCApplication.h"
#include "../nCine/IAppEventHandler.h"
#include "../nCine/Input/IInputEventHandler.h"
#include "../nCine/Graphics/Camera.h"
#include "../nCine/Graphics/Sprite.h"
#include "../nCine/Graphics/Texture.h"
#include "../nCine/Graphics/Viewport.h"
#include "../nCine/Graphics/RenderQueue.h"
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
	LevelHandler::LevelHandler(IRootController* root, const LevelInitialization& levelInit)
		:
		_root(root),
		_eventSpawner(this),
		_levelFileName(levelInit.LevelName),
		_episodeName(levelInit.EpisodeName),
		_difficulty(levelInit.Difficulty),
		_reduxMode(levelInit.ReduxMode),
		_cheatsUsed(levelInit.CheatsUsed),
		_shakeDuration(0.0f),
		_waterLevel(FLT_MAX),
		_ambientLightDefault(1.0f),
		_ambientLightCurrent(1.0f),
		_ambientLightTarget(1.0f),
#if ENABLE_POSTPROCESSING
		_downsamplePass(this),
		_blurPass1(this),
		_blurPass2(this),
		_blurPass3(this),
		_blurPass4(this),
#endif
		_pressedActions(0)
	{
		auto& resolver = Jazz2::ContentResolver::Current();
		resolver.BeginLoading();

		_rootNode = std::make_unique<SceneNode>();

		if (!ContentResolver::Current().LoadLevel(this, _episodeName + "/" + _levelFileName, _difficulty)) {
			LOGE("Cannot load specified level");
		}

		// Process carry overs
		for (int i = 0; i < _countof(levelInit.PlayerCarryOvers); i++) {
			if (levelInit.PlayerCarryOvers[i].Type == PlayerType::None) {
				continue;
			}

			Vector2 spawnPosition = _eventMap->GetSpawnPosition(levelInit.PlayerCarryOvers[i].Type);
			if (spawnPosition.X < 0.0f && spawnPosition.Y < 0.0f) {
				spawnPosition = _eventMap->GetSpawnPosition(PlayerType::Jazz);
				if (spawnPosition.X < 0.0f && spawnPosition.Y < 0.0f) {
					continue;
				}
			}

			std::shared_ptr<Actors::Player> player = std::make_shared<Actors::Player>();
			uint8_t playerParams[2] = { (uint8_t)levelInit.PlayerCarryOvers[i].Type, (uint8_t)i };
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

			ptr->ReceiveLevelCarryOver(levelInit.ExitType, levelInit.PlayerCarryOvers[i]);
		}

		// TODO
		//_commonResources = ContentResolver::Current().RequestMetadata("Common/Scenery");

		_eventMap->PreloadEventsAsync();

		resolver.EndLoading();
	}

	LevelHandler::~LevelHandler()
	{
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

	void LevelHandler::SetAmbientLight(float value)
	{
		_ambientLightTarget = value;
	}

	void LevelHandler::OnLevelLoaded(const std::string& name, const std::string& nextLevel, const std::string& secretLevel, std::unique_ptr<Tiles::TileMap>& tileMap, std::unique_ptr<Events::EventMap>& eventMap, const std::string& musicPath, float ambientLight)
	{
		//_name = name;
		_defaultNextLevel = nextLevel;
		_defaultSecretLevel = secretLevel;

		_tileMap = std::move(tileMap);
		_tileMap->setParent(_rootNode.get());

		_eventMap = std::move(eventMap);

		_levelBounds = _tileMap->LevelBounds();
		_viewBounds = Rectf((float)_levelBounds.X, (float)_levelBounds.Y, (float)_levelBounds.W, (float)_levelBounds.H);
		_viewBoundsTarget = _viewBounds;		

		_ambientLightDefault = ambientLight;
		_ambientLightCurrent = ambientLight;
		_ambientLightTarget = ambientLight;

		_musicPath = musicPath;
		if (!musicPath.empty()) {
			_music = std::make_unique<AudioStreamPlayer>(FileSystem::joinPath("Content/Music", musicPath).c_str());
			_music->setLooping(true);
			_music->setGain(0.2f);
			_music->play();
		}
	}

	void LevelHandler::OnBeginFrame()
	{
		float timeMult = theApplication().timeMult();

		UpdatePressedActions();

		// Destroy stopped players
		for (int i = _playingSounds.size() - 1; i >= 0; i--) {
			if (_playingSounds[i]->state() == IAudioPlayer::PlayerState::Stopped) {
				_playingSounds.erase(&_playingSounds[i]);
			} else {
				break;
			}
		}

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

		_lightingView->setClearColor(_ambientLightCurrent, 0.0f, 0.0f, 1.0f);

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

	void LevelHandler::OnInitializeViewport(int width, int height)
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
			InitializeCamera();
		}
		_camera->setOrthoProjection(w * (-0.5f), w * (+0.5f), h * (-0.5f), h * (+0.5f));
		_view->setCamera(_camera.get());
		_view->setRootNode(_rootNode.get());

#if ENABLE_POSTPROCESSING
		if (_lightingRenderer == nullptr) {
			constexpr char LightingVs[] = R"(
uniform mat4 uProjectionMatrix;
uniform mat4 uViewMatrix;

layout (std140) uniform InstanceBlock
{
	mat4 modelMatrix;
	vec4 color;
	vec4 texRect;
	vec2 spriteSize;
};

out vec4 vTexCoords;
out vec4 vColor;

void main()
{
	vec2 aPosition = vec2(0.5 - float(gl_VertexID >> 1), 0.5 - float(gl_VertexID % 2));
	vec4 position = vec4(aPosition.x * spriteSize.x, aPosition.y * spriteSize.y, 0.0, 1.0);

	gl_Position = uProjectionMatrix * uViewMatrix * modelMatrix * position;
	vTexCoords = texRect;
	vColor = vec4(color.x, color.y, aPosition.x * 2.0, aPosition.y * 2.0);
}
)";

			constexpr char LightingFs[] = R"(
#ifdef GL_ES
precision mediump float;
#endif
uniform vec2 ViewSize;

uniform sampler2D uTexture; // Normal

in vec4 vTexCoords;
in vec4 vColor;

out vec4 fragColor;

float lightBlend(float t) {
	return t * t;
}

void main() {
	vec2 center = vTexCoords.xy;
	float radiusNear = vTexCoords.z;
	float intensity = vColor.r;
	float brightness = vColor.g;

	float dist = distance(vec2(0.0, 0.0), vec2(vColor.z, vColor.w));
	if (dist > 1.0) {
		fragColor = vec4(0.0, 0.0, 0.0, 1.0);
		return;
	}

	// TODO
	/*vec4 clrNormal = texture(uTexture, vec2(gl_FragCoord) / ViewSize);
	vec3 normal = normalize(clrNormal.xyz - vec3(0.5, 0.5, 0.5));
	normal.z = -normal.z;

	vec3 lightDir = vec3((center.x - gl_FragCoord.x), (center.y - gl_FragCoord.y), 0);

	// Diffuse lighting
	float diffuseFactor = 1.0 - max(dot(normal, normalize(lightDir)), 0.0);
	diffuseFactor = diffuseFactor * 0.8 + 0.2;*/
	float diffuseFactor = 1.0f;
	
	float strength = diffuseFactor * lightBlend(clamp(1.0 - ((dist - radiusNear) / (1.0 - radiusNear)), 0.0, 1.0));
	fragColor = vec4(strength * intensity, strength * brightness, 0.0, 1.0);
}
)";

			constexpr char BlurFs[] = R"(
#ifdef GL_ES
precision mediump float;
#endif
uniform sampler2D uTexture;
uniform vec2 uPixelOffset;
uniform vec2 uDirection;
in vec2 vTexCoords;
out vec4 fragColor;
void main()
{
	vec4 color = vec4(0.0);
	vec2 off1 = vec2(1.3846153846) * uPixelOffset * uDirection;
	vec2 off2 = vec2(3.2307692308) * uPixelOffset * uDirection;
	color += texture(uTexture, vTexCoords) * 0.2270270270;
	color += texture(uTexture, vTexCoords + off1) * 0.3162162162;
	color += texture(uTexture, vTexCoords - off1) * 0.3162162162;
	color += texture(uTexture, vTexCoords + off2) * 0.0702702703;
	color += texture(uTexture, vTexCoords - off2) * 0.0702702703;
	fragColor = color;
}
)";

			constexpr char DownsampleFs[] = R"(
#ifdef GL_ES
precision mediump float;
#endif
uniform sampler2D uTexture;
uniform vec2 uPixelOffset;
in vec2 vTexCoords;
out vec4 fragColor;
void main()
{
	vec4 color = texture(uTexture, vTexCoords);
	color += texture(uTexture, vTexCoords + vec2(0.0, uPixelOffset.y));
	color += texture(uTexture, vTexCoords + vec2(uPixelOffset.x, 0.0));
	color += texture(uTexture, vTexCoords + uPixelOffset);
	fragColor = vec4(0.25) * color;
}
)";

			constexpr char CombineFs[] = R"(
#ifdef GL_ES
precision mediump float;
#endif
uniform vec2 ViewSize;

uniform sampler2D uTexture;
uniform sampler2D lightTex;
uniform sampler2D blurHalfTex;
uniform sampler2D blurQuarterTex;

uniform float ambientLight;
uniform vec4 darknessColor;

in vec2 vTexCoords;
in vec4 vColor;

out vec4 fragColor;

float lightBlend(float t) {
	return t * t;
}

void main() {
	vec4 blur1 = texture(blurHalfTex, vTexCoords);
	vec4 blur2 = texture(blurQuarterTex, vTexCoords);

	vec4 main = texture(uTexture, vTexCoords);
	vec4 light = texture(lightTex, vTexCoords);

	vec4 blur = (blur1 + blur2) * vec4(0.5);

	float gray = dot(blur.rgb, vec3(0.299, 0.587, 0.114));
	blur = vec4(gray, gray, gray, blur.a);

	fragColor = mix(mix(
		main * (1.0 + light.g),
		blur,
		vec4(clamp((1.0 - light.r) / sqrt(max(ambientLight, 0.35)), 0.0, 1.0))
	), darknessColor, vec4(1.0 - light.r));
	fragColor.a = 1.0;
}
)";

			_lightingShader = std::make_unique<Shader>("Lighting", Shader::LoadMode::STRING, LightingVs, LightingFs);
			_blurShader = std::make_unique<Shader>("Blur", Shader::LoadMode::STRING, Shader::DefaultVertex::SPRITE, BlurFs);
			_downsampleShader = std::make_unique<Shader>("Downsample", Shader::LoadMode::STRING, Shader::DefaultVertex::SPRITE, DownsampleFs);
			_combineShader = std::make_unique<Shader>("Combine", Shader::LoadMode::STRING, Shader::DefaultVertex::SPRITE, CombineFs);

			_lightingRenderer = std::make_unique<LightingRenderer>(this);

		}

		if (_lightingBuffer == nullptr) {
			_lightingBuffer = std::make_unique<Texture>(nullptr, Texture::Format::RG8, w, h);
		} else {
			_lightingBuffer->init(nullptr, Texture::Format::RG8, w, h);
		}
		_lightingBuffer->setMagFiltering(SamplerFilter::Nearest);

		_lightingView = std::make_unique<Viewport>(_lightingBuffer.get(), Viewport::DepthStencilFormat::NONE);
		_lightingView->setRootNode(_lightingRenderer.get());
		_lightingView->setCamera(_camera.get());

		_downsamplePass.Initialize(_viewTexture.get(), w / 2, h / 2, Vector2f::Zero);
		_blurPass1.Initialize(_downsamplePass.GetTarget(), w / 2, h / 2, Vector2f(1.0f, 0.0f));
		_blurPass2.Initialize(_blurPass1.GetTarget(), w / 2, h / 2, Vector2f(0.0f, 1.0f));
		_blurPass3.Initialize(_blurPass2.GetTarget(), w / 4, h / 4, Vector2f(1.0f, 0.0f));
		_blurPass4.Initialize(_blurPass3.GetTarget(), w / 4, h / 4, Vector2f(0.0f, 1.0f));

		// Viewports must be registered in reverse order
		_blurPass4.Register();
		_blurPass3.Register();
		_blurPass2.Register();
		_blurPass1.Register();
		_downsamplePass.Register();

		Viewport::chain().push_back(_lightingView.get());

		if (_viewSprite == nullptr) {
			SceneNode& rootNode = theApplication().rootNode();
			_viewSprite = std::make_unique<CombineRenderer>(this);
			_viewSprite->setParent(&rootNode);
		}

		_viewSprite->Initialize();
#else
		if (_viewSprite == nullptr) {
			SceneNode& rootNode = theApplication().rootNode();
			_viewSprite = std::make_unique<Sprite>(&rootNode, _viewTexture.get(), 0.0f, 0.0f);
		}

		_viewSprite->setBlendingEnabled(false);
#endif
		_viewSprite->setSize((float)width, (float)height);
		_viewSprite->setPosition(0, 0);

		Viewport::chain().push_back(_view.get());
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

	void LevelHandler::PlaySfx(AudioBuffer* buffer, const Vector3f& pos, float gain, float pitch)
	{
		auto& player = _playingSounds.emplace_back(std::make_unique<AudioBufferPlayer>(buffer));
		player->setPosition(Vector3f((pos.X - _cameraPos.X) / (DefaultWidth * 3), (pos.Y - _cameraPos.Y) / (DefaultHeight * 3), 0.8f));
		player->setGain(gain * 0.6f);
		player->setPitch(pitch);
		player->play();
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

	void LevelHandler::BeginLevelChange(ExitType exitType, const std::string& nextLevel)
	{
		/*if (initState == InitState.Disposing) {
			return;
		}

		initState = InitState.Disposing;*/

		for (auto& player : _players) {
			player->OnLevelChanging(exitType);
		}

		std::string realNextLevel;
		if (nextLevel.empty()) {
			realNextLevel = (exitType == ExitType::Bonus ? _defaultSecretLevel : _defaultNextLevel);
		} else {
			realNextLevel = nextLevel;
		}

		LevelInitialization levelInit;

		if (!realNextLevel.empty()) {
			int i = realNextLevel.find('/');
			if (i == std::string::npos) {
				levelInit.EpisodeName = _episodeName;
				levelInit.LevelName = realNextLevel;
			} else {
				levelInit.EpisodeName = realNextLevel.substr(0, i);
				levelInit.LevelName = realNextLevel.substr(i + 1);
			}
		}

		levelInit.Difficulty = _difficulty;
		levelInit.ReduxMode = _reduxMode;
		levelInit.CheatsUsed = _cheatsUsed;
		levelInit.ExitType = exitType;

		for (int i = 0; i < _players.size(); i++) {
			levelInit.PlayerCarryOvers[i] = _players[i]->PrepareLevelCarryOver();
		}

		levelInit.LastEpisodeName = _episodeName;

		// TODO
		//_nextLevelInit = levelInit;
		//_levelChangeTimer = 50.0f;

		_root->ChangeLevel(levelInit);
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
		bool isGamepad;
		return PlayerActionPressed(index, action, includeGamepads, isGamepad);
	}

	__success(return) bool LevelHandler::PlayerActionPressed(int index, PlayerActions action, bool includeGamepads, __out bool& isGamepad)
	{
		// TODO
		if (index != 0) {
			return false;
		}

		isGamepad = false;
		if ((_pressedActions & (1 << (int)action)) != 0) {
			return true;
		}
		
		if (includeGamepads) {
			switch (action) {
				case PlayerActions::Left: if (_playerRequiredMovement.X < -0.8f) { isGamepad = true; return true; } break;
				case PlayerActions::Right: if (_playerRequiredMovement.X > 0.8f) { isGamepad = true; return true; } break;
				case PlayerActions::Up: if (_playerRequiredMovement.Y < -0.8f) { isGamepad = true; return true; } break;
				case PlayerActions::Down: if (_playerRequiredMovement.Y > 0.8f) { isGamepad = true; return true; } break;
			}
		}

		return false;
	}

	bool LevelHandler::PlayerActionHit(int index, PlayerActions action, bool includeGamepads)
	{
		bool isGamepad;
		return PlayerActionHit(index, action, includeGamepads, isGamepad);
	}

	__success(return) bool LevelHandler::PlayerActionHit(int index, PlayerActions action, bool includeGamepads, __out bool& isGamepad)
	{
		// TODO
		if (index != 0) {
			return false;
		}

		isGamepad = false;
		if ((_pressedActions & ((1 << (int)action) | (1 << (16 + (int)action)))) == (1 << (int)action)) {
			return true;
		}

		return false;
	}

	float LevelHandler::PlayerHorizontalMovement(int index)
	{
		// TODO
		if (index != 0) {
			return 0.0f;
		}

		if ((_pressedActions & (1 << (int)PlayerActions::Right)) != 0) {
			return 1.0f;
		} else if ((_pressedActions & (1 << (int)PlayerActions::Left)) != 0) {
			return -1.0f;
		}

		return _playerRequiredMovement.X;
	}

	float LevelHandler::PlayerVerticalMovement(int index)
	{
		// TODO
		if (index != 0) {
			return 0.0f;
		}

		if ((_pressedActions & (1 << (int)PlayerActions::Up)) != 0) {
			return -1.0f;
		} else if ((_pressedActions & (1 << (int)PlayerActions::Down)) != 0) {
			return 1.0f;
		}

		return _playerRequiredMovement.Y;
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
		auto& input = theApplication().inputManager();
		auto& keyState = input.keyboardState();

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

		int firstJoy = -1;
		for (int i = 0; i < IInputManager::MaxNumJoysticks; i++) {
			if (input.isJoyPresent(i)) {
				const int numButtons = input.joyNumButtons(i);
				const int numAxes = input.joyNumAxes(i);
				if (numButtons >= 4 && numAxes >= 2) {
					firstJoy = i;
					break;
				}
			}
		}

		if (firstJoy >= 0) {
			const auto& joyState = input.joystickState(firstJoy);
			if (joyState.isButtonPressed(ButtonName::DPAD_LEFT)) {
				_pressedActions |= (1 << (int)PlayerActions::Left);
			}
			if (joyState.isButtonPressed(ButtonName::DPAD_RIGHT)) {
				_pressedActions |= (1 << (int)PlayerActions::Right);
			}
			if (joyState.isButtonPressed(ButtonName::DPAD_UP)) {
				_pressedActions |= (1 << (int)PlayerActions::Up);
			}
			if (joyState.isButtonPressed(ButtonName::DPAD_DOWN)) {
				_pressedActions |= (1 << (int)PlayerActions::Down);
			}

			if (joyState.isButtonPressed(ButtonName::A)) {
				_pressedActions |= (1 << (int)PlayerActions::Jump);
			}
			if (joyState.isButtonPressed(ButtonName::B)) {
				_pressedActions |= (1 << (int)PlayerActions::Run);
			}
			if (joyState.isButtonPressed(ButtonName::X)) {
				_pressedActions |= (1 << (int)PlayerActions::Fire);
			}
			if (joyState.isButtonPressed(ButtonName::Y)) {
				_pressedActions |= (1 << (int)PlayerActions::SwitchWeapon);
			}

			_playerRequiredMovement.X = joyState.axisNormValue(0);
			_playerRequiredMovement.Y = joyState.axisNormValue(1);
		}
	}

#if ENABLE_POSTPROCESSING
	bool LevelHandler::LightingRenderer::OnDraw(RenderQueue& renderQueue)
	{
		_renderCommandsCount = 0;

		// Collect all active light emitters
		// TODO: move this to class variable
		SmallVector<LightEmitter, 8> emittedLights;
		for (auto& actor : _owner->_actors) {
			actor->OnEmitLights(emittedLights);
		}

		auto viewSize = _owner->_viewTexture->size();
		auto viewPos = _owner->_cameraPos;
		for (auto& light : emittedLights) {
			auto command = RentRenderCommand();
			auto instanceBlock = command->material().uniformBlock(Material::InstanceBlockName);
			instanceBlock->uniform(Material::TexRectUniformName)->setFloatValue(light.Pos.X, light.Pos.Y, light.RadiusNear / light.RadiusFar, 0.0f);
			instanceBlock->uniform(Material::SpriteSizeUniformName)->setFloatValue(light.RadiusFar * 2, light.RadiusFar * 2);
			instanceBlock->uniform(Material::ColorUniformName)->setFloatValue(light.Intensity, light.Brightness, 0.0f, 0.0f);
			command->setTransformation(Matrix4x4f::Translation(light.Pos.X, light.Pos.Y, 0));

			renderQueue.addCommand(command);
		}

		return true;
	}

	RenderCommand* LevelHandler::LightingRenderer::RentRenderCommand()
	{
		if (_renderCommandsCount < _renderCommands.size()) {
			RenderCommand* command = _renderCommands[_renderCommandsCount].get();
			_renderCommandsCount++;
			return command;
		} else {
			std::unique_ptr<RenderCommand>& command = _renderCommands.emplace_back(std::make_unique<RenderCommand>());
			command->setType(RenderCommand::CommandTypes::SPRITE);
			command->material().setShader(_owner->_lightingShader.get());
			command->material().setBlendingEnabled(true);
			command->material().setBlendingFactors(GL_SRC_ALPHA, GL_ONE);
			command->material().reserveUniformsDataMemory();
			command->geometry().setDrawParameters(GL_TRIANGLE_STRIP, 0, 4);

			GLUniformCache* textureUniform = command->material().uniform(Material::TextureUniformName);
			if (textureUniform && textureUniform->intValue(0) != 0) {
				textureUniform->setIntValue(0); // GL_TEXTURE0
			}
			return command.get();
		}
	}

	void LevelHandler::BlurRenderPass::Initialize(Texture* source, int width, int height, const Vector2f& direction)
	{
		_source = source;
		_downsampleOnly = (direction.X <= std::numeric_limits<float>::epsilon() && direction.Y <= std::numeric_limits<float>::epsilon());
		_direction = direction;

		if (_camera == nullptr) {
			_camera = std::make_unique<Camera>();
		}
		_camera->setOrthoProjection(width * (-0.5f), width * (+0.5f), height * (-0.5f), height * (+0.5f));
		_camera->setView(0, 0, 0, 1);

		if (_target == nullptr) {
			_target = std::make_unique<Texture>(nullptr, Texture::Format::RGB8, width, height);
		} else {
			_target->init(nullptr, Texture::Format::RGB8, width, height);
		}
		_target->setMagFiltering(SamplerFilter::Linear);

		_view = std::make_unique<Viewport>(_target.get(), Viewport::DepthStencilFormat::NONE);
		_view->setRootNode(this);
		_view->setCamera(_camera.get());

		// Prepare render command
		_renderCommand.setType(RenderCommand::CommandTypes::SPRITE);
		_renderCommand.material().setShader(_downsampleOnly ? _owner->_downsampleShader.get() : _owner->_blurShader.get());
		//_renderCommand.material().setBlendingEnabled(true);
		_renderCommand.material().reserveUniformsDataMemory();
		_renderCommand.geometry().setDrawParameters(GL_TRIANGLE_STRIP, 0, 4);

		GLUniformCache* textureUniform = _renderCommand.material().uniform(Material::TextureUniformName);
		if (textureUniform && textureUniform->intValue(0) != 0) {
			textureUniform->setIntValue(0); // GL_TEXTURE0
		}
	}

	void LevelHandler::BlurRenderPass::Register()
	{
		Viewport::chain().push_back(_view.get());
	}

	bool LevelHandler::BlurRenderPass::OnDraw(RenderQueue& renderQueue)
	{
		auto size = _target->size();

		auto instanceBlock = _renderCommand.material().uniformBlock(Material::InstanceBlockName);
		instanceBlock->uniform(Material::TexRectUniformName)->setFloatValue(1.0f, 0.0f, -1.0f, 1.0f);
		instanceBlock->uniform(Material::SpriteSizeUniformName)->setFloatValue(size.X, size.Y);
		instanceBlock->uniform(Material::ColorUniformName)->setFloatVector(Colorf(1.0f, 1.0f, 1.0f, 1.0f).Data());

		_renderCommand.material().uniform("uPixelOffset")->setFloatValue(1.0f / size.X, 1.0f / size.Y);
		if (!_downsampleOnly) {
			_renderCommand.material().uniform("uDirection")->setFloatValue(_direction.X, _direction.Y);
		}
		_renderCommand.material().setTexture(0, *_source);

		renderQueue.addCommand(&_renderCommand);

		return true;
	}

	void LevelHandler::CombineRenderer::Initialize()
	{
		_renderCommand.setType(RenderCommand::CommandTypes::SPRITE);
		_renderCommand.material().setShader(_owner->_combineShader.get());
		//_renderCommand.material().setBlendingEnabled(true);
		_renderCommand.material().reserveUniformsDataMemory();
		_renderCommand.geometry().setDrawParameters(GL_TRIANGLE_STRIP, 0, 4);

		GLUniformCache* textureUniform = _renderCommand.material().uniform(Material::TextureUniformName);
		if (textureUniform && textureUniform->intValue(0) != 0) {
			textureUniform->setIntValue(0); // GL_TEXTURE0
		}
		GLUniformCache* lightTexUniform = _renderCommand.material().uniform("lightTex");
		if (lightTexUniform && lightTexUniform->intValue(0) != 1) {
			lightTexUniform->setIntValue(1); // GL_TEXTURE1
		}
		GLUniformCache* blurHalfTexUniform = _renderCommand.material().uniform("blurHalfTex");
		if (blurHalfTexUniform && blurHalfTexUniform->intValue(0) != 2) {
			blurHalfTexUniform->setIntValue(2); // GL_TEXTURE2
		}
		GLUniformCache* blurQuarterTexUniform = _renderCommand.material().uniform("blurQuarterTex");
		if (blurQuarterTexUniform && blurQuarterTexUniform->intValue(0) != 3) {
			blurQuarterTexUniform->setIntValue(3); // GL_TEXTURE3
		}
	}

	bool LevelHandler::CombineRenderer::OnDraw(RenderQueue& renderQueue)
	{
		_renderCommand.material().setTexture(0, *_owner->_viewTexture);
		_renderCommand.material().setTexture(1, *_owner->_lightingBuffer);
		_renderCommand.material().setTexture(2, *_owner->_blurPass2.GetTarget());
		_renderCommand.material().setTexture(3, *_owner->_blurPass4.GetTarget());

		auto instanceBlock = _renderCommand.material().uniformBlock(Material::InstanceBlockName);
		instanceBlock->uniform(Material::TexRectUniformName)->setFloatValue(1.0f, 0.0f, 1.0f, 0.0f);
		instanceBlock->uniform(Material::SpriteSizeUniformName)->setFloatValue(_size.X, _size.Y);
		instanceBlock->uniform(Material::ColorUniformName)->setFloatVector(Colorf(1.0f, 1.0f, 1.0f, 1.0f).Data());

		renderQueue.addCommand(&_renderCommand);

		return true;
	}
#endif
}