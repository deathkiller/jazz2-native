#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "ToolHost.h"
#include "../Document/DocumentChanges.h"

#include <Containers/Function.h>
#include <Containers/SmallVector.h>

#include <memory>

namespace Jazz2
{
	enum class EventType : std::uint16_t;
}

namespace Jazz2::Formats
{
	struct TileSetData;
}

namespace Jazz2::Editor
{
#ifndef DOXYGEN_GENERATING_OUTPUT
	using Death::Containers::Function;
	using Death::Containers::SmallVectorImpl;
#endif

	class ActionRegistry;
	class CommandHistory;
	class ContentTileSetLibrary;
	class EditorLevelHandler;
	class PlayPathHistory;
	class TileSetDataCache;
	class ToolManager;
	struct EditorSettings;
	struct ToolPointer;
	struct UiFileRoot;
	enum class SnapshotParts : std::uint32_t;

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

	/**
		@brief What the panels and the dialogs of the level editor need from it

		Everything tools get (see @ref IToolHost), and the level being edited as a whole --- its file, its history and
		its tile sets --- with the settings, the actions and the tools of the editor. Implemented by @ref EditorSession.
	*/
	class IEditorContext : public IToolHost
	{
	public:
		/** @brief Function told about changes of the level, see @ref AddChangeObserver() */
		using ChangeObserver = Function<void(const DocumentChanges&)>;

		~IEditorContext() override = default;

		/** @brief Returns the path of the shortcuts the user changed, `Jazz2.EditorHotkeys.json` in the configuration directory */
		static String GetHotkeysPath();
		/** @brief Returns the name of a level as episode/level from the path of its file */
		static String MakeLevelName(StringView path);
		/**
		 * @brief Returns whether another editor holds the lock of a level file
		 *
		 * A lock written less than the lock timeout ago counts, unless it's of this editor or of an editor of this
		 * computer whose process has ended.
		 */
		static bool IsLevelLockedElsewhere(StringView levelPath);

		/** @brief Returns the history of the level being edited */
		virtual CommandHistory& GetHistory() = 0;
		/** @brief Returns the actions */
		virtual ActionRegistry& GetActions() = 0;
		/** @brief Returns the tools */
		virtual ToolManager& GetTools() = 0;
		/** @brief Returns the settings */
		virtual EditorSettings& GetSettings() = 0;
		/** @brief Returns the level handler that shows the level, or `nullptr` */
		virtual EditorLevelHandler* GetEditHandler() const = 0;
		/** @brief Returns what the editor was used with last */
		virtual InputModality GetInputModality() const = 0;

		/** @brief Returns what is known about the tile sets of the game */
		virtual ContentTileSetLibrary& GetTileSetLibrary() = 0;
		/** @brief Returns a whole tile set with the pixels and masks of its tiles, read once, or `nullptr` if it can't be read */
		virtual const Formats::TileSetData* GetTileSetData(StringView name) = 0;
		/** @brief Returns the tile sets read whole for the level being edited, see @ref GetTileSetData() */
		virtual TileSetDataCache& GetTileSetDataCache() = 0;
		/** @brief Forgets what was read of a tile set, after its file was written */
		virtual void InvalidateTileSet(StringView name) = 0;

		/** @brief Returns the path of the level file, may be empty */
		virtual StringView GetFilePath() const = 0;
		/** @brief Returns the name of the level as episode/level, may be empty */
		virtual StringView GetLevelName() const = 0;
		/** @brief Returns the places levels are opened from or saved to */
		virtual void GetFileRoots(bool forSaving, SmallVectorImpl<UiFileRoot>& roots) = 0;
		/** @brief Returns the places images are opened from or saved to, outside of the content of the game too */
		virtual void GetImageRoots(bool forSaving, SmallVectorImpl<UiFileRoot>& roots) = 0;
		/** @brief Opens a level file in the editor, asking about unsaved changes first */
		virtual void OpenFile(StringView path) = 0;
		/** @brief Restores a recovery copy into the editor, asking about unsaved changes first, see @ref EditorSession::OpenRecoveryCopy() */
		virtual void OpenRecoveryCopy(StringView recoveryPath) = 0;
		/** @brief Saves the level into another file and edits that one from then on, see @ref EditorSession::SaveAsFile() */
		virtual bool SaveAsFile(StringView path, bool move) = 0;
		/** @brief Returns `true` if the level can be saved to its file */
		virtual bool CanSaveInPlace() const = 0;
		/** @brief Shows a dialog to save the level as a new file, or to export it, @p onSaved is called once it's saved */
		virtual void ShowSaveAsDialog(bool exportOnly, Function<void()>&& onSaved = {}) = 0;
		/** @brief Tells that a level file was moved or renamed outside of Save As, its place among the recent files goes along */
		virtual void NoteLevelFileMoved(StringView from, StringView to) = 0;

