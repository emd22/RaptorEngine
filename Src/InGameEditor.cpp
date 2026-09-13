#include "InGameEditor.hpp"

#include <Blockout.hpp>
#include <Engine.hpp>
#include <Script/ScriptManager.hpp>
#include <World.hpp>

namespace fx {

/////////////////////////////////////
// Translate Object
/////////////////////////////////////

void EditorMode::Create(const String& name, const String& script_path)
{
	ModeName = name;
	pScript = gScriptManager->LoadScript(script_path.CStr());
}

void EditorMode::ReloadHotFunctions() { pUpdateFunction = pScript->GetFunction<UpdateFnDef>("mode_update"); }

void EditorMode::Update(const Vec3f& movement_vector, float32 delta_time) const
{
	if (pUpdateFunction) {
		pUpdateFunction(movement_vector.mIntrin, delta_time);
	}
}


bool EditorMode::SelectObject(Object* object, bool append_selection)
{
	if (pScript == nullptr) {
		return false;
	}

	auto mode_select_object = pScript->GetFunction<void (*)(void*, bool, bool)>("_internal_editor_select_object");

	// Clear selection
	if (object == nullptr) {
		for (SelectedObject& selected_obj : mSelectedObjects) {
			if (selected_obj.pObject == nullptr) {
				continue;
			}

			selected_obj.pObject->SetMaterial(selected_obj.OldMaterial);
		}

		mSelectedObjects.Clear();

		if (mode_select_object) {
			mode_select_object(nullptr, false, false);
			return true;
		}

		return false;
	}

	if (IsInSelection(object)) {
		return true;
	}

	// If we aren't appending to the selection (overwrite selection), then we need to reset the previous selection's
	// materials
	if (append_selection == false) {
		for (SelectedObject& selected_obj : mSelectedObjects) {
			if (selected_obj.pObject == nullptr) {
				continue;
			}

			selected_obj.pObject->SetMaterial(selected_obj.OldMaterial);
		}
		mSelectedObjects.Clear();
	}

	mSelectedObjects.Insert(SelectedObject { .pObject = object, .OldMaterial = object->GetMaterialID() });

	// Set the newly selected object to the selection material.
	object->SetMaterial(gWorld->pBlockout->SelectionMaterialID);

	// Call the script's object selection routine
	if (mode_select_object) {
		mode_select_object(reinterpret_cast<void*>(object), true, append_selection);
		return true;
	}

	return false;
}

Object* EditorMode::GetLastSelectedObject()
{
	if (mSelectedObjects.Size == 0) {
		return nullptr;
	}

	return mSelectedObjects[mSelectedObjects.Size - 1].pObject;
}


bool EditorMode::IsInSelection(Object* object) const
{
	for (const SelectedObject& sel : mSelectedObjects) {
		if (sel.pObject == object) {
			return true;
		}
	}
	return false;
}

void EditorMode::Load()
{
	auto mode_load = pScript->GetFunction<void (*)()>("mode_load");
	if (mode_load) {
		mode_load();
	}

	auto mode_set_xform_marker = pScript->GetFunction<void (*)(void*)>("editor_set_transform_marker");
	if (mode_set_xform_marker) {
		mode_set_xform_marker(reinterpret_cast<void*>(gWorld->pBlockout->pXFormObject));
	}

	ReloadHotFunctions();

	// Set selected objects back to selection material
	for (SelectedObject& selected_obj : mSelectedObjects) {
		if (selected_obj.pObject == nullptr) {
			continue;
		}

		selected_obj.pObject->SetMaterial(gWorld->pBlockout->SelectionMaterialID);
	}
}

void EditorMode::Unload()
{
	auto mode_unload = pScript->GetFunction<void (*)()>("mode_unload");
	if (mode_unload) {
		mode_unload();
	}

	// Reset selection materials
	if (mSelectedObjects.Size > 0) {
		for (SelectedObject& selected_obj : mSelectedObjects) {
			if (selected_obj.pObject == nullptr) {
				continue;
			}

			selected_obj.pObject->SetMaterial(selected_obj.OldMaterial);
		}
	}
}

float EditorMode::GetQuantizeFraction() const
{
	auto editor_get_snap_value = pScript->GetFunction<float (*)()>("editor_get_snap_value");
	if (editor_get_snap_value) {
		return editor_get_snap_value();
	}

	return 0.0f;
}

bool EditorMode::GetQuantizeEnabled() const
{
	auto mode_get_quantize = pScript->GetFunction<bool (*)()>("editor_get_snap_enabled");
	if (mode_get_quantize) {
		return mode_get_quantize();
	}

	return false;
}


} // namespace fx
