#include "PlayTestController.h"

#if defined(WITH_EDITOR)

#include "EditorLevelHandler.h"
#include "PlayTestLevelHandler.h"
#include "RuntimeSync.h"
#include "../UI/UiTheme.h"
#if defined(WITH_MULTIPLAYER)
#	include "PlayTestMpLevelHandler.h"
#endif
#include "../../Actors/Player.h"
#include "../../Events/EventMap.h"
#include "../../Formats/LevelTextsChunk.h"
#include "../../Rendering/PlayerViewport.h"
#include "../../Tiles/TileMap.h"

#include "../../../nCine/Application.h"
#include "../../../nCine/I18n.h"
#include "../../../nCine/Graphics/Viewport.h"

#include <Containers/StringConcatenable.h>
#include <IO/MemoryStream.h>

#include <algorithm>
#include <cmath>
#include <cstring>

using namespace Death::IO;
using namespace Death::Containers::Literals;

namespace Jazz2::Editor
{
	namespace
	{
		// How much the music is muffled while the level is edited, 1 for not at all
		constexpr float EditMusicLowPass = EditorLevelHandler::EditMusicLowPass;
		// How long the music takes to open up or to be muffled again, in frames at 60 FPS
		constexpr float MusicFadeFrames = 45.0f;
	}

	PlayTestController::PlayTestController(IPlayTestHost& host)
		: _host(host), _mode(PlayTestMode::Edit), _playRevision(0), _resumePlay(false), _restorePlayState(false),
			_enteringFrameShown(false), _editCameraScale(1.0f), _playTestCount(0), _playTestNumber(0), _musicFadeFrom(1.0f), _musicFadeTo(1.0f),
			_musicFadeTime(-1.0f), _hasResumePosition(false)
	{
	}

	PlayTestController::~PlayTestController()
	{
		Release();
	}

	void PlayTestController::Release()
	{
		// The rings refer to the last images of the play-tests
		_transition.Stop();
		_closingPlayHandler = nullptr;
		_playHandler = nullptr;
		_mode = PlayTestMode::Edit;
	}

	LevelHandler* PlayTestController::GetGameHandler() const
	{
		return (_mode == PlayTestMode::Play && _playHandler != nullptr ? &_playHandler->GetLevelHandler() : nullptr);
	}

	void PlayTestController::ReadPathHistory(const LevelDocument& document)
	{
		_pathHistory.Read(document);
		ArrayView<const PlayPath> paths = _pathHistory.GetPaths();
		if (!paths.empty()) {
			_lastPlayPath = paths.back();
		}
	}

	void PlayTestController::ClearPathHistory()
	{
		_pathHistory.Clear();
		_lastPlayPath.Clear();
	}

	void PlayTestController::MarkPlayTestOutdated()
	{
		if (_playHandler != nullptr) {
			// Not current, and not something that could be given to it either
			_playRevision = 0;
			_changesSincePlay.Add(DocumentChangeKind::LevelProperties);
		}
	}

	void PlayTestController::NoteDocumentChanges(const DocumentChanges& changes)
	{
		if (_playHandler != nullptr) {
			_changesSincePlay.Add(changes);
		}
	}

	namespace
	{
		// Events the level loader handles otherwise than by storing them in their tile, they can't be changed under a
		// level being played
		bool IsStructuralEvent(const Formats::EventRecord& event)
		{
			if ((event.Flags & Formats::EventRecord::Generator) != 0) {
				return true;
			}
			switch (EventType(event.Type)) {
				case EventType::LevelStart:
				case EventType::LevelStartMultiplayer:
				case EventType::WarpTarget:
				case EventType::ModifierOneWay:
				case EventType::ModifierVine:
				case EventType::ModifierHook:
				case EventType::SceneryDestruct:
				case EventType::SceneryDestructButtstomp:
				case EventType::TriggerArea:
				case EventType::SceneryDestructSpeed:
				case EventType::SceneryCollapse:
				case EventType::ModifierHPole:
				case EventType::ModifierVPole:
					return true;
				default:
					return false;
			}
		}

		const Formats::EventRecord* FindEvent(const SmallVectorImpl<DocumentEvent>& events, std::uint32_t cell)
		{
			auto it = std::lower_bound(events.begin(), events.end(), cell, [](const DocumentEvent& event, std::uint32_t c) {
				return (event.Cell < c);
			});
			return (it != events.end() && it->Cell == cell ? &it->Record : nullptr);
		}

		// Chunks the game reads from a level, see ContentResolver::TryLoadLevel(), the others are only of the editor
		constexpr std::uint32_t GameChunkTags[] = { Formats::LevelTextsChunk::Tag, Formats::LayerPropertiesChunk::Tag, Formats::ScriptDataChunk::Tag };

		void CollectGameChunks(const LevelDocument& document, SmallVectorImpl<Formats::ChunkRecord>& chunks)
		{
			chunks.clear();
			for (std::uint32_t tag : GameChunkTags) {
				if (const Formats::ChunkRecord* chunk = document.FindChunk(tag)) {
					chunks.push_back(*chunk);
				}
			}
		}

