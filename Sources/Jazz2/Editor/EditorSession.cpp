#include "EditorSessionPrivate.h"

#if defined(WITH_EDITOR)

namespace Jazz2::Editor
{
	namespace
	{
		// Sizes of the interface offered, in steps of 25% as the system offers its own, sizes between whole ones make the
		// text a bit soft
		constexpr float UiScaleStep = 0.25f;
		constexpr float MinUiScale = 1.0f;
		constexpr float MaxUiScale = 6.0f;
	}

	EditorSession::EditorSession(IRootController* root)
		: _root(root), _shell(nullptr), _showPlayPath(true), _currentLayer(0), _showGrid(true), _maskMode(0), _showEvents(true),
			_pickerPanel(PickerPanel::Tiles)
	{
		_playTest = std::make_unique<PlayTestController>(static_cast<IPlayTestHost&>(*this));
		_maskOverlay = std::make_unique<MaskOverlay>();
		_tileLod = std::make_unique<TileLod>(_document, _tileSetData);
		// The tools only keep the session, they're told about the level once it's open
		_tools = std::make_unique<ToolManager>(*this);
		_files = std::make_unique<DocumentFiles>(*this, static_cast<IDocumentFilesHost&>(*this), *root);
		_comparison = std::make_unique<SavedComparison>(*this);
		_playOverlays = std::make_unique<PlayOverlays>(*this);
		_input = std::make_unique<EditorInputRouter>(*this, static_cast<IInputRouterHost&>(*this), *_playTest);
#if defined(DEATH_DEBUG)
		_debugConsole = std::make_unique<EditorDebugConsole>(*this, *_files, *_input, *_tileLod);
#endif
	}

	EditorSession::~EditorSession()
	{
		_files->ReleaseLock();
		_settings.Save();
		// The interface refers to the session, so it goes first
		_surface = nullptr;
		Viewport::GetChain().clear();
		// The play-tests and the rings that show their last images go before the level handler of the editor
		_playTest->Release();
		_editHandler = nullptr;
	}

	bool EditorSession::Initialize(const EditorInitialization& editorInit)
	{
		auto& resolver = ContentResolver::Get();
		_settings.Load();
		_showGrid = _settings.ShowGrid;
		// The debug toggles of play-tests are kept for all of them
		ApplyPlayTestDebugOptions();
		UiTheme::Apply(UiThemeKind(_settings.Theme), _settings.ReducedMotion);

		String fullPath, levelName, filePath;
		if (!editorInit.FilePath.empty()) {
			fullPath = editorInit.FilePath;
			String fileName = fs::GetFileNameWithoutExtension(fullPath);
			StringUtils::lowercaseInPlace(fileName);
			levelName = "unknown/"_s + fileName;
		} else if (!editorInit.LevelName.empty()) {
			levelName = editorInit.LevelName;
			fullPath = resolver.FindContentFile(fs::CombinePath("Episodes"_s, String(fs::ToNativeSeparators(levelName) + ".j2l"_s)));
			if (fullPath.empty()) {
				auto p = levelName.partition('/');
				if (p[0] != "unknown"_s && !p[2].empty()) {
					levelName = "unknown/"_s + p[2];
					fullPath = resolver.FindContentFile(fs::CombinePath("Episodes"_s, String(fs::ToNativeSeparators(levelName) + ".j2l"_s)));
				}
			}
		}

		bool showOpenDialog = false;
		if (fullPath.empty()) {
			if (!editorInit.FilePath.empty() || !editorInit.LevelName.empty()) {
				LOGE("Level to edit was not found");
				return false;
			}
			// Nothing to open, so a new level, and the dialog to open one is shown over it
			String tileSet = (!editorInit.NewLevelTileSet.empty() ? String(editorInit.NewLevelTileSet) : _files->FindDefaultTileSet());
			std::int32_t tileCount = _tileSets.GetTileCount(tileSet);
			if (tileCount < 0) {
				LOGE("No tile set was found to make a new level with");
				return false;
			}
			_document.CreateNew(tileSet, tileCount, Vector2i(128, 64));
			_document.Properties.Author = PreferencesCache::GetEffectivePlayerName();
			levelName = "unknown/untitled"_s;
			showOpenDialog = true;
		} else {
			auto s = fs::Open(fullPath, FileAccess::Read);
			if (!s->IsValid() || !_document.Load(*s, &_tileSets)) {
				LOGE("Level \"{}\" cannot be opened in the editor", fullPath);
				return false;
			}
			if ((_document.Flags & LevelFlags::NotEditable) == LevelFlags::NotEditable) {
				LOGE("Level \"{}\" was exported only to be played, it cannot be opened in the editor", fullPath);
				return false;
			}
			if (_tileSets.GetTileCount(_document.Properties.DefaultTileSet) < 0) {
				LOGE("Tile set \"{}\" of level \"{}\" was not found", _document.Properties.DefaultTileSet, fullPath);
				return false;
			}
			s = nullptr;
			filePath = fullPath;
		}
		// The recovery copy belongs to the file from now on
		_files->SetFile(filePath, levelName);

		String problem;
		if (!_document.Validate(&problem)) {
			LOGW("Level \"{}\" has a problem: {}", fullPath, problem);
		}

		CreateHistory();
		_currentLayer = std::max<std::int32_t>(_document.GetSpriteLayerIndex(), 0);
		_stamp = TileStamp::FromTile(TileRef{ 1, TileRef::DefaultTileSet, 0 });
		_files->LoadEventExtensions(true);

		_editHandler = std::make_shared<EditorLevelHandler>(this);
		if (!_editHandler->Initialize(_document, _files->GetLevelName(), _files->GetFilePath())) {
			_editHandler = nullptr;
			return false;
		}
		AttachSceneContent();
		RememberAppliedOverrides();
		ApplyViewState();

		CreateActions();
		_actions.CaptureDefaults();
		_actions.LoadOverrides(GetHotkeysPath());
		CreateInterface();
		_files->ShowEventExtensionErrors();
		// The script and the tile sets changed outside of the editor, and another editor with the same level
		_files->WatchFiles();
		_files->AcquireLock();

		LOGI("Level \"{}\" opened in the editor", _files->GetFilePath());
		_files->UpdateWindowTitle();

		if (!filePath.empty()) {
			_settings.AddRecentFile(filePath);
			_settings.Save();
		}
		// Changes that weren't saved last time come first
		if (!_files->OfferRecovery() && showOpenDialog) {
			_files->ShowOpenDialog();
		}

#if defined(DEATH_DEBUG)
		_debugConsole->ReadStartupCommands();
#endif
		return true;
	}

