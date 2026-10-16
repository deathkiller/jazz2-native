#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../IStateHandler.h"
#include "../IRootController.h"
#include "EditorInitialization.h"
#include "Document/CommandHistory.h"
#include "Document/LevelDocument.h"
#include "Events/EventPreviews.h"
#include "Files/AutosaveService.h"
#include "Files/ContentTileSetLibrary.h"
#include "Files/EditorSettings.h"
#include "Input/ActionRegistry.h"
#include "Input/TouchGestures.h"
#include "Runtime/MaskOverlay.h"
#include "Smart/SmartTileModel.h"
#include "Smart/TileShapes.h"
#include "Runtime/TileLod.h"
#include "Runtime/PlayTransition.h"
#include "Runtime/PlayTestLevelHandler.h"
#if defined(WITH_MULTIPLAYER)
#	include "Runtime/PlayTestMpLevelHandler.h"
#endif
#include "Tools/EditorTool.h"
#include "Tools/EventTools.h"

#include <memory>

namespace Jazz2::Formats
{
	struct TileSetData;
}

namespace Jazz2::Editor
{
	class EditorLevelHandler;
	class EditorShell;

	/** @brief Where a play-test starts */
	enum class PlayStartMode : std::uint8_t
	{
		Resume,			/**< Where the last play-test was left, as it was if the level wasn't changed since */
		Here,			/**< At the pointer, or at the center of the view */
		FromStart,		/**< At the start of the level */
		Marker			/**< At the play-start marker, see @ref DocumentViewState::PlayStart */
	};

	/** @brief What the editor was used with last */
	enum class InputModality : std::uint8_t
	{
		KeyboardMouse,		/**< Keyboard and mouse */
		Gamepad,			/**< Gamepad, with a cursor of its own */
		Touch				/**< Fingers on a touch screen, see @ref TouchGestures */
	};

	/** @brief What the panel with the tiles and the events shows */
	enum class PickerPanel : std::uint8_t
	{
		Tiles,			/**< Tiles to paint with */
		Events			/**< Events to place */
	};

	class UiRadialMenu;
	class UiSurface;
	struct UiFileRoot;

	/**
		@brief The level editor

		The state handler of the editor. It owns the level being edited (see @ref LevelDocument) with its history,
		hosts the level handler that shows it (see @ref EditorLevelHandler) and the interface around it. The hosted
		handler gets the session as its root controller, so whatever it would do to the game as a whole --- going to
		another level or to the main menu, saving a state to resume --- is decided by the session.
	*/
	class EditorSession : public IStateHandler, public IRootController, public IToolHost
	{
		DEATH_RUNTIME_OBJECT(IStateHandler);

	public:
		/** @brief Creates a new instance */
		explicit EditorSession(IRootController* root);
		~EditorSession() override;

		/** @brief Opens a level, returns `false` if it can't be opened */
		bool Initialize(const EditorInitialization& editorInit);

		/** @brief Returns the history of the level being edited */
		CommandHistory& GetHistory() {
			return *_history;
		}
		/** @brief Returns the actions */
		ActionRegistry& GetActions() {
			return _actions;
		}
		/** @brief Returns the path of the shortcuts the user changed, `Jazz2.EditorHotkeys.json` in the configuration directory */
		static String GetHotkeysPath();
		/** @brief Returns the tools */
		ToolManager& GetTools() {
			return *_tools;
		}
		/** @brief Returns the level handler that shows the level, or `nullptr` */
		EditorLevelHandler* GetEditHandler() const {
			return _editHandler.get();
		}
		/** @brief Returns the surface of the interface */
		UiSurface* GetSurface() const override {
			return _surface.get();
		}
		/** @brief Returns the settings */
		EditorSettings& GetSettings() {
			return _settings;
		}
		/** @brief Returns what is known about the tile sets of the game */
		ContentTileSetLibrary& GetTileSetLibrary() {
			return _tileSets;
		}
		/** @brief Opens a level file in the editor, asking about unsaved changes first */
		void OpenFile(StringView path);
		/** @brief Returns the path of the level file, may be empty */
		StringView GetFilePath() const {
			return _filePath;
		}
		/** @brief Returns the name of the level as episode/level, may be empty */
		StringView GetLevelName() const {
			return _levelName;
		}
		/** @brief Returns the places levels are opened from or saved to */
		void GetFileRoots(bool forSaving, SmallVectorImpl<UiFileRoot>& roots);
		/** @brief Returns the places images are opened from or saved to, outside of the content of the game too */
		void GetImageRoots(bool forSaving, SmallVectorImpl<UiFileRoot>& roots);
		/** @brief Shows a dialog to save the default tile set as images, of its tiles and of their masks */
		void ShowExportTileSetImageDialog();
		/** @brief Returns the name of a level as episode/level from the path of its file */
		static String MakeLevelName(StringView path);

