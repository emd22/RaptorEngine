#include "InGameEditor.hpp"

#include <Blockout.hpp>
#include <Engine.hpp>
#include <Script/ScriptManager.hpp>

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

void EditorMode::Update(const Vec3f& movement_vector) const
{
	if (pUpdateFunction) {
		pUpdateFunction(movement_vector.mIntrin);
	}
}

bool EditorMode::SelectObject(Object* object)
{
	if (pScript == nullptr) {
		return false;
	}

	auto mode_select_object = pScript->GetFunction<void (*)(void*, bool)>("mode_select_object");

	if (object == nullptr) {
		if (mpLastSelectedObject != nullptr) {
			mpLastSelectedObject->mMaterialID = mSelectedObjectPreviousMaterial;
			mpLastSelectedObject = nullptr;
		}

		if (mode_select_object) {
			mode_select_object(nullptr, false);
			return true;
		}

		return false;
	}

	if (object->HasTags(eObjectTag::LockTransform)) {
		return false;
	}

	if (mpLastSelectedObject != nullptr) {
		mpLastSelectedObject->mMaterialID = mSelectedObjectPreviousMaterial;
		mpLastSelectedObject = nullptr;
	}

	mpLastSelectedObject = object;
	mSelectedObjectPreviousMaterial = object->mMaterialID;

	object->mMaterialID = gWorld->pBlockout->SelectionMaterialID;

	if (mode_select_object) {
		mode_select_object(reinterpret_cast<void*>(object), true);
		return true;
	}

	return false;
}

void EditorMode::Load()
{
	auto mode_load = pScript->GetFunction<void (*)()>("mode_load");
	if (mode_load) {
		mode_load();
	}

	ReloadHotFunctions();
}

void EditorMode::Unload()
{
	auto mode_unload = pScript->GetFunction<void (*)()>("mode_unload");
	if (mode_unload) {
		mode_unload();
	}

	if (mpLastSelectedObject != nullptr) {
		mpLastSelectedObject->mMaterialID = mSelectedObjectPreviousMaterial;
		mpLastSelectedObject = nullptr;
	}
}

float EditorMode::GetQuantizeFraction() const
{
	auto mode_get_quantize = pScript->GetFunction<float (*)()>("mode_common_get_quantize_fraction");
	if (mode_get_quantize) {
		return mode_get_quantize();
	}

	return 0.0f;
}


} // namespace fx
