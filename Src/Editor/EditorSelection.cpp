#include "EditorSelection.hpp"

#include <Blockout.hpp>
#include <Engine.hpp>
#include <Object/Object.hpp>
#include <World.hpp>

namespace fx::editor {

/**
 * @brief Draws an object with the selection material, including any parts of its mesh with materials of their own
 */
static void ShowAsSelected(Object* object)
{
	object->SetMaterial(gWorld->pBlockout->SelectionMaterialID);
	object->SetMaterialOverridesSections(true);
}

static void ShowAsDeselected(Object* object, const MaterialID& material)
{
	object->SetMaterial(material);
	object->SetMaterialOverridesSections(false);
}


bool EditorSelection::Add(Object* object)
{
	if (object == nullptr || Contains(object)) {
		return true;
	}

	if (mEntries.Size >= scMaxSelectedObjects) {
		return false;
	}

	mEntries.Insert(Entry { .pObject = object, .StoredMaterial = object->GetMaterialID() });
	ShowAsSelected(object);

	return true;
}

void EditorSelection::Remove(Object* object)
{
	if (object == nullptr) {
		return;
	}

	for (uint32 i = 0; i < mEntries.Size; i++) {
		if (mEntries[i].pObject != object) {
			continue;
		}

		ShowAsDeselected(object, mEntries[i].StoredMaterial);

		// Keep the order so that the last selected object stays last
		for (uint32 j = i + 1; j < mEntries.Size; j++) {
			mEntries[j - 1] = mEntries[j];
		}

		mEntries.Size--;
		return;
	}
}

void EditorSelection::Clear()
{
	for (Entry& entry : mEntries) {
		ShowAsDeselected(entry.pObject, entry.StoredMaterial);
	}

	mEntries.Clear();
}

bool EditorSelection::Contains(const Object* object) const
{
	for (const Entry& entry : mEntries) {
		if (entry.pObject == object) {
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

	return mEntries[mEntries.Size - 1].pObject;
}

MaterialID EditorSelection::GetStoredMaterial(Object* object) const
{
	if (object == nullptr) {
		return MaterialID::scNull;
	}

	for (const Entry& entry : mEntries) {
		if (entry.pObject == object) {
			return entry.StoredMaterial;
		}
	}

	return object->GetMaterialID();
}

void EditorSelection::SetStoredMaterial(Object* object, MaterialID material)
{
	if (object == nullptr) {
		return;
	}

	for (Entry& entry : mEntries) {
		if (entry.pObject == object) {
			entry.StoredMaterial = material;
			break;
		}
	}

	// Shown straight away, even over the selection highlight, as a preview of the change
	object->SetMaterial(material);
}

} // namespace fx::editor
