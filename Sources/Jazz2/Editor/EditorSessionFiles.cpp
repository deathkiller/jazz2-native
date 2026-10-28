#include "EditorSessionPrivate.h"

#if defined(WITH_EDITOR)

// The level replaced by another one and how it's seen, of EditorSession, its file is kept by DocumentFiles

namespace Jazz2::Editor
{
	StringView EditorSession::GetFilePath() const
	{
		return _files->GetFilePath();
	}

	StringView EditorSession::GetLevelName() const
	{
		return _files->GetLevelName();
	}

	void EditorSession::GetFileRoots(bool forSaving, SmallVectorImpl<UiFileRoot>& roots)
	{
		_files->GetFileRoots(forSaving, roots);
	}

	void EditorSession::GetImageRoots(bool forSaving, SmallVectorImpl<UiFileRoot>& roots)
	{
		_files->GetImageRoots(forSaving, roots);
	}

	void EditorSession::OpenFile(StringView path)
	{
		_files->OpenFile(path);
	}

	void EditorSession::OpenRecoveryCopy(StringView recoveryPath)
	{
		_files->OpenRecoveryCopy(recoveryPath);
	}

	bool EditorSession::SaveAsFile(StringView path, bool move)
	{
		return _files->SaveAsFile(path, move);
	}

	bool EditorSession::CanSaveInPlace() const
	{
		return _files->CanSaveInPlace();
	}

	void EditorSession::ShowSaveAsDialog(bool exportOnly, Function<void()>&& onSaved)
	{
		_files->ShowSaveAsDialog(exportOnly, Death::move(onSaved));
	}

	void EditorSession::NoteLevelFileMoved(StringView from, StringView to)
	{
		_files->NoteLevelFileMoved(from, to);
	}

	void EditorSession::ExitEditor()
	{
		_files->ExitEditor();
	}

	void EditorSession::PrepareForSave()
	{
		// A stroke of a tool writes some of what it does into the level as it goes, a level saved under it would differ
		// from the level once the stroke is cancelled
		if (!_input->IsInToolCallback()) {
			CancelInteractions(false);
		}
		StoreViewState();
	}

	void EditorSession::OnScriptChanged()
	{
		// A kept play-test runs the old script, so it starts again when it's resumed
		_playTest->MarkPlayTestOutdated();
#if defined(WITH_ANGELSCRIPT)
		_scriptGraphicsDirty = true;
#endif
	}

	void EditorSession::ReloadTileSet(StringView name)
	{
		InvalidateTileSet(name);
		_tileLod->Invalidate();
		_maskOverlay->Invalidate();
		RebuildRuntime();
		RememberAppliedOverrides();
	}

	void EditorSession::StoreViewState()
	{
		DocumentViewState state;
		if (_editHandler != nullptr) {
			// While the level is played, where the editor was left
			EditorCamera& camera = _editHandler->GetCamera();
			bool editing = (_playTest->IsEditing());
			float scale = (editing ? camera.GetTargetScale() : _playTest->GetEditCameraScale());
			if (camera.GetGameplayScale() > 0.0f && scale > 0.0f) {
				state.HasCamera = true;
				state.CameraCenter = (editing ? camera.GetCenter() : _playTest->GetEditCameraCenter());
				state.CameraZoom = scale / camera.GetGameplayScale();
			}
		}
		state.CurrentLayer = _currentLayer;
		state.ShowGrid = _showGrid;
		state.ShowEvents = _showEvents;
		state.ShowPlayPath = _showPlayPath;
		if (Tiles::TileMap* tileMap = (_editHandler != nullptr ? _editHandler->TileMap() : nullptr)) {
			state.FlatLayers = (tileMap->GetViewOptions().FlatAmount > 0.5f);
		}
		state.MaskMode = std::uint8_t(_maskMode);
		state.HasPlaySettings = true;
		state.PlayCharacter = std::uint8_t(_settings.PlayCharacter);
		state.PlayDifficulty = std::uint8_t(_settings.PlayDifficulty);
		state.PlayGameMode = std::uint8_t(_settings.PlayGameMode);
		state.PlayPlayerCount = std::uint8_t(_settings.PlayPlayerCount);
		state.Write(_document);
		// The paths of the play-tests go with the level, a chunk of a newer version that couldn't be read stays
		if (!_playTest->GetPathHistory().IsEmpty()) {
			_playTest->GetPathHistory().Write(_document);
		}
	}

