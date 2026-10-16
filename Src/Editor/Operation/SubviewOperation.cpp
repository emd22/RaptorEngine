#include "SubviewOperation.hpp"

#include <Blockout.hpp>
#include <Editor/RaptorEditor.hpp>
#include <Engine.hpp>
#include <Object/Object.hpp>
#include <Object/ObjectManager.hpp>
#include <World.hpp>


namespace fx {

namespace editor {


/////////////////////////////////////
// Subviews
/////////////////////////////////////

static void DestroySubviewBrushes(EditorSelection& selection, std::vector<SubviewBrushRef>& brushes)
{
	for (const SubviewBrushRef& brush : brushes) {
		Object* live = gObjectManager->GetObject(brush.ID);

		if (live == nullptr || live != brush.pObject) {
			continue;
		}

		selection.Remove(live);

		gWorld->pPrototype->DestroyObject(live);
	}

	brushes.clear();
}

static void RemoveSubviewInstances(EditorSelection& selection, std::vector<uint32>& instances)
{
	Blockout* blockout = gWorld->pPrototype;

	for (const uint32 instance : instances) {
		selection.Remove(blockout->GetSubviewObject(instance));

		blockout->RemoveSubviewInstance(instance);
	}

	instances.clear();
}

static void DissolveSubview(EditorSelection& selection, uint32 instance, std::vector<SubviewBrushRef>& out_brushes,
							std::vector<uint32>& out_instances)
{
	Blockout* blockout = gWorld->pPrototype;

	selection.Remove(blockout->GetSubviewObject(instance));

	std::vector<ObjectID> created;
	blockout->DissolveSubviewInstance(instance, created, out_instances);

	out_brushes.clear();

	for (const ObjectID id : created) {
		Object* brush = gObjectManager->GetObject(id);

		if (brush != nullptr) {
			out_brushes.push_back(SubviewBrushRef { brush, id });
		}
	}
}

SubviewPlaceOperation::SubviewPlaceOperation(uint32 instance, const BrushPlacement& placement, int32 group_size)
	: EditOperation(group_size), mInstance(instance), mPlacement(placement)
{
}

void SubviewPlaceOperation::Execute(EditorSelection& selection)
{
	gWorld->pPrototype->PlaceSubviewInstance(mInstance, mPlacement);
}

void SubviewPlaceOperation::Undo(EditorSelection& selection)
{
	Blockout* blockout = gWorld->pPrototype;

	blockout->GetSubviewPlacement(mInstance, mPlacement);

	selection.Remove(blockout->GetSubviewObject(mInstance));

	blockout->RemoveSubviewInstance(mInstance);
}

SubviewRemoveOperation::SubviewRemoveOperation(uint32 instance, int32 group_size)
	: EditOperation(group_size), mInstance(instance)
{
}

void SubviewRemoveOperation::Execute(EditorSelection& selection)
{
	Blockout* blockout = gWorld->pPrototype;

	blockout->GetSubviewPlacement(mInstance, mPlacement);

	selection.Remove(blockout->GetSubviewObject(mInstance));

	blockout->RemoveSubviewInstance(mInstance);
}

void SubviewRemoveOperation::Undo(EditorSelection& selection)
{
	selection.Add(gWorld->pPrototype->PlaceSubviewInstance(mInstance, mPlacement));
}

SubviewCreateOperation::SubviewCreateOperation(uint32 instance, Object* const* objects, uint32 count,
											   const BrushPlacement& placement)
	: mInstance(instance), mPlacement(placement)
{
	for (uint32 i = 0; i < count; i++) {
		if (gWorld->pPrototype->IsSubviewObject(objects[i])) {
			mInstances.push_back(objects[i]->SubviewInstance);
		}
		else {
			mBrushes.push_back(SubviewBrushRef { objects[i], objects[i]->ID });
		}
	}
}

void SubviewCreateOperation::Execute(EditorSelection& selection)
{
	DestroySubviewBrushes(selection, mBrushes);
	RemoveSubviewInstances(selection, mInstances);

	gWorld->pPrototype->PlaceSubviewInstance(mInstance, mPlacement);
}

void SubviewCreateOperation::Undo(EditorSelection& selection)
{
	gWorld->pPrototype->GetSubviewPlacement(mInstance, mPlacement);

	DissolveSubview(selection, mInstance, mBrushes, mInstances);
}

SubviewDissolveOperation::SubviewDissolveOperation(uint32 instance, int32 group_size)
	: EditOperation(group_size), mInstance(instance)
{
}

void SubviewDissolveOperation::Execute(EditorSelection& selection)
{
	gWorld->pPrototype->GetSubviewPlacement(mInstance, mPlacement);

	DissolveSubview(selection, mInstance, mBrushes, mInstances);

	for (const SubviewBrushRef& brush : mBrushes) {
		selection.Add(brush.pObject);
	}

	for (const uint32 instance : mInstances) {
		Object* root = gWorld->pPrototype->GetSubviewObject(instance);

		if (root != nullptr) {
			selection.Add(root);
		}
	}
}

void SubviewDissolveOperation::Undo(EditorSelection& selection)
{
	DestroySubviewBrushes(selection, mBrushes);
	RemoveSubviewInstances(selection, mInstances);

	selection.Add(gWorld->pPrototype->PlaceSubviewInstance(mInstance, mPlacement));
}


} // namespace editor
} // namespace fx