		/** @brief Starts playing the level */
		void StartPlayTest(PlayStartMode mode);
		/** @brief Stops playing the level and returns to editing, @p keep keeps the play-test to resume later */
		void StopPlayTest(bool keep);
		/** @brief Returns `true` while the level is played, or is about to be */
		bool IsPlaying() const {
			return (_mode != SessionMode::Edit);
		}
		/** @brief Returns `true` for the frame before the level is played */
		bool IsEnteringPlay() const {
			return (_mode == SessionMode::EnteringPlay);
		}
		/** @brief Returns the color the tile set of the level is known by, see @ref FindAccentColor() */
		Colorf GetAccentColor();
		/** @brief Returns a whole tile set with the pixels and masks of its tiles, read once, or `nullptr` if it can't be read */
		const Formats::TileSetData* GetTileSetData(StringView name);
		/** @brief Forgets what was read of a tile set, after its file was written */
		void InvalidateTileSet(StringView name);
		/** @brief Returns the path the player took in the last play-test */
		const PlayPath& GetLastPlayPath() const {
			return _lastPlayPath;
		}
		/** @brief Returns `true` if the panels are shown, while the level is edited and while the ring into the game grows over them */
		bool ArePanelsShown() const {
			return (_mode != SessionMode::Play || (_playTransition.IsActive() && _playTransition.HasInside()));
		}
		/** @brief Returns the transition into the level being played */
		PlayTransition& GetPlayTransition() {
			return _playTransition;
		}
		/** @brief Returns `true` if the path of the last play-test is shown */
		bool IsPlayPathShown() const {
			return _showPlayPath;
		}
		/** @brief Returns `true` if the play-start marker is placed, with its position in the level in pixels */
		bool GetPlayStart(Vector2i& pos) const {
			pos = _playStart;
			return _hasPlayStart;
		}
		/** @brief Places the play-start marker at a position in the level in pixels, or removes it, see @ref PlayStartMode::Marker */
		void SetPlayStart(bool placed, Vector2i pos = {});
		/**
		 * @brief Returns how far each tile of the sprite layer is from the start, for the player, row by row
		 *
		 * -1 where the player can't get. Empty while it's not shown, see the action `view.reachability`.
		 */
		ArrayView<const std::int32_t> GetReachability() const {
			return _reachability;
		}
		/**
		 * @brief Finds where the player can get from the start of the play character, as the level is loaded now
		 *
		 * Returns `false` if it can't be found out, as without the start or without multiplayer compiled in, whose
		 * route tracer it uses.
		 */
		bool ComputeReachability(SmallVectorImpl<std::int32_t>& distances);

		/** @brief Changes the layer being edited */
		void SetCurrentLayer(std::int32_t layer);
		/** @brief Returns `true` if the grid is shown */
		bool IsGridShown() const {
			return _showGrid;
		}
		/** @brief Returns `true` if events are shown over the level */
		bool AreEventsShown() const {
			return _showEvents;
		}
		/** @brief Returns how collision masks are shown, 0 not at all, 1 over the tiles, 2 instead of the tiles */
		std::int32_t GetMaskMode() const {
			return _maskMode;
		}
		/** @brief Returns what shows the collision masks over the level */
		MaskOverlay& GetMaskOverlay() {
			return _maskOverlay;
		}
		/** @brief Returns what the panel with the tiles and the events shows */
		PickerPanel GetPickerPanel() const {
			return _pickerPanel;
		}
		/** @brief Changes what the panel with the tiles and the events shows */
		void SetPickerPanel(PickerPanel panel) {
			_pickerPanel = panel;
		}
		/** @brief Chooses the events the event tool places, and the tool */
		void SetEventBrush(EventStamp&& brush);
		/** @brief Lets the next event chosen in the list go to a callback, instead of becoming the one to place */
		void PickEventFromList(Function<void(EventType)>&& callback);
		/** @brief Returns `true` if the next event chosen in the list goes to a callback */
		bool IsPickingEvent() const {
			return (bool)_eventPick;
		}
		/** @brief Called when an event is chosen in the list */
		void OnEventChosen(EventType type);
		/** @brief Returns what the editor was used with last */
		InputModality GetInputModality() const {
			return _inputModality;
		}
		/** @brief Returns the cursor moved by a gamepad, in pixels of the screen */
		Vector2f GetGamepadCursor() const {
			return _gamepadCursor;
		}
		/** @brief Returns `true` if the editing actions work on events, not on tiles */
		bool IsEditingEvents() const;
		/** @brief Returns `true` if something is selected, that can be copied or deleted */
		bool HasSelection() const;
		/** @brief Returns the pointer over the view, `false` if it's not over the view */
		bool GetHoverPointer(ToolPointer& pointer);

