#include "LevelHandler.h"
#include "PreferencesCache.h"
#include "UI/ControlScheme.h"
#include "UI/HUD.h"
#include "../Common.h"

#if defined(WITH_ANGELSCRIPT)
#	include "Scripting/LevelScriptLoader.h"
#endif

#include "../nCine/MainApplication.h"
#include "../nCine/IAppEventHandler.h"
#include "../nCine/ServiceLocator.h"
#include "../nCine/Input/IInputEventHandler.h"
#include "../nCine/Graphics/Camera.h"
#include "../nCine/Graphics/Sprite.h"
#include "../nCine/Graphics/Texture.h"
#include "../nCine/Graphics/Viewport.h"
#include "../nCine/Graphics/RenderQueue.h"
#include "../nCine/Audio/AudioReaderMpt.h"
#include "../nCine/Base/Random.h"

#include "Actors/Player.h"
#include "Actors/SolidObjectBase.h"
#include "Actors/Enemies/Bosses/BossBase.h"
#include "Actors/Environment/IceBlock.h"

#include <float.h>

#include <Utf8.h>
#include <Containers/StaticArray.h>

using namespace nCine;

namespace Jazz2
{
	LevelHandler::LevelHandler(IRootController* root, const LevelInitialization& levelInit)
		: _root(root), _eventSpawner(this), _levelFileName(levelInit.LevelName), _episodeName(levelInit.EpisodeName),
			_difficulty(levelInit.Difficulty), _isReforged(levelInit.IsReforged), _cheatsUsed(levelInit.CheatsUsed), _cheatsBufferLength(0),
			_nextLevelType(ExitType::None), _nextLevelTime(0.0f), _elapsedFrames(0.0f), _checkpointFrames(0.0f),
			_shakeDuration(0.0f), _waterLevel(FLT_MAX), _ambientLightTarget(1.0f), _weatherType(WeatherType::None),
			_downsamplePass(this), _blurPass1(this), _blurPass2(this), _blurPass3(this), _blurPass4(this),
			_pressedKeys((uint32_t)KeySym::COUNT), _pressedActions(0), _overrideActions(0), _playerFrozenEnabled(false),
			_lastPressedNumericKey(UINT32_MAX)
	{
		constexpr float DefaultGravity = 0.3f;

		if (_isReforged) {
			Gravity = DefaultGravity;
		} else {
			Gravity = DefaultGravity * 0.8f;
		}

		auto& resolver = ContentResolver::Get();
		resolver.BeginLoading();

		_noiseTexture = resolver.GetNoiseTexture();

		_rootNode = std::make_unique<SceneNode>();
		_rootNode->setVisitOrderState(SceneNode::VisitOrderState::Disabled);

		if (!ContentResolver::Get().LoadLevel(this, "/"_s.joinWithoutEmptyParts({ _episodeName, _levelFileName }), _difficulty)) {
			LOGE("Cannot load specified level");
			return;
		}

		// Process carry overs
		for (int32_t i = 0; i < countof(levelInit.PlayerCarryOvers); i++) {
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
			player->OnActivated(Actors::ActorActivationDetails(
				this,
				Vector3i(spawnPosition.X + (i * 30), spawnPosition.Y - (i * 30), PlayerZ - i),
				playerParams
			));

			Actors::Player* ptr = player.get();
			_players.push_back(ptr);
			AddActor(player);

			ptr->ReceiveLevelCarryOver(levelInit.LastExitType, levelInit.PlayerCarryOvers[i]);
		}

		_commonResources = resolver.RequestMetadata("Common/Scenery"_s);
		resolver.PreloadMetadataAsync("Common/Explosions"_s);

		// Create HUD
		_hud = std::make_unique<UI::HUD>(this);
		if ((levelInit.LastExitType & ExitType::FastTransition) != ExitType::FastTransition) {
			_hud->BeginFadeIn();
		}

#if defined(WITH_ANGELSCRIPT)
		if (_scripts != nullptr) {
			_scripts->OnLevelLoad();
			// TODO: Call this when all objects are created (first checkpoint)
			_scripts->OnLevelBegin();
		}
#endif

		_eventMap->PreloadEventsAsync();

		resolver.EndLoading();
	}

	LevelHandler::~LevelHandler()
	{
		// Remove nodes from UpscaleRenderPass
		_combineRenderer->setParent(nullptr);
		_hud->setParent(nullptr);
	}

	Recti LevelHandler::LevelBounds() const
	{
		return _levelBounds;
	}

	float LevelHandler::WaterLevel() const
	{
		return _waterLevel;
	}

	const SmallVectorImpl<std::shared_ptr<Actors::ActorBase>>& LevelHandler::GetActors() const
	{
		return _actors;
	}

	const SmallVectorImpl<Actors::Player*>& LevelHandler::GetPlayers() const
	{
		return _players;
	}

	float LevelHandler::GetAmbientLight() const
	{
		return _ambientLightTarget;
	}

	void LevelHandler::SetAmbientLight(float value)
	{
		_ambientLightTarget = value;
	}

	void LevelHandler::OnLevelLoaded(const StringView& fullPath, const StringView& name, const StringView& nextLevel, const StringView& secretLevel, std::unique_ptr<Tiles::TileMap>& tileMap, std::unique_ptr<Events::EventMap>& eventMap, const StringView& musicPath, const Vector4f& ambientColor, WeatherType weatherType, uint8_t weatherIntensity, uint16_t waterLevel, SmallVectorImpl<String>& levelTexts)
	{
		if (!name.empty()) {
			theApplication().gfxDevice().setWindowTitle(StringView(NCINE_APP_NAME " - ") + name);
		} else {
			theApplication().gfxDevice().setWindowTitle(NCINE_APP_NAME);
		}

		_defaultNextLevel = nextLevel;
		_defaultSecretLevel = secretLevel;

		_tileMap = std::move(tileMap);
		_tileMap->setParent(_rootNode.get());

		_eventMap = std::move(eventMap);

		Vector2i levelBounds = _tileMap->LevelBounds();
		_levelBounds = Recti(0, 0, levelBounds.X, levelBounds.Y);
		_viewBounds = Rectf((float)_levelBounds.X, (float)_levelBounds.Y, (float)_levelBounds.W, (float)_levelBounds.H);
		_viewBoundsTarget = _viewBounds;		

		_ambientColor = ambientColor;
		_ambientLightTarget = ambientColor.W;

		_weatherType = weatherType;
		_weatherIntensity = weatherIntensity;
		_waterLevel = waterLevel;

#if defined(WITH_AUDIO)
		if (!musicPath.empty()) {
			_music = ContentResolver::Get().GetMusic(musicPath);
			if (_music != nullptr) {
				_musicCurrentPath = musicPath;
				_musicDefaultPath = _musicCurrentPath;
				_music->setLooping(true);
				_music->setGain(PreferencesCache::MasterVolume * PreferencesCache::MusicVolume);
				_music->setSourceRelative(true);
				_music->play();
			}
		}
#endif

		_levelTexts = std::move(levelTexts);

#if defined(WITH_ANGELSCRIPT) || defined(DEATH_TRACE)
		// TODO: Allow script signing
		if (PreferencesCache::AllowUnsignedScripts) {
			const StringView foundDot = fullPath.findLastOr('.', fullPath.end());
			String scriptPath = (foundDot == fullPath.end() ? StringView(fullPath) : fullPath.prefix(foundDot.begin())) + ".j2as"_s;
			if (fs::IsReadableFile(scriptPath)) {
#	if defined(WITH_ANGELSCRIPT)
				_scripts = std::make_unique<Scripting::LevelScriptLoader>(this, scriptPath);
#	else
				LOGW("Level requires scripting, but scripting support is disabled in this build");
#	endif
			}
		}
#endif
	}

