#include "LevelHandler.h"
#include "PreferencesCache.h"
#include "UI/ControlScheme.h"
#include "UI/HUD.h"
#include "../Common.h"

#if (defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)) || defined(DEATH_TARGET_UNIX)
#	include "UI/DiscordRpcClient.h"
#endif

#if defined(WITH_ANGELSCRIPT)
#	include "Scripting/LevelScriptLoader.h"
#endif

#include "../nCine/MainApplication.h"
#include "../nCine/IAppEventHandler.h"
#include "../nCine/ServiceLocator.h"
#include "../nCine/tracy.h"
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

#include <Containers/StaticArray.h>
#include <Containers/StringConcatenable.h>
#include <Utf8.h>

using namespace nCine;

namespace Jazz2
{
	namespace Resources
	{
		static constexpr AnimState Snow = (AnimState)0;
		static constexpr AnimState Rain = (AnimState)1;
	}

	using namespace Jazz2::Resources;

	LevelHandler::LevelHandler(IRootController* root)
		: _root(root), _eventSpawner(this), _difficulty(GameDifficulty::Default), _isReforged(false), _cheatsUsed(false), _checkpointCreated(false),
			_cheatsBufferLength(0), _nextLevelType(ExitType::None), _nextLevelTime(0.0f), _elapsedFrames(0.0f), _checkpointFrames(0.0f),
			_cameraResponsiveness(1.0f, 1.0f), _shakeDuration(0.0f), _waterLevel(FLT_MAX), _ambientLightTarget(1.0f), _weatherType(WeatherType::None),
			_downsamplePass(this), _blurPass1(this), _blurPass2(this), _blurPass3(this), _blurPass4(this),
			_pressedKeys((uint32_t)KeySym::COUNT), _pressedActions(0), _pressedActionsLast(0), _overrideActions(0),
			_playerFrozenEnabled(false)
	{
	}

	LevelHandler::~LevelHandler()
	{
		// Remove nodes from UpscaleRenderPass
		_combineRenderer->setParent(nullptr);
		_hud->setParent(nullptr);

		TracyPlot("Actors", 0LL);
	}

	bool LevelHandler::Initialize(const LevelInitialization& levelInit)
	{
		ZoneScopedC(0x4876AF);

		_levelFileName = levelInit.LevelName;
		_episodeName = levelInit.EpisodeName;
		_difficulty = levelInit.Difficulty;
		_isReforged = levelInit.IsReforged;
		_cheatsUsed = levelInit.CheatsUsed;

		auto& resolver = ContentResolver::Get();
		resolver.BeginLoading();

		_noiseTexture = resolver.GetNoiseTexture();

		_rootNode = std::make_unique<SceneNode>();
		_rootNode->setVisitOrderState(SceneNode::VisitOrderState::Disabled);

		LevelDescriptor descriptor;
		if (!resolver.TryLoadLevel("/"_s.joinWithoutEmptyParts({ _episodeName, _levelFileName }), _difficulty, descriptor)) {
			LOGE("Cannot load level \"%s/%s\"", _episodeName.data(), _levelFileName.data());
			return false;
		}

		AttachComponents(std::move(descriptor));
		SpawnPlayers(levelInit);		

		OnInitialized();
		resolver.EndLoading();

		if ((levelInit.LastExitType & ExitType::FastTransition) != ExitType::FastTransition) {
			_hud->BeginFadeIn();
		}

		return true;
	}

	bool LevelHandler::Initialize(Stream& src)
	{
		ZoneScopedC(0x4876AF);

		std::uint8_t flags = src.ReadValue<std::uint8_t>();

		std::uint8_t stringSize = src.ReadValue<std::uint8_t>();
		_episodeName = String(NoInit, stringSize);
		src.Read(_episodeName.data(), stringSize);

		stringSize = src.ReadValue<std::uint8_t>();
		_levelFileName = String(NoInit, stringSize);
		src.Read(_levelFileName.data(), stringSize);

		_difficulty = (GameDifficulty)src.ReadValue<std::uint8_t>();
		_isReforged = (flags & 0x01) != 0;
		_cheatsUsed = (flags & 0x02) != 0;
		_checkpointFrames = src.ReadValue<float>();

		auto& resolver = ContentResolver::Get();
		resolver.BeginLoading();

		_noiseTexture = resolver.GetNoiseTexture();

		_rootNode = std::make_unique<SceneNode>();
		_rootNode->setVisitOrderState(SceneNode::VisitOrderState::Disabled);

		LevelDescriptor descriptor;
		if (!resolver.TryLoadLevel("/"_s.joinWithoutEmptyParts({ _episodeName, _levelFileName }), _difficulty, descriptor)) {
			LOGE("Cannot load level \"%s/%s\"", _episodeName.data(), _levelFileName.data());
			return false;
		}

		AttachComponents(std::move(descriptor));

		// All components are ready, deserialize the rest of state
		_waterLevel = src.ReadValue<float>();
		_weatherType = (WeatherType)src.ReadValue<std::uint8_t>();
		_weatherIntensity = src.ReadValue<std::uint8_t>();

		_tileMap->InitializeFromStream(src);
		_eventMap->InitializeFromStream(src);

		std::uint32_t playerCount = src.ReadValue<std::uint8_t>();
		_players.reserve(playerCount);

		for (std::uint32_t i = 0; i < playerCount; i++) {
			std::shared_ptr<Actors::Player> player = std::make_shared<Actors::Player>();
			player->InitializeFromStream(this, src);

			Actors::Player* ptr = player.get();
			_players.push_back(ptr);
			AddActor(player);
		}

		OnInitialized();
		resolver.EndLoading();

		_hud->BeginFadeIn();

		// Set it at the end, so ambient light transition is skipped
		_elapsedFrames = _checkpointFrames;

		return true;
	}

