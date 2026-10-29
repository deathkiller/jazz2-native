#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../IStateHandler.h"
#include "../IRootController.h"
#include "EditorInitialization.h"
#include "Files/TileSetDataCache.h"
#include "Document/CommandHistory.h"
#include "Document/DocumentSnapshot.h"
#include "Document/LevelDocument.h"
#include "Events/EventPreviews.h"
#include "Files/ContentTileSetLibrary.h"
#include "Files/EditorSettings.h"
#include "Input/ActionRegistry.h"
#include "Runtime/PlayTestTypes.h"
#include "Runtime/ScriptGraphics.h"
#include "Session/DocumentFiles.h"
#include "Session/EditorInputRouter.h"
#include "Smart/SmartTileModel.h"
#include "Smart/TileShapes.h"
#include "Tools/BlockGrab.h"
#include "Tools/EditorContext.h"
#include "Tools/EventTools.h"

#include <memory>

namespace Jazz2
{
	class LevelHandler;
}

namespace Jazz2::Formats
{
	struct TileSetData;
}

namespace Jazz2::Editor
{
	class EditorDebugConsole;
	class EditorLevelHandler;
	class EditorShell;
	class MaskOverlay;
	class PlayOverlays;
	class PlayTestController;
	class PlayTransition;
	class SavedComparison;
	class TileLod;
	class ToolManager;
	class UiPainter;
	class UiSurface;
	struct UiFileRoot;