	void LevelHandler::OnBeginFrame()
	{
		float timeMult = theApplication().timeMult();

		UpdatePressedActions();

		if (PlayerActionHit(0, PlayerActions::Menu) && _pauseMenu == nullptr && _nextLevelType == ExitType::None) {
			PauseGame();
		}
#if defined(DEATH_DEBUG)
		if (PlayerActionPressed(0, PlayerActions::ChangeWeapon) && PlayerActionHit(0, PlayerActions::Jump)) {
			_cheatsUsed = true;
			BeginLevelChange(ExitType::Warp | ExitType::FastTransition, nullptr);
		}
#endif
#if defined(WITH_AUDIO)
		// Destroy stopped players and resume music after Sugar Rush
		if (_sugarRushMusic != nullptr && _sugarRushMusic->isStopped()) {
			_sugarRushMusic = nullptr;
			if (_music != nullptr) {
				_music->play();
			}
		}

		for (int32_t i = (int32_t)_playingSounds.size() - 1; i >= 0; i--) {
			if (_playingSounds[i]->isStopped()) {
				_playingSounds.erase(&_playingSounds[i]);
			} else {
				break;
			}
		}
#endif

		if (_pauseMenu == nullptr) {
			if (_nextLevelType != ExitType::None) {
				_nextLevelTime -= timeMult;

				bool playersReady = true;
				for (auto player : _players) {
					// Exit type was already provided in BeginLevelChange()
					playersReady &= player->OnLevelChanging(ExitType::None);
				}

				if (playersReady && _nextLevelTime <= 0.0f) {
					StringView realNextLevel;
					if (!_nextLevel.empty()) {
						realNextLevel = _nextLevel;
					} else {
						realNextLevel = ((_nextLevelType & ExitType::TypeMask) == ExitType::Bonus ? _defaultSecretLevel : _defaultNextLevel);
					}

					LevelInitialization levelInit;

					if (!realNextLevel.empty()) {
						auto found = realNextLevel.partition('/');
						if (found[2].empty()) {
							levelInit.EpisodeName = _episodeName;
							levelInit.LevelName = realNextLevel;
						} else {
							levelInit.EpisodeName = found[0];
							levelInit.LevelName = found[2];
						}
					}

					levelInit.Difficulty = _difficulty;
					levelInit.IsReforged = _isReforged;
					levelInit.CheatsUsed = _cheatsUsed;
					levelInit.LastExitType = _nextLevelType;
					levelInit.LastEpisodeName = _episodeName;

					for (int32_t i = 0; i < _players.size(); i++) {
						levelInit.PlayerCarryOvers[i] = _players[i]->PrepareLevelCarryOver();
					}

					_root->ChangeLevel(std::move(levelInit));
					return;
				}
			}

			if (_difficulty != GameDifficulty::Multiplayer) {
				if (!_players.empty()) {
					auto& pos = _players[0]->GetPos();
					int32_t tx1 = (int32_t)pos.X / Tiles::TileSet::DefaultTileSize;
					int32_t ty1 = (int32_t)pos.Y / Tiles::TileSet::DefaultTileSize;
					int32_t tx2 = tx1;
					int32_t ty2 = ty1;

					tx1 -= ActivateTileRange;
					ty1 -= ActivateTileRange;
					tx2 += ActivateTileRange;
					ty2 += ActivateTileRange;

					int32_t tx1d = tx1 - 4;
					int32_t ty1d = ty1 - 4;
					int32_t tx2d = tx2 + 4;
					int32_t ty2d = ty2 + 4;

					for (auto& actor : _actors) {
						if ((actor->_state & (Actors::ActorState::IsCreatedFromEventMap | Actors::ActorState::IsFromGenerator)) != Actors::ActorState::None) {
							Vector2i originTile = actor->_originTile;
							if (originTile.X < tx1d || originTile.Y < ty1d || originTile.X > tx2d || originTile.Y > ty2d) {
								if (actor->OnTileDeactivated()) {
									if ((actor->_state & Actors::ActorState::IsFromGenerator) == Actors::ActorState::IsFromGenerator) {
										_eventMap->ResetGenerator(originTile.X, originTile.Y);
									}

									_eventMap->Deactivate(originTile.X, originTile.Y);

									actor->_state |= Actors::ActorState::IsDestroyed;
								}
							}
						}
					}

					_eventMap->ActivateEvents(tx1, ty1, tx2, ty2, true);
				}

				_eventMap->ProcessGenerators(timeMult);
			}

			// Weather
			if (_weatherType != WeatherType::None) {
				int32_t weatherIntensity = std::max((int32_t)(_weatherIntensity * timeMult), 1);
				for (int32_t i = 0; i < weatherIntensity; i++) {
					TileMap::DebrisFlags debrisFlags;
					if ((_weatherType & WeatherType::OutdoorsOnly) == WeatherType::OutdoorsOnly) {
						debrisFlags = TileMap::DebrisFlags::Disappear;
					} else {
						debrisFlags = (Random().FastFloat() > 0.7f
							? TileMap::DebrisFlags::None
							: TileMap::DebrisFlags::Disappear);
					}

					Vector2i viewSize = _viewTexture->size();
					Vector2f debrisPos = Vector2f(_cameraPos.X + Random().FastFloat(viewSize.X * -1.5f, viewSize.X * 1.5f),
						_cameraPos.Y + Random().NextFloat(viewSize.Y * -1.5f, viewSize.Y * 1.5f));

					WeatherType realWeatherType = (_weatherType & ~WeatherType::OutdoorsOnly);
					if (realWeatherType == WeatherType::Rain) {
						auto it = _commonResources->Graphics.find(String::nullTerminatedView("Rain"_s));
						if (it != _commonResources->Graphics.end()) {
							auto& resBase = it->second.Base;
							Vector2i texSize = resBase->TextureDiffuse->size();
							float scale = Random().FastFloat(0.4f, 1.1f);
							float speedX = Random().FastFloat(2.2f, 2.7f) * scale;
							float speedY = Random().FastFloat(7.6f, 8.6f) * scale;

							TileMap::DestructibleDebris debris = { };
							debris.Pos = debrisPos;
							debris.Depth = MainPlaneZ - 100 + (uint16_t)(200 * scale);
							debris.Size = Vector2f((float)resBase->FrameDimensions.X, (float)resBase->FrameDimensions.Y);
							debris.Speed = Vector2f(speedX, speedY);
							debris.Acceleration = Vector2f(0.0f, 0.0f);

							debris.Scale = scale;
							debris.ScaleSpeed = 0.0f;
							debris.Angle = atan2f(speedY, speedX);
							debris.AngleSpeed = 0.0f;
							debris.Alpha = 1.0f;
							debris.AlphaSpeed = 0.0f;

							debris.Time = 180.0f;

							uint32_t curAnimFrame = it->second.FrameOffset + Random().Next(0, it->second.FrameCount);
							uint32_t col = curAnimFrame % resBase->FrameConfiguration.X;
							uint32_t row = curAnimFrame / resBase->FrameConfiguration.X;
							debris.TexScaleX = (float(resBase->FrameDimensions.X) / float(texSize.X));
							debris.TexBiasX = (float(resBase->FrameDimensions.X * col) / float(texSize.X));
							debris.TexScaleY = (float(resBase->FrameDimensions.Y) / float(texSize.Y));
							debris.TexBiasY = (float(resBase->FrameDimensions.Y * row) / float(texSize.Y));

							debris.DiffuseTexture = resBase->TextureDiffuse.get();
							debris.Flags = debrisFlags;

							_tileMap->CreateDebris(debris);
						}
					} else {
						auto it = _commonResources->Graphics.find(String::nullTerminatedView("Snow"_s));
						if (it != _commonResources->Graphics.end()) {
							auto& resBase = it->second.Base;
							Vector2i texSize = resBase->TextureDiffuse->size();
							float scale = Random().FastFloat(0.4f, 1.1f);
							float speedX = Random().FastFloat(-1.6f, -1.2f) * scale;
							float speedY = Random().FastFloat(3.0f, 4.0f) * scale;
							float accel = Random().FastFloat(-0.008f, 0.008f) * scale;

							TileMap::DestructibleDebris debris = { };
							debris.Pos = debrisPos;
							debris.Depth = MainPlaneZ - 100 + (uint16_t)(200 * scale);
							debris.Size = Vector2f((float)resBase->FrameDimensions.X, (float)resBase->FrameDimensions.Y);
							debris.Speed = Vector2f(speedX, speedY);
							debris.Acceleration = Vector2f(accel, -std::abs(accel));

							debris.Scale = scale;
							debris.ScaleSpeed = 0.0f;
							debris.Angle = Random().FastFloat(0.0f, fTwoPi);
							debris.AngleSpeed = speedX * 0.02f,
								debris.Alpha = 1.0f;
							debris.AlphaSpeed = 0.0f;

							debris.Time = 180.0f;

							uint32_t curAnimFrame = it->second.FrameOffset + Random().Next(0, it->second.FrameCount);
							uint32_t col = curAnimFrame % resBase->FrameConfiguration.X;
							uint32_t row = curAnimFrame / resBase->FrameConfiguration.X;
							debris.TexScaleX = (float(resBase->FrameDimensions.X) / float(texSize.X));
							debris.TexBiasX = (float(resBase->FrameDimensions.X * col) / float(texSize.X));
							debris.TexScaleY = (float(resBase->FrameDimensions.Y) / float(texSize.Y));
							debris.TexBiasY = (float(resBase->FrameDimensions.Y * row) / float(texSize.Y));

							debris.DiffuseTexture = resBase->TextureDiffuse.get();
							debris.Flags = debrisFlags;

							_tileMap->CreateDebris(debris);
						}
					}
				}
			}

			// Active Boss
			if (_activeBoss != nullptr && _activeBoss->GetHealth() <= 0) {
				_activeBoss = nullptr;
				BeginLevelChange(ExitType::Boss, nullptr);
			}

#if defined(WITH_ANGELSCRIPT)
			if (_scripts != nullptr) {
				_scripts->OnLevelUpdate(timeMult);
			}
#endif
		}
	}