	void EditorSession::CreateInterface()
	{
		_surface = std::make_unique<UiSurface>();
		_surface->TouchInput = (_input->GetInputModality() == InputModality::Touch);
		// Saving works while a text is typed, see EditorAction::WorksWhileTyping
		_surface->IsPassThroughKey = [this](const KeyboardEvent& event) { return _actions.MatchesWhileTyping(event); };
		// A function key that has an action, as the help or playing, does it while typing too, a key that types a character
		// is typed whatever it does otherwise
		_surface->IsKeyLeftByText = [this](const KeyboardEvent& event) {
			return (event.sym >= Keys::F1 && event.sym <= Keys::F12 && _actions.FindByShortcut(Shortcut::FromEvent(event)) != nullptr);
		};
		_shell = _surface->SetRoot<EditorShell>(*this);
		// The ring back into the editor reveals all of it, the panels too, so it's over them. The ring into the game is
		// over the panels too, they're seen outside of it as long as it grows.
		_surface->DrawOver = [this](UiPainter& painter, Vector2f screenSize) {
			PlayTransition& transition = _playTest->GetTransition();
			// The ring back from a play-test grows on while the view already flies into the next one
			if (_playTest->IsEditing() || _playTest->IsEntering()) {
				transition.Draw(painter, screenSize);
			} else if (_playTest->IsInGame() && transition.HasInside()) {
				transition.DrawRing(painter, screenSize);
			}
		};
		// Events that turn by the tiles around them are shown turned as the game turns them
		_eventPreviews.IsAreaEmpty = [this](const AABBf& aabb) {
			Tiles::TileMap* tileMap = (_editHandler != nullptr ? _editHandler->TileMap() : nullptr);
			if (tileMap == nullptr) {
				return true;
			}
			Tiles::TileCollisionParams params = { Tiles::TileDestructType::None, true };
			return tileMap->IsTileEmpty(aabb, params);
		};
	}

	ToolManager& EditorSession::GetTools()
	{
		return *_tools;
	}

	MaskOverlay& EditorSession::GetMaskOverlay()
	{
		return *_maskOverlay;
	}

	void EditorSession::SetCurrentLayer(std::int32_t layer)
	{
		if (layer < 0 || layer >= std::int32_t(_document.Layers.size()) || layer == _currentLayer) {
			return;
		}
		_tools->GetActive()->Cancel();
		_currentLayer = layer;
	}

	bool EditorSession::GetHoverPointer(ToolPointer& pointer)
	{
		return _input->GetHoverPointer(pointer);
	}

	InputModality EditorSession::GetInputModality() const
	{
		return _input->GetInputModality();
	}

	void EditorSession::CancelInteractions(bool releasePointer)
	{
		_input->CancelInteractions(releasePointer);
	}

	PlayOverlays& EditorSession::GetPlayOverlays()
	{
		return *_playOverlays;
	}

	void EditorSession::DrawSessionOverlays(UiPainter& painter, const Rectf& viewRect)
	{
		_comparison->Draw(painter, viewRect, _input->IsToolPointerDown());
		_playOverlays->DrawDeaths(painter, viewRect);
	}

	bool EditorSession::Undo()
	{
		// Also a path clicked point by point, and a handle of the layer being dragged
		CancelInteractions(true);
		_tools->GetActive()->Cancel();
		DocumentChanges changes;
		if (!_history->Undo(changes)) {
			return false;
		}
		OnDocumentChanged(changes);
		return true;
	}

