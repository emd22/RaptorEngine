#include "History.hpp"


namespace fx {

namespace editor {


EditHistory::EditHistory(EditorSelection& selection) : mSelection(selection)
{
	mOperations.InitCapacity(scMaxOperations);
}

EditOperation& EditHistory::Push(std::unique_ptr<EditOperation> op)
{
	// Make room by dropping the oldest groups whole, so that no group is left half undoable
	const uint32 capacity = mOperations.GetCapacity();

	while (capacity > 0 && mOperations.GetSize() >= capacity) {
		const uint32 size = mOperations.GetSize();
		const uint32 evict = std::min(static_cast<uint32>(std::max(mOperations.First()->GroupSize, 1)), size);

		for (uint32 i = 0; i < evict; i++) {
			mBaseRevision = mOperations.First()->Revision;
			mOperations.PopFront();
		}
	}

	op->Revision = mNextRevision++;

	EditOperation& pushed = *mOperations.Push(std::move(op));

	pushed.Execute(mSelection);

	return pushed;
}

bool EditHistory::Undo()
{
	bool selection_changed = false;

	// Every operation in a group carries the size of the group. It is clamped so that one bad operation can't unwind
	// the whole stack.
	int32 remaining = 1;

	for (bool first = true; remaining > 0; remaining--, first = false) {
		std::unique_ptr<EditOperation>* slot = mOperations.Undo();
		if (slot == nullptr) {
			break;
		}

		EditOperation& op = **slot;

		if (first) {
			remaining = std::max(op.GroupSize, 1);
		}

		op.Undo(mSelection);

		selection_changed |= op.ChangesObjects();
	}

	return selection_changed;
}

bool EditHistory::Redo()
{
	bool selection_changed = false;

	int32 remaining = 1;

	for (bool first = true; remaining > 0; remaining--, first = false) {
		std::unique_ptr<EditOperation>* slot = mOperations.Redo();
		if (slot == nullptr) {
			break;
		}

		EditOperation& op = **slot;

		if (first) {
			remaining = std::max(op.GroupSize, 1);
		}

		op.Redo(mSelection);

		selection_changed |= op.ChangesObjects();
	}

	return selection_changed;
}

void EditHistory::Clear()
{
	mBaseRevision = GetRevision();
	mOperations.Clear();
}

uint64 EditHistory::GetRevision() const
{
	const std::unique_ptr<EditOperation>* last = mOperations.Last();

	return (last != nullptr) ? (*last)->Revision : mBaseRevision;
}


} // namespace editor
} // namespace fx