	/**
		@brief The level editor

		The state handler of the editor. It owns the level being edited (see @ref LevelDocument) with its history,
		hosts the level handler that shows it (see @ref EditorLevelHandler) and the interface around it. The hosted
		handler gets the session as its root controller, so whatever it would do to the game as a whole --- going to
		another level or to the main menu, saving a state to resume --- is decided by the session.

		The session coordinates what keeps state of its own: the file of the level with its recovery copy, lock and
		script (see @ref DocumentFiles), the input (see @ref EditorInputRouter), the play-tests (see
		@ref PlayTestController), the comparison with the saved file (see @ref SavedComparison) and what is shown of how
		the level is played (see @ref PlayOverlays).
	*/
	class EditorSession : public IStateHandler, public IRootController, public IEditorContext, private IPlayTestHost,
		private IDocumentFilesHost, private IInputRouterHost
	{
		DEATH_RUNTIME_OBJECT(IStateHandler);

	public:
		/** @brief Creates a new instance */
		explicit EditorSession(IRootController* root);
		~EditorSession() override;

		/** @brief Opens a level, returns `false` if it can't be opened */
		bool Initialize(const EditorInitialization& editorInit);

		/** @brief Returns the history of the level being edited */
		CommandHistory& GetHistory() override {
			return *_history;
		}
		/** @brief Returns the actions */
		ActionRegistry& GetActions() override {
			return _actions;
		}
		/** @brief Returns the tools */
		ToolManager& GetTools() override;
		/** @brief Returns the block grabbed by its corners with a key, marked in the level or in the picker of tiles */
		BlockGrab& GetBlockGrab() override;
		/** @brief Returns the level handler that shows the level, or `nullptr` */
		EditorLevelHandler* GetEditHandler() const override {
			return _editHandler.get();
		}
		/** @brief Returns the surface of the interface */
		UiSurface* GetSurface() const override {
			return _surface.get();
		}
		/** @brief Returns the settings */
		EditorSettings& GetSettings() override {
			return _settings;
		}
		/** @brief Returns what is known about the tile sets of the game */
		ContentTileSetLibrary& GetTileSetLibrary() override {
			return _tileSets;
		}
		/** @brief Opens a level file in the editor, asking about unsaved changes first */
		void OpenFile(StringView path) override;
		/**
		 * @brief Restores a recovery copy into the editor, asking about unsaved changes first
		 *
		 * The level is edited as the file the copy belongs to, if that file still exists, as a new level otherwise.
		 */
		void OpenRecoveryCopy(StringView recoveryPath) override;
		/** @brief Returns the path of the level file, may be empty */
		StringView GetFilePath() const override;
		/** @brief Returns the name of the level as episode/level, may be empty */
		StringView GetLevelName() const override;
		/** @brief Returns the places levels are opened from or saved to */
		void GetFileRoots(bool forSaving, SmallVectorImpl<UiFileRoot>& roots) override;
		/** @brief Returns the places images are opened from or saved to, outside of the content of the game too */
		void GetImageRoots(bool forSaving, SmallVectorImpl<UiFileRoot>& roots) override;

		/** @brief Starts playing the level */
		void StartPlayTest(PlayStartMode mode) override;
		void PlayTestWeapon(WeaponType weapon, bool poweredUp) override;
		/** @brief Stops playing the level and returns to editing, @p keep keeps the play-test to resume later */
		void StopPlayTest(bool keep) override;
		/** @brief Returns `true` while the level is played, or is about to be */
		bool IsPlaying() const override;
		/** @brief Returns what plays the level being edited */
		PlayTestController& GetPlayTest();
		/** @brief Returns the color the tile set of the level is known by, see @ref FindAccentColor() */
		Colorf GetAccentColor() override;
		/** @brief Returns a whole tile set with the pixels and masks of its tiles, read once, or `nullptr` if it can't be read */
		const Formats::TileSetData* GetTileSetData(StringView name) override;
		/** @brief Returns the tile sets read whole for the level being edited, see @ref GetTileSetData() */
		TileSetDataCache& GetTileSetDataCache() override {
			return _tileSetData;
		}
		/** @brief Forgets what was read of a tile set, after its file was written */
		void InvalidateTileSet(StringView name) override;
		/** @brief Returns the path the player took in the last play-test */
		const PlayPath& GetLastPlayPath() const;
		/** @brief Returns the paths of the last play-tests, kept in the level */
		const PlayPathHistory& GetPlayPathHistory() const override;
		/** @brief Returns `true` if the panels are shown, while the level is edited and while the ring into the game grows over them */
		bool ArePanelsShown() const override;
		/** @brief Returns the transition into the level being played */
		PlayTransition& GetPlayTransition();
		/** @brief Returns `true` if the path of the last play-test is shown */
		bool IsPlayPathShown() const {
			return _showPlayPath;
		}
		/** @brief Returns what is shown over the level of how it's played, where the player can get and where it died */
		PlayOverlays& GetPlayOverlays();
		/**
		 * @brief Finds where the player can get from the start of the play character, as the level is loaded now
		 *
		 * With the moves of any character, see @ref PlayOverlays::ComputeReachability(). Returns `false` if it can't be
		 * found out, as without the start or without multiplayer compiled in, whose route tracer it uses.
		 */
		bool ComputeReachability(SmallVectorImpl<std::int32_t>& distances) override;
		/** @brief Draws what is shown over the level of the editor, the differences from the saved file and the deaths in the last play-tests */
		void DrawSessionOverlays(UiPainter& painter, const Rectf& viewRect);
		/** @brief Loads the level again from the document, so the events in the game are as they are now, see @ref ComputeReachability() */
		void ReloadRuntime() override {
			RebuildRuntime();
		}

		/** @brief Changes the layer being edited */
		void SetCurrentLayer(std::int32_t layer) override;
		/** @brief Returns `true` if the grid is shown */
		bool IsGridShown() const {
			return _showGrid;
		}
		/** @brief Returns `true` if events are shown over the level */
		bool AreEventsShown() const override {
			return _showEvents;
		}
		/** @brief Returns how collision masks are shown, 0 not at all, 1 over the tiles, 2 instead of the tiles */
		std::int32_t GetMaskMode() const {
			return _maskMode;
		}
		/** @brief Returns what shows the collision masks over the level */
		MaskOverlay& GetMaskOverlay();
		/** @brief Returns what the panel with the tiles and the events shows */
		PickerPanel GetPickerPanel() const override {
			return _pickerPanel;
		}
		/** @brief Changes what the panel with the tiles and the events shows */
		void SetPickerPanel(PickerPanel panel) override {
			_pickerPanel = panel;
		}
		/** @brief Chooses the events the event tool places, and the tool */
		void SetEventBrush(EventStamp&& brush);
		/** @brief Lets the next event chosen in the list go to a callback, instead of becoming the one to place */
		void PickEventFromList(Function<void(EventType)>&& callback) override;
		/** @brief Returns `true` if the next event chosen in the list goes to a callback */
		bool IsPickingEvent() const override {
			return (bool)_eventPick;
		}
		/** @brief Lets the next event chosen in the list become the one to place again, see @ref PickEventFromList() */
		void CancelEventPick() override {
			_eventPick = {};
		}
		/** @brief Called when an event is chosen in the list */
		void OnEventChosen(EventType type) override;
		/** @brief Returns what the editor was used with last */
		InputModality GetInputModality() const override;
		/** @brief Returns `true` if the editing actions work on events, not on tiles */
		bool IsEditingEvents() const override;
		/** @brief Returns the pointer over the view, `false` if it's not over the view */
		bool GetHoverPointer(ToolPointer& pointer) override;

		/** @brief Undoes the last change */
		bool Undo() override;
		/** @brief Redoes the last undone change */
		bool Redo() override;
		/** @brief Shows changes of the level being edited */
		void OnDocumentChanged(const DocumentChanges& changes);
		/**
		 * @brief Tells a function about every change of the level being edited, all of it changes when another level is opened
		 *
		 * Only a weak reference is kept, the function is called as long as its owner keeps it.
		 */
		void AddChangeObserver(const std::shared_ptr<ChangeObserver>& observer) override;
		/**
		 * @brief Saves the level into another file and edits that one from then on, as Save As does
		 *
		 * See @ref DocumentFiles::SaveAsFile(), the script of the level goes along, and with @p move the file the level
		 * was in is removed. Returns `false` if the level couldn't be saved, nothing is changed then.
		 */
		bool SaveAsFile(StringView path, bool move) override;
		/** @brief Returns `true` if the level can be saved to its file */
		bool CanSaveInPlace() const override;
		/** @brief Shows a dialog to save the level as a new file, or to export it, @p onSaved is called once it's saved */
		void ShowSaveAsDialog(bool exportOnly, Function<void()>&& onSaved = {}) override;
		/**
		 * @brief Tells that a level file was moved or renamed outside of Save As, its place among the recent files goes along
		 *
		 * @ref SaveAsFile() does it itself for the level being edited.
		 */
		void NoteLevelFileMoved(StringView from, StringView to) override;
		/**
		 * @brief Ends what the pointer is in the middle of, before something else changes the level
		 *
		 * The stroke of a tool that follows the pointer is cancelled, as by <kbd>Esc</kbd>, and with @p releasePointer
		 * also what the interface holds by the pointer, as a handle of the layer dragged. Called before an action, an
		 * undoable step that doesn't come from the tool itself, saving, undoing and playing, so a step of a tool
		 * started before them never takes what they changed as its own. Commands the tool runs itself aren't
		 * affected.
		 */
		void CancelInteractions(bool releasePointer = true);
		/**
		 * @brief Makes a change directly on the level and records it as an undoable step, kept as snapshots of @p parts
		 *
		 * For changes no dedicated command makes, see @ref SnapshotCommand. As with @ref Execute(), a stroke of the tool
		 * going on is cancelled first, unless the tool makes the change itself. @p change returns `false` if it changed
		 * nothing, nothing is recorded then. @p kinds and @p layer are as in @ref SnapshotCommand::Begin(). Returns
		 * `true` if the step was recorded.
		 */
		bool RecordSnapshotStep(String name, SnapshotParts parts, Function<bool(LevelDocument&)>&& change,
			DocumentChangeKind kinds = DocumentChangeKind::None, std::int32_t layer = -1) override;
		/**
		 * @brief Changes the editor state of the level, such as which layers are hidden or locked and how opaque they are shown
		 *
		 * It's saved with the level, but isn't a change to undo, so no step is recorded --- the level counts as changed
		 * since it was saved, so it's autosaved and asked about when it's closed.
		 */
		void SetEditorState(Function<void(LevelDocument&)>&& change) override;
		/** @brief Toggles the editor state of a layer, such as whether it's hidden */
		void ToggleLayerFlag(std::int32_t layer, std::uint8_t flag) override;
		/** @brief Moves a layer to another position in the list of layers, as an undoable step */
		void MoveLayerTo(std::int32_t from, std::int32_t to) override;

		// IToolHost
		LevelDocument& GetDocument() override;
		std::int32_t GetCurrentLayer() const override;
		bool IsLayerEditable(std::int32_t layer) const override;
		LayerView GetLayerView(std::int32_t layer) override;
		TileStamp& GetStamp() override;
		void SetStamp(TileStamp&& stamp) override;
		TileSelection& GetSelection() override;
		void Execute(std::unique_ptr<EditCommand> command, bool allowMerge = false) override;
		void BeginChanges(String name) override;
		void EndChanges() override;
		void PreviewTile(std::int32_t layer, std::int32_t x, std::int32_t y, const TileRef& tile) override;
		void RevertPreview(std::int32_t layer, const Recti& area) override;
		Tiles::TileMap* GetTileMap() override;
		EditorCamera* GetCamera() override;
		void ShowMessage(StringView text) override;
		EventStamp& GetEventBrush() override;
		EventSelection& GetEventSelection() override;
		EventPreviews& GetEventPreviews() override;
		SmartTileModel& GetSmartTiles() override;
		const TileShapes& GetTileShapes() override;
		float GetAnimTime() const override;

		// IStateHandler
		Vector2i GetViewSize() const override;
		void OnBeginFrame() override;
		void OnEndFrame() override;
		void OnInitializeViewport(std::int32_t width, std::int32_t height) override;
		void OnKeyPressed(const KeyboardEvent& event) override;
		void OnKeyReleased(const KeyboardEvent& event) override;
		void OnTextInput(const TextInputEvent& event) override;
		void OnTouchEvent(const TouchEvent& event) override;
		void OnMouseDown(const MouseEvent& event) override;
		void OnMouseUp(const MouseEvent& event) override;
		void OnMouseMove(const MouseState& state) override;
		void OnMouseWheel(const ScrollEvent& event) override;
		void OnSuspend() override;
		CursorPolicy GetCursorPolicy() const override;

		// IRootController
		void InvokeAsync(Function<void()>&& callback) override;
		void InvokeAsync(std::weak_ptr<void> reference, Function<void()>&& callback) override;
		void GoToMainMenu(bool afterIntro) override;
		void ChangeLevel(LevelInitialization&& levelInit) override;
		bool HasResumableState() const override;
		void ResumeSavedState() override;
		bool SaveCurrentStateIfAny() override;
		void ApplyCursorPolicy() override;
#if defined(WITH_MULTIPLAYER)
		void ConnectToServer(StringView endpoint, std::uint16_t defaultPort, StringView password = {}) override;
		bool CreateServer(Multiplayer::ServerInitialization&& serverInit) override;
#endif
		void OpenEditor(EditorInitialization&& editorInit) override;
		Flags GetFlags() const override;
		StringView GetNewestVersion() const override;
		float GetInitializationProgress() const override;
		void RefreshCacheLevels(bool recreateAll, Compatibility::ConversionProgress progress = {}) override;

	private:
		IRootController* _root;
		ContentTileSetLibrary _tileSets;
		LevelDocument _document;
		std::unique_ptr<CommandHistory> _history;
		std::shared_ptr<EditorLevelHandler> _editHandler;
		std::unique_ptr<UiSurface> _surface;
		EditorShell* _shell;
		ActionRegistry _actions;
		EditorSettings _settings;
		// The file of the level, its recovery copy, its lock and its script, see DocumentFiles
		std::unique_ptr<DocumentFiles> _files;
		std::unique_ptr<ToolManager> _tools;
		// A block grabbed by its corners with a key, as B of Jazz Creation Station
		std::unique_ptr<BlockGrab> _blockGrab;

		// Plays the level, owns the play-tests and the rings into the level and back
		std::unique_ptr<PlayTestController> _playTest;
		// Where the level differs from the file saved on disk, while it's shown
		std::unique_ptr<SavedComparison> _comparison;
		// Where the player can get and where it died in the last play-tests
		std::unique_ptr<PlayOverlays> _playOverlays;
		bool _showPlayPath;
		// How the level is played in this session only, as the game is set up at first
		bool _playReforged;
		bool _playLedgeClimb;
		// The weapon the next play-test starts with, see PlayTestWeapon()
		WeaponType _playWeapon = WeaponType::Unknown;
		bool _playWeaponPowered = false;
		// Changes not shown yet, during a transaction or while the level is played, see OnDocumentChanged()
		DocumentChanges _pendingChanges;
		// See GetAccentColor()
		Colorf _accentColor;
		std::uint32_t _accentRevision = 0;
		std::uint64_t _accentKey = 0;
		// See GetSmartTiles()
		std::uint32_t _smartTilesRevision = 0;
		// Tile sets read whole, for the pixels of their tiles, see GetTileSetData()
		TileSetDataCache _tileSetData;

		std::int32_t _currentLayer;
		TileStamp _stamp;
		TileStamp _clipboard;
		TileSelection _selection;
		bool _showGrid;
		EventPreviews _eventPreviews;
		EventStamp _eventBrush;
		EventStamp _eventClipboard;
		EventSelection _eventSelection;
		Function<void(EventType)> _eventPick;
		// See AddChangeObserver(), the expired ones are dropped as they're found
		SmallVector<std::weak_ptr<ChangeObserver>, 4> _changeObservers;
		std::unique_ptr<MaskOverlay> _maskOverlay;
		// How the tiles of the default tile set fit together, learned from levels once it's asked for
		SmartTileModel _smartTiles;
		bool _smartTilesUsed = false;
		TileShapes _tileShapes;
		// Draws the layers as images from far away
		std::unique_ptr<TileLod> _tileLod;
#if defined(WITH_ANGELSCRIPT)
		// The sprites the script of the level gives the objects, taken again once it's marked, see UpdateScriptGraphics()
		ScriptGraphics _scriptGraphics;
		bool _scriptGraphicsDirty = true;
		// The weapons of the level and the script with their lines that the sprites were taken with last
		SmallVector<std::uint8_t, 0> _scriptGraphicsWeapons;
		String _scriptGraphicsScript;
#endif
		std::int32_t _maskMode;
		// The masks are shown only while the key of peeking at them is held, see ActionIds::ViewPeekMasks
		bool _maskPeek = false;
		// Layer drawn differently while it's edited, see UpdateLayerOverride()
		std::int32_t _overriddenLayer = -1;
		// Tiles the shown level overrides, and the tiles of the tile sets it stores in itself, each with a hash of what
		// it shows, sorted by the tile ID, see ApplyTileOverrides()
		struct AppliedTile
		{
			std::uint16_t TileId;
			std::uint64_t Hash;
		};
		SmallVector<AppliedTile, 0> _appliedDiffuseOverrides;
		SmallVector<AppliedTile, 0> _appliedMaskOverrides;
		SmallVector<AppliedTile, 0> _appliedLevelTiles;
		std::uint32_t _appliedLevelTilesGeneration = 0;
		bool _showEvents;
		PickerPanel _pickerPanel;

		// The keyboard, the mouse, the touch screen and the gamepad, see EditorInputRouter
		std::unique_ptr<EditorInputRouter> _input;
		// How many times the level shown was loaded again, see OnPlayTestReturned()
		std::uint32_t _runtimeBuildCount = 0;

#if defined(DEATH_DEBUG)
		// Commands to test the editor without input, see EditorDebugConsole
		std::unique_ptr<EditorDebugConsole> _debugConsole;
#endif

		/** @brief Puts how the level is seen and played into it, see @ref DocumentViewState */
		void StoreViewState() override;
		/** @brief Shows the level as it was seen when it was saved, once the level handler exists but before its view does */
		void ApplyViewState();
#if defined(WITH_ANGELSCRIPT)
		/** @brief Runs the script of the level for the sprites it gives the objects, if they were marked to be taken again */
		void UpdateScriptGraphics();
#endif

		// IDocumentFilesHost
		void PrepareForSave() override;
		void ReplaceDocument(LevelDocument&& document, StringView path, StringView levelName) override;
		void OnScriptChanged() override;
		void ReloadTileSet(StringView name) override;

		// IInputRouterHost
		Rectf GetViewRect() const override;
		void ClearSelection() override;
		void ChangeUiScale(std::int32_t direction) override;

		// IPlayTestHost
		std::uint32_t GetDocumentRevision() const override;
		IRootController& GetPlayTestRoot() override;
		std::weak_ptr<void> GetPlayTestHostReference() override;
		void OnPlayTestViewChanged() override;
		bool OnPlayTestReturned() override;
		void ExitEditor() override;

		/** @brief Creates all actions */
		void CreateActions();
		// The actions of CreateActions() by menu or area, called in the order the actions are added, which is the order
		// of the command palette and the one that decides which of two actions with the same key runs
		void CreateFileActions();
		void CreateEditActions();
		void CreateHelpActions();
		void CreateNavigationActions();
		void CreateEventActions();
		void CreateStampActions();
		void CreateToolActions();
		void CreateViewActions();
		void CreateLayerActions();
		void CreateLevelActions();
		void CreateLayerSelectActions();
		void CreatePlayActions();
		void CreatePlayTestDebugActions();
		void CreateReachabilityActions();
		void CreateScriptAndCompareActions();
		// Makes every action except those of the view and the help end what the pointer is in the middle of first
		void CancelInteractionsBeforeActions();
		/** @brief Creates the interface */
		void CreateInterface();
		/** @brief Loads the shown level again from the document, keeping the camera */
		void RebuildRuntime();
		// Tells the functions added by AddChangeObserver()
		void NotifyChangeObservers(const DocumentChanges& changes);
		// Gives the level being edited what is drawn among its layers
		void AttachSceneContent();
		/**
		 * @brief Shows the tiles the level overrides as it has them now, returns `false` if the level has to be loaded again
		 *
		 * Only the tiles that changed since are given to the shown level, with @p palette also those whose colors
		 * were taken from the palette when they were given, after it changed.
		 */
		bool ApplyTileOverrides(bool palette = false);
		/** @brief Remembers which tiles the shown level overrides, after it was loaded */
		void RememberAppliedOverrides();
		// Collects what the overrides of the level and the tiles it stores in itself show, sorted by the tile ID
		void CollectAppliedTiles(SmallVectorImpl<AppliedTile>& diffuse, SmallVectorImpl<AppliedTile>& masks);
		// Hashes the tiles of the tile sets the level stores in itself into _appliedLevelTiles, those that changed since
		// are given to the tile map unless it's nullptr, and added to changed, returns `false` if the level has to be
		// loaded again
		bool SyncLevelTiles(Tiles::TileMap* tileMap, bool palette, SmallVectorImpl<std::uint16_t>& changed);
		// Shows changes collected by OnDocumentChanged(), unless a transaction is in progress or the level is played
		void FlushDocumentChanges();
		// Brings everything that shows the level up to date with changes of it
		void ApplyDocumentChanges(const DocumentChanges& changes);
		// Makes a new history of the level being edited
		void CreateHistory();
		// Forgets the play-tests, their path and what depends on the level being edited, before another one is
		void ResetPlayState();
		// Plays the ambient sound of the event selected alone while events are edited, see EventPreviews::UpdateSound()
		void UpdateEventSound();
		// Gives the play-tests the debug toggles of the settings, see PlayTestDebugOptions
		void ApplyPlayTestDebugOptions();
		// Turns a debug toggle of play-tests on or off, told while the level is played
		void TogglePlayTestDebug(bool EditorSettings::PlayTestSettings::*option, StringView onMessage, StringView offMessage);
		// Goes through the speeds of the time of the level played, normal, half, a quarter
		void CyclePlayTestTimeScale();
		// Stops the time of the level played, or lets it go on
		void TogglePlayTestPause();
		// Moves the player of the level played to the pointer
		void TeleportPlayTestPlayer();
		// Returns the file of a level that follows the one being edited, as the game names it, or an empty string
		String FindNextLevelFile(StringView levelName);
		// Offers to edit or play the level that follows, once the level being edited was finished in a play-test
		void OfferNextLevel(const std::shared_ptr<LevelInitialization>& next, StringView levelName);
		// Sets the size of the interface by the settings and the system
		void UpdateUiScale();
		// Returns the size of the interface chosen by how large the system shows its own interface, as long as the panels fit
		static float GetAutoUiScale();
		// Returns the size offered nearest to a size, in steps of 25%
		static float RoundUiScale(float scale);
		/**
		 * @brief Draws the layer being edited so it can be painted on
		 *
		 * A sky or a tunnel is a picture made of the tiles of the layer, its tiles couldn't be told apart in it, and a
		 * reflection doesn't show its tiles at all, so they're drawn as plain tiles --- but not those of a reflection with
		 * an image of its own, which is shown as it is, as its tiles aren't any part of it, nor any of them while
		 * @ref EditorSettings::PreviewTexturedLayers is set. A layer with an automatic speed stays where its offset puts
		 * it. With @p restore the
		 * layer is drawn as the game draws it again whichever layer is edited. Layers are kept still as a whole too if
		 * the settings say so. If the layers of the document were moved since, but not those of the shown level yet,
		 * @p layerOrder tells where they went (see @ref DocumentChanges::GetLayerOrder()).
		 */
		void UpdateLayerOverride(bool restore = false, ArrayView<const std::int32_t> layerOrder = {});
		void UpdateTileLod();
		// Chooses the selected tiles again by their neighbors, see SmartTileModel
		void RetileSelection();
		// Fills the empty cells right of the one under the pointer with the tiles the levels have there the most
		void ContinueRow();
		// Cuts ground to the ground line chosen last into tiles of this level, so the collision follows it exactly
		void BakeGroundLine();
		/** @brief Returns `true` if a layer is drawn differently while it's edited, see @ref UpdateLayerOverride() */
		bool NeedsLayerOverride(const DocumentLayer& layer) const;
		/** @brief Returns the handler that gets the frames and the input */
		LevelHandler* GetActiveHandler() const;

		// Returns `true` if something is selected, that can be copied or deleted
		bool HasSelection() const;
		void CopySelection(bool cut);
		// Copies the selected tiles of the sprite layer with the events on them, or the selected events with the tiles under them
		void CopyTilesAndEvents();
		void DeleteSelection();
		void Paste();
		// Marks a corner of a block, or grabs it, over the tiles of the picker or of the layer being edited, see BlockGrab
		void GrabBlock(BlockGrab::Mode mode);
		void SelectAll();
		void AddLayer();
		void RemoveLayer();
		// Moves the events off the grid onto the tiles they're in, those whose tile has an event already stay off it
		void SnapOffGridEvents();
		// Selects all events of the grid of the kind of the first selected one
		void SelectEventsOfSameType();
		// Places the selected event on every tile of the sprite layer that shows the same tile as its own, and has no event
		void CopyEventToMatchingTiles();
		void MoveLayer(std::int32_t direction);
	};
}

#endif