	void LevelHandler::OnEndFrame()
	{
		float timeMult = theApplication().timeMult();

		if (_pauseMenu == nullptr) {
			ResolveCollisions(timeMult);

			// Ambient Light Transition
			if (_ambientColor.W != _ambientLightTarget) {
				float step = timeMult * 0.012f;
				if (std::abs(_ambientColor.W - _ambientLightTarget) < step) {
					_ambientColor.W = _ambientLightTarget;
				} else {
					_ambientColor.W += step * ((_ambientLightTarget < _ambientColor.W) ? -1 : 1);
				}
			}

			UpdateCamera(timeMult);

			_elapsedFrames += timeMult;
		}

		_lightingView->setClearColor(_ambientColor.W, 0.0f, 0.0f, 1.0f);
	}

	void LevelHandler::OnInitializeViewport(int32_t width, int32_t height)
	{
		constexpr float defaultRatio = (float)DefaultWidth / DefaultHeight;
		float currentRatio = (float)width / height;

		int32_t w, h;
		if (currentRatio > defaultRatio) {
			w = std::min(DefaultWidth, width);
			h = (int32_t)(w / currentRatio);
		} else if (currentRatio < defaultRatio) {
			h = std::min(DefaultHeight, height);
			w = (int32_t)(h * currentRatio);
		} else {
			w = std::min(DefaultWidth, width);
			h = std::min(DefaultHeight, height);
		}

		bool notInitialized = (_view == nullptr);

		if (notInitialized) {
			_viewTexture = std::make_unique<Texture>(nullptr, Texture::Format::RGB8, w, h);
			_view = std::make_unique<Viewport>(_viewTexture.get(), Viewport::DepthStencilFormat::None);

			_camera = std::make_unique<Camera>();
			InitializeCamera();

			_view->setCamera(_camera.get());
			_view->setRootNode(_rootNode.get());
		} else {
			_view->removeAllTextures();
			_viewTexture->init(nullptr, Texture::Format::RGB8, w, h);
			_view->setTexture(_viewTexture.get());
		}

		_viewTexture->setMagFiltering(SamplerFilter::Nearest);
		_viewTexture->setWrap(SamplerWrapping::ClampToEdge);

		_camera->setOrthoProjection(w * (-0.5f), w * (+0.5f), h * (-0.5f), h * (+0.5f));

		auto& resolver = ContentResolver::Get();

		if (_lightingRenderer == nullptr) {			
			_lightingShader = resolver.GetShader(PrecompiledShader::Lighting);
			_blurShader = resolver.GetShader(PrecompiledShader::Blur);
			_downsampleShader = resolver.GetShader(PrecompiledShader::Downsample);
			_combineShader = resolver.GetShader(PrecompiledShader::Combine);

			_lightingRenderer = std::make_unique<LightingRenderer>(this);
		}

		_combineWithWaterShader = resolver.GetShader(PreferencesCache::LowGraphicsQuality
			? PrecompiledShader::CombineWithWaterLow
			: PrecompiledShader::CombineWithWater);

		if (notInitialized) {
			_lightingBuffer = std::make_unique<Texture>(nullptr, Texture::Format::RG8, w, h);
			_lightingView = std::make_unique<Viewport>(_lightingBuffer.get(), Viewport::DepthStencilFormat::None);
			_lightingView->setRootNode(_lightingRenderer.get());
			_lightingView->setCamera(_camera.get());
		} else {
			_lightingView->removeAllTextures();
			_lightingBuffer->init(nullptr, Texture::Format::RG8, w, h);
			_lightingView->setTexture(_lightingBuffer.get());
		}

		_lightingBuffer->setMagFiltering(SamplerFilter::Nearest);
		_lightingBuffer->setWrap(SamplerWrapping::ClampToEdge);

		_downsamplePass.Initialize(_viewTexture.get(), w / 2, h / 2, Vector2f::Zero);
		_blurPass1.Initialize(_downsamplePass.GetTarget(), w / 2, h / 2, Vector2f(1.0f, 0.0f));
		_blurPass2.Initialize(_blurPass1.GetTarget(), w / 2, h / 2, Vector2f(0.0f, 1.0f));
		_blurPass3.Initialize(_blurPass2.GetTarget(), w / 4, h / 4, Vector2f(1.0f, 0.0f));
		_blurPass4.Initialize(_blurPass3.GetTarget(), w / 4, h / 4, Vector2f(0.0f, 1.0f));
		_upscalePass.Initialize(w, h, width, height);

		// Viewports must be registered in reverse order
		_upscalePass.Register();
		_blurPass4.Register();
		_blurPass3.Register();
		_blurPass2.Register();
		_blurPass1.Register();
		_downsamplePass.Register();

		Viewport::chain().push_back(_lightingView.get());

		if (notInitialized) {
			_combineRenderer = std::make_unique<CombineRenderer>(this);
			_combineRenderer->setParent(_upscalePass.GetNode());

			if (_hud != nullptr) {
				_hud->setParent(_upscalePass.GetNode());
			}
		}

		_combineRenderer->Initialize(w, h);

		Viewport::chain().push_back(_view.get());

		if (_tileMap != nullptr) {
			_tileMap->OnInitializeViewport();
		}

		if (_pauseMenu != nullptr) {
			_pauseMenu->OnInitializeViewport(w, h);
		}
	}

	void LevelHandler::OnKeyPressed(const KeyboardEvent& event)
	{
		_pressedKeys.Set((uint32_t)event.sym);

		// Cheats
		if (PreferencesCache::AllowCheats && _difficulty != GameDifficulty::Multiplayer && !_players.empty()) {
			if (event.sym >= KeySym::A && event.sym <= KeySym::Z) {
				if (event.sym == KeySym::J && _cheatsBufferLength >= 2) {
					_cheatsBufferLength = 0;
					_cheatsBuffer[_cheatsBufferLength++] = (char)event.sym;
				} else if (_cheatsBufferLength < countof(_cheatsBuffer)) {
					_cheatsBuffer[_cheatsBufferLength++] = (char)event.sym;

					if (_cheatsBufferLength >= 5 && _cheatsBuffer[0] == (char)KeySym::J && _cheatsBuffer[1] == (char)KeySym::J) {
						switch (_cheatsBufferLength) {
							case 5:
								if (_cheatsBuffer[2] == (char)KeySym::G && _cheatsBuffer[3] == (char)KeySym::O && _cheatsBuffer[4] == (char)KeySym::D) {
									_cheatsBufferLength = 0;
									_cheatsUsed = true;
									_players[0]->SetInvulnerability(36000.0f, true);
								}
								break;
							case 6:
								if (_cheatsBuffer[2] == (char)KeySym::N && _cheatsBuffer[3] == (char)KeySym::E && _cheatsBuffer[4] == (char)KeySym::X && _cheatsBuffer[5] == (char)KeySym::T) {
									_cheatsBufferLength = 0;
									_cheatsUsed = true;
									BeginLevelChange(ExitType::Warp | ExitType::FastTransition, nullptr);
								} else if ((_cheatsBuffer[2] == (char)KeySym::G && _cheatsBuffer[3] == (char)KeySym::U && _cheatsBuffer[4] == (char)KeySym::N && _cheatsBuffer[5] == (char)KeySym::S) ||
										   (_cheatsBuffer[2] == (char)KeySym::A && _cheatsBuffer[3] == (char)KeySym::M && _cheatsBuffer[4] == (char)KeySym::M && _cheatsBuffer[5] == (char)KeySym::O)) {
									_cheatsBufferLength = 0;
									_cheatsUsed = true;
									for (int32_t i = 0; i < (int32_t)WeaponType::Count; i++) {
										_players[0]->AddAmmo((WeaponType)i, 99);
									}
								} else if (_cheatsBuffer[2] == (char)KeySym::R && _cheatsBuffer[3] == (char)KeySym::U && _cheatsBuffer[4] == (char)KeySym::S && _cheatsBuffer[5] == (char)KeySym::H) {
									_cheatsBufferLength = 0;
									_cheatsUsed = true;
									_players[0]->ActivateSugarRush(1300.0f);
								} else if (_cheatsBuffer[2] == (char)KeySym::G && _cheatsBuffer[3] == (char)KeySym::E && _cheatsBuffer[4] == (char)KeySym::M && _cheatsBuffer[5] == (char)KeySym::S) {
									_cheatsBufferLength = 0;
									_cheatsUsed = true;
									_players[0]->AddGems(5);
								} else if (_cheatsBuffer[2] == (char)KeySym::B && _cheatsBuffer[3] == (char)KeySym::I && _cheatsBuffer[4] == (char)KeySym::R && _cheatsBuffer[5] == (char)KeySym::D) {
									_cheatsBufferLength = 0;
									_cheatsUsed = true;
									_players[0]->SpawnBird(0, _players[0]->GetPos());
								}
								break;
							case 7:
								if (_cheatsBuffer[2] == (char)KeySym::P && _cheatsBuffer[3] == (char)KeySym::O && _cheatsBuffer[4] == (char)KeySym::W && _cheatsBuffer[5] == (char)KeySym::E && _cheatsBuffer[6] == (char)KeySym::R) {
									_cheatsBufferLength = 0;
									_cheatsUsed = true;
									for (int32_t i = 0; i < (int32_t)WeaponType::Count; i++) {
										_players[0]->AddWeaponUpgrade((WeaponType)i, 0x01);
									}
								} else if (_cheatsBuffer[2] == (char)KeySym::C && _cheatsBuffer[3] == (char)KeySym::O && _cheatsBuffer[4] == (char)KeySym::I && _cheatsBuffer[5] == (char)KeySym::N && _cheatsBuffer[6] == (char)KeySym::S) {
									_cheatsBufferLength = 0;
									_cheatsUsed = true;
									_players[0]->AddCoins(5);
								}
								break;
							case 8:
								if (_cheatsBuffer[2] == (char)KeySym::S && _cheatsBuffer[3] == (char)KeySym::H && _cheatsBuffer[4] == (char)KeySym::I && _cheatsBuffer[5] == (char)KeySym::E && _cheatsBuffer[6] == (char)KeySym::L && _cheatsBuffer[7] == (char)KeySym::D) {
									_cheatsBufferLength = 0;
									_cheatsUsed = true;
									ShieldType shieldType = (ShieldType)(((int32_t)_players[0]->GetActiveShield() + 1) % (int32_t)ShieldType::Count);
									_players[0]->SetShield(shieldType, 600.0f * FrameTimer::FramesPerSecond);
								}
								break;
						}
					}
				}
			}
		}
	}

