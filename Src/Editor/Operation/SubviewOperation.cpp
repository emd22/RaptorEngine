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

static void DestroySubviewBrushes(EditorSelection& selection, SubviewBrushArray& brushes)
{
	for (const SubviewBrushRef& brush : brushes) {
		Object* live = gObjectManager->GetObject(brush.ID);

		if (live == nullptr || live != brush.pObject) {
			continue;
		}

		selection.Remove(live);

		gWorld->pPrototype->DestroyObject(live);
	}

	brushes.Clear();
}

static void RemoveSubviewInstances(EditorSelection& selection, SubviewInstanceArray& instances)
{
	Blockout* blockout = gWorld->pPrototype;

	for (const uint32 instance : instances) {
		selection.Remove(blockout->GetSubviewObject(instance));

		blockout->RemoveSubviewInstance(instance);
	}

	instances.Clear();
}

static void RemoveSubviewPlacement(EditorSelection& selection, uint32 instance, BrushPlacement& out_placement)
{
	Blockout* blockout = gWorld->pPrototype;

	blockout->GetSubviewPlacement(instance, out_placement);

	selection.Remove(blockout->GetSubviewObject(instance));

	blockout->RemoveSubviewInstance(instance);
}

static void DissolveSubview(EditorSelection& selection, uint32 instance, SubviewBrushArray& out_brushes,
							SubviewInstanceArray& out_instances)
{
	Blockout* blockout = gWorld->pPrototype;

	selection.Remove(blockout->GetSubviewObject(instance));

	DynArray<ObjectID, GrowthFunctions::Double> created;
	blockout->DissolveSubviewInstance(instance, created, out_instances);

	out_brushes.Clear();

	for (const ObjectID id : created) {
		Object* brush = gObjectManager->GetObject(id);

		if (brush != nullptr) {
			out_brushes.Insert(SubviewBrushRef { brush, id });
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
	RemoveSubviewPlacement(selection, mInstance, mPlacement);
}

SubviewRemoveOperation::SubviewRemoveOperation(uint32 instance, int32 group_size)
	: EditOperation(group_size), mInstance(instance)
{
}

void SubviewRemoveOperation::Execute(EditorSelection& selection)
{
	RemoveSubviewPlacement(selection, mInstance, mPlacement);
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
			mInstances.Insert(objects[i]->SubviewInstance);
		}
		else {
			mBrushes.Insert(SubviewBrushRef { objects[i], objects[i]->ID });
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
