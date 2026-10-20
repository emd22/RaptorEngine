#pragma once

#include "EditorTool.hpp"

#include <Core/StackArray.hpp>
#include <Core/Types.hpp>

namespace fx {

class Object;

namespace editor {

/**
 * @brief The objects selected in the editor, in the order they were selected
 */
class EditorSelection
{
public:
	EditorSelection() = default;

	/// Adds an object to the selection. Returns false if the selection is full.
	bool Add(Object* object);
	void Remove(Object* object);
	void Clear();

	bool Contains(const Object* object) const;

	FX_FORCE_INLINE bool IsEmpty() const { return (mEntries.Size == 0); }
	FX_FORCE_INLINE uint32 GetCount() const { return mEntries.Size; }
	FX_FORCE_INLINE Object* GetObject(uint32 index) const { return mEntries[index]; }

	/// The most recently selected object, or nullptr if nothing is selected
	Object* GetLast() const;

	~EditorSelection() = default;

private:
	StackArray<Object*, scMaxSelectedObjects> mEntries;
};

} // namespace editor
} // namespace fx