		bool AreGameChunksSame(const LevelDocument& document, const SmallVectorImpl<Formats::ChunkRecord>& chunks)
		{
			std::size_t found = 0;
			for (std::uint32_t tag : GameChunkTags) {
				const Formats::ChunkRecord* now = document.FindChunk(tag);
				const Formats::ChunkRecord* before = nullptr;
				for (const Formats::ChunkRecord& chunk : chunks) {
					if (chunk.Tag == tag) {
						before = &chunk;
						break;
					}
				}
				if ((now == nullptr) != (before == nullptr)) {
					return false;
				}
				if (now != nullptr) {
					if (now->Data.size() != before->Data.size() || (!now->Data.empty() &&
						std::memcmp(now->Data.data(), before->Data.data(), now->Data.size()) != 0)) {
						return false;
					}
					found++;
				}
			}
			return (found == chunks.size());
		}
	}

	bool PlayTestController::PushChanges()
	{
#if defined(DEATH_TARGET_CONSTRAINED_MEMORY)
		// Without the level the play-test was made from, it isn't known what changed
		return false;
#else
		// Only tiles and events of the grid are given to a level being played, the rest is only for the editor
		constexpr DocumentChangeKind PushableKinds = DocumentChangeKind::Tiles | DocumentChangeKind::Events |
			DocumentChangeKind::EditorState | DocumentChangeKind::Chunks;
		if (_playHandler == nullptr || _changesSincePlay.IsEmpty() || _playedOptions.GameMode != 0 ||
			(_changesSincePlay.GetKinds() & ~PushableKinds) != DocumentChangeKind::None) {
			return false;
		}

		const LevelDocument& document = _host.GetDocument();
		LevelHandler& gameHandler = _playHandler->GetLevelHandler();
		Tiles::TileMap* tileMap = gameHandler.TileMap();
		Events::EventMap* eventMap = gameHandler.EventMap();
		std::int32_t spriteLayer = document.GetSpriteLayerIndex();
		if (tileMap == nullptr || eventMap == nullptr || spriteLayer < 0 || tileMap->GetLayerCount() != std::int32_t(document.Layers.size())) {
			return false;
		}
		// Off-grid events are kept by their order, they can't be moved under a level being played
		if (_suspendedOffGridEvents.size() != document.OffGridEvents.size() || std::memcmp(_suspendedOffGridEvents.data(),
			document.OffGridEvents.data(), document.OffGridEvents.size() * sizeof(Formats::OffGridEventRecord)) != 0) {
			return false;
		}
		// The translations of the texts and the rest the game reads of the chunks only when the level is loaded
		if (_changesSincePlay.HasAny(DocumentChangeKind::Chunks) && !AreGameChunksSame(document, _suspendedGameChunks)) {
			return false;
		}

		Vector2i gridSize = document.GetEventGridSize();
		const DocumentLayer& sprites = document.Layers[spriteLayer];
		// A tile of the sprite layer with an event that changes what the tile does was set up by the level loader
		auto hasTileEvent = [&](std::int32_t x, std::int32_t y) {
			if (x < 0 || y < 0 || x >= gridSize.X || y >= gridSize.Y) {
				return false;
			}
			std::uint32_t cell = std::uint32_t(x + y * gridSize.X);
			const Formats::EventRecord* now = FindEvent(document.Events, cell);
			const Formats::EventRecord* before = FindEvent(_suspendedEvents, cell);
			return ((now != nullptr && IsStructuralEvent(*now)) || (before != nullptr && IsStructuralEvent(*before)));
		};

		// Everything is checked first, the level isn't changed at all if something can't be given to it
		for (const DocumentTileArea& area : _changesSincePlay.GetTileAreas()) {
			if (area.Layer != spriteLayer) {
				continue;
			}
			for (std::int32_t y = std::max<std::int32_t>(area.Area.Y, 0); y < area.Area.Y + area.Area.H && y < sprites.GetHeight(); y++) {
				for (std::int32_t x = std::max<std::int32_t>(area.Area.X, 0); x < area.Area.X + area.Area.W && x < sprites.GetWidth(); x++) {
					if (hasTileEvent(x, y)) {
						return false;
					}
				}
			}
		}
		const Recti& eventArea = _changesSincePlay.GetEventArea();
		SmallVector<std::uint32_t, 0> changedCells;
		for (std::int32_t y = std::max<std::int32_t>(eventArea.Y, 0); y < eventArea.Y + eventArea.H && y < gridSize.Y; y++) {
			for (std::int32_t x = std::max<std::int32_t>(eventArea.X, 0); x < eventArea.X + eventArea.W && x < gridSize.X; x++) {
				std::uint32_t cell = std::uint32_t(x + y * gridSize.X);
				const Formats::EventRecord* now = FindEvent(document.Events, cell);
				const Formats::EventRecord* before = FindEvent(_suspendedEvents, cell);
				bool same = (now == nullptr ? before == nullptr : (before != nullptr && LevelDocument::AreEventsEqual(*now, *before)));
				if (same) {
					// Left as the game has it, collected or not
					continue;
				}
				if ((now != nullptr && IsStructuralEvent(*now)) || (before != nullptr && IsStructuralEvent(*before))) {
					return false;
				}
				changedCells.push_back(cell);
			}
		}

		for (const DocumentTileArea& area : _changesSincePlay.GetTileAreas()) {
			RuntimeSync::ApplyTiles(document, *tileMap, area.Layer, area.Area);
		}
		// As the level loader stores them, an event not of the difficulty played isn't there at all
		std::uint8_t difficultyFlag = (_playedOptions.Difficulty == GameDifficulty::Easy ? Formats::EventRecord::Easy
			: (_playedOptions.Difficulty == GameDifficulty::Hard ? Formats::EventRecord::Hard : Formats::EventRecord::Normal));
		// The objects spawned from the events before go, all in one pass, they're spawned again as the events are now, at
		// once if they're near the player - otherwise a light would stay as it was until the player went away from it and
		// came back; the cells were collected row by row, so they're sorted
		if (PlayTestDebug* debug = gameHandler.GetPlayTestDebug()) {
			debug->DespawnTileEventActors(ArrayView<const std::uint32_t>(changedCells.data(), changedCells.size()), gridSize.X);
		}
		for (std::uint32_t cell : changedCells) {
			std::int32_t x = std::int32_t(cell % std::uint32_t(gridSize.X)), y = std::int32_t(cell / std::uint32_t(gridSize.X));
			const Formats::EventRecord* now = FindEvent(document.Events, cell);
			bool present = (now != nullptr && EventType(now->Type) != EventType::Empty &&
				((now->Flags & Formats::EventRecord::DifficultyMask) == 0 || (now->Flags & difficultyFlag) != 0) &&
				(now->Flags & Formats::EventRecord::MultiplayerOnly) == 0);
			if (present) {
				std::uint8_t params[Events::EventSpawner::SpawnParamsSize];
				std::memcpy(params, now->Params, sizeof(params));
				eventMap->StoreTileEvent(x, y, EventType(now->Type), Actors::ActorState(now->Flags & Formats::EventRecord::Illuminated), params);
			} else {
				eventMap->StoreTileEvent(x, y, EventType::Empty);
			}
		}

		LOGI("Edited tiles and events were given to the play-test, {} events changed", std::int32_t(changedCells.size()));
		_playRevision = _host.GetDocumentRevision();
		RememberSuspendedLevel();
		_changesSincePlay.Clear();
		return true;
#endif
	}

