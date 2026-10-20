#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "LevelDocument.h"
#include "DocumentChanges.h"

#include <Base/TypeInfo.h>
#include <Containers/Function.h>
#include <Containers/SmallVector.h>
#include <Containers/String.h>

#include <memory>

namespace Jazz2::Editor
{
	/**
		@brief Undoable change of a level being edited
	*/
	class EditCommand
	{
		DEATH_RUNTIME_OBJECT();

	public:
		virtual ~EditCommand() = default;

		/** @brief Applies the change to the document, again after it was undone */
		virtual void Do(LevelDocument& document, DocumentChanges& changes) = 0;
		/** @brief Reverts the change */
		virtual void Undo(LevelDocument& document, DocumentChanges& changes) = 0;

		/**
		 * @brief Tries to take over a change that follows this one, which was already applied
		 *
		 * Lets a continuous change, such as dragging a slider, be undone in a single step. Returns `true` if
		 * @p next was taken over, it's then dropped.
		 */
		virtual bool TryMerge(EditCommand& next) {
			return false;
		}
		/**
		 * @brief Returns `true` if @ref TryMerge() would take over @p next, without taking it over
		 *
		 * A transaction is merged only if all of its changes can be, it asks each of them first. A command that
		 * merges has to say the same here as its @ref TryMerge() does.
		 */
		virtual bool CanMerge(EditCommand& next) {
			return false;
		}

		/** @brief Returns the name shown in the history */
		virtual String GetName() const = 0;
		/** @brief Returns the memory the command takes */
		virtual std::size_t GetMemoryUsage() const = 0;
	};

	/**
		@brief Undo and redo history of a level being edited

		Every change of the document goes through the history. A change is either executed by it, or ---
		when it was already applied, as a stroke is shown while it's being drawn --- just recorded. Changes made
		between @ref BeginTransaction() and @ref EndTransaction() are undone as one step.

		Each state of the document has its own revision, so the document can tell whether it's back where it was
		saved. When the commands take more memory than allowed, the oldest ones are forgotten.
	*/
	class CommandHistory
	{
	public:
		/** @brief Creates the history of a document */
		explicit CommandHistory(LevelDocument& document, std::size_t memoryBudget);
		~CommandHistory();

		CommandHistory(const CommandHistory&) = delete;
		CommandHistory& operator=(const CommandHistory&) = delete;

		/** @brief Returns the document */
		LevelDocument& GetDocument() {
			return _document;
		}

		/**
		 * @brief Applies a change and records it
		 *
		 * With @p allowMerge the change may be merged into the previous one (see @ref EditCommand::TryMerge()),
		 * unless @ref BreakMerge() was called since.
		 */
		void Execute(std::unique_ptr<EditCommand> command, DocumentChanges& changes, bool allowMerge = false);
		/** @brief Records a change that was already applied to the document */
		void Record(std::unique_ptr<EditCommand> command, bool allowMerge = false);
		/**
		 * @brief Makes the next change a step of its own, even if it could be merged into the previous one
		 *
		 * Called where a continuous change ends --- a value is no longer dragged, a text is committed --- or where
		 * a new one starts, so two separate changes of the same value aren't undone at once. Undo and redo break
		 * merging too.
		 */
		void BreakMerge() {
			_mergeBroken = true;
		}

		/** @brief Starts a transaction, all changes until it ends are undone as one step, transactions can be nested */
		void BeginTransaction(String name);
		/** @brief Ends a transaction */
		void EndTransaction();
		/** @brief Ends a transaction and reverts all its changes */
		void CancelTransaction(DocumentChanges& changes);
		/** @brief Returns `true` if a transaction is in progress */
		bool IsInTransaction() const {
			return !_transactions.empty();
		}
		/**
		 * @brief Called when the outermost transaction ended or was cancelled
		 *
		 * The editor shows the changes made in a transaction only then, all at once, see
		 * @ref EditorSession::OnDocumentChanged().
		 */
		Function<void()> OnTransactionsEnded;

