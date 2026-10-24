#include "EditorLevelHandler.h"
#include "RuntimeSync.h"

#if defined(WITH_EDITOR)

#include "../../ContentResolver.h"
#include "../../PreferencesCache.h"
#include "../../Rendering/PlayerViewport.h"
#include "../../Tiles/TileMap.h"
#include "../../Events/EventMap.h"

#include "../../../nCine/Application.h"
#include "../../../nCine/ServiceLocator.h"
#include "../../../nCine/Audio/AudioStreamPlayer.h"

#include <IO/MemoryStream.h>

using namespace Death::IO;

namespace Jazz2::Editor
{
	EditorLevelHandler::EditorLevelHandler(IRootController* root)
		: LevelHandler(root), _musicLowPass(EditMusicLowPass), _musicPaused(false), _cameraPlaced(false), _initialCameraScale(0.0f)
	{
	}

	EditorLevelHandler::~EditorLevelHandler()
	{
	}

	bool EditorLevelHandler::Initialize(const LevelDocument& document, StringView levelName, StringView fullPath)
	{
		// The level is loaded from what the document saves, by the same loader as any level, so it's shown as the
		// game would show it. Stored without compression, it's only kept in memory until it's loaded.
		MemoryStream s;
		if (!document.Save(s, true, 0)) {
			LOGE("Level \"{}\" cannot be prepared for the editor", levelName);
			return false;
		}
		_levelData.assign(s.GetBuffer(), s.GetBuffer() + s.GetSize());
		_fullPath = fullPath;
		_loadedTileSet = document.Properties.DefaultTileSet;
		_loadedFlags = document.Flags;

		LevelInitialization levelInit(levelName, GameDifficulty::Normal, PreferencesCache::EnableReforgedGameplay, false, PlayerType::Jazz);
		bool result = LevelHandler::Initialize(levelInit);
		_levelData = SmallVector<std::uint8_t, 0>();
		if (result) {
			// Layers hidden in the editor aren't shown, see RuntimeSync::IsLayerShown()
			RuntimeSync::ApplyLayerStates(document, *_tileMap);
		}
		return result;
	}

	bool EditorSceneCanvas::OnDraw(RenderQueue& renderQueue)
	{
		UI::Canvas::OnDraw(renderQueue);
		if (DrawContent) {
			DrawContent(*this);
		}
		return false;
	}

	void EditorLevelHandler::SetSceneContent(Function<void(UI::Canvas&)>&& drawContent)
	{
		if (_sceneCanvas == nullptr) {
			if (_rootNode == nullptr) {
				return;
			}
			_sceneCanvas = std::make_unique<EditorSceneCanvas>();
			_sceneCanvas->setParent(_rootNode.get());
		}
		_sceneCanvas->DrawContent = Death::move(drawContent);
	}

	void EditorLevelHandler::SetInitialCamera(Vector2f center, float scale, bool relative)
	{
		_initialCameraCenter = center;
		_initialCameraScale = scale;
		_initialCameraRelative = relative;
	}

	Rendering::PlayerViewport* EditorLevelHandler::GetViewport() const
	{
		return (!_assignedViewports.empty() ? _assignedViewports[0].get() : nullptr);
	}

	void EditorLevelHandler::SetMusicLowPass(float value)
	{
		_musicLowPass = value;
#if defined(WITH_AUDIO)
		if (_music != nullptr) {
			_music->setLowPass(value);
		}
#endif
	}

	void EditorLevelHandler::SetMusicPaused(bool paused)
	{
#if defined(WITH_AUDIO)
		_musicPaused = paused;
		if (_music == nullptr) {
			return;
		}
		if (paused) {
			_music->pause();
		} else if (_musicStartDelay == 0) {
			_music->play();
		}
#endif
	}

	bool EditorLevelHandler::NeedsReload(const LevelDocument& document) const
	{
		// The tile set and the palette are taken when the level is loaded, pits are part of the tile map
		constexpr LevelFlags LoadedFlags = LevelFlags::UseLevelPalette | LevelFlags::HasPit | LevelFlags::HasPitInstantDeath;
		return (document.Properties.DefaultTileSet != _loadedTileSet || (document.Flags & LoadedFlags) != (_loadedFlags & LoadedFlags));
	}

	void EditorLevelHandler::ApplyLevelProperties(const Formats::LevelProperties& properties)
	{
		// As the level loader sets them
		_waterLevel = properties.WaterLevel;
		std::uint32_t color = properties.AmbientColor;
		_defaultAmbientLight = Vector4f((color & 0xff) / 255.0f, ((color >> 8) & 0xff) / 255.0f, ((color >> 16) & 0xff) / 255.0f, ((color >> 24) & 0xff) / 255.0f);
		_weatherType = WeatherType(properties.Weather);
		_weatherIntensity = properties.WeatherIntensity;

#if defined(WITH_AUDIO)
		// Other music starts right away, muffled as the one before, so it can be heard when it's chosen
		if (properties.Music != _musicCurrentPath) {
			if (_music != nullptr) {
				_music->stop();
			}
			_music = (!properties.Music.empty() ? ContentResolver::Get().GetMusic(properties.Music) : nullptr);
			if (_music != nullptr) {
				_music->setLooping(true);
				_music->setGain(PreferencesCache::MasterVolume * PreferencesCache::MusicVolume);
				_music->setSourceRelative(true);
				_music->setLowPass(_musicLowPass);
				if (!_musicPaused) {
					_music->play();
				}
			}
			_musicStartDelay = 0;
			_musicCurrentPath = properties.Music;
			_musicDefaultPath = properties.Music;
		}
#endif
	}