	void EditorSession::ApplyViewState()
	{
		_playTest->ReadPathHistory(_document);
		DocumentViewState state;
		if (!DocumentViewState::Read(_document, state)) {
			return;
		}
		if (state.HasCamera) {
			_editHandler->SetInitialCamera(state.CameraCenter, state.CameraZoom, true);
		}
		if (state.CurrentLayer >= 0 && state.CurrentLayer < std::int32_t(_document.Layers.size())) {
			_currentLayer = state.CurrentLayer;
		}
		_showGrid = state.ShowGrid;
		_showEvents = state.ShowEvents;
		_showPlayPath = state.ShowPlayPath;
		_maskMode = std::min<std::int32_t>(state.MaskMode, 2);
		if (state.FlatLayers) {
			if (Tiles::TileMap* tileMap = _editHandler->TileMap()) {
				Tiles::TileMapViewOptions options = tileMap->GetViewOptions();
				options.FlatAmount = 1.0f;
				tileMap->SetViewOptions(options);
			}
		}
		if (state.HasPlaySettings) {
			// The level is played as it was the last time, which becomes how the next level is played too
			_settings.PlayCharacter = std::clamp<std::int32_t>(state.PlayCharacter, 1, 3);
			_settings.PlayDifficulty = std::clamp<std::int32_t>(state.PlayDifficulty, 1, 3);
			_settings.PlayGameMode = state.PlayGameMode;
			_settings.PlayPlayerCount = std::clamp<std::int32_t>(state.PlayPlayerCount, 1, 4);
		}
	}

	void EditorSession::ReplaceDocument(LevelDocument&& document, StringView path, StringView levelName)
	{
		CancelInteractions(true);
		_tools->GetActive()->Cancel();
		_input->ReleasePointer(false);
		_selection.Clear();
		_eventSelection.Clear();
		// The corner of a block marked is in the old level, as is what the event was picked for
		_blockGrab->Cancel();
		_eventPick = {};
		_maskOverlay->Invalidate();
		_overriddenLayer = -1;
		// Nothing of the play-tests of the old level goes on in the new one
		ResetPlayState();
		_pendingChanges.Clear();

		_document = Death::move(document);
		CreateHistory();
		// The tile sets the old level stored in itself are gone with it
		_tileSetData.InvalidateLevelTileSets();
		_accentKey = 0;
		_files->OnDocumentReplaced(path, levelName);
		_currentLayer = std::max<std::int32_t>(_document.GetSpriteLayerIndex(), 0);
		_tileLod->Invalidate();
		_files->LoadEventExtensions(false);
		{
			DocumentChanges changes;
			changes.Add(DocumentChangeKind::All);
			NotifyChangeObservers(changes);
		}

		// Loaded again with the camera where the new level starts
		Viewport::GetChain().clear();
		_editHandler = nullptr;
		auto handler = std::make_shared<EditorLevelHandler>(this);
		if (!handler->Initialize(_document, _files->GetLevelName(), _files->GetFilePath())) {
			LOGE("New level cannot be shown");
			_root->GoToMainMenu(false);
			return;
		}
		_editHandler = Death::move(handler);
		AttachSceneContent();
		RememberAppliedOverrides();
#if defined(WITH_ANGELSCRIPT)
		_scriptGraphicsDirty = true;
#endif
		// A recovered level is seen as it was
		ApplyViewState();
		Vector2i res = theApplication().GetResolution();
		OnInitializeViewport(res.X, res.Y);
		_files->UpdateWindowTitle();
		_comparison->Hide();
		_files->WatchFiles();
		_files->AcquireLock();
	}
}

#endif
