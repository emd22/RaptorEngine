#pragma once

#include "BaseOperation.hpp"

#include <Core/UndoStack.hpp>

namespace fx {

namespace editor {

/**
 * @brief The undo/redo history of edit operations. Keeps the selection in step with objects that undoing and redoing
 * brings back or destroys.
 */
class EditHistory
{
	static constexpr uint32 scMaxOperations = 256;

public:
	explicit EditHistory(EditorSelection& selection);

	EditOperation& Push(std::unique_ptr<EditOperation> op);

	/// Reverts the last group of operations. Returns true if the selection changed.
	bool Undo();

	/// Reapplies the last undone group of operations. Returns true if the selection changed.
	bool Redo();

	void Clear();

	uint64 GetRevision() const;

	~EditHistory() = default;

private:
	EditorSelection& mSelection;
	UndoStack<std::unique_ptr<EditOperation>> mOperations;

	uint64 mBaseRevision = 0;
	uint64 mNextRevision = 1;
};


} // namespace editor


} // namespace fx
