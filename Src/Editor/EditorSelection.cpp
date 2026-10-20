#include "EditorSelection.hpp"

#include <Object/Object.hpp>

namespace fx::editor {

bool EditorSelection::Add(Object* object)
{
	if (object == nullptr || Contains(object)) {
		return true;
	}

	if (mEntries.Size >= scMaxSelectedObjects) {
		return false;
	}

	mEntries.Insert(object);

	return true;
}

void EditorSelection::Remove(Object* object)
{
	if (object == nullptr) {
		return;
	}

	for (uint32 i = 0; i < mEntries.Size; i++) {
		if (mEntries[i] != object) {
			continue;
		}

		// Keep the order so that the last selected object stays last
		for (uint32 j = i + 1; j < mEntries.Size; j++) {
			mEntries[j - 1] = mEntries[j];
		}

		mEntries.Size--;
		return;
	}
}

void EditorSelection::Clear() { mEntries.Clear(); }

bool EditorSelection::Contains(const Object* object) const
{
	for (const Object* entry : mEntries) {
		if (entry == object) {
			return true;
		}
	}

	return false;
}

Object* EditorSelection::GetLast() const
{
	if (mEntries.Size == 0) {
		return nullptr;
	}

	return mEntries[mEntries.Size - 1];
}

} // namespace fx::editor