	bool EditorSession::Redo()
	{
		CancelInteractions(true);
		_tools->GetActive()->Cancel();
		DocumentChanges changes;
		if (!_history->Redo(changes)) {
			return false;
		}
		OnDocumentChanged(changes);
		return true;
	}

	void EditorSession::OnDocumentChanged(const DocumentChanges& changes)
	{
		if (changes.IsEmpty()) {
			return;
		}
		if (changes.HasAny(DocumentChangeKind::TileOverrides | DocumentChangeKind::TileSets)) {
			// The bytes of a tile set the level stores in itself may have changed in place, see GetTileSetData()
			_tileSetData.MarkLevelTilesChanged();
		}
		_pendingChanges.Add(changes);
		// Compared with the saved level again only where it changed
		_comparison->NoteDocumentChanges(changes);
		// Tiles and events edited while a play-test is kept may be given to it, see PlayTestController::PushChanges()
		_playTest->NoteDocumentChanges(changes);

		// A step made of many changes, as moving the palette with all tiles, is shown once when it's done, and the shown
		// level is brought up to date only once it's seen again
		if ((_history != nullptr && _history->IsInTransaction()) || !_playTest->IsEditing()) {
			_files->UpdateWindowTitle();
			return;
		}
		FlushDocumentChanges();
	}

	void EditorSession::FlushDocumentChanges()
	{
		if (_pendingChanges.IsEmpty()) {
			return;
		}
		// Changes made while these are shown are collected again for the next time
		DocumentChanges changes = Death::move(_pendingChanges);
		_pendingChanges.Clear();
		ApplyDocumentChanges(changes);
	}

	void EditorSession::CreateHistory()
	{
		_history = std::make_unique<CommandHistory>(_document, HistoryMemoryBudget);
		_history->OnTransactionsEnded = [this]() {
			if (_playTest->IsEditing()) {
				FlushDocumentChanges();
			}
		};
	}

	void EditorSession::ApplyDocumentChanges(const DocumentChanges& changes)
	{
		if (changes.IsEmpty() || _editHandler == nullptr) {
			return;
		}

		if (changes.HasAny(DocumentChangeKind::LayerStructure)) {
			_selection.Clear();
			_eventSelection.Clear();
			_currentLayer = std::clamp<std::int32_t>(_currentLayer, 0, std::max<std::int32_t>(std::int32_t(_document.Layers.size()) - 1, 0));
			// Kept by the index of the layer, the shown level may keep its layers and only move them
			_maskOverlay->Invalidate();
		}

		if (changes.HasAny(DocumentChangeKind::Events)) {
			// Undone events aren't selected anymore
			_eventSelection.DropEmpty(_document);
		}
		// Overridden tiles are dropped from it by ApplyTileOverrides(), only those that changed
		_tileLod->OnDocumentChanged(changes);
		if (changes.HasAny(DocumentChangeKind::AnimationStructure | DocumentChangeKind::AnimationTiming)) {
			// Tiles show other frames, so other masks
			_maskOverlay->Invalidate();
		} else if (changes.HasAny(DocumentChangeKind::Tiles)) {
			for (const DocumentTileArea& area : changes.GetTileAreas()) {
				_maskOverlay->InvalidateTiles(area.Layer, area.Area);
			}
		}

		if (changes.HasAny(DocumentChangeKind::TileSets | DocumentChangeKind::LevelProperties)) {
			// Other tile sets may be used now, they're watched for changes on disk
			_files->WatchFiles();
		}
		_playOverlays->NoteDocumentChanges(changes);
		if (changes.HasAny(DocumentChangeKind::LayerProperties | DocumentChangeKind::EditorState | DocumentChangeKind::LayerStructure)) {
			// Layers are shown as the document has them, the layer being edited is drawn as tiles again afterwards
			UpdateLayerOverride(true, changes.GetLayerOrder());
		}
		bool reload = (_editHandler->TileMap() == nullptr || !RuntimeSync::Apply(_document, *_editHandler->TileMap(), changes));
		if (!reload && changes.HasAny(DocumentChangeKind::LevelProperties) && _editHandler->NeedsReload(_document)) {
			reload = true;
		}
		if (!reload && changes.HasAny(DocumentChangeKind::Palette) && (_document.Flags & LevelFlags::UseLevelPalette) == LevelFlags::UseLevelPalette) {
			// Indexed tiles and sprites take their colors from the palette as they're drawn, so it's enough to replace it,
			// before the overridden tiles of a tile set that isn't indexed are made again with it
			ContentResolver::Get().SetSpritePalette(_document.LevelPalette);
		}
		if (!reload && changes.HasAny(DocumentChangeKind::TileOverrides | DocumentChangeKind::Palette)) {
			reload = !ApplyTileOverrides(changes.HasAny(DocumentChangeKind::Palette));
		}
		if (!reload && changes.HasAny(DocumentChangeKind::LevelProperties)) {
			_editHandler->ApplyLevelProperties(_document.Properties);
		}
		if (reload) {
			// The tiles may look otherwise when the level is loaded again, as a tile that isn't overridden anymore; the colors
			// of the tiles stay as they are unless what they're made of changed
			_tileLod->Invalidate(!changes.HasAny(DocumentChangeKind::TileOverrides | DocumentChangeKind::TileSets | DocumentChangeKind::Palette));
			RebuildRuntime();
		}
		_files->UpdateWindowTitle();
		NotifyChangeObservers(changes);
	}

