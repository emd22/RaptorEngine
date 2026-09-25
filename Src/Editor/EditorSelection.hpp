#pragma once

#include "EditorTool.hpp"

#include <Core/StackArray.hpp>
#include <Core/Types.hpp>
#include <Material/MaterialID.hpp>

namespace fx {

class Object;

namespace editor {

/**
 * @brief The objects selected in the editor. Selected objects are drawn with the selection material, so each one
 * remembers the material it had before it was selected.
 */
class EditorSelection
{
	struct Entry
	{
		Object* pObject = nullptr;
		MaterialID StoredMaterial = MaterialID::scNull;
	};

public:
	EditorSelection() = default;

	/// Adds an object to the selection. Returns false if the selection is full.
	bool Add(Object* object);
	void Remove(Object* object);
	void Clear();

	bool Contains(const Object* object) const;

	FX_FORCE_INLINE bool IsEmpty() const { return (mEntries.Size == 0); }
	FX_FORCE_INLINE uint32 GetCount() const { return mEntries.Size; }
	FX_FORCE_INLINE Object* GetObject(uint32 index) const { return mEntries[index].pObject; }

	/// The most recently selected object, or nullptr if nothing is selected
	Object* GetLast() const;

	/// The material the object had before it was selected, falling back to its current material
	MaterialID GetStoredMaterial(Object* object) const;

	/// Changes an object's material, and the material it goes back to once it is deselected
	void SetStoredMaterial(Object* object, MaterialID material);

	~EditorSelection() = default;

private:
	StackArray<Entry, scMaxSelectedObjects> mEntries;
};

} // namespace editor
} // namespace fx