	void PlayTestController::SetDebugOptions(const PlayTestDebugOptions& options)
	{
		_debugOptions = options;
		if (_playHandler != nullptr) {
			// The kept one too, it goes on with them when it's resumed
			if (PlayTestDebug* debug = _playHandler->GetLevelHandler().GetPlayTestDebug()) {
				debug->SetOptions(_debugOptions);
			}
		}
	}

	void PlayTestController::StepFrame()
	{
		LevelHandler* gameHandler = GetGameHandler();
		if (PlayTestDebug* debug = (gameHandler != nullptr ? gameHandler->GetPlayTestDebug() : nullptr)) {
			debug->Step();
			// It stands still afterwards
			_debugOptions = debug->GetOptions();
		}
	}

	bool PlayTestController::TeleportPlayer(Vector2f screenPos, Vector2f screenSize)
	{
		LevelHandler* gameHandler = GetGameHandler();
		Rendering::PlayerViewport* viewport = (gameHandler != nullptr ? _playHandler->GetViewport() : nullptr);
		PlayTestDebug* debug = (gameHandler != nullptr ? gameHandler->GetPlayTestDebug() : nullptr);
		if (viewport == nullptr || debug == nullptr || screenSize.X <= 0.0f || screenSize.Y <= 0.0f) {
			return false;
		}
		// The game draws its view at its own size, scaled up to the screen, the first player sees the level in its part
		Vector2i viewSize = gameHandler->GetViewSize();
		Vector2f viewPos(screenPos.X * float(viewSize.X) / screenSize.X, screenPos.Y * float(viewSize.Y) / screenSize.Y);
		Rectf bounds = viewport->GetBounds();
		Vector2f worldPos = viewport->ViewToWorld(Vector2f(viewPos.X - bounds.X, viewPos.Y - bounds.Y));
		return debug->TeleportPlayer(0, worldPos);
	}