		/** @brief Undoes the last change */
		virtual bool Undo() = 0;
		/** @brief Redoes the last undone change */
		virtual bool Redo() = 0;
		/**
		 * @brief Tells a function about every change of the level being edited, all of it changes when another level is opened
		 *
		 * Only a weak reference is kept, the function is called as long as its owner keeps it.
		 */
		virtual void AddChangeObserver(const std::shared_ptr<ChangeObserver>& observer) = 0;
		/** @brief Makes a change directly on the level and records it as an undoable step, see @ref EditorSession::RecordSnapshotStep() */
		virtual bool RecordSnapshotStep(String name, SnapshotParts parts, Function<bool(LevelDocument&)>&& change,
			DocumentChangeKind kinds = DocumentChangeKind::None, std::int32_t layer = -1) = 0;
		/** @brief Changes the editor state of the level, which isn't a change to undo, see @ref EditorSession::SetEditorState() */
		virtual void SetEditorState(Function<void(LevelDocument&)>&& change) = 0;

		/** @brief Changes the layer being edited */
		virtual void SetCurrentLayer(std::int32_t layer) = 0;
		/** @brief Toggles the editor state of a layer, such as whether it's hidden */
		virtual void ToggleLayerFlag(std::int32_t layer, std::uint8_t flag) = 0;
		/** @brief Moves a layer to another position in the list of layers, as an undoable step */
		virtual void MoveLayerTo(std::int32_t from, std::int32_t to) = 0;
		/** @brief Returns `true` if the editing actions work on events, not on tiles */
		virtual bool IsEditingEvents() const = 0;
		/** @brief Returns the pointer over the view, `false` if it's not over the view */
		virtual bool GetHoverPointer(ToolPointer& pointer) = 0;
		/** @brief Returns `true` if events are shown over the level */
		virtual bool AreEventsShown() const = 0;

		/** @brief Returns what the panel with the tiles and the events shows */
		virtual PickerPanel GetPickerPanel() const = 0;
		/** @brief Changes what the panel with the tiles and the events shows */
		virtual void SetPickerPanel(PickerPanel panel) = 0;
		/** @brief Lets the next event chosen in the list go to a callback, instead of becoming the one to place */
		virtual void PickEventFromList(Function<void(EventType)>&& callback) = 0;
		/** @brief Returns `true` if the next event chosen in the list goes to a callback */
		virtual bool IsPickingEvent() const = 0;
		/** @brief Lets the next event chosen in the list become the one to place again, see @ref PickEventFromList() */
		virtual void CancelEventPick() = 0;
		/** @brief Called when an event is chosen in the list */
		virtual void OnEventChosen(EventType type) = 0;

		/** @brief Returns `true` while the level is played, or is about to be */
		virtual bool IsPlaying() const = 0;
		/** @brief Returns `true` if the panels are shown, while the level is edited and while the ring into the game grows over them */
		virtual bool ArePanelsShown() const = 0;
		/** @brief Returns the paths of the last play-tests, kept in the level */
		virtual const PlayPathHistory& GetPlayPathHistory() const = 0;
		/** @brief Finds where the player can get from the start of the play character, see @ref EditorSession::ComputeReachability() */
		virtual bool ComputeReachability(SmallVectorImpl<std::int32_t>& distances) = 0;
		/** @brief Loads the level again from the document, so the events in the game are as they are now, see @ref ComputeReachability() */
		virtual void ReloadRuntime() = 0;
	};
}

#endif
