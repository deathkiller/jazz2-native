#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../Files/AutosaveService.h"

#include <Containers/Function.h>
#include <Containers/SmallVector.h>
#include <Containers/String.h>

#include <memory>

namespace Jazz2
{
	class IRootController;
}

namespace Jazz2::Editor
{
#ifndef DOXYGEN_GENERATING_OUTPUT
	using Death::Containers::Function;
	using Death::Containers::SmallVector;
	using Death::Containers::SmallVectorImpl;
	using Death::Containers::String;
	using Death::Containers::StringView;
#endif

	class IEditorContext;
	class LevelDocument;
	struct UiFileRoot;

	/**
		@brief What @ref DocumentFiles needs of the level editor beyond @ref IEditorContext
	*/
	class IDocumentFilesHost
	{
	public:
		virtual ~IDocumentFilesHost() = default;

		/** @brief Gets the level ready to be written, what the pointer is in the middle of ends and how it's seen goes into it */
		virtual void PrepareForSave() = 0;
		/** @brief Puts how the level is seen and played into it, before a recovery copy is written */
		virtual void StoreViewState() = 0;
		/** @brief Replaces the level being edited, which is edited as @p path from then on, may be empty */
		virtual void ReplaceDocument(LevelDocument&& document, StringView path, StringView levelName) = 0;
		/** @brief Called when the script of the level changed on disk, a play-test kept to resume runs the old one */
		virtual void OnScriptChanged() = 0;
		/** @brief Loads a tile set of the level again, after another program changed it */
		virtual void ReloadTileSet(StringView name) = 0;
	};

	/**
		@brief The file of the level being edited and the files around it

		Knows where the level is kept and under which name the game knows it, makes new levels, opens, saves and exports
		them, and asks about unsaved changes. Owns the recovery copy of the unsaved changes (see @ref AutosaveService),
		the lock against two editors editing the same file, the script next to the file with the events it describes,
		and watches the script and the tile sets of the level for changes made by other programs. Shows the file in the
		title of the window too.
	*/
	class DocumentFiles
	{
	public:
		/** @brief Creates the files of the level of an editor, @p root opens another level or leaves the editor */
		DocumentFiles(IEditorContext& context, IDocumentFilesHost& host, IRootController& root);
		~DocumentFiles();

		DocumentFiles(const DocumentFiles&) = delete;
		DocumentFiles& operator=(const DocumentFiles&) = delete;

		/** @brief Returns the path of the level file, may be empty */
		StringView GetFilePath() const {
			return _filePath;
		}
		/** @brief Returns the name of the level as episode/level, may be empty */
		StringView GetLevelName() const {
			return _levelName;
		}
		/** @brief Sets the file the level is edited as, and its name, the recovery copy belongs to it then */
		void SetFile(StringView path, StringView levelName);
		/**
		 * @brief Sets the file another level replacing the one edited until now is edited as
		 *
		 * The recovery copy kept without a choice belongs to the file edited until now, as in @ref OfferRecovery().
		 */
		void OnDocumentReplaced(StringView path, StringView levelName);
		/** @brief Returns `true` if the level can be saved to its file */
		bool CanSaveInPlace() const;
		/** @brief Returns the places levels are opened from or saved to */
		void GetFileRoots(bool forSaving, SmallVectorImpl<UiFileRoot>& roots);
		/** @brief Returns the places images are opened from or saved to, outside of the content of the game too */
		void GetImageRoots(bool forSaving, SmallVectorImpl<UiFileRoot>& roots);
		/** @brief Returns a tile set to make a new level with */
		String FindDefaultTileSet();

		/** @brief Opens a level file in a new editor, asking about unsaved changes first */
		void OpenFile(StringView path);
		/** @brief Restores a recovery copy into the editor, asking about unsaved changes first, see @ref RestoreRecoveryCopy() */
		void OpenRecoveryCopy(StringView recoveryPath);
		/**
		 * @brief Replaces the level being edited by a recovery copy, with the history of its changes
		 *
		 * The level is then edited as @p filePath, which may be empty, and is unsaved. Returns `false` if the copy
		 * can't be read.
		 */
		bool RestoreRecoveryCopy(StringView recoveryPath, StringView filePath, StringView levelName);
		/** @brief Offers to restore a recovery copy of the level, returns `false` if there is none */
		bool OfferRecovery();
		/** @brief Asks whether to save unsaved changes, then proceeds unless cancelled */
		void ConfirmDiscard(Function<void()>&& proceed);
		/** @brief Leaves the editor for the main menu, asking about unsaved changes first */
		void ExitEditor();
		/** @brief Shows a dialog to open a level */
		void ShowOpenDialog();
		/** @brief Shows a dialog to make a new level */
		void ShowNewLevelDialog();