	void PlayTestController::Enter(PlayStartMode mode, const PlayTestOptions& options)
	{
		EditorLevelHandler* editHandler = _host.GetEditHandler();
		if (!IsEditing() || editHandler == nullptr) {
			return;
		}

		LevelDocument& document = _host.GetDocument();
		EditorCamera& camera = editHandler->GetCamera();
		_editCameraCenter = camera.GetCenter();
		_editCameraScale = camera.GetTargetScale();

		_playOptions = options;
		_resumePlay = false;
		_restorePlayState = false;

		if (mode == PlayStartMode::Resume && _playHandler == nullptr && _hasResumePosition) {
			// The play-test wasn't kept, it starts where the player was
			_playOptions.HasSpawnPosition = true;
			_playOptions.SpawnPosition = _resumePosition;
		} else if (mode == PlayStartMode::Resume && _playHandler != nullptr) {
			// Multiplayer chosen since, or another one, starts again too, so does another character or difficulty
			bool sameMode = (_playedOptions.GameMode == _playOptions.GameMode && (_playOptions.GameMode == 0 || _playedOptions.PlayerCount == _playOptions.PlayerCount));
			bool sameDifficulty = (_playedOptions.Difficulty == _playOptions.Difficulty);
			bool sameRun = (sameMode && sameDifficulty && _playedOptions.Player == _playOptions.Player && _playHandler->GetViewport() != nullptr);
			if (sameRun && (IsPlayTestCurrent() || PushChanges())) {
				// Nothing changed, or only tiles and events that were given to it, the play-test goes on where it was left
				_resumePlay = true;
			} else if (Actors::Player* player = _playHandler->GetPlayer()) {
				// The level changed, so it starts again, but where the player was and with what the player had, and
				// what was collected or destroyed stays so, events differ by the difficulty though
				_playOptions.HasSpawnPosition = true;
				_playOptions.SpawnPosition = player->GetPos();
				_restorePlayState = (sameMode && sameDifficulty && _playOptions.GameMode == 0);
			}
		}

		// The kept play-test stays until the new one replaces it, see Finish(), so it's still there when the flight is
		// cancelled, and the ring back from it still has its last frame

		// The view flies first to where the game will show the player, as large as the game shows it, then the game is
		// revealed over it in a ring. The camera of the game follows the player, so it's close to it.
		bool hasTarget = false;
		Vector2f target;
		if (_resumePlay && _playHandler != nullptr && _playHandler->GetViewport() != nullptr) {
			target = _playHandler->GetViewport()->GetCameraCenter();
			hasTarget = true;
		} else if (_playOptions.HasSpawnPosition) {
			target = _playOptions.SpawnPosition;
			hasTarget = true;
		} else {
			// The start of the character played, as EventMap::AddSpawnPosition() places it, the game picks one of several
			std::int32_t width = std::max<std::int32_t>(document.GetEventGridSize().X, 1);
			std::uint8_t playerBit = std::uint8_t(1 << std::clamp<std::int32_t>(std::int32_t(_playOptions.Player) - 1, 0, 2));
			for (const DocumentEvent& event : document.Events) {
				if (EventType(event.Record.Type) == EventType::LevelStart && (event.Record.Params[0] & playerBit) != 0) {
					target = Vector2f(float(event.Cell % std::uint32_t(width)) * 32.0f, float(event.Cell / std::uint32_t(width)) * 32.0f - 8.0f);
					hasTarget = true;
					break;
				}
			}
		}
		if (hasTarget) {
			// The camera of the game doesn't show anything past the edges of the level
			float scale = camera.GetGameplayScale();
			Vector2i viewSize = camera.GetViewSize();
			Vector2f halfView(viewSize.X * 0.5f / scale, viewSize.Y * 0.5f / scale);
			std::int32_t spriteLayer = document.GetSpriteLayerIndex();
			if (spriteLayer >= 0 && !_resumePlay) {
				Vector2f levelSize(document.Layers[spriteLayer].GetWidth() * 32.0f, document.Layers[spriteLayer].GetHeight() * 32.0f);
				target.X = (levelSize.X > halfView.X * 2.0f ? std::clamp(target.X, halfView.X, levelSize.X - halfView.X) : levelSize.X * 0.5f);
				target.Y = (levelSize.Y > halfView.Y * 2.0f ? std::clamp(target.Y, halfView.Y, levelSize.Y - halfView.Y) : levelSize.Y * 0.5f);
			}
			LOGD("Flying to {:.0f}, {:.0f} before the level is played", target.X, target.Y);
			camera.FlyTo(target, scale);
		} else {
			camera.Stop();
		}
		_mode = PlayTestMode::Entering;
		_enteringFrameShown = false;
	}

	void PlayTestController::UpdateEntering()
	{
		if (_mode != PlayTestMode::Entering) {
			return;
		}
		EditorLevelHandler* editHandler = _host.GetEditHandler();
		if (editHandler == nullptr) {
			return;
		}
		// The frame without the panels was shown, the game takes over now, once the camera is there and a frame showing
		// it has been drawn
		if (editHandler->GetCamera().IsMoving()) {
			_enteringFrameShown = false;
		} else if (_enteringFrameShown) {
			Finish();
		} else {
			_enteringFrameShown = true;
		}
	}