	void EditorSession::AddChangeObserver(const std::shared_ptr<ChangeObserver>& observer)
	{
		if (observer != nullptr) {
			_changeObservers.push_back(observer);
		}
	}

	void EditorSession::NotifyChangeObservers(const DocumentChanges& changes)
	{
		for (std::size_t i = 0; i < _changeObservers.size(); ) {
			std::shared_ptr<ChangeObserver> observer = _changeObservers[i].lock();
			if (observer == nullptr) {
				_changeObservers.erase(_changeObservers.begin() + i);
				continue;
			}
			if (*observer) {
				(*observer)(changes);
			}
			i++;
		}
	}

	void EditorSession::ToggleLayerFlag(std::int32_t layer, std::uint8_t flag)
	{
		if (layer < 0 || layer >= std::int32_t(_document.Layers.size())) {
			return;
		}
		SetEditorState([this, layer, flag](LevelDocument& document) {
			document.Layers[layer].EditorFlags ^= flag;
			if (layer == _currentLayer) {
				_tools->GetActive()->Cancel();
			}
		});
	}

	void EditorSession::SetEditorState(Function<void(LevelDocument&)>&& change)
	{
		change(_document);
		if (_history != nullptr) {
			// Saved with the level, but neither a change to undo nor one that makes the level unsaved alone, it's only
			// counted for those keeping a copy of the document, see CommandHistory::NoteOutsideChange()
			_history->NoteOutsideChange();
		}
		DocumentChanges changes;
		changes.Add(DocumentChangeKind::EditorState);
		OnDocumentChanged(changes);
	}

	bool EditorSession::RecordSnapshotStep(String name, SnapshotParts parts, Function<bool(LevelDocument&)>&& change, DocumentChangeKind kinds, std::int32_t layer)
	{
		if (!_input->IsInToolCallback()) {
			// A step from elsewhere, a stroke of the tool going on would take what it changes as its own
			CancelInteractions(false);
		}
		auto command = SnapshotCommand::Begin(_document, parts, Death::move(name), kinds, layer);
		if (!change(_document)) {
			return false;
		}
		DocumentChanges changes;
		command->End(_document, changes);
		// Already done, executing it would only put the same state back
		_history->Record(Death::move(command));
		OnDocumentChanged(changes);
		return true;
	}

	LevelDocument& EditorSession::GetDocument()
	{
		return _document;
	}

	std::int32_t EditorSession::GetCurrentLayer() const
	{
		return _currentLayer;
	}

	bool EditorSession::IsLayerEditable(std::int32_t layer) const
	{
		return (layer >= 0 && layer < std::int32_t(_document.Layers.size()) &&
			(_document.Layers[layer].EditorFlags & (DocumentLayer::EditorHidden | DocumentLayer::EditorLocked)) == 0);
	}

	LayerView EditorSession::GetLayerView(std::int32_t layer)
	{
		LayerView view;
		Tiles::TileMap* tileMap = GetTileMap();
		if (tileMap == nullptr || layer < 0 || layer >= std::int32_t(_document.Layers.size())) {
			return view;
		}

		// The same view origin as the viewport uses, see PlayerViewport::UpdateCamera()
		EditorCamera& camera = _editHandler->GetCamera();
		Vector2i viewSize = camera.GetViewSize();
		Vector2f half(viewSize.X * 0.5f, viewSize.Y * 0.5f);
		float scale = camera.GetScale();
		Vector2f origin = camera.GetCenter() * scale - half;
		if (!PreferencesCache::UnalignedViewport) {
			origin = Vector2f(std::floor(origin.X), std::floor(origin.Y));
		}
		Vector2f center = (origin + half) / scale;

		const DocumentLayer& documentLayer = _document.Layers[layer];
		view.Scale = scale;
		view.ViewOrigin = origin;
		view.Origin = tileMap->GetLayerOrigin(layer, center, Vector2f(viewSize.X / scale, viewSize.Y / scale));
		view.Size = Vector2i(documentLayer.GetWidth(), documentLayer.GetHeight());
		if (!documentLayer.IsSpriteLayer()) {
			view.RepeatX = (documentLayer.Record.Flags & Formats::LayerRecord::RepeatX) != 0;
			view.RepeatY = (documentLayer.Record.Flags & Formats::LayerRecord::RepeatY) != 0;
		}
		return view;
	}

	TileStamp& EditorSession::GetStamp()
	{
		return _stamp;
	}