		/** @brief Undoes the last change */
		bool Undo();
		/** @brief Redoes the last undone change */
		bool Redo();
		/** @brief Shows changes of the level being edited */
		void OnDocumentChanged(const DocumentChanges& changes);
		/** @brief Saves the level to a file, with @p exportOnly without the data only the editor uses */
		bool SaveTo(StringView path, bool exportOnly);
		/** @brief Toggles the editor state of a layer, such as whether it's hidden */
		void ToggleLayerFlag(std::int32_t layer, std::uint8_t flag);
		/** @brief Moves a layer to another position in the list of layers, as an undoable step */
		void MoveLayerTo(std::int32_t from, std::int32_t to);

		// IToolHost
		LevelDocument& GetDocument() override;
		std::int32_t GetCurrentLayer() const override;
		bool IsLayerEditable(std::int32_t layer) const override;
		LayerView GetLayerView(std::int32_t layer) override;
		TileStamp& GetStamp() override;
		void SetStamp(TileStamp&& stamp) override;
		TileSelection& GetSelection() override;
		void Execute(std::unique_ptr<EditCommand> command, bool allowMerge = false) override;
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
		enum class SessionMode : std::uint8_t
		{
			Edit,
			EnteringPlay,
			Play
		};

		IRootController* _root;
		ContentTileSetLibrary _tileSets;
		LevelDocument _document;
		std::unique_ptr<CommandHistory> _history;
		std::shared_ptr<EditorLevelHandler> _editHandler;
		std::unique_ptr<UiSurface> _surface;
		EditorShell* _shell;
		ActionRegistry _actions;
		EditorSettings _settings;
		AutosaveService _autosave;
		std::unique_ptr<ToolManager> _tools;
		String _levelName;
		String _filePath;

		SessionMode _mode;
		// Alone or in local splitscreen, see PlayTestLevelHandler and PlayTestMpLevelHandler
		std::shared_ptr<IPlayTest> _playHandler;
		// A play-test that was left, kept only for its last frame while the ring back into the editor is shown
		std::shared_ptr<IPlayTest> _closingPlayHandler;
		std::uint32_t _playRevision;
		PlayTestOptions _playOptions;
		// How the play-test kept was started
		PlayTestOptions _playedOptions;
		bool _resumePlay;
		Vector2f _editCameraCenter;
		float _editCameraScale;
		PlayPath _lastPlayPath;
		PlayTransition _playTransition;
		bool _showPlayPath;
		// See SetPlayStart()
		bool _hasPlayStart = false;
		Vector2i _playStart;
		// The music opens up when the level is played and is muffled again in the editor, over a while, see UpdateMusicFade()
		float _musicFadeFrom = 1.0f;
		float _musicFadeTo = 1.0f;
		float _musicFadeTime = -1.0f;
		// The sprite layer and the events when the kept play-test was left, so it's known what was edited since
		SmallVector<TileRef, 0> _suspendedSpriteTiles;
		Vector2i _suspendedSpriteSize;
		SmallVector<DocumentEvent, 0> _suspendedEvents;
		SmallVector<Formats::OffGridEventRecord, 0> _suspendedOffGridEvents;
		// The kept play-test, after the level was edited, until the new one took what it can from it
		std::shared_ptr<IPlayTest> _restoreFrom;
		// Where the player was, where memory is too short to keep the play-test
		Vector2f _resumePosition;
		bool _hasResumePosition = false;
		// See GetReachability(), empty while it's not shown
		SmallVector<std::int32_t, 0> _reachability;
		bool _enteringPlayFrameShown = false;
		// Export marks the level so the editor doesn't open it, see LevelFlags::NotEditable
		bool _exportNotEditable = false;
		// Tile sets read whole, for the pixels of their tiles, see GetTileSetData()
		struct CachedTileSet
		{
			String Name;
			std::unique_ptr<Formats::TileSetData> Data;
			// Bytes it was read from, for a tile set the level stores in itself, it's read again when they change
			const std::uint8_t* Bytes = nullptr;
			std::size_t ByteCount = 0;
		};
		SmallVector<CachedTileSet, 0> _tileSetData;

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
		MaskOverlay _maskOverlay;
		// How the tiles of the default tile set fit together, learned from levels once it's asked for
		SmartTileModel _smartTiles;
		bool _smartTilesUsed = false;
		TileShapes _tileShapes;
		// Draws the layers as images from far away
		std::unique_ptr<TileLod> _tileLod;
		std::int32_t _maskMode;
		// Layer drawn differently while it's edited, see UpdateLayerOverride()
		std::int32_t _overriddenLayer = -1;
		// Tiles the shown level overrides, see ApplyTileOverrides()
		SmallVector<std::uint16_t, 0> _appliedDiffuseOverrides;
		SmallVector<std::uint16_t, 0> _appliedMaskOverrides;
		bool _showEvents;
		PickerPanel _pickerPanel;