	void PlayTestController::UpdateTransition(float timeMult)
	{
		_transition.Update(timeMult);
		// The ring back into the editor, the play-test it shows the last frame of is released once it's done
		if (_mode != PlayTestMode::Play && !_transition.IsActive()) {
			_closingPlayHandler = nullptr;
			if (_mode == PlayTestMode::Returning) {
				_mode = PlayTestMode::Edit;
			}
		}
	}

	void PlayTestController::SetPlayTestExit(IPlayTest& handler)
	{
		std::weak_ptr<void> reference = _host.GetPlayTestHostReference();
		handler.OnExitEditor = [this, reference]() {
			_host.GetPlayTestRoot().InvokeAsync(reference, [this]() {
				Stop(false);
				_host.ExitEditor();
			});
		};
	}

	bool PlayTestController::IsPlayTestCurrent() const
	{
		// Revisions are never repeated by another history, see CommandHistory
		return (_playHandler != nullptr && _host.GetDocumentRevision() == _playRevision);
	}

	void PlayTestController::Finish()
	{
		EditorLevelHandler* editHandler = _host.GetEditHandler();
		if (editHandler == nullptr) {
			_mode = PlayTestMode::Edit;
			return;
		}

		// Changed since the kept play-test was chosen to go on as it was, it starts again where the player is
		if (_resumePlay && (!IsPlayTestCurrent() || _playHandler->GetViewport() == nullptr)) {
			_resumePlay = false;
			if (_playHandler != nullptr) {
				if (Actors::Player* player = _playHandler->GetPlayer()) {
					_playOptions.HasSpawnPosition = true;
					_playOptions.SpawnPosition = player->GetPos();
					_restorePlayState = (_playOptions.GameMode == 0);
				}
			}
		}

		// The ring back from a play-test and the play-test left go first, so no more levels than needed are loaded at once
		_transition.Stop();
		_closingPlayHandler = nullptr;
		Viewport::GetChain().clear();
		editHandler->SuspendRendering();

		if (_resumePlay) {
			_playHandler->Resume();
		} else {
			LevelDocument& document = _host.GetDocument();
			IRootController* root = &_host.GetPlayTestRoot();
			// The kept play-test stays until the new one is there, it's taken from or it's kept if the new one fails
			std::shared_ptr<IPlayTest> previous = Death::move(_playHandler);
			// Alone, or a game mode of multiplayer in local splitscreen
			std::shared_ptr<IPlayTest> handler;
			bool initialized;
#if defined(WITH_MULTIPLAYER)
			if (_playOptions.GameMode != 0) {
				auto mpHandler = std::make_shared<PlayTestMpLevelHandler>(root, _playOptions);
				handler = mpHandler;
				SetPlayTestExit(*handler);
				initialized = mpHandler->Initialize(document, _host.GetLevelName(), _host.GetFilePath());
			} else
#endif
			{
				auto spHandler = std::make_shared<PlayTestLevelHandler>(root);
				handler = spHandler;
				SetPlayTestExit(*handler);
				initialized = spHandler->Initialize(document, _host.GetLevelName(), _host.GetFilePath(), _playOptions);
			}
			if (!initialized) {
				handler = nullptr;
				_playHandler = Death::move(previous);
				_restorePlayState = false;
				LOGE("Edited level cannot be played");
				_mode = PlayTestMode::Edit;
				editHandler->SetMusicPaused(false);
				// The view is set up again, by loading the level shown again if it has to be for the changes made meanwhile
				if (!_host.OnPlayTestReturned()) {
					_host.OnPlayTestViewChanged();
				}
				_host.ShowMessage(_x("editor", "The level couldn't be played"));
				return;
			}
			_playHandler = Death::move(handler);
			_playedOptions = _playOptions;
			if (_playHandler->HasSpawnFallenBack()) {
				_host.ShowMessage(_x("editor", "There's no room for the player there, it starts at the start of the level"));
			}
			if (_restorePlayState && previous != nullptr) {
				RestorePlayState(*previous, *_playHandler);
			}
			previous = nullptr;

			// What the play-test is made from, so it's known whether it can go on exactly as it's left, and what was edited
			// since when it can't, see RestorePlayState()
			_playRevision = _host.GetDocumentRevision();
#if !defined(DEATH_TARGET_CONSTRAINED_MEMORY)
			RememberSuspendedLevel();
#endif
			_changesSincePlay.Clear();
			_playTestNumber = ++_playTestCount;
			// A new play-test starts running, whatever the toggles were
			_debugOptions.Paused = false;
		}
		_restorePlayState = false;
		// The debug toggles go on in every play-test
		if (PlayTestDebug* debug = _playHandler->GetLevelHandler().GetPlayTestDebug()) {
			debug->SetOptions(_debugOptions);
		}
		// The keys went to the editor until now, those held are held in the game from the start
		_playHandler->GetLevelHandler().ResetPressedKeys(true);

		// The music goes on, it opens up while the game is revealed
#if defined(WITH_AUDIO)
		if (editHandler->HandOverMusic(_playHandler->GetLevelHandler(), EditMusicLowPass)) {
			StartMusicFade(EditMusicLowPass, 1.0f);
		} else
#endif
		{
			editHandler->SetMusicPaused(true);
		}

		_mode = PlayTestMode::Play;
		_host.OnPlayTestViewChanged();
		_host.GetPlayTestRoot().ApplyCursorPolicy();

		// The game is revealed in a ring growing over the last image of the editor, its panels are still drawn outside
		// of the ring while it grows, the game is drawn over them inside of it
		_transition.Start(editHandler->GetFrameTexture(), _playHandler->GetFrameTexture(), _host.GetAccentColor());
	}