	void EditorSession::SetStamp(TileStamp&& stamp)
	{
		// Painting at random stays on for any other tiles
		bool random = _stamp.Random;
		std::uint32_t seed = _stamp.Seed;
		_stamp = Death::move(stamp);
		_stamp.Random = random;
		_stamp.Seed = seed;
	}

	TileSelection& EditorSession::GetSelection()
	{
		return _selection;
	}

	void EditorSession::Execute(std::unique_ptr<EditCommand> command, bool allowMerge)
	{
		if (!_input->IsInToolCallback()) {
			// A step from elsewhere, a stroke of the tool going on would take what it changes as its own
			CancelInteractions(false);
		}
		DocumentChanges changes;
		// A tile of the tile sets past the tile IDs the level addresses makes room for itself, in the same step
		ExecuteGrowingTileIdSpace(*_history, Death::move(command), changes, allowMerge);
		OnDocumentChanged(changes);
	}

	void EditorSession::BeginChanges(String name)
	{
		_history->BeginTransaction(Death::move(name));
	}

	void EditorSession::EndChanges()
	{
		_history->EndTransaction();
	}

	void EditorSession::PreviewTile(std::int32_t layer, std::int32_t x, std::int32_t y, const TileRef& tile)
	{
		if (Tiles::TileMap* tileMap = GetTileMap()) {
			tileMap->ReplaceLayerTile(layer, x, y, RuntimeSync::MakeLayerTile(_document, tile));
		}
	}

	void EditorSession::RevertPreview(std::int32_t layer, const Recti& area)
	{
		if (Tiles::TileMap* tileMap = GetTileMap()) {
			RuntimeSync::ApplyTiles(_document, *tileMap, layer, area);
		}
	}

	Tiles::TileMap* EditorSession::GetTileMap()
	{
		return (_editHandler != nullptr ? _editHandler->TileMap() : nullptr);
	}

	EditorCamera* EditorSession::GetCamera()
	{
		return (_editHandler != nullptr ? &_editHandler->GetCamera() : nullptr);
	}

	void EditorSession::ShowMessage(StringView text)
	{
		if (_surface != nullptr) {
			_surface->ShowToast(text);
		}
	}

	EventStamp& EditorSession::GetEventBrush()
	{
		return _eventBrush;
	}

	EventSelection& EditorSession::GetEventSelection()
	{
		return _eventSelection;
	}

	EventPreviews& EditorSession::GetEventPreviews()
	{
		return _eventPreviews;
	}

	float EditorSession::GetAnimTime() const
	{
		return (_surface != nullptr ? _surface->AnimTime : 0.0f);
	}

	Vector2i EditorSession::GetViewSize() const
	{
		LevelHandler* handler = GetActiveHandler();
		return (handler != nullptr ? handler->GetViewSize() : Vector2i());
	}

	LevelHandler* EditorSession::GetActiveHandler() const
	{
		if (LevelHandler* gameHandler = _playTest->GetGameHandler()) {
			return gameHandler;
		}
		return _editHandler.get();
	}

	void EditorSession::OnBeginFrame()
	{
#if defined(DEATH_DEBUG)
		_debugConsole->Update();
#endif

		float timeMult = theApplication().GetTimeMult();
		if (_editHandler != nullptr) {
			_input->Update(timeMult);
		}
		UpdateEventSound();
		_playTest->UpdateMusicFade(timeMult);
#if defined(DEATH_TARGET_EMSCRIPTEN)
		WebFileBridge::Update();
#endif
		if (_surface != nullptr) {
			_surface->ShowVirtualCursor = (_input->GetInputModality() == InputModality::Gamepad && _playTest->IsEditing());
			_surface->VirtualCursor = _input->GetGamepadCursor();
			// Windows stay open while the level is played, they're only hidden, and they come back with the editor
			_surface->WindowsHidden = !_playTest->IsEditing();
			// Clear of the edges of the screen the game keeps its own interface clear of, the setting can be changed in the
			// pause menu of a play-test
			_surface->SafeMargins = UiThickness(PreferencesCache::SafeArea[std::size_t(SafeAreaEdge::Left)] * 0.001f,
				PreferencesCache::SafeArea[std::size_t(SafeAreaEdge::Top)] * 0.001f, PreferencesCache::SafeArea[std::size_t(SafeAreaEdge::Right)] * 0.001f,
				PreferencesCache::SafeArea[std::size_t(SafeAreaEdge::Bottom)] * 0.001f);
		}
		if (LevelHandler* gameHandler = _playTest->GetGameHandler()) {
			_playTest->UpdateTransition(timeMult);
			gameHandler->OnBeginFrame();
		} else if (_editHandler != nullptr) {
			// The ring back into the editor, the play-test it shows the last frame of is released once it's done
			_playTest->UpdateTransition(timeMult);
			UpdateLayerOverride();
			UpdateTileLod();
			// The colors of the tiles for the view from far away are prepared ahead, not while the view flies into the game
			if (_playTest->IsEditing()) {
				_tileLod->WarmUp(TileLod::WarmUpMilliseconds);
			}
			if (_smartTilesUsed && !_smartTiles.IsReady()) {
				_smartTiles.Update();
			}
			_editHandler->OnBeginFrame();
		}
		if (_history != nullptr) {
			_files->UpdateAutosave(timeMult);
		}
		// The level compared with is read again once it was saved, also by another program
		if (_files->UpdateFileWatches(timeMult)) {
			_comparison->ReloadIfSavedChanged();
		}
		_playOverlays->Update(timeMult, _input->IsToolPointerDown());
	}