	bool EditorLevelHandler::IsPausable() const
	{
		return false;
	}

	void EditorLevelHandler::OnBeginFrame()
	{
		float timeMult = theApplication().GetTimeMult();

#if defined(WITH_AUDIO)
		if DEATH_UNLIKELY(_musicStartDelay > 0 && --_musicStartDelay == 0 && _music != nullptr) {
			_music->setLowPass(_musicLowPass);
			if (!_musicPaused) {
				_music->play();
			}
		}
#endif

		_camera.Update(timeMult);
		ApplyCamera();
	}

	void EditorLevelHandler::OnEndFrame()
	{
		float timeMult = theApplication().GetTimeMult();

		_tileMap->OnEndFrame();

		for (auto& viewport : _assignedViewports) {
			viewport->UpdateCamera(timeMult);
			viewport->OnEndFrame();
		}

#if defined(WITH_AUDIO)
		IAudioDevice& audioDevice = theServiceLocator().GetAudioDevice();
		audioDevice.updateListener(Vector3f(_camera.GetCenter(), 0.0f), Vector3f::Zero);
#endif

		_elapsedFrames += timeMult;
	}

	void EditorLevelHandler::OnInitializeViewport(std::int32_t width, std::int32_t height)
	{
		LevelHandler::OnInitializeViewport(width, height);

		Rendering::PlayerViewport* viewport = GetViewport();
		if (viewport == nullptr) {
			return;
		}

		// The editor shows the level as it is, without the lighting of the level
		viewport->_ambientLight.W = 1.0f;
		viewport->_ambientLightTarget = 1.0f;

		float gameplayScale = (_referenceViewSize.X > 0 ? float(_viewSize.X) / float(_referenceViewSize.X) : 1.0f);
		_camera.SetView(_viewSize, gameplayScale);
		_camera.SetLevelBounds(_levelBounds.As<float>());

		if (!_cameraPlaced && _initialCameraScale > 0.0f) {
			_cameraPlaced = true;
			_camera.JumpTo(_initialCameraCenter, _initialCameraRelative
				? _camera.SnapScale(_initialCameraScale * gameplayScale, 0) : _initialCameraScale);
		} else if (!_cameraPlaced) {
			_cameraPlaced = true;
			Vector2f center = _eventMap->GetSpawnPosition(PlayerType::Jazz);
			if (center.X < 0.0f && center.Y < 0.0f) {
				center = Vector2f(_levelBounds.X + _levelBounds.W * 0.5f, _levelBounds.Y + _levelBounds.H * 0.5f);
			}
			_camera.JumpTo(center, _camera.SnapScale(gameplayScale, 0));
		}

		// The layers are placed as the game would place them for a view of its own size around the center
		Tiles::TileMapViewOptions options = _tileMap->GetViewOptions();
		options.Placement = Tiles::LayerPlacement::ReferenceView;
		options.ReferenceViewSize = _referenceViewSize;
		options.DrawSideBars = false;
		_tileMap->SetViewOptions(options);

		ApplyCamera();
		viewport->UpdateCamera(0.0f);
	}

	void EditorLevelHandler::OnKeyPressed(const KeyboardEvent& event)
	{
		// The editor session handles all input
	}

	void EditorLevelHandler::OnKeyReleased(const KeyboardEvent& event)
	{
	}

	void EditorLevelHandler::OnTextInput(const TextInputEvent& event)
	{
	}

	void EditorLevelHandler::OnTouchEvent(const TouchEvent& event)
	{
	}

	LevelHandler::ViewportSetup EditorLevelHandler::GetViewportSetup(std::int32_t width, std::int32_t height)
	{
		_referenceViewSize = Rendering::UpscaleRenderPass::CalculateViewSize(width, height, DefaultWidth, DefaultHeight);

		ViewportSetup setup;
		setup.ViewSize = Vector2i(width, height);
		setup.Supersample = 1;
		setup.NativeOutput = true;
		return setup;
	}

	bool EditorLevelHandler::LoadLevel(LevelDescriptor& descriptor)
	{
		MemoryStream s(_levelData.data(), std::int64_t(_levelData.size()));
		return ContentResolver::Get().TryLoadLevel(s, _fullPath, GetLevelLoadOptions(), descriptor);
	}

	void EditorLevelHandler::LoadLevelScripts(StringView fullPath)
	{
		// Scripts don't run while the level is being edited
	}

	void EditorLevelHandler::SpawnPlayers(const LevelInitialization& levelInit)
	{
		// No players, only a view with a free camera
		AssignViewport(nullptr);
	}

	void EditorLevelHandler::ApplyCamera()
	{
		Rendering::PlayerViewport* viewport = GetViewport();
		if (viewport != nullptr) {
			viewport->SetFreeCamera(_camera.GetCenter(), _camera.GetScale());
		}
	}
}

#endif