		// Last position of the pointer in pixels of the drawable, which may differ from the window on a high-DPI display
		Vector2f _pointerPos;
		bool _pointerKnown;
		bool _panning;
		bool _toolPointerDown;
		MouseButton _toolButton;
		std::int32_t _modifiers;
		// An action is run by its key, not chosen from a menu or by a button
		bool _handlingShortcut = false;

		// What the editor was used with last, and the state of the gamepad
		InputModality _inputModality = InputModality::KeyboardMouse;
		Vector2f _mousePos;
		Vector2f _gamepadCursor;
		std::uint32_t _gamepadButtons = 0;
		float _gamepadRepeat = 0.0f;
		// How long Y has been held, held long it opens the menu of tools around the cursor
		float _gamepadYTime = -1.0f;
		UiRadialMenu* _radialMenu = nullptr;
		TouchGestures _touch;
		// Frames the sticks have been held for, they move faster then
		float _gamepadCursorHold = 0.0f;
		float _gamepadPanHold = 0.0f;
		bool _gamepadPicking = false;

		/** @brief Presses the pointer at a position on the screen, by the mouse or a gamepad */
		void PointerDown(Vector2f pos, MouseButton button);
		/** @brief Releases the pointer at a position on the screen */
		void PointerUp(Vector2f pos, MouseButton button);
		/** @brief Moves the pointer to a position on the screen */
		void PointerMove(Vector2f pos);
		/** @brief Switches what the editor is used with, the hints follow */
		void SetInputModality(InputModality modality);
		/** @brief Reads the gamepad and uses the editor by it */
		void UpdateGamepad(float timeMult);
		/** @brief Sets up what the fingers on a touch screen do */
		void SetupTouchGestures();
		/** @brief Shows the menu of tools in a ring around a position on the screen */
		void ShowToolMenu(Vector2f pos);

#if defined(DEATH_DEBUG)
		// Commands given on the command line by `/console <command>`, run once the view exists
		SmallVector<String, 0> _debugStartupCommands;
		std::int32_t _debugFrame = 0;
		std::int32_t _debugWaitFrames = 0;

		/** @brief Runs a debug command, used to test the editor without input */
		bool ExecuteDebugCommand(StringView line);
#endif

		/** @brief Puts how the level is seen and played into it, see @ref DocumentViewState */
		void StoreViewState();
		/** @brief Shows the level as it was seen when it was saved, once the level handler exists but before its view does */
		void ApplyViewState();
		/** @brief Starts muffling the music, or opening it up, over a while */
		void StartMusicFade(float from, float to);
		/** @brief Advances the fade of the music */
		void UpdateMusicFade(float timeMult);
		/** @brief Remembers the sprite layer and the events when a play-test is kept, see @ref RestorePlayState() */
		void RememberSuspendedLevel();
		/**
		 * @brief Takes what the player had and what was collected or destroyed from a play-test into a new one
		 *
		 * Cells of the sprite layer and events edited since the old play-test was left start as new, the rest of
		 * the level is as it was left, if the sprite layer has the same size.
		 */
		void RestorePlayState(IPlayTest& from, IPlayTest& to);