	void EditorSession::OnEndFrame()
	{
		if (LevelHandler* gameHandler = _playTest->GetGameHandler()) {
			gameHandler->OnEndFrame();
		} else if (_editHandler != nullptr) {
			_editHandler->OnEndFrame();
			// The game takes over once the view flew into the level, between two frames, so its first frame starts with
			// its own beginning
			_playTest->UpdateEntering();
		}
	}

	void EditorSession::OnInitializeViewport(std::int32_t width, std::int32_t height)
	{
		if (LevelHandler* gameHandler = _playTest->GetGameHandler()) {
			gameHandler->OnInitializeViewport(width, height);
		} else if (_editHandler != nullptr) {
			_editHandler->OnInitializeViewport(width, height);
		}

		if (_surface != nullptr) {
			// Drawn over everything, at the size of the window
			_surface->setParent(&theApplication().GetRootNode());
		}
		UpdateUiScale();
	}

	void EditorSession::UpdateUiScale()
	{
		if (_surface == nullptr) {
			return;
		}
		_surface->SetScale(_settings.UiScale > 0.0f ? _settings.UiScale : GetAutoUiScale());
	}

	float EditorSession::GetAutoUiScale()
	{
		IGfxDevice& gfxDevice = theApplication().GetGfxDevice();
		float width = float(gfxDevice.drawableWidth()), height = float(gfxDevice.drawableHeight());
		// By the size of the window up to the largest size offered, a larger one would only waste the room
		float byWindow = std::clamp(std::floor(std::min(width / 640.0f, height / 400.0f)), 1.0f, 6.0f);

		// By how large the system shows its own interface, so the editor isn't tiny on a small screen with many pixels,
		// two pixels a unit at 100%; never smaller than by the window, and only as large as still leaves room for the
		// smallest layout of the panels
		float systemScale = gfxDevice.windowScalingFactor();
		if (!(systemScale > 0.0f && systemScale < 16.0f)) {
			systemScale = 1.0f;
		}
#if defined(DEATH_TARGET_ANDROID) || defined(DEATH_TARGET_IOS)
		// A phone is held close, the panels may get as few units as the compact layout needs
		constexpr float MinWidthUnits = 480.0f, MinHeightUnits = 270.0f;
#else
		constexpr float MinWidthUnits = 640.0f, MinHeightUnits = 360.0f;
#endif
		// The size of the system to the nearest step, as a density of 420 dpi would be 263%; the room for the panels is a
		// limit, so it's rounded down
		float fit = std::floor(std::min(width / MinWidthUnits, height / MinHeightUnits) / UiScaleStep) * UiScaleStep;
		float bySystem = std::min(RoundUiScale(2.0f * systemScale), fit);
		return std::clamp(std::max(byWindow, bySystem), MinUiScale, MaxUiScale);
	}

	float EditorSession::RoundUiScale(float scale)
	{
		return std::clamp(std::round(scale / UiScaleStep) * UiScaleStep, MinUiScale, MaxUiScale);
	}

	void EditorSession::OnSuspend()
	{
		if (_history != nullptr) {
			_files->SaveRecoveryCopyNow();
		}
		_settings.Save();
	}

	void EditorSession::OnKeyPressed(const KeyboardEvent& event)
	{
		_input->OnKeyPressed(event);
	}

	void EditorSession::OnKeyReleased(const KeyboardEvent& event)
	{
		_input->OnKeyReleased(event);
	}

	void EditorSession::OnTextInput(const TextInputEvent& event)
	{
		_input->OnTextInput(event);
	}

	void EditorSession::OnTouchEvent(const TouchEvent& event)
	{
		_input->OnTouchEvent(event);
	}

	void EditorSession::OnMouseDown(const MouseEvent& event)
	{
		_input->OnMouseDown(event);
	}

	void EditorSession::OnMouseUp(const MouseEvent& event)
	{
		_input->OnMouseUp(event);
	}

	void EditorSession::OnMouseMove(const MouseState& state)
	{
		_input->OnMouseMove(state);
	}

	void EditorSession::OnMouseWheel(const ScrollEvent& event)
	{
		_input->OnMouseWheel(event);
	}

	IStateHandler::CursorPolicy EditorSession::GetCursorPolicy() const
	{
		// While playing, as in the game
		return (_playTest->IsInGame() ? CursorPolicy::Default : CursorPolicy::Visible);
	}

	void EditorSession::InvokeAsync(Function<void()>&& callback)
	{
		_root->InvokeAsync(Death::move(callback));
	}

	void EditorSession::InvokeAsync(std::weak_ptr<void> reference, Function<void()>&& callback)
	{
		_root->InvokeAsync(Death::move(reference), Death::move(callback));
	}