	void PlayTestController::Stop(bool keep)
	{
		EditorLevelHandler* editHandler = _host.GetEditHandler();
		if (_mode == PlayTestMode::Entering) {
			// Nothing was started yet, the kept play-test is still there as it was
			_mode = PlayTestMode::Edit;
			_resumePlay = false;
			_restorePlayState = false;
			if (editHandler != nullptr) {
				editHandler->GetCamera().FlyTo(_editCameraCenter, _editCameraScale);
			}
			_host.OnPlayTestReturned();
			return;
		}
		if (_mode != PlayTestMode::Play || editHandler == nullptr) {
			return;
		}

		_transition.Stop();
		Vector2f center = _editCameraCenter;
		bool musicHandedOver = false;
		// Only where the play-test isn't kept, from where it was left
		_hasResumePosition = false;
#if defined(DEATH_TARGET_CONSTRAINED_MEMORY)
		// Two levels at once don't fit, the play-test isn't kept, the next one starts where the player was
		if (keep && _playHandler != nullptr) {
			if (Actors::Player* player = _playHandler->GetPlayer()) {
				_resumePosition = player->GetPos();
				_hasResumePosition = true;
			}
			keep = false;
		}
#endif
		if (_playHandler != nullptr) {
			if (Rendering::PlayerViewport* viewport = _playHandler->GetViewport()) {
				center = viewport->GetCameraCenter();
			}
			_lastPlayPath = _playHandler->GetPlayPath();
			_pathHistory.Update(_lastPlayPath, _playTestNumber);
#if defined(WITH_AUDIO)
			// Muffled again over a while, as the editor comes back
			musicHandedOver = _playHandler->GetLevelHandler().HandOverMusic(*editHandler, 1.0f);
			if (musicHandedOver) {
				StartMusicFade(1.0f, EditMusicLowPass);
			}
#endif
		}

		// The editor is revealed in a ring growing over the last frame of the game, as the game was revealed
		Texture* lastFrame = (_playHandler != nullptr ? _playHandler->GetFrameTexture() : nullptr);
		if (lastFrame != nullptr) {
			_transition.Start(lastFrame, nullptr, _host.GetAccentColor());
		}

		Viewport::GetChain().clear();
		if (_playHandler != nullptr) {
			// The keys go to the editor from now on, the game doesn't see them released, so it lets them go itself
			_playHandler->GetLevelHandler().ResetPressedKeys(false);
		}
		if (keep && _playHandler != nullptr) {
			// It goes on exactly as it's left only if the level is still as it was made from, see Finish()
			_playHandler->GetLevelHandler().SuspendRendering();
			_playHandler->Suspend();
		} else if (_playHandler != nullptr) {
			// Not drawn anymore, but its last frame is shown until the ring is gone, and not heard anymore either, its
			// sounds are stopped once it's released
			_playHandler->GetLevelHandler().SuspendRendering();
			_playHandler->Suspend();
			_closingPlayHandler = Death::move(_playHandler);
		}

		_mode = (_transition.IsActive() ? PlayTestMode::Returning : PlayTestMode::Edit);
		// Changes made while the level was played are shown now, the level handler may be loaded again for them, its view
		// is set up then already
		if (!_host.OnPlayTestReturned()) {
			_host.OnPlayTestViewChanged();
		}
		editHandler = _host.GetEditHandler();
		if (editHandler != nullptr) {
			if (!musicHandedOver) {
				editHandler->SetMusicPaused(false);
			}
			// Back where the player was, at the zoom of the editor
			editHandler->GetCamera().JumpTo(center, _editCameraScale);
		}

		_host.GetPlayTestRoot().ApplyCursorPolicy();
	}

	void PlayTestController::ReturnFromGame(StringView message, bool exited, Function<void()>&& afterwards)
	{
		if (exited && _playHandler != nullptr) {
			auto& segments = _playHandler->GetPlayPath().Segments;
			if (!segments.empty()) {
				segments.back().End = PlayPath::Break::Exited;
			}
		}
		// Called from inside the frame of the play-test, which can't be destroyed there
		String text = message;
		auto then = std::make_shared<Function<void()>>(Death::move(afterwards));
		_host.GetPlayTestRoot().InvokeAsync(_host.GetPlayTestHostReference(), [this, text, then]() {
			// Not a flight into another play-test started meanwhile
			if (_mode == PlayTestMode::Play) {
				Stop(false);
				_host.ShowMessage(text);
				if (*then) {
					(*then)();
				}
			}
		});
	}

