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
		// Whether all its changes were made so they may be merged, as while a value is dragged
		bool Mergeable;

		explicit Transaction(String name)
			: Name(Death::move(name)), Mergeable(true) {}

		// The same changes again, as a value of more things dragged is a transaction on each step
		bool CanMerge(EditCommand& next) override {
			auto* other = runtime_cast<Transaction>(&next);
			if (other == nullptr || other->Name != Name || other->Commands.size() != Commands.size() || Commands.empty()) {
				return false;
			}
			for (std::size_t i = 0; i < Commands.size(); i++) {
				if (!Commands[i]->CanMerge(*other->Commands[i])) {
					return false;
				}
			}
			return true;
		}

		bool TryMerge(EditCommand& next) override {
			// All of them are asked first, a part merged and a part not would leave the step broken
			if (!CanMerge(next)) {
				return false;
			}
			auto* other = runtime_cast<Transaction>(&next);
			for (std::size_t i = 0; i < Commands.size(); i++) {
				Commands[i]->TryMerge(*other->Commands[i]);
			}
			return true;
		}

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

	std::uint32_t CommandHistory::_nextRevision = 1;

	const SmallVector<std::unique_ptr<EditCommand>, 0>* CommandHistory::GetTransactionCommands(const EditCommand& command, String& name)
	{
		// The type itself, a const one is another type to runtime_cast<T>(), the constness comes from the pointer
		auto* transaction = runtime_cast<Transaction>(&command);
		if (transaction == nullptr) {
			return nullptr;
		}
		name = transaction->Name;
		return &transaction->Commands;
	}

	std::unique_ptr<EditCommand> CommandHistory::MakeTransaction(String name, SmallVector<std::unique_ptr<EditCommand>, 0>&& commands)
	{
		auto transaction = std::make_unique<Transaction>(Death::move(name));
		transaction->Commands = Death::move(commands);
		return transaction;
	}

	CommandHistory::CommandHistory(LevelDocument& document, std::size_t memoryBudget)
		: _document(document), _memoryBudget(memoryBudget), _memoryUsage(0), _doneCount(0),
			_baseRevision(_nextRevision++), _savedRevision(_baseRevision), _outsideChanges(0), _mergeBroken(false)
	{
	}

	CommandHistory::~CommandHistory()
	{
	}

	void CommandHistory::Execute(std::unique_ptr<EditCommand> command, DocumentChanges& changes, bool allowMerge)
	{
		command->Do(_document, changes);
		Push(Death::move(command), allowMerge);
	}

	void CommandHistory::Record(std::unique_ptr<EditCommand> command, bool allowMerge)
	{
		Push(Death::move(command), allowMerge);
	}

	void CommandHistory::BeginTransaction(String name)
	{
		_transactions.push_back(std::make_unique<Transaction>(Death::move(name)));
	}

	void CommandHistory::EndTransaction()
	{
		// A real check, the assert is compiled out with the tracing
		if (_transactions.empty()) {
			LOGE("No transaction is in progress");
			return;
		}

		std::unique_ptr<Transaction> transaction = Death::move(_transactions.back());
		_transactions.pop_back();
		if (!transaction->Commands.empty()) {
			if (!_transactions.empty()) {
				_transactions.back()->Mergeable &= transaction->Mergeable;
				_transactions.back()->Commands.push_back(Death::move(transaction));
			} else {
				// Merged into the previous transaction only if all its changes were made so they may be, a break of
				// merging before its first change made it unmergeable already
				bool mergeable = transaction->Mergeable;
				Push(Death::move(transaction), mergeable);
			}
		}
		if (_transactions.empty() && OnTransactionsEnded) {
			OnTransactionsEnded();
		}
	}

	void CommandHistory::CancelTransaction(DocumentChanges& changes)
	{
		if (_transactions.empty()) {
			LOGE("No transaction is in progress");
			return;
		}

		std::unique_ptr<Transaction> transaction = Death::move(_transactions.back());
		_transactions.pop_back();
		transaction->Undo(_document, changes);
		if (_transactions.empty() && OnTransactionsEnded) {
			OnTransactionsEnded();
		}
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
		// What follows is a new change, not a continuation of the one before the undone one
		_mergeBroken = true;
		return true;
	}

	bool CommandHistory::Redo(DocumentChanges& changes)
	{
		if (!CanRedo()) {
			return false;
		}

		_entries[_doneCount].Command->Do(_document, changes);
		_doneCount++;
		_mergeBroken = true;
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
		_outsideChanges++;
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
		_mergeBroken = true;
	}

	void CommandHistory::Push(std::unique_ptr<EditCommand> command, bool allowMerge)
	{
		// A break applies to the next change only
		if (_mergeBroken) {
			allowMerge = false;
			_mergeBroken = false;
		}

		if (!_transactions.empty()) {
			Transaction& transaction = *_transactions.back();
			// A change that may not be merged makes the whole transaction one that may not be
			transaction.Mergeable &= allowMerge;
			auto& commands = transaction.Commands;
			if (allowMerge && !commands.empty() && commands.back()->TryMerge(*command)) {
				return;
			}
			commands.push_back(Death::move(command));
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
		entry.Command = Death::move(command);
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