	bool EditorSession::HasResumableState() const
	{
		return false;
	}

	void EditorSession::ResumeSavedState()
	{
	}

	bool EditorSession::SaveCurrentStateIfAny()
	{
		// The editor never leaves a state to resume, nor does a play-test
		return false;
	}

	void EditorSession::ApplyCursorPolicy()
	{
		_root->ApplyCursorPolicy();
	}

#if defined(WITH_MULTIPLAYER)
	void EditorSession::ConnectToServer(StringView endpoint, std::uint16_t defaultPort, StringView password)
	{
	}

	bool EditorSession::CreateServer(Multiplayer::ServerInitialization&& serverInit)
	{
		return false;
	}
#endif

	void EditorSession::OpenEditor(EditorInitialization&& editorInit)
	{
		_root->OpenEditor(Death::move(editorInit));
	}

	IRootController::Flags EditorSession::GetFlags() const
	{
		return _root->GetFlags();
	}

	StringView EditorSession::GetNewestVersion() const
	{
		return _root->GetNewestVersion();
	}

	float EditorSession::GetInitializationProgress() const
	{
		return _root->GetInitializationProgress();
	}

	void EditorSession::RefreshCacheLevels(bool recreateAll, Compatibility::ConversionProgress progress)
	{
	}

	void EditorSession::UpdateTileLod()
	{
		Tiles::TileMap* tileMap = GetTileMap();
		if (tileMap == nullptr) {
			return;
		}
		// From far away, the layers are drawn as images instead of tiles, by how many tiles are in view, which the zoom
		// relative to how large the game shows the level tells, and how large they are on the screen
		const EditorCamera& camera = _editHandler->GetCamera();
		float scale = camera.GetScale();
		float zoom = (camera.GetGameplayScale() > 0.0f ? scale / camera.GetGameplayScale() : scale);
		bool useLod = _tileLod->UpdateActive(zoom, scale * 32.0f);
#if defined(DEATH_DEBUG)
		if (_debugConsole->GetLodMode() >= 0) {
			useLod = (_debugConsole->GetLodMode() != 0);
		}
#endif
		Tiles::ITileMapLayerOverride* lod = (useLod ? _tileLod.get() : nullptr);
		Tiles::TileMapViewOptions viewOptions = tileMap->GetViewOptions();
		if (viewOptions.LayerOverride != lod) {
			viewOptions.LayerOverride = lod;
			tileMap->SetViewOptions(viewOptions);
		}
		_tileLod->BeginFrame(scale);
	}

	void EditorSession::AttachSceneContent()
	{
		// The sprites of the events are a part of the level, layers in front of them cover them
		_editHandler->SetSceneContent([this](UI::Canvas& canvas) {
			if (_shell != nullptr) {
				_shell->DrawSceneEvents(canvas);
			}
		});
	}

	void EditorSession::RebuildRuntime()
	{
		if (_editHandler == nullptr) {
			return;
		}

		LOGD("Loading the edited level again");
		_maskOverlay->Invalidate();
		// The new one shows all layers as the document has them
		_overriddenLayer = -1;

		if (!_input->IsInToolCallback()) {
			CancelInteractions(false);
		}
		_tools->GetActive()->Cancel();
		EditorCamera& camera = _editHandler->GetCamera();
		Vector2f center = camera.GetCenter();
		float scale = camera.GetTargetScale();
		Tiles::TileMapViewOptions viewOptions = _editHandler->TileMap()->GetViewOptions();

		// The music goes on playing, taken out of the old one
#if defined(WITH_AUDIO)
		String musicPath;
		std::unique_ptr<AudioStreamPlayer> music = _editHandler->TakeMusic(musicPath);
#endif

		// The old one goes first, the resources it doesn't share with the new one are released while loading
		Viewport::GetChain().clear();
		_editHandler = nullptr;

		auto handler = std::make_shared<EditorLevelHandler>(this);
		handler->SetInitialCamera(center, scale);
		if (!handler->Initialize(_document, _files->GetLevelName(), _files->GetFilePath())) {
			LOGE("Edited level cannot be shown");
			_root->GoToMainMenu(false);
			return;
		}
#if defined(WITH_AUDIO)
		handler->ContinueMusic(music, musicPath, handler->GetMusicLowPass());
#endif

		_editHandler = Death::move(handler);
		AttachSceneContent();
		Vector2i res = theApplication().GetResolution();
		OnInitializeViewport(res.X, res.Y);
		_runtimeBuildCount++;
		// The flat layers stay as they were
		viewOptions.ReferenceViewSize = _editHandler->GetReferenceViewSize();
		_editHandler->TileMap()->SetViewOptions(viewOptions);
		RememberAppliedOverrides();
	}