	void PlayTestController::Reset()
	{
		if (_mode == PlayTestMode::Play) {
			Stop(false);
		}
		_mode = PlayTestMode::Edit;
		_transition.Stop();
		_closingPlayHandler = nullptr;
		_playHandler = nullptr;
		_resumePlay = false;
		_restorePlayState = false;
		_playRevision = 0;
		_hasResumePosition = false;
		_lastPlayPath.Clear();
		_pathHistory.Clear();
		_suspendedSpriteTiles.clear();
		_suspendedSpriteSize = Vector2i();
		_suspendedEvents.clear();
		_suspendedOffGridEvents.clear();
		_suspendedGameChunks.clear();
		_changesSincePlay.Clear();
	}

	void PlayTestController::StartMusicFade(float from, float to)
	{
		_musicFadeFrom = from;
		_musicFadeTo = to;
		_musicFadeTime = 0.0f;
	}

	void PlayTestController::UpdateMusicFade(float timeMult)
	{
#if defined(WITH_AUDIO)
		if (_musicFadeTime < 0.0f) {
			return;
		}
		_musicFadeTime += timeMult;
		// With reduced motion, the music is muffled or opened up at once, as the rest changes at once then
		float t = (UiTheme::ReducedMotion ? 1.0f : std::min(_musicFadeTime / MusicFadeFrames, 1.0f));
		float eased = t * t * (3.0f - 2.0f * t);
		// The cutoff is heard on a log scale, so it's faded on one
		float value = _musicFadeFrom * std::pow(_musicFadeTo / _musicFadeFrom, eased);
		EditorLevelHandler* editHandler = _host.GetEditHandler();
		LevelHandler* handler = (_mode == PlayTestMode::Play && _playHandler != nullptr ? &_playHandler->GetLevelHandler() : editHandler);
		if (handler != nullptr) {
			handler->SetMusicMuffling(value);
		}
		if (t >= 1.0f) {
			_musicFadeTime = -1.0f;
			if (IsEditing() && editHandler != nullptr) {
				// Kept, so the music started again later is muffled too
				editHandler->SetMusicLowPass(_musicFadeTo);
			}
		}
#endif
	}

	void PlayTestController::RememberSuspendedLevel()
	{
		const LevelDocument& document = _host.GetDocument();
		std::int32_t spriteLayer = document.GetSpriteLayerIndex();
		_suspendedSpriteTiles.clear();
		_suspendedSpriteSize = Vector2i();
		if (spriteLayer >= 0) {
			const auto& layer = document.Layers[spriteLayer];
			_suspendedSpriteTiles = layer.Tiles;
			_suspendedSpriteSize = Vector2i(layer.GetWidth(), layer.GetHeight());
		}
		_suspendedEvents = document.Events;
		_suspendedOffGridEvents = document.OffGridEvents;
		CollectGameChunks(document, _suspendedGameChunks);
	}