		/** @brief Creates all actions */
		void CreateActions();
		/** @brief Creates the interface */
		void CreateInterface();
		/** @brief Loads the shown level again from the document, keeping the camera */
		void RebuildRuntime();
		// Gives the level being edited what is drawn among its layers
		void AttachSceneContent();
		/** @brief Shows the tiles the level overrides as it has them now, returns `false` if the level has to be loaded again */
		bool ApplyTileOverrides();
		/** @brief Remembers which tiles the shown level overrides, after it was loaded */
		void RememberAppliedOverrides();
		/**
		 * @brief Draws the layer being edited so it can be painted on
		 *
		 * A sky or a circle is a picture made of the tiles of the layer, its tiles couldn't be told apart in it, so it's
		 * drawn as plain tiles. A layer with an automatic speed stays where its offset puts it. With @p restore the
		 * layer is drawn as the game draws it again whichever layer is edited. Layers are kept still as a whole too if
		 * the settings say so.
		 */
		void UpdateLayerOverride(bool restore = false);
		void UpdateTileLod();
		void SetPlayTestExit(IPlayTest& handler);
		// Chooses the selected tiles again by their neighbors, see SmartTileModel
		void RetileSelection();
		// Fills the empty cells right of the one under the pointer with the tiles the levels have there the most
		void ContinueRow();
		// Cuts ground to the ground line chosen last into tiles of this level, so the collision follows it exactly
		void BakeGroundLine();
		// Describes the events of event packs, when asked, and of the script of the level, see EventCatalog::LoadExtensions()
		void LoadEventExtensions(bool packs);
		/** @brief Returns `true` if a layer is drawn differently while it's edited, see @ref UpdateLayerOverride() */
		static bool NeedsLayerOverride(const DocumentLayer& layer);
		/** @brief Switches to the play-test once the view flew into the level */
		void FinishStartPlayTest();
		/** @brief Returns to editing after the level was left, with a message */
		void ReturnFromPlayTest(StringView message);
		/** @brief Returns the handler that gets the frames and the input */
		LevelHandler* GetActiveHandler() const;
		/** @brief Makes a pointer for a tool from a position on the screen */
		ToolPointer MakeToolPointer(Vector2f pos, MouseButton button);
		/** @brief Returns the view of the level on the screen, what the panels don't cover */
		Rectf GetViewRect() const;

		void CopySelection(bool cut);
		void DeleteSelection();
		void Paste();
		void SelectAll();
		void ClearSelection();
		void AddLayer();
		void RemoveLayer();
		// Moves the events off the grid onto the tiles they're in, those whose tile has an event already stay off it
		void SnapOffGridEvents();
		// Selects all events of the grid of the kind of the first selected one
		void SelectEventsOfSameType();
		// Places the selected event on every tile of the sprite layer that shows the same tile as its own, and has no event
		void CopyEventToMatchingTiles();
		void MoveLayer(std::int32_t direction);
		void Save();
		void ExitEditor();

		/** @brief Asks whether to save unsaved changes, then proceeds unless cancelled */
		void ConfirmDiscard(Function<void()>&& proceed);
		/** @brief Shows a dialog to open a level */
		void ShowOpenDialog();
		/** @brief Shows a dialog to save the level as a new file, or to export it */
		void ShowSaveAsDialog(bool exportOnly, Function<void()>&& onSaved = {});
		/** @brief Saves the level to a file chosen by the user */
		void SaveAsConfirmed(StringView path, bool exportOnly, const std::shared_ptr<Function<void()>>& onSaved);
		/** @brief Checks the level and shows a dialog to export it */
		void ShowExportDialog();
		/** @brief Collects problems that don't keep the level from being saved, but from being played as intended */
		void CollectExportWarnings(SmallVectorImpl<String>& warnings);
		/** @brief Shows a dialog to make a new level */
		void ShowNewLevelDialog();
		/** @brief Replaces the level being edited */
		void ReplaceDocument(LevelDocument&& document, StringView path, StringView levelName);
		/** @brief Returns `true` if the level can be saved to its file */
		bool CanSaveInPlace() const;
		/** @brief Makes the interface larger or smaller by a step, or chooses its size by the window */
		void ChangeUiScale(std::int32_t direction);
		/** @brief Offers to restore a recovery copy of the level, returns `false` if there is none */
		bool OfferRecovery();
		/** @brief Returns a tile set to make a new level with */
		String FindDefaultTileSet();

		/** @brief Converts a position of the mouse from the window to the drawable */
		static Vector2f ToDrawable(std::int32_t x, std::int32_t y);
		/** @brief Updates the title of the window */
		void UpdateWindowTitle();
	};
}

#endif