	void LevelHandler::OnKeyReleased(const KeyboardEvent& event)
	{
		_pressedKeys.Reset((uint32_t)event.sym);
	}

	void LevelHandler::OnTouchEvent(const  TouchEvent& event)
	{
		if (_pauseMenu != nullptr) {
			_pauseMenu->OnTouchEvent(event);
		} else {
			_hud->OnTouchEvent(event, _overrideActions);
		}
	}

	void LevelHandler::AddActor(std::shared_ptr<Actors::ActorBase> actor)
	{
		actor->SetParent(_rootNode.get());

		if (!actor->GetState(Actors::ActorState::ForceDisableCollisions)) {
			actor->UpdateAABB();
			actor->CollisionProxyID = _collisions.CreateProxy(actor->AABB, actor.get());
		}

		_actors.emplace_back(actor);
	}

	std::shared_ptr<AudioBufferPlayer> LevelHandler::PlaySfx(AudioBuffer* buffer, const Vector3f& pos, bool sourceRelative, float gain, float pitch)
	{
		auto& player = _playingSounds.emplace_back(std::make_shared<AudioBufferPlayer>(buffer));
		player->setPosition(Vector3f(pos.X, pos.Y, 100.0f));
		player->setGain(gain * PreferencesCache::MasterVolume * PreferencesCache::SfxVolume);
		player->setSourceRelative(sourceRelative);

		if (pos.Y >= _waterLevel) {
			player->setLowPass(0.05f);
			player->setPitch(pitch * 0.7f);
		} else {
			player->setPitch(pitch);
		}

		player->play();
		return player;
	}

	std::shared_ptr<AudioBufferPlayer> LevelHandler::PlayCommonSfx(const StringView& identifier, const Vector3f& pos, float gain, float pitch)
	{
		auto it = _commonResources->Sounds.find(String::nullTerminatedView(identifier));
		if (it != _commonResources->Sounds.end()) {
			int32_t idx = (it->second.Buffers.size() > 1 ? Random().Next(0, (int32_t)it->second.Buffers.size()) : 0);
			auto& player = _playingSounds.emplace_back(std::make_shared<AudioBufferPlayer>(it->second.Buffers[idx].get()));
			player->setPosition(Vector3f(pos.X, pos.Y, 100.0f));
			player->setGain(gain * PreferencesCache::MasterVolume * PreferencesCache::SfxVolume);

			if (pos.Y >= _waterLevel) {
				player->setLowPass(/*0.2f*/0.05f);
				player->setPitch(pitch * 0.7f);
			} else {
				player->setPitch(pitch);
			}

			player->play();
			return player;
		} else {
			return nullptr;
		}
	}

	void LevelHandler::WarpCameraToTarget(const std::shared_ptr<Actors::ActorBase>& actor, bool fast)
	{
		// TODO: Allow multiple cameras
		if (_players[0] != actor.get()) {
			return;
		}

		Vector2f focusPos = actor->_pos;
		if (!fast) {
			_cameraPos.X = focusPos.X;
			_cameraPos.Y = focusPos.Y;
			_cameraLastPos = _cameraPos;
			_cameraDistanceFactor.X = 0.0f;
			_cameraDistanceFactor.Y = 0.0f;
		} else {
			Vector2f diff = _cameraLastPos - _cameraPos;
			_cameraPos.X = focusPos.X;
			_cameraPos.Y = focusPos.Y;
			_cameraLastPos = _cameraPos + diff;
		}
	}

	bool LevelHandler::IsPositionEmpty(Actors::ActorBase* self, const AABBf& aabb, TileCollisionParams& params, Actors::ActorBase** collider)
	{
		*collider = nullptr;

		if (self->GetState(Actors::ActorState::CollideWithTileset)) {
			if (_tileMap != nullptr) {
				if (self->GetState(Actors::ActorState::CollideWithTilesetReduced) && aabb.B - aabb.T >= 20.0f) {
					// If hitbox height is larger than 20px, check bottom and top separately (and top only if going upwards)
					AABBf aabbTop = aabb;
					aabbTop.B = aabbTop.T + 6.0f;
					AABBf aabbBottom = aabb;
					aabbBottom.T = aabbBottom.B - 14.0f;
					if (!_tileMap->IsTileEmpty(aabbBottom, params)) {
						return false;
					}
					if (!params.Downwards) {
						params.Downwards = false;
						if (!_tileMap->IsTileEmpty(aabbTop, params)) {
							return false;
						}
					}
				} else {
					if (!_tileMap->IsTileEmpty(aabb, params)) {
						return false;
					}
				}
			}
		}

		// Check for solid objects
		if (self->GetState(Actors::ActorState::CollideWithSolidObjects)) {
			Actors::ActorBase* colliderActor = nullptr;
			FindCollisionActorsByAABB(self, aabb, [self, &colliderActor, &params](Actors::ActorBase* actor) -> bool {
				if ((actor->GetState() & (Actors::ActorState::IsSolidObject | Actors::ActorState::IsDestroyed)) != Actors::ActorState::IsSolidObject) {
					return true;
				}
				if (self->GetState(Actors::ActorState::ExcludeSimilar) && actor->GetState(Actors::ActorState::ExcludeSimilar)) {
					// If both objects have ExcludeSimilar, ignore it
					return true;
				}
				if (self->GetState(Actors::ActorState::CollideWithSolidObjectsBelow) &&
					self->AABBInner.B > (actor->AABBInner.T + actor->AABBInner.B) * 0.5f) {
					return true;
				}

				Actors::SolidObjectBase* solidObject = dynamic_cast<Actors::SolidObjectBase*>(actor);
				if (solidObject == nullptr || !solidObject->IsOneWay || params.Downwards) {
					std::shared_ptr selfShared = self->shared_from_this();
					std::shared_ptr actorShared = actor->shared_from_this();
					if (!selfShared->OnHandleCollision(actorShared) && !actorShared->OnHandleCollision(selfShared)) {
						colliderActor = actor;
						return false;
					}
				}

				return true;
			});

			*collider = colliderActor;
		}

		return (*collider == nullptr);
	}