		/** @brief Returns `true` if there is a change to undo */
		bool CanUndo() const;
		/** @brief Returns `true` if there is a change to redo */
		bool CanRedo() const;
		/** @brief Undoes the last change */
		bool Undo(DocumentChanges& changes);
		/** @brief Redoes the last undone change */
		bool Redo(DocumentChanges& changes);
		/** @brief Returns the name of the change that would be undone */
		String GetUndoName() const;
		/** @brief Returns the name of the change that would be redone */
		String GetRedoName() const;

		/** @brief Returns the number of recorded changes, done and undone */
		std::int32_t GetCount() const {
			return std::int32_t(_entries.size());
		}
		/** @brief Returns the number of changes that are done */
		std::int32_t GetDoneCount() const {
			return _doneCount;
		}
		/** @brief Returns the name of a recorded change */
		String GetName(std::int32_t index) const;

		/** @brief Returns the revision of the current state of the document */
		std::uint32_t GetRevision() const;
		/** @brief Remembers the current state as saved */
		void MarkSaved();
		/** @brief Makes the current state count as unsaved, as when it was restored from a recovery copy */
		void MarkDirty();
		/**
		 * @brief Counts a change made outside of the history that doesn't make the document unsaved
		 *
		 * Such as hiding a layer: it's saved with the level when it's saved, but alone it's no reason to save. Whoever
		 * keeps a copy of the document sees it by @ref GetOutsideChangeCount().
		 */
		void NoteOutsideChange() {
			_outsideChanges++;
		}
		/** @brief Returns `true` if the document changed since it was saved */
		bool IsDirty() const;
		/**
		 * @brief Returns how many times the document was marked as changed outside of the history
		 *
		 * The revision doesn't change then, so whoever keeps a copy of the document compares this too.
		 */
		std::uint32_t GetOutsideChangeCount() const {
			return _outsideChanges;
		}

		/** @brief Forgets all changes, the current state becomes the saved one */
		void Clear();

		/** @brief Returns the memory taken by all recorded changes */
		std::size_t GetMemoryUsage() const {
			return _memoryUsage;
		}

	private:
		friend class CommandArchive;

		struct Entry
		{
			std::unique_ptr<EditCommand> Command;
			std::uint32_t Revision;
			std::size_t MemoryUsage;
			// The command as CommandArchive writes it, kept while it stays the same --- a command changes only when
			// another one is merged into it, which gives it a new revision, and some of them when they're done or undone
			mutable SmallVector<std::uint8_t, 0> Archived;
			// Revision the written command is of, 0 if there's none
			mutable std::uint32_t ArchivedRevision = 0;
			// Whether the command was done when it was written
			mutable bool ArchivedDone = false;
			// Whether the command could be written, and fit the archive by itself
			mutable bool ArchivedValid = false;
		};

		class Transaction;

		// Returns the commands of a transaction and its name, or `nullptr` if the command isn't one, for CommandArchive
		static const SmallVector<std::unique_ptr<EditCommand>, 0>* GetTransactionCommands(const EditCommand& command, String& name);
		// Makes a transaction of commands, for CommandArchive
		static std::unique_ptr<EditCommand> MakeTransaction(String name, SmallVector<std::unique_ptr<EditCommand>, 0>&& commands);

		LevelDocument& _document;
		std::size_t _memoryBudget;
		std::size_t _memoryUsage;
		SmallVector<Entry, 0> _entries;
		std::int32_t _doneCount;
		std::uint32_t _baseRevision;
		std::uint32_t _savedRevision;
		// Shared by all histories, so a revision never means a state of another document, as after a new level
		static std::uint32_t _nextRevision;
		std::uint32_t _outsideChanges;
		SmallVector<std::unique_ptr<Transaction>, 2> _transactions;
		bool _mergeBroken;

		void Push(std::unique_ptr<EditCommand> command, bool allowMerge);
		void TrimToBudget();
	};
}

#endif