	void LevelHandler::OnInitialized()
	{
		constexpr float DefaultGravity = 0.3f;

		// Higher gravity in Reforged mode
		Gravity = (_isReforged ? DefaultGravity : DefaultGravity * 0.8f);

		auto& resolver = ContentResolver::Get();
		_commonResources = resolver.RequestMetadata("Common/Scenery"_s);
		resolver.PreloadMetadataAsync("Common/Explosions"_s);

		_hud = std::make_unique<UI::HUD>(this);

		_eventMap->PreloadEventsAsync();

		UpdateRichPresence();

#if defined(WITH_ANGELSCRIPT)
		if (_scripts != nullptr) {
			_scripts->OnLevelLoad();
		}
#endif
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

	void LevelHandler::SetAmbientLight(Actors::Player* player, float value)
	{
		_ambientLightTarget = value;

		// Skip transition if it was changed at the beginning of level
		if (_elapsedFrames < FrameTimer::FramesPerSecond * 0.25f) {
			_ambientColor.W = value;
		}
	}

	void LevelHandler::AttachComponents(LevelDescriptor&& descriptor)
	{
		ZoneScopedC(0x4876AF);

		if (!descriptor.DisplayName.empty()) {
			theApplication().gfxDevice().setWindowTitle(String(NCINE_APP_NAME " - " + descriptor.DisplayName));
		} else {
			theApplication().gfxDevice().setWindowTitle(NCINE_APP_NAME);
		}

		_defaultNextLevel = std::move(descriptor.NextLevel);
		_defaultSecretLevel = std::move(descriptor.SecretLevel);

		_tileMap = std::move(descriptor.TileMap);
		_tileMap->SetOwner(this);
		_tileMap->setParent(_rootNode.get());

		_eventMap = std::move(descriptor.EventMap);
		_eventMap->SetLevelHandler(this);

		Vector2i levelBounds = _tileMap->GetLevelBounds();
		_levelBounds = Recti(0, 0, levelBounds.X, levelBounds.Y);
		_viewBounds = _levelBounds.As<float>();
		_viewBoundsTarget = _viewBounds;		

		_ambientColor = descriptor.AmbientColor;
		_ambientLightTarget = descriptor.AmbientColor.W;

		_weatherType = descriptor.Weather;
		_weatherIntensity = descriptor.WeatherIntensity;
		_waterLevel = descriptor.WaterLevel;

#if defined(WITH_AUDIO)
		if (!descriptor.MusicPath.empty()) {
			_music = ContentResolver::Get().GetMusic(descriptor.MusicPath);
			if (_music != nullptr) {
				_musicCurrentPath = std::move(descriptor.MusicPath);
				_musicDefaultPath = _musicCurrentPath;
				_music->setLooping(true);
				_music->setGain(PreferencesCache::MasterVolume * PreferencesCache::MusicVolume);
				_music->setSourceRelative(true);
				_music->play();
			}
		}
#endif

		_levelTexts = std::move(descriptor.LevelTexts);

#if defined(WITH_ANGELSCRIPT) || defined(DEATH_TRACE)
		// TODO: Allow script signing
		if (PreferencesCache::AllowUnsignedScripts) {
			const StringView foundDot = descriptor.FullPath.findLastOr('.', descriptor.FullPath.end());
			String scriptPath = (foundDot == descriptor.FullPath.end() ? StringView(descriptor.FullPath) : descriptor.FullPath.prefix(foundDot.begin())) + ".j2as"_s;
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

	void LevelHandler::SpawnPlayers(const LevelInitialization& levelInit)
	{
		for (std::int32_t i = 0; i < countof(levelInit.PlayerCarryOvers); i++) {
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
			std::uint8_t playerParams[2] = { (std::uint8_t)levelInit.PlayerCarryOvers[i].Type, (std::uint8_t)i };
			player->OnActivated(Actors::ActorActivationDetails(
				this,
				Vector3i((std::int32_t)spawnPosition.X + (i * 30), (std::int32_t)spawnPosition.Y - (i * 30), PlayerZ - i),
				playerParams
			));

			Actors::Player* ptr = player.get();
			_players.push_back(ptr);
			AddActor(player);

			ptr->ReceiveLevelCarryOver(levelInit.LastExitType, levelInit.PlayerCarryOvers[i]);
		}
	}

	void LevelHandler::OnBeginFrame()
	{
		ZoneScopedC(0x4876AF);

		float timeMult = theApplication().timeMult();

		if (_pauseMenu == nullptr) {
			UpdatePressedActions();

			if (PlayerActionHit(0, PlayerActions::Menu) && _nextLevelType == ExitType::None) {
				PauseGame();
			}
#if defined(DEATH_DEBUG)
			if (PreferencesCache::AllowCheats && PlayerActionPressed(0, PlayerActions::ChangeWeapon) && PlayerActionHit(0, PlayerActions::Jump)) {
				_cheatsUsed = true;
				BeginLevelChange(ExitType::Warp | ExitType::FastTransition, nullptr);
			}
#endif
		}

#if defined(WITH_AUDIO)
		// Destroy stopped players and resume music after Sugar Rush
		if (_sugarRushMusic != nullptr && _sugarRushMusic->isStopped()) {
			_sugarRushMusic = nullptr;
			if (_music != nullptr) {
				_music->play();
			}
		}

		for (std::int32_t i = (std::int32_t)_playingSounds.size() - 1; i >= 0; i--) {
			if (_playingSounds[i]->isStopped()) {
				_playingSounds.erase(&_playingSounds[i]);
			} else {
				break;
			}
		}
#endif

		if (!IsPausable() || _pauseMenu == nullptr) {
			if (_nextLevelType != ExitType::None) {
				_nextLevelTime -= timeMult;
				ProcessQueuedNextLevel();
			}

			ProcessEvents(timeMult);

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
						auto* res = _commonResources->FindAnimation(Rain);
						if (res != nullptr) {
							auto& resBase = res->Base;
							Vector2i texSize = resBase->TextureDiffuse->size();
							float scale = Random().FastFloat(0.4f, 1.1f);
							float speedX = Random().FastFloat(2.2f, 2.7f) * scale;
							float speedY = Random().FastFloat(7.6f, 8.6f) * scale;

							TileMap::DestructibleDebris debris = { };
							debris.Pos = debrisPos;
							debris.Depth = MainPlaneZ - 100 + (uint16_t)(200 * scale);
							debris.Size = resBase->FrameDimensions.As<float>();
							debris.Speed = Vector2f(speedX, speedY);
							debris.Acceleration = Vector2f(0.0f, 0.0f);

							debris.Scale = scale;
							debris.ScaleSpeed = 0.0f;
							debris.Angle = atan2f(speedY, speedX);
							debris.AngleSpeed = 0.0f;
							debris.Alpha = 1.0f;
							debris.AlphaSpeed = 0.0f;

							debris.Time = 180.0f;

							uint32_t curAnimFrame = res->FrameOffset + Random().Next(0, res->FrameCount);
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
						auto* res = _commonResources->FindAnimation(Snow);
						if (res != nullptr) {
							auto& resBase = res->Base;
							Vector2i texSize = resBase->TextureDiffuse->size();
							float scale = Random().FastFloat(0.4f, 1.1f);
							float speedX = Random().FastFloat(-1.6f, -1.2f) * scale;
							float speedY = Random().FastFloat(3.0f, 4.0f) * scale;
							float accel = Random().FastFloat(-0.008f, 0.008f) * scale;

							TileMap::DestructibleDebris debris = { };
							debris.Pos = debrisPos;
							debris.Depth = MainPlaneZ - 100 + (uint16_t)(200 * scale);
							debris.Size = resBase->FrameDimensions.As<float>();
							debris.Speed = Vector2f(speedX, speedY);
							debris.Acceleration = Vector2f(accel, -std::abs(accel));

							debris.Scale = scale;
							debris.ScaleSpeed = 0.0f;
							debris.Angle = Random().FastFloat(0.0f, fTwoPi);
							debris.AngleSpeed = speedX * 0.02f;
							debris.Alpha = 1.0f;
							debris.AlphaSpeed = 0.0f;

							debris.Time = 180.0f;

							uint32_t curAnimFrame = res->FrameOffset + Random().Next(0, res->FrameCount);
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
		ZoneScopedC(0x4876AF);

		float timeMult = theApplication().timeMult();

		if (!IsPausable() || _pauseMenu == nullptr) {
			ResolveCollisions(timeMult);

			// Ambient Light Transition
			if (_ambientColor.W != _ambientLightTarget) {
				float step = timeMult * 0.012f;
				if (std::abs(_ambientColor.W - _ambientLightTarget) < step) {
					_ambientColor.W = _ambientLightTarget;
				} else {
					_ambientColor.W += step * ((_ambientLightTarget < _ambientColor.W) ? -1.0f : 1.0f);
				}
			}

			UpdateCamera(timeMult);

			_elapsedFrames += timeMult;
		}

		_lightingView->setClearColor(_ambientColor.W, 0.0f, 0.0f, 1.0f);

#if defined(DEATH_DEBUG) && defined(WITH_IMGUI)
		if (PreferencesCache::ShowPerformanceMetrics) {
			ImDrawList* drawList = ImGui::GetBackgroundDrawList();

			std::size_t actorsCount = _actors.size();
			for (std::size_t i = 0; i < actorsCount; i++) {
				auto* actor = _actors[i].get();

				auto pos = WorldPosToScreenSpace(actor->_pos);
				auto aabbMin = WorldPosToScreenSpace({ actor->AABB.L, actor->AABB.T });
				auto aabbMax = WorldPosToScreenSpace({ actor->AABB.R, actor->AABB.B });
				auto aabbInnerMin = WorldPosToScreenSpace({ actor->AABBInner.L, actor->AABBInner.T });
				auto aabbInnerMax = WorldPosToScreenSpace({ actor->AABBInner.R, actor->AABBInner.B });

				drawList->AddRect(ImVec2(pos.x - 2.4f, pos.y - 2.4f), ImVec2(pos.x + 2.4f, pos.y + 2.4f), ImColor(0, 0, 0, 220));
				drawList->AddRect(ImVec2(pos.x - 1.0f, pos.y - 1.0f), ImVec2(pos.x + 1.0f, pos.y + 1.0f), ImColor(120, 255, 200, 220));
				drawList->AddRect(aabbMin, aabbMax, ImColor(120, 200, 255, 180));
				drawList->AddRect(aabbInnerMin, aabbInnerMax, ImColor(255, 255, 255));
			}
		}
#endif

		TracyPlot("Actors", static_cast<int64_t>(_actors.size()));
	}

	void LevelHandler::OnInitializeViewport(int32_t width, int32_t height)
	{
		ZoneScopedC(0x4876AF);

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

		_camera->setOrthoProjection(0.0f, (float)w, (float)h, 0.0f);

		auto& resolver = ContentResolver::Get();

		if (_lightingRenderer == nullptr) {			
			_lightingShader = resolver.GetShader(PrecompiledShader::Lighting);
			_blurShader = resolver.GetShader(PrecompiledShader::Blur);
			_downsampleShader = resolver.GetShader(PrecompiledShader::Downsample);
			_combineShader = resolver.GetShader(PrecompiledShader::Combine);

			_lightingRenderer = std::make_unique<LightingRenderer>(this);
		}

		_combineWithWaterShader = resolver.GetShader(PreferencesCache::LowWaterQuality
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
			UpdateCamera(0.0f);	// Force update camera if game is paused
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

					if (_cheatsBufferLength >= 3 && _cheatsBuffer[0] == (char)KeySym::J && _cheatsBuffer[1] == (char)KeySym::J) {
						switch (_cheatsBufferLength) {
							case 3:
								if (_cheatsBuffer[2] == (char)KeySym::K) {
									_cheatsBufferLength = 0;
									_cheatsUsed = true;
									_players[0]->TakeDamage(INT32_MAX);
								}
								break;
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

	std::shared_ptr<AudioBufferPlayer> LevelHandler::PlaySfx(Actors::ActorBase* self, const StringView identifier, AudioBuffer* buffer, const Vector3f& pos, bool sourceRelative, float gain, float pitch)
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

	std::shared_ptr<AudioBufferPlayer> LevelHandler::PlayCommonSfx(const StringView identifier, const Vector3f& pos, float gain, float pitch)
	{
		auto it = _commonResources->Sounds.find(String::nullTerminatedView(identifier));
		if (it != _commonResources->Sounds.end()) {
			int32_t idx = (it->second.Buffers.size() > 1 ? Random().Next(0, (int32_t)it->second.Buffers.size()) : 0);
			auto& player = _playingSounds.emplace_back(std::make_shared<AudioBufferPlayer>(&it->second.Buffers[idx]->Buffer));
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

	void LevelHandler::WarpCameraToTarget(Actors::ActorBase* actor, bool fast)
	{
		// TODO: Allow multiple cameras
		if (_players[0] != actor) {
			return;
		}

		Vector2f focusPos = actor->_pos;
		if (!fast) {
			_cameraPos = focusPos;
			_cameraLastPos = _cameraPos;
			_cameraDistanceFactor = Vector2f(0.0f, 0.0f);
			_cameraResponsiveness = Vector2f(1.0f, 1.0f);
		} else {
			Vector2f diff = _cameraLastPos - _cameraPos;
			_cameraPos = focusPos;
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

				auto* solidObject = runtime_cast<Actors::SolidObjectBase*>(actor);
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

	void LevelHandler::BeginLevelChange(ExitType exitType, const StringView nextLevel)
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

	void LevelHandler::HandleGameOver(Actors::Player* player)
	{
		// TODO: Implement Game Over screen
		_root->GoToMainMenu(false);
	}

	bool LevelHandler::HandlePlayerDied(Actors::Player* player)
	{
		if (_activeBoss != nullptr) {
			if (_activeBoss->OnPlayerDied()) {
				_activeBoss = nullptr;
			}
		}

		// Single player can respawn immediately
		return true;
	}

	void LevelHandler::HandlePlayerWarped(Actors::Player* player, const Vector2f& prevPos, bool fast)
	{
		if (fast) {
			WarpCameraToTarget(player, true);
		} else {
			Vector2f pos = player->GetPos();
			if ((prevPos - pos).Length() > 250.0f) {
				WarpCameraToTarget(player);
			}
		}
	}

	void LevelHandler::SetCheckpoint(Actors::Player* player, const Vector2f& pos)
	{
		_checkpointFrames = ElapsedFrames();

		for (auto& player : _players) {
			player->SetCheckpoint(pos, _ambientLightTarget);
		}

		if (_difficulty != GameDifficulty::Multiplayer) {
			_eventMap->CreateCheckpointForRollback();
		}
	}

	void LevelHandler::RollbackToCheckpoint(Actors::Player* player)
	{
		// Reset the camera
		LimitCameraView(0, 0);

		WarpCameraToTarget(player);

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

	void LevelHandler::ActivateSugarRush(Actors::Player* player)
	{
#if defined(WITH_AUDIO)
		if (_sugarRushMusic != nullptr) {
			return;
		}

		auto it = _commonResources->Sounds.find(String::nullTerminatedView("SugarRush"_s));
		if (it != _commonResources->Sounds.end()) {
			int32_t idx = (it->second.Buffers.size() > 1 ? Random().Next(0, (int32_t)it->second.Buffers.size()) : 0);
			_sugarRushMusic = _playingSounds.emplace_back(std::make_shared<AudioBufferPlayer>(&it->second.Buffers[idx]->Buffer));
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

	void LevelHandler::ShowLevelText(const StringView text)
	{
		_hud->ShowLevelText(text);
	}

	void LevelHandler::ShowCoins(Actors::Player* player, std::int32_t count)
	{
		_hud->ShowCoins(count);
	}

	void LevelHandler::ShowGems(Actors::Player* player, std::int32_t count)
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

	void LevelHandler::OverrideLevelText(uint32_t textId, const StringView value)
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
			isGamepad = (_pressedActions & (1ull << (32 + (int32_t)action))) != 0;
			return true;
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
		if ((_pressedActions & (1ull << (int32_t)action)) != 0 && (_pressedActionsLast & (1ull << (int32_t)action)) == 0) {
			isGamepad = (_pressedActions & (1ull << (32 + (int32_t)action))) != 0;
			return true;
		}

		return false;
	}

	float LevelHandler::PlayerHorizontalMovement(int32_t index)
	{
		if (index != 0) {
			return 0.0f;
		}

		return (_playerFrozenEnabled ? _playerFrozenMovement.X : _playerRequiredMovement.X);
	}

	float LevelHandler::PlayerVerticalMovement(int32_t index)
	{
		if (index != 0) {
			return 0.0f;
		}

		return (_playerFrozenEnabled ? _playerFrozenMovement.Y : _playerRequiredMovement.Y);
	}

	bool LevelHandler::SerializeResumableToStream(Stream& dest)
	{
		std::uint8_t flags = 0;
		if (_isReforged) flags |= 0x01;
		if (_cheatsUsed) flags |= 0x02;
		dest.WriteValue<std::uint8_t>(flags);

		dest.WriteValue<std::uint8_t>((std::uint8_t)_episodeName.size());
		dest.Write(_episodeName.data(), (std::uint32_t)_episodeName.size());
		dest.WriteValue<std::uint8_t>((std::uint8_t)_levelFileName.size());
		dest.Write(_levelFileName.data(), (std::uint32_t)_levelFileName.size());

		dest.WriteValue<std::uint8_t>((std::uint8_t)_difficulty);
		dest.WriteValue<float>(_checkpointFrames);
		dest.WriteValue<float>(_waterLevel);
		dest.WriteValue<std::uint8_t>((std::uint8_t)_weatherType);
		dest.WriteValue<std::uint8_t>(_weatherIntensity);

		_tileMap->SerializeResumableToStream(dest);
		_eventMap->SerializeResumableToStream(dest);

		std::size_t playerCount = _players.size();
		dest.WriteValue<std::uint8_t>((std::uint8_t)playerCount);
		for (std::size_t i = 0; i < playerCount; i++) {
			_players[i]->SerializeResumableToStream(dest);
		}

		return true;
	}

	void LevelHandler::OnTileFrozen(std::int32_t x, std::int32_t y)
	{
		bool iceBlockFound = false;
		FindCollisionActorsByAABB(nullptr, AABBf(x - 1.0f, y - 1.0f, x + 1.0f, y + 1.0f), [&iceBlockFound](Actors::ActorBase* actor) -> bool {
			if ((actor->GetState() & Actors::ActorState::IsDestroyed) != Actors::ActorState::None) {
				return true;
			}

			auto* iceBlock = runtime_cast<Actors::Environment::IceBlock*>(actor);
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

	void LevelHandler::BeforeActorDestroyed(Actors::ActorBase* actor)
	{
		// Nothing to do here
	}

	void LevelHandler::ProcessEvents(float timeMult)
	{
		ZoneScopedC(0x4876AF);

		if (!_players.empty()) {
			std::size_t playerCount = _players.size();
			SmallVector<AABBi, 2> playerZones;
			playerZones.reserve(playerCount * 2);
			for (std::size_t i = 0; i < playerCount; i++) {
				auto pos = _players[i]->GetPos();
				std::int32_t tx = (std::int32_t)pos.X / TileSet::DefaultTileSize;
				std::int32_t ty = (std::int32_t)pos.Y / TileSet::DefaultTileSize;

				const auto& activationRange = playerZones.emplace_back(tx - ActivateTileRange, ty - ActivateTileRange, tx + ActivateTileRange, ty + ActivateTileRange);
				playerZones.emplace_back(activationRange.L - 4, activationRange.T - 4, activationRange.R + 4, activationRange.B + 4);
			}

			for (auto& actor : _actors) {
				if ((actor->_state & (Actors::ActorState::IsCreatedFromEventMap | Actors::ActorState::IsFromGenerator)) != Actors::ActorState::None) {
					Vector2i originTile = actor->_originTile;
					bool isInside = false;
					for (std::size_t i = 1; i < playerZones.size(); i += 2) {
						if (playerZones[i].Contains(originTile)) {
							isInside = true;
							break;
						}
					}

					if (!isInside && actor->OnTileDeactivated()) {
						if ((actor->_state & Actors::ActorState::IsFromGenerator) == Actors::ActorState::IsFromGenerator) {
							_eventMap->ResetGenerator(originTile.X, originTile.Y);
						}

						_eventMap->Deactivate(originTile.X, originTile.Y);
						actor->_state |= Actors::ActorState::IsDestroyed;
					}
				}
			}

			for (std::size_t i = 0; i < playerZones.size(); i += 2) {
				const auto& activationZone = playerZones[i];
				_eventMap->ActivateEvents(activationZone.L, activationZone.T, activationZone.R, activationZone.B, true);
			}

			if (!_checkpointCreated) {
				// Create checkpoint after first call to ActivateEvents() to avoid duplication of objects that are spawned near player spawn
				_checkpointCreated = true;
				_eventMap->CreateCheckpointForRollback();
#if defined(WITH_ANGELSCRIPT)
				if (_scripts != nullptr) {
					_scripts->OnLevelBegin();
				}
#endif
			}
		}

		_eventMap->ProcessGenerators(timeMult);
	}

	void LevelHandler::ProcessQueuedNextLevel()
	{
		bool playersReady = true;
		for (auto player : _players) {
			// Exit type was already provided in BeginLevelChange()
			playersReady &= player->OnLevelChanging(ExitType::None);
		}

		if (playersReady && _nextLevelTime <= 0.0f) {
			LevelInitialization levelInit;
			PrepareNextLevelInitialization(levelInit);
			_root->ChangeLevel(std::move(levelInit));
		}
	}

	void LevelHandler::PrepareNextLevelInitialization(LevelInitialization& levelInit)
	{
		StringView realNextLevel;
		if (!_nextLevel.empty()) {
			realNextLevel = _nextLevel;
		} else {
			realNextLevel = ((_nextLevelType & ExitType::TypeMask) == ExitType::Bonus ? _defaultSecretLevel : _defaultNextLevel);
		}

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

		for (std::int32_t i = 0; i < _players.size(); i++) {
			levelInit.PlayerCarryOvers[i] = _players[i]->PrepareLevelCarryOver();
		}
	}

	void LevelHandler::ResolveCollisions(float timeMult)
	{
		ZoneScopedC(0x4876AF);

		auto it = _actors.begin();
		while (it != _actors.end()) {
			Actors::ActorBase* actor = it->get();
			if (actor->GetState(Actors::ActorState::IsDestroyed)) {
				BeforeActorDestroyed(actor);
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

		auto* targetObj = _players[0];

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
		ZoneScopedC(0x4876AF);

		constexpr float ResponsivenessChange = 0.04f;
		constexpr float ResponsivenessMin = 0.3f;
		constexpr float SlowRatioX = 0.3f;
		constexpr float SlowRatioY = 0.3f;
		constexpr float FastRatioX = 0.2f;
		constexpr float FastRatioY = 0.04f;

		if (_players.empty()) {
			return;
		}

		auto* targetObj = _players[0];

		// Viewport bounds animation
		if (_viewBounds != _viewBoundsTarget) {
			if (std::abs(_viewBounds.X - _viewBoundsTarget.X) < 2.0f) {
				_viewBounds = _viewBoundsTarget;
			} else {
				constexpr float TransitionSpeed = 0.02f;
				float dx = (_viewBoundsTarget.X - _viewBounds.X) * TransitionSpeed * timeMult;
				_viewBounds.X += dx;
				_viewBounds.W -= dx;
			}
		}

		// The position to focus on
		Vector2i halfView = _view->size() / 2;
		Vector2f focusPos = targetObj->_pos;

		// If player doesn't move but has some speed, it's probably stuck, so reset the speed
		Vector2f focusSpeed = targetObj->_speed;
		if (std::abs(_cameraLastPos.X - focusPos.X) < 1.0f) {
			focusSpeed.X = 0.0f;
		}
		if (std::abs(_cameraLastPos.Y - focusPos.Y) < 1.0f) {
			focusSpeed.Y = 0.0f;
		}

		Vector2f focusVelocity = Vector2f(std::abs(focusSpeed.X), std::abs(focusSpeed.Y));

		// Camera responsiveness (smoothing unexpected movements)
		if (focusVelocity.X < 1.0f) {
			if (_cameraResponsiveness.X > ResponsivenessMin) {
				_cameraResponsiveness.X = std::max(_cameraResponsiveness.X - ResponsivenessChange * timeMult, ResponsivenessMin);
			}
		} else {
			if (_cameraResponsiveness.X < 1.0f) {
				_cameraResponsiveness.X = std::min(_cameraResponsiveness.X + ResponsivenessChange * timeMult, 1.0f);
			}
		}
		if (focusVelocity.Y < 1.0f) {
			if (_cameraResponsiveness.Y > ResponsivenessMin) {
				_cameraResponsiveness.Y = std::max(_cameraResponsiveness.Y - ResponsivenessChange * timeMult, ResponsivenessMin);
			}
		} else {
			if (_cameraResponsiveness.Y < 1.0f) {
				_cameraResponsiveness.Y = std::min(_cameraResponsiveness.Y + ResponsivenessChange * timeMult, 1.0f);
			}
		}

		_cameraLastPos.X = lerpByTime(_cameraLastPos.X, focusPos.X, _cameraResponsiveness.X, timeMult);
		_cameraLastPos.Y = lerpByTime(_cameraLastPos.Y, focusPos.Y, _cameraResponsiveness.Y, timeMult);

		_cameraDistanceFactor.X = lerpByTime(_cameraDistanceFactor.X, focusSpeed.X * 8.0f, (focusVelocity.X < 2.0f ? SlowRatioX : FastRatioX), timeMult);
		_cameraDistanceFactor.Y = lerpByTime(_cameraDistanceFactor.Y, focusSpeed.Y * 5.0f, (focusVelocity.Y < 2.0f ? SlowRatioY : FastRatioY), timeMult);

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
			_cameraPos.X = std::clamp(_cameraLastPos.X + _cameraDistanceFactor.X, _viewBounds.X + halfView.X, _viewBounds.X + _viewBounds.W - halfView.X) + _shakeOffset.X;
			if (!PreferencesCache::UnalignedViewport || std::abs(_cameraDistanceFactor.X) < 1.0f) {
				_cameraPos.X = std::floor(_cameraPos.X);
			}
		} else {
			_cameraPos.X = std::floor(_viewBounds.X + _viewBounds.W * 0.5f + _shakeOffset.X);
		}
		if (_viewBounds.H > halfView.Y * 2) {
			_cameraPos.Y = std::clamp(_cameraLastPos.Y + _cameraDistanceFactor.Y, _viewBounds.Y + halfView.Y - 1.0f, _viewBounds.Y + _viewBounds.H - halfView.Y - 2.0f) + _shakeOffset.Y;
			if (!PreferencesCache::UnalignedViewport || std::abs(_cameraDistanceFactor.Y) < 1.0f) {
				_cameraPos.Y = std::floor(_cameraPos.Y);
			}
		} else {
			_cameraPos.Y = std::floor(_viewBounds.Y + _viewBounds.H * 0.5f + _shakeOffset.Y);
		}

		_camera->setView(_cameraPos - halfView.As<float>(), 0.0f, 1.0f);

		// Update audio listener position
		IAudioDevice& device = theServiceLocator().audioDevice();
		device.updateListener(Vector3f(_cameraPos, 0.0f), Vector3f(focusSpeed, 0.0f));
	}

	void LevelHandler::LimitCameraView(int left, int width)
	{
		_levelBounds.X = left;
		if (width > 0.0f) {
			_levelBounds.W = width;
		} else {
			_levelBounds.W = _tileMap->GetLevelBounds().X - left;
		}

		if (left == 0 && width == 0) {
			_viewBounds = _levelBounds.As<float>();
			_viewBoundsTarget = _viewBounds;
		} else {
			Rectf bounds = _levelBounds.As<float>();
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

	bool LevelHandler::GetTrigger(std::uint8_t triggerId)
	{
		return _tileMap->GetTrigger(triggerId);
	}

	void LevelHandler::SetTrigger(std::uint8_t triggerId, bool newState)
	{
		_tileMap->SetTrigger(triggerId, newState);
	}

	void LevelHandler::SetWeather(WeatherType type, uint8_t intensity)
	{
		_weatherType = type;
		_weatherIntensity = intensity;
	}

	bool LevelHandler::BeginPlayMusic(const StringView path, bool setDefault, bool forceReload)
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
		ZoneScopedC(0x4876AF);

		auto& input = theApplication().inputManager();
		_pressedActionsLast = _pressedActions;

		const JoyMappedState* joyStates[UI::ControlScheme::MaxConnectedGamepads];
		std::int32_t joyStatesCount = 0;
		for (std::int32_t i = 0; i < IInputManager::MaxNumJoysticks && joyStatesCount < countof(joyStates); i++) {
			if (input.isJoyMapped(i)) {
				joyStates[joyStatesCount++] = &input.joyMappedState(i);
			}
		}

		auto processedInput = UI::ControlScheme::FetchProcessedInput(0,
			_pressedKeys, ArrayView(joyStates, joyStatesCount), !_hud->IsWeaponWheelVisible());
		_pressedActions = processedInput.PressedActions;
		_playerRequiredMovement = processedInput.Movement;

		// Also apply overriden actions (by touch controls)
		_pressedActions |= _overrideActions;

		if ((_overrideActions & (1 << (std::int32_t)PlayerActions::Right)) != 0) {
			_playerRequiredMovement.X = 1.0f;
		} else if ((_overrideActions & (1 << (std::int32_t)PlayerActions::Left)) != 0) {
			_playerRequiredMovement.X = -1.0f;
		}
		if ((_overrideActions & (1 << (std::int32_t)PlayerActions::Down)) != 0) {
			_playerRequiredMovement.Y = 1.0f;
		} else if ((_overrideActions & (1 << (std::int32_t)PlayerActions::Up)) != 0) {
			_playerRequiredMovement.Y = -1.0f;
		}
	}

	void LevelHandler::UpdateRichPresence()
	{
#if (defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)) || defined(DEATH_TARGET_UNIX)
		if (!PreferencesCache::EnableDiscordIntegration || !UI::DiscordRpcClient::Get().IsSupported()) {
			return;
		}

		UI::DiscordRpcClient::RichPresence richPresence;
		if (_episodeName == "prince"_s) {
			if (_levelFileName == "01_castle1"_s || _levelFileName == "02_castle1n"_s) {
				richPresence.LargeImage = "level-prince-01"_s;
			} else if (_levelFileName == "03_carrot1"_s || _levelFileName == "04_carrot1n"_s) {
				richPresence.LargeImage = "level-prince-02"_s;
			} else if (_levelFileName == "05_labrat1"_s || _levelFileName == "06_labrat2"_s || _levelFileName == "bonus_labrat3"_s) {
				richPresence.LargeImage = "level-prince-03"_s;
			}
		} else if (_episodeName == "rescue"_s) {
			if (_levelFileName == "01_colon1"_s || _levelFileName == "02_colon2"_s) {
				richPresence.LargeImage = "level-rescue-01"_s;
			} else if (_levelFileName == "03_psych1"_s || _levelFileName == "04_psych2"_s || _levelFileName == "bonus_psych3"_s) {
				richPresence.LargeImage = "level-rescue-02"_s;
			} else if (_levelFileName == "05_beach"_s || _levelFileName == "06_beach2"_s) {
				richPresence.LargeImage = "level-rescue-03"_s;
			}
		} else if (_episodeName == "flash"_s) {
			if (_levelFileName == "01_diam1"_s || _levelFileName == "02_diam3"_s) {
				richPresence.LargeImage = "level-flash-01"_s;
			} else if (_levelFileName == "03_tube1"_s || _levelFileName == "04_tube2"_s || _levelFileName == "bonus_tube3"_s) {
				richPresence.LargeImage = "level-flash-02"_s;
			} else if (_levelFileName == "05_medivo1"_s || _levelFileName == "06_medivo2"_s || _levelFileName == "bonus_garglair"_s) {
				richPresence.LargeImage = "level-flash-03"_s;
			}
		} else if (_episodeName == "monk"_s) {
			if (_levelFileName == "01_jung1"_s || _levelFileName == "02_jung2"_s) {
				richPresence.LargeImage = "level-monk-01"_s;
			} else if (_levelFileName == "03_hell"_s || _levelFileName == "04_hell2"_s) {
				richPresence.LargeImage = "level-monk-02"_s;
			} else if (_levelFileName == "05_damn"_s || _levelFileName == "06_damn2"_s) {
				richPresence.LargeImage = "level-monk-03"_s;
			}
		} else if (_episodeName == "secretf"_s) {
			if (_levelFileName == "01_easter1"_s || _levelFileName == "02_easter2"_s || _levelFileName == "03_easter3"_s) {
				richPresence.LargeImage = "level-secretf-01"_s;
			} else if (_levelFileName == "04_haunted1"_s || _levelFileName == "05_haunted2"_s || _levelFileName == "06_haunted3"_s) {
				richPresence.LargeImage = "level-secretf-02"_s;
			} else if (_levelFileName == "07_town1"_s || _levelFileName == "08_town2"_s || _levelFileName == "09_town3"_s) {
				richPresence.LargeImage = "level-secretf-03"_s;
			}
		} else if (_episodeName == "xmas98"_s || _episodeName == "xmas99"_s) {
			richPresence.LargeImage = "level-xmas"_s;
		} else if (_episodeName == "share"_s) {
			richPresence.LargeImage = "level-share"_s;
		}

		if (richPresence.LargeImage.empty()) {
			richPresence.Details = "Playing as "_s;
			richPresence.LargeImage = "main-transparent"_s;

			if (!_players.empty())
				switch (_players[0]->GetPlayerType()) {
					default:
					case PlayerType::Jazz: richPresence.SmallImage = "playing-jazz"_s; break;
					case PlayerType::Spaz: richPresence.SmallImage = "playing-spaz"_s; break;
					case PlayerType::Lori: richPresence.SmallImage = "playing-lori"_s; break;
				}
		} else {
			richPresence.Details = "Playing episode as "_s;
		}

		if (!_players.empty()) {
			switch (_players[0]->GetPlayerType()) {
				default:
				case PlayerType::Jazz: richPresence.Details += "Jazz"_s; break;
				case PlayerType::Spaz: richPresence.Details += "Spaz"_s; break;
				case PlayerType::Lori: richPresence.Details += "Lori"_s; break;
			}
		}

		UI::DiscordRpcClient::Get().SetRichPresence(richPresence);
#endif
	}

	void LevelHandler::PauseGame()
	{
		// Show in-game pause menu
		_pauseMenu = std::make_shared<UI::Menu::InGameMenu>(this);
		if (IsPausable()) {
			// Prevent updating of all level objects
			_rootNode->setUpdateEnabled(false);
		}

#if defined(WITH_AUDIO)
		// Use low-pass filter on music and pause all SFX
		if (_music != nullptr) {
			_music->setLowPass(0.1f);
		}
		if (IsPausable()) {
			for (auto& sound : _playingSounds) {
				if (sound->isPlaying()) {
					sound->pause();
				}
			}
			// If Sugar Rush music is playing, pause it and play normal music instead
			if (_sugarRushMusic != nullptr && _music != nullptr) {
				_music->play();
			}
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
		_pressedActions |= (1ull << (int32_t)PlayerActions::Menu);
		_pressedActionsLast |= (1ull << (int32_t)PlayerActions::Menu);
	}

#if defined(WITH_IMGUI)
	ImVec2 LevelHandler::WorldPosToScreenSpace(const Vector2f pos)
	{
		Vector2i originalSize = _view->size();
		Vector2f upscaledSize = _upscalePass.GetTargetSize();
		Vector2i halfView = originalSize / 2;
		return ImVec2(
			(pos.X - _cameraPos.X + halfView.X) * upscaledSize.X / originalSize.X,
			(pos.Y - _cameraPos.Y + halfView.Y) * upscaledSize.Y / originalSize.Y
		);
	}
#endif

	bool LevelHandler::LightingRenderer::OnDraw(RenderQueue& renderQueue)
	{
		_renderCommandsCount = 0;
		_emittedLightsCache.clear();

		// Collect all active light emitters
		std::size_t actorsCount = _owner->_actors.size();
		for (std::size_t i = 0; i < actorsCount; i++) {
			_owner->_actors[i]->OnEmitLights(_emittedLightsCache);
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
			_renderCommandsCount++;
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
		_camera->setOrthoProjection(0.0f, (float)width, (float)height, 0.0f);
		_camera->setView(0.0f, 0.0f, 0.0f, 1.0f);

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

		auto* instanceBlock = _renderCommand.material().uniformBlock(Material::InstanceBlockName);
		instanceBlock->uniform(Material::TexRectUniformName)->setFloatValue(1.0f, 0.0f, 1.0f, 0.0f);
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
		if (viewHasWater && !PreferencesCache::LowWaterQuality) {
			command.material().setTexture(4, *_owner->_noiseTexture);
		}

		auto* instanceBlock = command.material().uniformBlock(Material::InstanceBlockName);
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