	void LevelHandler::FindCollisionActorsByAABB(Actors::ActorBase* self, const AABBf& aabb, const std::function<bool(Actors::ActorBase*)>& callback)
	{
		struct QueryHelper {
			const LevelHandler* Handler;
			const Actors::ActorBase* Self;
			const AABBf& AABB;
			const std::function<bool(Actors::ActorBase*)>& Callback;

			bool OnCollisionQuery(int32_t nodeId) {
				Actors::ActorBase* actor = (Actors::ActorBase*)Handler->_collisions.GetUserData(nodeId);
				if (Self == actor || (actor->GetState() & (Actors::ActorState::CollideWithOtherActors | Actors::ActorState::IsDestroyed)) != Actors::ActorState::CollideWithOtherActors) {
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

	void LevelHandler::FindCollisionActorsByRadius(float x, float y, float radius, const std::function<bool(Actors::ActorBase*)>& callback)
	{
		AABBf aabb = AABBf(x - radius, y - radius, x + radius, y + radius);
		float radiusSquared = (radius * radius);

		struct QueryHelper {
			const LevelHandler* Handler;
			const float x, y;
			const float RadiusSquared;
			const std::function<bool(Actors::ActorBase*)>& Callback;

			bool OnCollisionQuery(int32_t nodeId) {
				Actors::ActorBase* actor = (Actors::ActorBase*)Handler->_collisions.GetUserData(nodeId);
				if ((actor->GetState() & (Actors::ActorState::CollideWithOtherActors | Actors::ActorState::IsDestroyed)) != Actors::ActorState::CollideWithOtherActors) {
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

	void LevelHandler::GetCollidingPlayers(const AABBf& aabb, const std::function<bool(Actors::ActorBase*)> callback)
	{
		for (auto& player : _players) {
			if (aabb.Overlaps(player->AABB)) {
				if (!callback(player)) {
					break;
				}
			}
		}
	}

	void LevelHandler::BroadcastTriggeredEvent(Actors::ActorBase* initiator, EventType eventType, uint8_t* eventParams)
	{
		switch (eventType) {
			case EventType::AreaActivateBoss: {
				if (_activeBoss == nullptr) {
					for (auto& actor : _actors) {
						_activeBoss = std::dynamic_pointer_cast<Actors::Bosses::BossBase>(actor);
						if (_activeBoss != nullptr) {
							break;
						}
					}

					if (_activeBoss == nullptr) {
						// No boss was found, it's probably a bug in the level, so go to the next level
						LOGW("No boss was found, skipping to the next level");
						BeginLevelChange(ExitType::Boss, nullptr);
						return;
					}

					if (_activeBoss->OnActivatedBoss()) {
						if (eventParams != nullptr) {
							size_t musicPathLength = strnlen((const char*)eventParams, 16);
							StringView musicPath((const char*)eventParams, musicPathLength);
							BeginPlayMusic(musicPath);
						}
					}
				}
				break;
			}
			case EventType::AreaCallback: {
#if defined(WITH_ANGELSCRIPT)
				if (_scripts != nullptr) {
					_scripts->OnLevelCallback(initiator, eventParams);
				}
#endif
				break;
			}
			case EventType::ModifierSetWater: {
				// TODO: Implement Instant (non-instant transition), Lighting
				_waterLevel = *(uint16_t*)&eventParams[0];
				break;
			}
		}

		for (auto& actor : _actors) {
			actor->OnTriggeredEvent(eventType, eventParams);
		}
	}

	void LevelHandler::BeginLevelChange(ExitType exitType, const StringView& nextLevel)
	{
		if (_nextLevelType != ExitType::None) {
			return;
		}

		_nextLevel = nextLevel;
		_nextLevelType = exitType;
		
		if ((exitType & ExitType::FastTransition) == ExitType::FastTransition) {
			ExitType exitTypeMasked = (exitType & ExitType::TypeMask);
			if (exitTypeMasked == ExitType::Warp || exitTypeMasked == ExitType::Bonus || exitTypeMasked == ExitType::Boss) {
				_nextLevelTime = 70.0f;
			} else {
				_nextLevelTime = 0.0f;
			}
		} else {
			_nextLevelTime = 360.0f;

			if (_hud != nullptr) {
				_hud->BeginFadeOut(_nextLevelTime - 40.0f);
			}

#if defined(WITH_AUDIO)
			if (_sugarRushMusic != nullptr) {
				_sugarRushMusic->stop();
				_sugarRushMusic = nullptr;
			}
			if (_music != nullptr) {
				_music->stop();
				_music = nullptr;
			}
#endif
		}

		for (auto player : _players) {
			player->OnLevelChanging(exitType);
		}
	}

	void LevelHandler::HandleGameOver()
	{
		// TODO: Implement Game Over screen
		_root->GoToMainMenu(false);
	}

	bool LevelHandler::HandlePlayerDied(const std::shared_ptr<Actors::ActorBase>& player)
	{
		if (_activeBoss != nullptr) {
			if (_activeBoss->OnPlayerDied()) {
				_activeBoss = nullptr;
			}
		}

		// Single player can respawn immediately
		return true;
	}

	void LevelHandler::HandlePlayerWarped(const std::shared_ptr<Actors::ActorBase>& player, const Vector2f& prevPos, bool fast)
	{
		if (fast) {
			WarpCameraToTarget(player, true);
		} else {
			Vector2f pos = player->GetPos();
			if (Vector2f(prevPos.X - pos.X, prevPos.Y - pos.Y).Length() > 250.0f) {
				WarpCameraToTarget(player);
			}
		}
	}

	void LevelHandler::SetCheckpoint(Vector2f pos)
	{
		_checkpointFrames = ElapsedFrames();

		for (auto& player : _players) {
			player->SetCheckpoint(pos, _ambientLightTarget);
		}

		if (_difficulty != GameDifficulty::Multiplayer) {
			_eventMap->CreateCheckpointForRollback();
		}
	}

	void LevelHandler::RollbackToCheckpoint()
	{
		// Reset the camera
		LimitCameraView(0, 0);

		if (!_players.empty()) {
			WarpCameraToTarget(_players[0]->shared_from_this());
		}

		if (_difficulty != GameDifficulty::Multiplayer) {
			for (auto& actor : _actors) {
				// Despawn all actors that were created after the last checkpoint
				if (actor->_spawnFrames > _checkpointFrames && !actor->GetState(Actors::ActorState::PreserveOnRollback)) {
					if ((actor->_state & (Actors::ActorState::IsCreatedFromEventMap | Actors::ActorState::IsFromGenerator)) != Actors::ActorState::None) {
						Vector2i originTile = actor->_originTile;
						if ((actor->_state & Actors::ActorState::IsFromGenerator) == Actors::ActorState::IsFromGenerator) {
							_eventMap->ResetGenerator(originTile.X, originTile.Y);
						}

						_eventMap->Deactivate(originTile.X, originTile.Y);
					}

					actor->_state |= Actors::ActorState::IsDestroyed;
				}
			}

			_eventMap->RollbackToCheckpoint();
			_elapsedFrames = _checkpointFrames;
		}

		BeginPlayMusic(_musicDefaultPath);

#if defined(WITH_ANGELSCRIPT)
		if (_scripts != nullptr) {
			_scripts->OnLevelReload();
		}
#endif
	}

	void LevelHandler::ActivateSugarRush()
	{
#if defined(WITH_AUDIO)
		if (_sugarRushMusic != nullptr) {
			return;
		}

		auto it = _commonResources->Sounds.find(String::nullTerminatedView("SugarRush"_s));
		if (it != _commonResources->Sounds.end()) {
			int32_t idx = (it->second.Buffers.size() > 1 ? Random().Next(0, (int32_t)it->second.Buffers.size()) : 0);
			_sugarRushMusic = _playingSounds.emplace_back(std::make_shared<AudioBufferPlayer>(it->second.Buffers[idx].get()));
			_sugarRushMusic->setPosition(Vector3f(0.0f, 0.0f, 100.0f));
			_sugarRushMusic->setGain(PreferencesCache::MasterVolume * PreferencesCache::MusicVolume);
			_sugarRushMusic->setSourceRelative(true);
			_sugarRushMusic->play();

			if (_music != nullptr) {
				_music->pause();
			}
		}
#endif
	}

	void LevelHandler::ShowLevelText(const StringView& text)
	{
		_hud->ShowLevelText(text);
	}

	void LevelHandler::ShowCoins(int32_t count)
	{
		_hud->ShowCoins(count);
	}

	void LevelHandler::ShowGems(int32_t count)
	{
		_hud->ShowGems(count);
	}

	StringView LevelHandler::GetLevelText(uint32_t textId, int32_t index, uint32_t delimiter)
	{
		if (textId >= _levelTexts.size()) {
			return { };
		}

		StringView text = _levelTexts[textId];
		int32_t textSize = (int32_t)text.size();

		if (textSize > 0 && index >= 0) {
			int32_t delimiterCount = 0;
			int32_t start = 0;
			int32_t idx = 0;
			do {
				std::pair<char32_t, std::size_t> cursor = Death::Utf8::NextChar(text, idx);

				if (cursor.first == delimiter) {
					if (delimiterCount == index - 1) {
						start = idx + 1;
					} else if (delimiterCount == index) {
						return StringView(text.data() + start, idx - start);
					}
					delimiterCount++;
				}

				idx = (int32_t)cursor.second;
			} while (idx < textSize);

			if (delimiterCount == index) {
				return StringView(text.data() + start, text.size() - start);
			} else {
				return { };
			}
		} else {
			return _x("/"_s.joinWithoutEmptyParts({ _episodeName, _levelFileName }), text.data());
		}

		return text;
	}

	void LevelHandler::OverrideLevelText(uint32_t textId, const StringView& value)
	{
		if (textId >= _levelTexts.size()) {
			if (value.empty()) {
				return;
			}

			_levelTexts.resize(textId + 1);
		}

		_levelTexts[textId] = value;
	}

	bool LevelHandler::PlayerActionPressed(int32_t index, PlayerActions action, bool includeGamepads)
	{
		bool isGamepad;
		return PlayerActionPressed(index, action, includeGamepads, isGamepad);
	}

	bool LevelHandler::PlayerActionPressed(int32_t index, PlayerActions action, bool includeGamepads, bool& isGamepad)
	{
		if (index != 0) {
			return false;
		}

		isGamepad = false;
		if ((_pressedActions & (1ull << (int32_t)action)) != 0) {
			isGamepad = (_pressedActions & (1ull << (16 + (int32_t)action))) != 0;
			return true;
		}

		if (includeGamepads) {
			switch (action) {
				case PlayerActions::Left: if (_playerRequiredMovement.X < -0.8f && !_playerFrozenEnabled) { isGamepad = true; return true; } break;
				case PlayerActions::Right: if (_playerRequiredMovement.X > 0.8f && !_playerFrozenEnabled) { isGamepad = true; return true; } break;
				case PlayerActions::Up: if (_playerRequiredMovement.Y < -0.8f && !_playerFrozenEnabled) { isGamepad = true; return true; } break;
				case PlayerActions::Down: if (_playerRequiredMovement.Y > 0.8f && !_playerFrozenEnabled) { isGamepad = true; return true; } break;
			}
		}

		return false;
	}

	bool LevelHandler::PlayerActionHit(int32_t index, PlayerActions action, bool includeGamepads)
	{
		bool isGamepad;
		return PlayerActionHit(index, action, includeGamepads, isGamepad);
	}

	bool LevelHandler::PlayerActionHit(int32_t index, PlayerActions action, bool includeGamepads, bool& isGamepad)
	{
		if (index != 0) {
			return false;
		}

		isGamepad = false;
		if ((_pressedActions & ((1ull << (int32_t)action) | (1ull << (32 + (int32_t)action)))) == (1ull << (int32_t)action)) {
			isGamepad = (_pressedActions & (1ull << (16 + (int32_t)action))) != 0;
			return true;
		}

		return false;
	}

	float LevelHandler::PlayerHorizontalMovement(int32_t index)
	{
		if (index != 0) {
			return 0.0f;
		}

		if ((_pressedActions & (1ull << (int32_t)PlayerActions::Right)) != 0) {
			return 1.0f;
		} else if ((_pressedActions & (1ull << (int32_t)PlayerActions::Left)) != 0) {
			return -1.0f;
		}

		return (_playerFrozenEnabled ? _playerFrozenMovement.X : _playerRequiredMovement.X);
	}

	float LevelHandler::PlayerVerticalMovement(int32_t index)
	{
		if (index != 0) {
			return 0.0f;
		}

		if ((_pressedActions & (1ull << (int32_t)PlayerActions::Up)) != 0) {
			return -1.0f;
		} else if ((_pressedActions & (1ull << (int32_t)PlayerActions::Down)) != 0) {
			return 1.0f;
		}

		return (_playerFrozenEnabled ? _playerFrozenMovement.Y : _playerRequiredMovement.Y);
	}

	void LevelHandler::OnTileFrozen(std::int32_t x, std::int32_t y)
	{
		bool iceBlockFound = false;
		FindCollisionActorsByAABB(nullptr, AABBf(x - 1.0f, y - 1.0f, x + 1.0f, y + 1.0f), [&iceBlockFound](Actors::ActorBase* actor) -> bool {
			if ((actor->GetState() & Actors::ActorState::IsDestroyed) != Actors::ActorState::None) {
				return true;
			}

			Actors::Environment::IceBlock* iceBlock = dynamic_cast<Actors::Environment::IceBlock*>(actor);
			if (iceBlock != nullptr) {
				iceBlock->ResetTimeLeft();
				iceBlockFound = true;
				return false;
			}

			return true;
		});

		if (!iceBlockFound) {
			std::shared_ptr<Actors::Environment::IceBlock> iceBlock = std::make_shared<Actors::Environment::IceBlock>();
			iceBlock->OnActivated(Actors::ActorActivationDetails(
				this,
				Vector3i(x - 1, y - 2, ILevelHandler::MainPlaneZ)
			));
			AddActor(iceBlock);
		}
	}

	void LevelHandler::ResolveCollisions(float timeMult)
	{
		auto it = _actors.begin();
		while (it != _actors.end()) {
			Actors::ActorBase* actor = it->get();
			if (actor->GetState(Actors::ActorState::IsDestroyed)) {
				if (actor->CollisionProxyID != Collisions::NullNode) {
					_collisions.DestroyProxy(actor->CollisionProxyID);
					actor->CollisionProxyID = Collisions::NullNode;
				}

				it = _actors.erase(it);
				continue;
			}
			
			if (actor->GetState(Actors::ActorState::IsDirty)) {
				if (actor->CollisionProxyID == Collisions::NullNode) {
					continue;
				}

				actor->UpdateAABB();
				_collisions.MoveProxy(actor->CollisionProxyID, actor->AABB, actor->_speed * timeMult);
				actor->SetState(Actors::ActorState::IsDirty, false);
			}
			++it;
		}

		struct UpdatePairsHelper {
			void OnPairAdded(void* proxyA, void* proxyB) {
				Actors::ActorBase* actorA = (Actors::ActorBase*)proxyA;
				Actors::ActorBase* actorB = (Actors::ActorBase*)proxyB;
				if (((actorA->GetState() | actorB->GetState()) & (Actors::ActorState::CollideWithOtherActors | Actors::ActorState::IsDestroyed)) != Actors::ActorState::CollideWithOtherActors) {
					return;
				}

				if (actorA->IsCollidingWith(actorB)) {
					std::shared_ptr<Actors::ActorBase> actorSharedA = actorA->shared_from_this();
					std::shared_ptr<Actors::ActorBase> actorSharedB = actorB->shared_from_this();
					if (!actorSharedA->OnHandleCollision(actorSharedB->shared_from_this())) {
						actorSharedB->OnHandleCollision(actorSharedA->shared_from_this());
					}
				}
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
			_cameraPos.X = std::floor(std::clamp(_cameraLastPos.X + _cameraDistanceFactor.X, _viewBounds.X + halfView.X, _viewBounds.X + _viewBounds.W - halfView.X) + _shakeOffset.X);
		} else {
			_cameraPos.X = std::floor(_viewBounds.X + _viewBounds.W * 0.5f + _shakeOffset.X);
		}
		if (_viewBounds.H > halfView.Y * 2) {
			_cameraPos.Y = std::floor(std::clamp(_cameraLastPos.Y + _cameraDistanceFactor.Y, _viewBounds.Y + halfView.Y, _viewBounds.Y + _viewBounds.H - halfView.Y - 1.0f) + _shakeOffset.Y);
		} else {
			_cameraPos.Y = std::floor(_viewBounds.Y + _viewBounds.H * 0.5f + _shakeOffset.Y);
		}

		_camera->setView(_cameraPos, 0.0f, 1.0f);

		// Update audio listener position
		IAudioDevice& device = theServiceLocator().audioDevice();
		device.updateListener(Vector3f(_cameraPos.X, _cameraPos.Y, 0.0f), Vector3f(speed.X, speed.Y, 0.0f));
	}

	void LevelHandler::LimitCameraView(int left, int width)
	{
		_levelBounds.X = left;
		if (width > 0.0f) {
			_levelBounds.W = width;
		} else {
			_levelBounds.W = _tileMap->LevelBounds().X - left;
		}

		if (left == 0 && width == 0) {
			_viewBounds = Rectf((float)_levelBounds.X, (float)_levelBounds.Y, (float)_levelBounds.W, (float)_levelBounds.H);
			_viewBoundsTarget = _viewBounds;
		} else {
			Rectf bounds = Rectf((float)_levelBounds.X, (float)_levelBounds.Y, (float)_levelBounds.W, (float)_levelBounds.H);
			float viewWidth = (float)_view->size().X;
			if (bounds.W < viewWidth) {
				bounds.X -= (viewWidth - bounds.W);
				bounds.W = viewWidth;
			}

			_viewBoundsTarget = bounds;

			float limit = _cameraPos.X - viewWidth * 0.6f;
			if (_viewBounds.X < limit) {
				_viewBounds.W += (_viewBounds.X - limit);
				_viewBounds.X = limit;
			}
		}
	}

	void LevelHandler::ShakeCameraView(float duration)
	{
		if (_shakeDuration < duration) {
			_shakeDuration = duration;
		}
	}

	void LevelHandler::SetWeather(WeatherType type, uint8_t intensity)
	{
		_weatherType = type;
		_weatherIntensity = intensity;
	}

	bool LevelHandler::BeginPlayMusic(const StringView& path, bool setDefault, bool forceReload)
	{
		bool result = false;

#if defined(WITH_AUDIO)
		if (_sugarRushMusic != nullptr) {
			_sugarRushMusic->stop();
		}

		if (!forceReload && _musicCurrentPath == path) {
			// Music is already playing or is paused
			if (_music != nullptr) {
				_music->play();
			}
			if (setDefault) {
				_musicDefaultPath = path;
			}
			return false;
		}

		if (_music != nullptr) {
			_music->stop();
		}

		if (!path.empty()) {
			_music = ContentResolver::Get().GetMusic(path);
			if (_music != nullptr) {
				_music->setLooping(true);
				_music->setGain(PreferencesCache::MasterVolume * PreferencesCache::MusicVolume);
				_music->setSourceRelative(true);
				_music->play();
				result = true;
			}
		} else {
			_music = nullptr;
		}

		_musicCurrentPath = path;
		if (setDefault) {
			_musicDefaultPath = path;
		}
#endif

		return result;
	}

	void LevelHandler::UpdatePressedActions()
	{
		auto& input = theApplication().inputManager();
		_pressedActions = ((_pressedActions & 0xffffffffu) << 32);

		if (_pressedKeys[(uint32_t)UI::ControlScheme::Key1(0, PlayerActions::Up)] || _pressedKeys[(uint32_t)UI::ControlScheme::Key2(0, PlayerActions::Up)]) {
			_pressedActions |= (1 << (int32_t)PlayerActions::Up);
		}
		if (_pressedKeys[(uint32_t)UI::ControlScheme::Key1(0, PlayerActions::Down)] || _pressedKeys[(uint32_t)UI::ControlScheme::Key2(0, PlayerActions::Down)]) {
			_pressedActions |= (1 << (int32_t)PlayerActions::Down);
		}
		if (_pressedKeys[(uint32_t)UI::ControlScheme::Key1(0, PlayerActions::Left)] || _pressedKeys[(uint32_t)UI::ControlScheme::Key2(0, PlayerActions::Left)]) {
			_pressedActions |= (1 << (int32_t)PlayerActions::Left);
		}
		if (_pressedKeys[(uint32_t)UI::ControlScheme::Key1(0, PlayerActions::Right)] || _pressedKeys[(uint32_t)UI::ControlScheme::Key2(0, PlayerActions::Right)]) {
			_pressedActions |= (1 << (int32_t)PlayerActions::Right);
		}
		if (_pressedKeys[(uint32_t)UI::ControlScheme::Key1(0, PlayerActions::Fire)] || _pressedKeys[(uint32_t)UI::ControlScheme::Key2(0, PlayerActions::Fire)]) {
			_pressedActions |= (1 << (int32_t)PlayerActions::Fire);
		}
		if (_pressedKeys[(uint32_t)UI::ControlScheme::Key1(0, PlayerActions::Jump)] || _pressedKeys[(uint32_t)UI::ControlScheme::Key2(0, PlayerActions::Jump)]) {
			_pressedActions |= (1 << (int32_t)PlayerActions::Jump);
		}
		if (_pressedKeys[(uint32_t)UI::ControlScheme::Key1(0, PlayerActions::Run)] || _pressedKeys[(uint32_t)UI::ControlScheme::Key2(0, PlayerActions::Run)]) {
			_pressedActions |= (1 << (int32_t)PlayerActions::Run);
		}
		if (_pressedKeys[(uint32_t)UI::ControlScheme::Key1(0, PlayerActions::ChangeWeapon)] || _pressedKeys[(uint32_t)UI::ControlScheme::Key2(0, PlayerActions::ChangeWeapon)]) {
			_pressedActions |= (1 << (int32_t)PlayerActions::ChangeWeapon);
		}
		// Allow Android Back button as menu key
		if (_pressedKeys[(uint32_t)UI::ControlScheme::Key1(0, PlayerActions::Menu)] || _pressedKeys[(uint32_t)UI::ControlScheme::Key2(0, PlayerActions::Menu)] || (PreferencesCache::UseNativeBackButton && _pressedKeys[(uint32_t)KeySym::BACK])) {
			_pressedActions |= (1 << (int32_t)PlayerActions::Menu);
		}

		// Use numeric key to switch weapons for the first player
		if (!_players.empty()) {
			bool found = false;
			for (uint32_t i = 0; i < 9; i++) {
				if (_pressedKeys[(uint32_t)KeySym::N1 + i]) {
					if (_lastPressedNumericKey != i) {
						_lastPressedNumericKey = i;
						_players[0]->SwitchToWeaponByIndex(i);
					}
					found = true;
					break;
				}
			}
			if (!found) {
				_lastPressedNumericKey = UINT32_MAX;
			}
		}

		// Try to get 8 connected joysticks
		const JoyMappedState* joyStates[UI::ControlScheme::MaxConnectedGamepads];
		int32_t jc = 0;
		for (int32_t i = 0; i < IInputManager::MaxNumJoysticks && jc < countof(joyStates); i++) {
			if (input.isJoyMapped(i)) {
				joyStates[jc++] = &input.joyMappedState(i);
			}
		}

		ButtonName jb; int32_t ji1, ji2, ji3, ji4;

		jb = UI::ControlScheme::Gamepad(0, PlayerActions::Up, ji1);
		if (ji1 >= 0 && ji1 < jc && joyStates[ji1]->isButtonPressed(jb)) {
			_pressedActions |= (1 << (int32_t)PlayerActions::Up);
		}
		jb = UI::ControlScheme::Gamepad(0, PlayerActions::Down, ji2);
		if (ji2 >= 0 && ji2 < jc && joyStates[ji2]->isButtonPressed(jb)) {
			_pressedActions |= (1 << (int32_t)PlayerActions::Down);
		}
		jb = UI::ControlScheme::Gamepad(0, PlayerActions::Left, ji3);
		if (ji3 >= 0 && ji3 < jc && joyStates[ji3]->isButtonPressed(jb)) {
			_pressedActions |= (1 << (int32_t)PlayerActions::Left);
		}
		jb = UI::ControlScheme::Gamepad(0, PlayerActions::Right, ji4);
		if (ji4 >= 0 && ji4 < jc && joyStates[ji4]->isButtonPressed(jb)) {
			_pressedActions |= (1 << (int32_t)PlayerActions::Right);
		}

		// Use analog controls only if all movement buttons are mapped to the same joystick
		if (ji1 == ji2 && ji2 == ji3 && ji3 == ji4 && ji1 >= 0 && ji1 < jc) {
			_playerRequiredMovement.X = joyStates[ji1]->axisValue(AxisName::LX);
			_playerRequiredMovement.Y = joyStates[ji1]->axisValue(AxisName::LY);
			input.deadZoneNormalize(_playerRequiredMovement, 0.1f);
		} else {
			_playerRequiredMovement.X = 0.0f;
			_playerRequiredMovement.Y = 0.0f;
		}

		jb = UI::ControlScheme::Gamepad(0, PlayerActions::Jump, ji1);
		if (ji1 >= 0 && ji1 < jc && joyStates[ji1]->isButtonPressed(jb)) {
			_pressedActions |= (1 << (int32_t)PlayerActions::Jump) | (1 << (16 + (int32_t)PlayerActions::Jump));
		}
		jb = UI::ControlScheme::Gamepad(0, PlayerActions::Run, ji1);
		if (ji1 >= 0 && ji1 < jc && joyStates[ji1]->isButtonPressed(jb)) {
			_pressedActions |= (1 << (int32_t)PlayerActions::Run) | (1 << (16 + (int32_t)PlayerActions::Run));
		}
		jb = UI::ControlScheme::Gamepad(0, PlayerActions::Fire, ji1);
		if (ji1 >= 0 && ji1 < jc && joyStates[ji1]->isButtonPressed(jb)) {
			_pressedActions |= (1 << (int32_t)PlayerActions::Fire) | (1 << (16 + (int32_t)PlayerActions::Fire));
		}
		jb = UI::ControlScheme::Gamepad(0, PlayerActions::ChangeWeapon, ji1);
		if (ji1 >= 0 && ji1 < jc && joyStates[ji1]->isButtonPressed(jb)) {
			_pressedActions |= (1 << (int32_t)PlayerActions::ChangeWeapon) | (1 << (16 + (int32_t)PlayerActions::ChangeWeapon));
		}
		jb = UI::ControlScheme::Gamepad(0, PlayerActions::Menu, ji1);
		if (ji1 >= 0 && ji1 < jc && joyStates[ji1]->isButtonPressed(jb)) {
			_pressedActions |= (1 << (int32_t)PlayerActions::Menu) | (1 << (16 + (int32_t)PlayerActions::Menu));
		}

		// Also apply overriden actions (by touch controls)
		_pressedActions |= _overrideActions;
	}

	void LevelHandler::PauseGame()
	{
		// Show in-game pause menu
		_pauseMenu = std::make_shared<UI::Menu::InGameMenu>(this);
		// Prevent updating of all level objects
		_rootNode->setUpdateEnabled(false);

#if defined(WITH_AUDIO)
		// Use low-pass filter on music and pause all SFX
		if (_music != nullptr) {
			_music->setLowPass(0.1f);
		}
		for (auto& sound : _playingSounds) {
			if (sound->isPlaying()) {
				sound->pause();
			}
		}
		// If Sugar Rush music is playing, pause it and play normal music instead
		if (_sugarRushMusic != nullptr && _music != nullptr) {
			_music->play();
		}
#endif
	}

	void LevelHandler::ResumeGame()
	{
		// Resume all level objects
		_rootNode->setUpdateEnabled(true);
		// Hide in-game pause menu
		_pauseMenu = nullptr;

#if defined(WITH_AUDIO)
		// If Sugar Rush music was playing, resume it and pause normal music again
		if (_sugarRushMusic != nullptr && _music != nullptr) {
			_music->pause();
		}
		// Resume all SFX
		for (auto& sound : _playingSounds) {
			if (sound->isPaused()) {
				sound->play();
			}
		}
		if (_music != nullptr) {
			_music->setLowPass(1.0f);
		}
#endif

		// Mark Menu button as already pressed to avoid some issues
		_pressedActions |= (1ull << (int32_t)PlayerActions::Menu) | (1ull << (32 + (int32_t)PlayerActions::Menu));
	}

	bool LevelHandler::LightingRenderer::OnDraw(RenderQueue& renderQueue)
	{
		_renderCommandsCount = 0;
		_emittedLightsCache.clear();

		// Collect all active light emitters
		for (auto& actor : _owner->_actors) {
			actor->OnEmitLights(_emittedLightsCache);
		}

		for (auto& light : _emittedLightsCache) {
			auto command = RentRenderCommand();
			auto instanceBlock = command->material().uniformBlock(Material::InstanceBlockName);
			instanceBlock->uniform(Material::TexRectUniformName)->setFloatValue(light.Pos.X, light.Pos.Y, light.RadiusNear / light.RadiusFar, 0.0f);
			instanceBlock->uniform(Material::SpriteSizeUniformName)->setFloatValue(light.RadiusFar * 2.0f, light.RadiusFar * 2.0f);
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
			command->material().setShader(_owner->_lightingShader);
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

	void LevelHandler::BlurRenderPass::Initialize(Texture* source, int32_t width, int32_t height, const Vector2f& direction)
	{
		_source = source;
		_downsampleOnly = (direction.X <= std::numeric_limits<float>::epsilon() && direction.Y <= std::numeric_limits<float>::epsilon());
		_direction = direction;

		bool notInitialized = (_view == nullptr);

		if (notInitialized) {
			_camera = std::make_unique<Camera>();
		}
		_camera->setOrthoProjection(width * (-0.5f), width * (+0.5f), height * (-0.5f), height * (+0.5f));
		_camera->setView(0, 0, 0, 1);

		if (notInitialized) {
			_target = std::make_unique<Texture>(nullptr, Texture::Format::RGB8, width, height);
			_target->setWrap(SamplerWrapping::ClampToEdge);
			_view = std::make_unique<Viewport>(_target.get(), Viewport::DepthStencilFormat::None);
			_view->setRootNode(this);
			_view->setCamera(_camera.get());
			//_view->setClearMode(Viewport::ClearMode::Never);
		} else {
			_view->removeAllTextures();
			_target->init(nullptr, Texture::Format::RGB8, width, height);
			_view->setTexture(_target.get());
		}
		_target->setMagFiltering(SamplerFilter::Linear);

		// Prepare render command
		_renderCommand.material().setShader(_downsampleOnly ? _owner->_downsampleShader : _owner->_blurShader);
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
		Vector2i size = _target->size();

		auto instanceBlock = _renderCommand.material().uniformBlock(Material::InstanceBlockName);
		instanceBlock->uniform(Material::TexRectUniformName)->setFloatValue(1.0f, 0.0f, -1.0f, 1.0f);
		instanceBlock->uniform(Material::SpriteSizeUniformName)->setFloatValue(static_cast<float>(size.X), static_cast<float>(size.Y));
		instanceBlock->uniform(Material::ColorUniformName)->setFloatVector(Colorf::White.Data());

		_renderCommand.material().uniform("uPixelOffset")->setFloatValue(1.0f / size.X, 1.0f / size.Y);
		if (!_downsampleOnly) {
			_renderCommand.material().uniform("uDirection")->setFloatValue(_direction.X, _direction.Y);
		}
		_renderCommand.material().setTexture(0, *_source);

		renderQueue.addCommand(&_renderCommand);

		return true;
	}

	void LevelHandler::CombineRenderer::Initialize(int32_t width, int32_t height)
	{
		_size = Vector2f(static_cast<float>(width), static_cast<float>(height));

		if (_renderCommand.material().setShader(_owner->_combineShader)) {
			_renderCommand.material().reserveUniformsDataMemory();
			_renderCommand.geometry().setDrawParameters(GL_TRIANGLE_STRIP, 0, 4);
			// Required to reset render command properly
			_renderCommand.setTransformation(_renderCommand.transformation());

			GLUniformCache* textureUniform = _renderCommand.material().uniform(Material::TextureUniformName);
			if (textureUniform && textureUniform->intValue(0) != 0) {
				textureUniform->setIntValue(0); // GL_TEXTURE0
			}
			GLUniformCache* lightTexUniform = _renderCommand.material().uniform("uTextureLighting");
			if (lightTexUniform && lightTexUniform->intValue(0) != 1) {
				lightTexUniform->setIntValue(1); // GL_TEXTURE1
			}
			GLUniformCache* blurHalfTexUniform = _renderCommand.material().uniform("uTextureBlurHalf");
			if (blurHalfTexUniform && blurHalfTexUniform->intValue(0) != 2) {
				blurHalfTexUniform->setIntValue(2); // GL_TEXTURE2
			}
			GLUniformCache* blurQuarterTexUniform = _renderCommand.material().uniform("uTextureBlurQuarter");
			if (blurQuarterTexUniform && blurQuarterTexUniform->intValue(0) != 3) {
				blurQuarterTexUniform->setIntValue(3); // GL_TEXTURE3
			}
		}

		if (_renderCommandWithWater.material().setShader(_owner->_combineWithWaterShader)) {
			_renderCommandWithWater.material().reserveUniformsDataMemory();
			_renderCommandWithWater.geometry().setDrawParameters(GL_TRIANGLE_STRIP, 0, 4);
			// Required to reset render command properly
			_renderCommandWithWater.setTransformation(_renderCommandWithWater.transformation());

			GLUniformCache* textureUniform = _renderCommandWithWater.material().uniform(Material::TextureUniformName);
			if (textureUniform && textureUniform->intValue(0) != 0) {
				textureUniform->setIntValue(0); // GL_TEXTURE0
			}
			GLUniformCache* lightTexUniform = _renderCommandWithWater.material().uniform("uTextureLighting");
			if (lightTexUniform && lightTexUniform->intValue(0) != 1) {
				lightTexUniform->setIntValue(1); // GL_TEXTURE1
			}
			GLUniformCache* blurHalfTexUniform = _renderCommandWithWater.material().uniform("uTextureBlurHalf");
			if (blurHalfTexUniform && blurHalfTexUniform->intValue(0) != 2) {
				blurHalfTexUniform->setIntValue(2); // GL_TEXTURE2
			}
			GLUniformCache* blurQuarterTexUniform = _renderCommandWithWater.material().uniform("uTextureBlurQuarter");
			if (blurQuarterTexUniform && blurQuarterTexUniform->intValue(0) != 3) {
				blurQuarterTexUniform->setIntValue(3); // GL_TEXTURE3
			}
			GLUniformCache* displacementTexUniform = _renderCommandWithWater.material().uniform("uTextureDisplacement");
			if (displacementTexUniform && displacementTexUniform->intValue(0) != 4) {
				displacementTexUniform->setIntValue(4); // GL_TEXTURE4
			}
		}
	}

	bool LevelHandler::CombineRenderer::OnDraw(RenderQueue& renderQueue)
	{
		float viewWaterLevel = _owner->_waterLevel - _owner->_cameraPos.Y + _size.Y * 0.5f;
		bool viewHasWater = (viewWaterLevel < _size.Y);
		auto& command = (viewHasWater ? _renderCommandWithWater : _renderCommand);

		command.material().setTexture(0, *_owner->_viewTexture);
		command.material().setTexture(1, *_owner->_lightingBuffer);
		command.material().setTexture(2, *_owner->_blurPass2.GetTarget());
		command.material().setTexture(3, *_owner->_blurPass4.GetTarget());
		if (viewHasWater && !PreferencesCache::LowGraphicsQuality) {
			command.material().setTexture(4, *_owner->_noiseTexture);
		}

		auto instanceBlock = command.material().uniformBlock(Material::InstanceBlockName);
		instanceBlock->uniform(Material::TexRectUniformName)->setFloatValue(1.0f, 0.0f, 1.0f, 0.0f);
		instanceBlock->uniform(Material::SpriteSizeUniformName)->setFloatValue(_size.X, _size.Y);
		instanceBlock->uniform(Material::ColorUniformName)->setFloatVector(Colorf::White.Data());

		command.material().uniform("uAmbientColor")->setFloatVector(_owner->_ambientColor.Data());
		command.material().uniform("uTime")->setFloatValue(_owner->_elapsedFrames * 0.0018f);

		if (viewHasWater) {
			command.material().uniform("uWaterLevel")->setFloatValue(viewWaterLevel / _size.Y);
			command.material().uniform("uCameraPos")->setFloatVector(_owner->_cameraPos.Data());
		}

		renderQueue.addCommand(&command);

		return true;
	}
}