		/** @brief Saves the level to its file, or as a new file if it can't be saved there */
		void Save();
		/** @brief Saves the level to a file, with @p exportOnly without the data only the editor uses */
		bool SaveTo(StringView path, bool exportOnly);
		/**
		 * @brief Saves the level into another file and edits that one from then on, as Save As does
		 *
		 * The script of the level goes along. With @p move, the file the level was in is removed, with its script and
		 * its place among the recent files --- unless the level can't be saved where it is (see @ref CanSaveInPlace()),
		 * as a level of the cache, it's only copied then. Returns `false` if the level couldn't be saved, nothing is
		 * changed then.
		 */
		bool SaveAsFile(StringView path, bool move);
		/** @brief Shows a dialog to save the level as a new file, or to export it, @p onSaved is called once it's saved */
		void ShowSaveAsDialog(bool exportOnly, Function<void()>&& onSaved = {});
		/** @brief Checks the level and shows a dialog to export it */
		void ShowExportDialog();
		/** @brief Tells that a level file was moved or renamed outside of Save As, its place among the recent files goes along */
		void NoteLevelFileMoved(StringView from, StringView to);
		/** @brief Sets whether the next export is marked so the editor doesn't open it, `LevelFlags::NotEditable` */
		void SetExportNotEditable(bool value) {
			_exportNotEditable = value;
		}

		/** @brief Writes the recovery copy when it's time to, unless the copy kept from before wasn't chosen about */
		void UpdateAutosave(float timeMult);
		/** @brief Writes the recovery copy now, before the application may be ended, unless the copy kept from before wasn't chosen about */
		void SaveRecoveryCopyNow();
		/** @brief Returns what keeps the recovery copy */
		AutosaveService& GetAutosave() {
			return _autosave;
		}

		/** @brief Returns the path of the script of the level, next to its file, empty if it wasn't saved yet */
		String GetScriptPath() const;
		/**
		 * @brief Opens the script of the level in the program of `EditorSettings::ScriptEditor` or the one the system has for it
		 *
		 * A script that doesn't exist yet is made from a template first.
		 */
		void EditScript();
		/** @brief Describes the events of event packs, with @p packs, and of the script of the level, see @ref EventCatalog::LoadExtensions() */
		void LoadEventExtensions(bool packs);
		/** @brief Tells how many descriptions of events from outside of the game couldn't be read, if any */
		void ShowEventExtensionErrors();

		/** @brief Remembers when the script and the tile sets of the level were changed on disk, see @ref UpdateFileWatches() */
		void WatchFiles();
		/**
		 * @brief Reads the script again once it changed, offers to load a tile set again once it changed, keeps the lock fresh
		 *
		 * Returns `true` in the frames the files are looked at, once a second.
		 */
		bool UpdateFileWatches(float timeMult);
		/** @brief Tells that the editor itself wrote a tile set, it isn't asked about */
		void NoteTileSetWritten(StringView name);
		/**
		 * @brief Takes the lock of the level file against two editors editing it, `<file>.lock` next to it
		 *
		 * The lock of the previous file is released. It's written again every half a minute, a level that seems to be
		 * open in another editor is asked about.
		 */
		void AcquireLock();
		/** @brief Releases the lock of the level file, if it's the lock of this editor */
		void ReleaseLock();

		/** @brief Updates the title of the window, it's made again only when the name, the file or the unsaved changes changed, or with @p force (after something else set it, as a play-test) */
		void UpdateWindowTitle(bool force = false);

	private:
		struct WatchedTileSet
		{
			String Name;
			String Path;
			std::int64_t Time = 0;
		};

		IEditorContext& _context;
		IDocumentFilesHost& _host;
		IRootController& _root;
		String _levelName;
		String _filePath;
		AutosaveService _autosave;
		// Whether the offer to restore unsaved changes was closed without a choice, the copy isn't written over then
		bool _recoveryKept = false;
		// Export marks the level so the editor doesn't open it, see LevelFlags::NotEditable
		bool _exportNotEditable = false;
		// Files of the level watched for changes on disk, see UpdateFileWatches()
		SmallVector<WatchedTileSet, 2> _watchedTileSets;
		std::int64_t _watchedScriptTime = 0;
		float _watchTime = 0.0f;
		// See AcquireLock()
		String _lockPath;
		String _lockId;
		float _lockRefreshTime = 0.0f;
		// See UpdateWindowTitle(), with what it was made of, so it's made again only when one of them changes
		String _windowTitle;
		String _windowTitleName;
		String _windowTitlePath;
		bool _windowTitleDirty = false;

		// Saves the level to a file chosen in the dialog of Save As or Export
		void SaveAsConfirmed(StringView path, bool exportOnly, const std::shared_ptr<Function<void()>>& onSaved);
		// Collects problems that don't keep the level from being saved, but from being played as intended
		void CollectExportWarnings(SmallVectorImpl<String>& warnings);
		// Writes the lock again, a folder that can't be written to has no lock
		void WriteLock();
	};
}

#endif