	void PlayTestController::RestorePlayState(IPlayTest& from, IPlayTest& to)
	{
		const LevelDocument& document = _host.GetDocument();
		Actors::Player* oldPlayer = from.GetPlayer();
		Actors::Player* newPlayer = to.GetPlayer();
		if (oldPlayer != nullptr && newPlayer != nullptr) {
			newPlayer->TakeOverPlayTestState(*oldPlayer);
		}

		// What was collected, destroyed or triggered stays so, only if the cells are where they were
		std::int32_t spriteLayer = document.GetSpriteLayerIndex();
		if (spriteLayer < 0) {
			return;
		}
		const auto& layer = document.Layers[spriteLayer];
		Vector2i size(layer.GetWidth(), layer.GetHeight());
		if (size != _suspendedSpriteSize || _suspendedSpriteTiles.size() != layer.Tiles.size()) {
			LOGI("Sprite layer was resized since the play-test was left, the level starts as new");
			return;
		}
		std::int32_t cellCount = size.X * size.Y;
		SmallVector<bool, 0> edited;
		edited.resize(std::size_t(cellCount), false);
		std::int32_t editedCount = 0;
		for (std::int32_t i = 0; i < cellCount; i++) {
			if (!(_suspendedSpriteTiles[i] == layer.Tiles[i])) {
				edited[i] = true;
			}
		}
		// Both lists are sorted by the cell
		auto markEvents = [&](const SmallVectorImpl<DocumentEvent>& a, const SmallVectorImpl<DocumentEvent>& b) {
			std::size_t j = 0;
			for (const DocumentEvent& event : a) {
				while (j < b.size() && b[j].Cell < event.Cell) {
					j++;
				}
				bool same = (j < b.size() && b[j].Cell == event.Cell && LevelDocument::AreEventsEqual(b[j].Record, event.Record));
				if (!same && event.Cell < std::uint32_t(cellCount)) {
					edited[event.Cell] = true;
				}
			}
		};
		markEvents(_suspendedEvents, document.Events);
		markEvents(document.Events, _suspendedEvents);
		for (bool cell : edited) {
			editedCount += (cell ? 1 : 0);
		}

		LevelHandler& oldHandler = from.GetLevelHandler();
		LevelHandler& newHandler = to.GetLevelHandler();

		// Tiles: a destruction frame for each cell of the sprite layer, then the triggers
		{
			MemoryStream oldState, newState, merged;
			oldHandler.TileMap()->SerializeResumableToStream(oldState, false);
			newHandler.TileMap()->SerializeResumableToStream(newState, false);
			oldState.Seek(0, SeekOrigin::Begin);
			newState.Seek(0, SeekOrigin::Begin);
			std::int32_t oldCount = oldState.ReadVariableInt32();
			std::int32_t newCount = newState.ReadVariableInt32();
			if (oldCount == cellCount && newCount == cellCount) {
				merged.WriteVariableInt32(cellCount);
				for (std::int32_t i = 0; i < cellCount; i++) {
					std::int32_t oldFrame = oldState.ReadVariableInt32();
					std::int32_t newFrame = newState.ReadVariableInt32();
					merged.WriteVariableInt32(edited[i] ? newFrame : oldFrame);
				}
				// Triggers are of the whole level, they stay as they were
				std::int64_t oldTriggerSize = oldState.GetSize() - oldState.GetPosition();
				std::int64_t newTriggerSize = newState.GetSize() - newState.GetPosition();
				MemoryStream& triggerSource = (oldTriggerSize == newTriggerSize ? oldState : newState);
				SmallVector<std::uint8_t, 0> triggers;
				triggers.resize(std::size_t(newTriggerSize));
				if (newTriggerSize > 0) {
					triggerSource.Read(triggers.data(), newTriggerSize);
					merged.Write(triggers.data(), newTriggerSize);
				}
				merged.Seek(0, SeekOrigin::Begin);
				if (!newHandler.TileMap()->InitializeFromStream(merged)) {
					// The tiles start as new then, as they were before anything was taken over
					LOGW("Destroyed tiles of the play-test left couldn't be taken over, the tiles start as new");
					newState.Seek(0, SeekOrigin::Begin);
					newHandler.TileMap()->InitializeFromStream(newState);
				}
			}
		}

		// Events: the type, the flags and the parameters of each cell, the grid and then rows of off-grid events, as
		// EventMap::SerializeResumableToStream() writes them
		{
			constexpr std::int32_t ParamsSize = Events::EventSpawner::SpawnParamsSize;
			MemoryStream oldState, newState, merged;
			oldHandler.EventMap()->SerializeResumableToStream(oldState, false);
			newHandler.EventMap()->SerializeResumableToStream(newState, false);
			oldState.Seek(0, SeekOrigin::Begin);
			newState.Seek(0, SeekOrigin::Begin);
			std::int32_t oldCount = oldState.ReadVariableInt32();
			std::int32_t newCount = newState.ReadVariableInt32();
			if (oldCount == newCount && oldCount >= cellCount) {
				// Off-grid events are kept by their order, only if none of them changed
				bool sameOffGrid = (_suspendedOffGridEvents.size() == document.OffGridEvents.size() &&
					std::memcmp(_suspendedOffGridEvents.data(), document.OffGridEvents.data(), document.OffGridEvents.size() * sizeof(Formats::OffGridEventRecord)) == 0);
				merged.WriteVariableInt32(newCount);
				for (std::int32_t i = 0; i < newCount; i++) {
					std::uint32_t oldType = oldState.ReadVariableUint32();
					std::uint32_t oldFlags = oldState.ReadVariableUint32();
					std::uint8_t oldParams[ParamsSize];
					oldState.Read(oldParams, sizeof(oldParams));
					std::uint32_t newType = newState.ReadVariableUint32();
					std::uint32_t newFlags = newState.ReadVariableUint32();
					std::uint8_t newParams[ParamsSize];
					newState.Read(newParams, sizeof(newParams));
					bool useNew = (i < cellCount ? edited[i] : !sameOffGrid);
					merged.WriteVariableUint32(useNew ? newType : oldType);
					merged.WriteVariableUint32(useNew ? newFlags : oldFlags);
					merged.Write(useNew ? newParams : oldParams, sizeof(oldParams));
				}
				merged.Seek(0, SeekOrigin::Begin);
				if (!newHandler.EventMap()->InitializeFromStream(merged)) {
					// The events start as new then, as they were before anything was taken over
					LOGW("Events of the play-test left couldn't be taken over, the events start as new");
					newState.Seek(0, SeekOrigin::Begin);
					newHandler.EventMap()->InitializeFromStream(newState);
				}
			}
		}

		LOGI("Play-test continues where it was left, {} edited cells start as new", editedCount);
	}
}

#endif
