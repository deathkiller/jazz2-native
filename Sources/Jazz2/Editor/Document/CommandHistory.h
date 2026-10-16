#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "LevelDocument.h"
#include "DocumentChanges.h"

#include <Base/TypeInfo.h>
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
		 * With @p allowMerge the change may be merged into the previous one (see @ref EditCommand::TryMerge()).
		 */
		void Execute(std::unique_ptr<EditCommand> command, DocumentChanges& changes, bool allowMerge = false);
		/** @brief Records a change that was already applied to the document */
		void Record(std::unique_ptr<EditCommand> command, bool allowMerge = false);

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
		/** @brief Returns `true` if the document changed since it was saved */
		bool IsDirty() const;

		/** @brief Forgets all changes, the current state becomes the saved one */
		void Clear();

		/** @brief Returns the memory taken by all recorded changes */
		std::size_t GetMemoryUsage() const {
			return _memoryUsage;
		}

	private:
		struct Entry
		{
			std::unique_ptr<EditCommand> Command;
			std::uint32_t Revision;
			std::size_t MemoryUsage;
		};

		class Transaction;

		LevelDocument& _document;
		std::size_t _memoryBudget;
		std::size_t _memoryUsage;
		SmallVector<Entry, 0> _entries;
		std::int32_t _doneCount;
		std::uint32_t _baseRevision;
		std::uint32_t _savedRevision;
		std::uint32_t _nextRevision;
		SmallVector<std::unique_ptr<Transaction>, 2> _transactions;

		void Push(std::unique_ptr<EditCommand> command, bool allowMerge);
		void TrimToBudget();
	};
}

#endif
