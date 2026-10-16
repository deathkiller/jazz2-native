#include "CommandHistory.h"

#if defined(WITH_EDITOR)

namespace Jazz2::Editor
{
	class CommandHistory::Transaction : public EditCommand
	{
		DEATH_RUNTIME_OBJECT(EditCommand);

	public:
		String Name;
		SmallVector<std::unique_ptr<EditCommand>, 0> Commands;

		explicit Transaction(String name)
			: Name(std::move(name)) {}

		void Do(LevelDocument& document, DocumentChanges& changes) override {
			for (auto& command : Commands) {
				command->Do(document, changes);
			}
		}

		void Undo(LevelDocument& document, DocumentChanges& changes) override {
			for (std::size_t i = Commands.size(); i > 0; i--) {
				Commands[i - 1]->Undo(document, changes);
			}
		}

		String GetName() const override {
			return Name;
		}

		std::size_t GetMemoryUsage() const override {
			std::size_t usage = sizeof(Transaction);
			for (const auto& command : Commands) {
				usage += command->GetMemoryUsage();
			}
			return usage;
		}
	};

	CommandHistory::CommandHistory(LevelDocument& document, std::size_t memoryBudget)
		: _document(document), _memoryBudget(memoryBudget), _memoryUsage(0), _doneCount(0),
			_baseRevision(1), _savedRevision(1), _nextRevision(2)
	{
	}

	CommandHistory::~CommandHistory()
	{
	}

	void CommandHistory::Execute(std::unique_ptr<EditCommand> command, DocumentChanges& changes, bool allowMerge)
	{
		command->Do(_document, changes);
		Push(std::move(command), allowMerge);
	}

	void CommandHistory::Record(std::unique_ptr<EditCommand> command, bool allowMerge)
	{
		Push(std::move(command), allowMerge);
	}

	void CommandHistory::BeginTransaction(String name)
	{
		_transactions.push_back(std::make_unique<Transaction>(std::move(name)));
	}

	void CommandHistory::EndTransaction()
	{
		DEATH_ASSERT(!_transactions.empty(), "No transaction is in progress", );

		std::unique_ptr<Transaction> transaction = std::move(_transactions.back());
		_transactions.pop_back();
		if (transaction->Commands.empty()) {
			return;
		}

		if (!_transactions.empty()) {
			_transactions.back()->Commands.push_back(std::move(transaction));
		} else {
			Push(std::move(transaction), false);
		}
	}

	void CommandHistory::CancelTransaction(DocumentChanges& changes)
	{
		DEATH_ASSERT(!_transactions.empty(), "No transaction is in progress", );

		std::unique_ptr<Transaction> transaction = std::move(_transactions.back());
		_transactions.pop_back();
		transaction->Undo(_document, changes);
	}

	bool CommandHistory::CanUndo() const
	{
		return (_transactions.empty() && _doneCount > 0);
	}

	bool CommandHistory::CanRedo() const
	{
		return (_transactions.empty() && _doneCount < std::int32_t(_entries.size()));
	}

	bool CommandHistory::Undo(DocumentChanges& changes)
	{
		if (!CanUndo()) {
			return false;
		}

		_doneCount--;
		_entries[_doneCount].Command->Undo(_document, changes);
		return true;
	}

	bool CommandHistory::Redo(DocumentChanges& changes)
	{
		if (!CanRedo()) {
			return false;
		}

		_entries[_doneCount].Command->Do(_document, changes);
		_doneCount++;
		return true;
	}

	String CommandHistory::GetUndoName() const
	{
		return (_doneCount > 0 ? _entries[_doneCount - 1].Command->GetName() : String());
	}

	String CommandHistory::GetRedoName() const
	{
		return (_doneCount < std::int32_t(_entries.size()) ? _entries[_doneCount].Command->GetName() : String());
	}

	String CommandHistory::GetName(std::int32_t index) const
	{
		return (index >= 0 && index < std::int32_t(_entries.size()) ? _entries[index].Command->GetName() : String());
	}

	std::uint32_t CommandHistory::GetRevision() const
	{
		return (_doneCount > 0 ? _entries[_doneCount - 1].Revision : _baseRevision);
	}

	void CommandHistory::MarkSaved()
	{
		_savedRevision = GetRevision();
	}

	void CommandHistory::MarkDirty()
	{
		// No state has this revision
		_savedRevision = 0;
	}

	bool CommandHistory::IsDirty() const
	{
		return (GetRevision() != _savedRevision || !_transactions.empty());
	}

	void CommandHistory::Clear()
	{
		_entries.clear();
		_transactions.clear();
		_doneCount = 0;
		_memoryUsage = 0;
		_baseRevision = _nextRevision++;
		_savedRevision = _baseRevision;
	}

	void CommandHistory::Push(std::unique_ptr<EditCommand> command, bool allowMerge)
	{
		if (!_transactions.empty()) {
			auto& commands = _transactions.back()->Commands;
			if (allowMerge && !commands.empty() && commands.back()->TryMerge(*command)) {
				return;
			}
			commands.push_back(std::move(command));
			return;
		}

		// Whatever was undone can't be redone anymore
		while (std::int32_t(_entries.size()) > _doneCount) {
			_memoryUsage -= _entries.back().MemoryUsage;
			_entries.pop_back();
		}

		if (allowMerge && _doneCount > 0) {
			Entry& last = _entries[_doneCount - 1];
			// The saved state has to stay reachable, so it's never merged into
			if (last.Revision != _savedRevision && last.Command->TryMerge(*command)) {
				_memoryUsage -= last.MemoryUsage;
				last.MemoryUsage = last.Command->GetMemoryUsage();
				_memoryUsage += last.MemoryUsage;
				last.Revision = _nextRevision++;
				TrimToBudget();
				return;
			}
		}

		Entry& entry = _entries.emplace_back();
		entry.MemoryUsage = command->GetMemoryUsage();
		entry.Command = std::move(command);
		entry.Revision = _nextRevision++;
		_memoryUsage += entry.MemoryUsage;
		_doneCount++;

		TrimToBudget();
	}

	void CommandHistory::TrimToBudget()
	{
		// The last change is always kept, however large it is
		std::int32_t dropCount = 0;
		std::size_t usage = _memoryUsage;
		while (usage > _memoryBudget && _doneCount - dropCount > 1) {
			usage -= _entries[dropCount].MemoryUsage;
			dropCount++;
		}
		if (dropCount == 0) {
			return;
		}

		_baseRevision = _entries[dropCount - 1].Revision;
		_entries.erase(_entries.begin(), _entries.begin() + dropCount);
		_doneCount -= dropCount;
		_memoryUsage = usage;
	}
}

#endif