	void EditorSession::UpdateLayerOverride(bool restore, ArrayView<const std::int32_t> layerOrder)
	{
		Tiles::TileMap* tileMap = GetTileMap();
		if (tileMap == nullptr) {
			return;
		}

		std::int32_t layerCount = std::int32_t(_document.Layers.size());

		// Layers that move by themselves stay where their offsets put them, as the document has them
		Tiles::TileMapViewOptions viewOptions = tileMap->GetViewOptions();
		if (viewOptions.PauseLayerMovement != _settings.PauseLayerMovement) {
			viewOptions.PauseLayerMovement = _settings.PauseLayerMovement;
			tileMap->SetViewOptions(viewOptions);
			if (viewOptions.PauseLayerMovement) {
				for (std::int32_t i = 0; i < layerCount && i < tileMap->GetLayerCount(); i++) {
					const DocumentLayer& documentLayer = _document.Layers[i];
					if (i != _overriddenLayer && (documentLayer.Record.AutoSpeedX != 0.0f || documentLayer.Record.AutoSpeedY != 0.0f)) {
						Tiles::LayerDescription description = Tiles::TileMap::MakeLayerDescription(documentLayer.Record);
						RuntimeSync::ApplyEditorOpacity(documentLayer, description);
						tileMap->SetLayerDescription(i, description, RuntimeSync::IsLayerShown(documentLayer));
					}
				}
			}
		}

		std::int32_t layer = -1;
		if (!restore && _playTest->IsEditing() && _currentLayer >= 0 && _currentLayer < layerCount &&
			NeedsLayerOverride(_document.Layers[_currentLayer])) {
			layer = _currentLayer;
		}
		if (layer == _overriddenLayer) {
			return;
		}

		if (_overriddenLayer >= 0 && _overriddenLayer < layerCount && _overriddenLayer < tileMap->GetLayerCount()) {
			// As the game draws it, with what was changed while it was edited --- if the layers were moved, the document has
			// it where it went, the shown level still where it was
			std::int32_t documentIndex = _overriddenLayer;
			for (std::int32_t i = 0; i < std::int32_t(layerOrder.size()); i++) {
				if (layerOrder[i] == _overriddenLayer) {
					documentIndex = i;
					break;
				}
			}
			const DocumentLayer& previous = _document.Layers[documentIndex];
			Tiles::LayerDescription previousDescription = Tiles::TileMap::MakeLayerDescription(previous.Record);
			RuntimeSync::ApplyEditorOpacity(previous, previousDescription);
			tileMap->SetLayerDescription(_overriddenLayer, previousDescription, RuntimeSync::IsLayerShown(previous));
			tileMap->InvalidateTexturedBackground();
		}
		_overriddenLayer = layer;
		if (layer >= 0 && layer < tileMap->GetLayerCount()) {
			const DocumentLayer& current = _document.Layers[layer];
			Tiles::LayerDescription description = Tiles::TileMap::MakeLayerDescription(current.Record);
			if (RuntimeSync::IsTexturedBackground(current)) {
				description.RendererType = Tiles::LayerRendererType::Default;
				// The color of a sky is its horizon and stars, that of a reflection its tint and ripples, the tiles are shown as
				// they are - those of a reflection are otherwise only seen as ripples
				description.Color = Vector4f(1.0f, 1.0f, 1.0f, 1.0f);
			}
			// A layer that moves by itself couldn't be painted on
			description.AutoSpeedX = 0.0f;
			description.AutoSpeedY = 0.0f;
			RuntimeSync::ApplyEditorOpacity(current, description);
			tileMap->SetLayerDescription(layer, description, RuntimeSync::IsLayerShown(current));
		}
	}

	bool EditorSession::NeedsLayerOverride(const DocumentLayer& layer)
	{
		// A reflection with an image of its own doesn't show its tiles at all, so it's shown as it is, rippling as in the game
		if (!layer.IsSpriteLayer() && Tiles::LayerRendererType(layer.Record.RendererType) == Tiles::LayerRendererType::Reflection &&
			!layer.Texture.empty()) {
			return false;
		}
		return (RuntimeSync::IsTexturedBackground(layer) || layer.Record.AutoSpeedX != 0.0f || layer.Record.AutoSpeedY != 0.0f);
	}

	Rectf EditorSession::GetViewRect() const
	{
		if (_shell != nullptr) {
			return _shell->GetViewRect();
		}
		Vector2i size = GetViewSize();
		return Rectf(0.0f, 0.0f, float(size.X), float(size.Y));
	}

	void EditorSession::ChangeUiScale(std::int32_t direction)
	{
		if (direction == 0) {
			_settings.UiScale = 0.0f;
		} else {
			// From the nearest step, a size set by hand in the settings may be between them
			float current = RoundUiScale(_surface->GetScale());
			_settings.UiScale = RoundUiScale(current + (direction > 0 ? UiScaleStep : -UiScaleStep));
		}
		_settings.Save();
		// Only the interface, the level keeps its render targets
		UpdateUiScale();
		// TRANSLATORS: Status message after making the interface larger or smaller, {} is its size in percent
		ShowMessage(_fx("editor", "Interface size {}%", std::int32_t(std::round(_surface->GetScale() * 100.0f))));
	}
}

#endif
