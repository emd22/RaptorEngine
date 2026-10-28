#include "ObjectManager.hpp"

#include <Engine.hpp>
#include <Material/MaterialManager.hpp>
#include <Math/Mat4.hpp>
#include <Object/Object.hpp>
#include <Renderer/Backend/DescriptorCache.hpp>
#include <Renderer/Backend/DsLayoutBuilder.hpp>
#include <Renderer/Backend/Sampler/SamplerCache.hpp>
#include <Renderer/Globals.hpp>
#include <Renderer/GraphicsBackend.hpp>
#include <Renderer/ShadowDirectional.hpp>
#include <World.hpp>

namespace fx {

const ObjectID ObjectID::scNull = ObjectID(UINT32_MAX);

void ObjectManager::Create()
{
	using namespace renderer;

	mObjectList.Init(scMaxObjects);

	uint32 buffer_size = (sizeof(ObjectGpuEntry) * scSlotsPerFrame) * renderer::FramesInFlight;

	// TODO: replace with DescriptorCache'd version
	mObjectGpuBuffer.Create(eGpuBufferType::StorageWithOffset, buffer_size, VMA_MEMORY_USAGE_CPU_ONLY,
							eGpuBufferFlags::PersistentMapped);
}

ObjectID ObjectManager::NewObjectID(const String& name, eObjectTag tags)
{
	std::lock_guard<std::mutex> guard(mInUse);

	uint32 index;

	Object* obj = mObjectList.NewItem(&index);
	Assert(obj != nullptr);

	obj->Name = Name(name);
	obj->ID = ObjectID(index);
	obj->Tags = tags;

	return obj->ID;
}


Object* ObjectManager::NewObject(const String& name, MaterialID material, eObjectTag tags, bool is_loading)
{
	std::lock_guard<std::mutex> guard(mInUse);

	uint32 index;

	Object* obj = mObjectList.NewItem(&index, 0, material, is_loading);
	Assert(obj != nullptr);

	obj->ID = ObjectID(index);
	obj->Name = name;
	obj->Tags = tags;

	return obj;
}

Object* ObjectManager::GetObject(ObjectID id)
{
	if (id.IsInvalid()) {
		return nullptr;
	}

	std::lock_guard<std::mutex> guard(mInUse);

	return mObjectList.GetItem(id.GetID());
}

Object* ObjectManager::FindObject(const Hash32 name_hash)
{
	std::lock_guard<std::mutex> guard(mInUse);

	const uint32 capacity = mObjectList.Capacity;

	for (uint32 i = 0; i < capacity; i++) {
		if (!mObjectList.SlotsInUse.Get(i)) {
			continue;
		}

		Object* object = mObjectList.GetItem(i);

		if (!object->IsLoading() && object->Name.GetHash() == name_hash) {
			return object;
		}
	}

	return nullptr;
}


void ObjectManager::DestroyObject(ObjectID& id)
{
	std::lock_guard<std::mutex> guard(mInUse);

	if (id.IsInvalid()) {
		return;
	}

	// If the ID is passed in directly from an object (as it probably will be in some places), then we want to make sure
	// we dont invalidate our ID before freeing it.
	ObjectID id_copy = id;

	// Delete the object id at the definition
	mObjectList.GetItem(id.GetID())->ID.Invalidate();

	// Free the object from the list
	mObjectList.FreeItem(id_copy.GetID());

	// Invalidate the passed ID
	id.Invalidate();
}


uint32 ObjectManager::GetBaseOffset() const
{
	return (renderer::gGraphics->GetFrameNumber() * scSlotsPerFrame * sizeof(ObjectGpuEntry));
}

uint32 ObjectManager::GetPageSize() const { return scSlotsPerFrame * sizeof(ObjectGpuEntry); }

uint32 ObjectManager::GetOffsetObjectIndex(uint32 object_id) const
{
	return GetBaseOffset() + (object_id * sizeof(ObjectGpuEntry));
}

ObjectGpuEntry* ObjectManager::GetBufferAtFrame(uint32 object_id)
{
	uint8* entry_buffer = reinterpret_cast<uint8*>(mObjectGpuBuffer.pMappedBuffer);
	return reinterpret_cast<ObjectGpuEntry*>(entry_buffer + GetOffsetObjectIndex(object_id));
}

void ObjectManager::Submit(const ObjectID& object_id, ObjectGpuEntry& entry)
{
	Assert(object_id.GetID() < scMaxObjects);
	memcpy(GetBufferAtFrame(object_id.GetID()), &entry, sizeof(ObjectGpuEntry));
}

void ObjectManager::Submit(const ObjectID& object_id, const Mat4f& model_matrix)
{
	static_assert(offsetof(ObjectGpuEntry, ModelMatrix) == 0);

	Assert(object_id.GetID() < scMaxObjects);
	memcpy(GetBufferAtFrame(object_id.GetID()), model_matrix.RawData, sizeof(Mat4f));
}


bool ObjectManager::HasLoadingObjects()
{
	for (Object& object : mObjectList) {
		if (object.IsLoading()) {
			return true;
		}
	}

	return false;
}

SizedArray<Object*> ObjectManager::CollectObjects()
{
	std::lock_guard<std::mutex> guard(mInUse);

	SizedArray<Object*> object_list(mObjectList.Size + 1);

	for (uint32 i = 0; i < mObjectList.Capacity; i++) {
		if (!mObjectList.SlotsInUse.Get(i)) {
			continue;
		}

		Object* object = mObjectList.GetItem(i);

		if (!object->IsLoading()) {
			object_list.Insert(object);
		}
	}

	return object_list;
}

SizedArray<Object*> ObjectManager::CollectWithTags(eObjectTag tags)
{
	std::lock_guard<std::mutex> guard(mInUse);

	SizedArray<Object*> object_list(mObjectList.Size + 1);

	for (uint32 i = 0; i < mObjectList.Capacity; i++) {
		if (!mObjectList.SlotsInUse.Get(i)) {
			continue;
		}

		Object* object = mObjectList.GetItem(i);

		if (!object->IsLoading() && object->HasTags(tags)) {
			object_list.Insert(object);
		}
	}

	return object_list;
}

void ObjectManager::ReleaseAllObjects()
{
	std::lock_guard<std::mutex> guard(mInUse);

	uint32 capacity = mObjectList.Capacity;

	for (uint32 i = 0; i < capacity; i++) {
		if (!mObjectList.SlotsInUse.Get(i)) {
			continue;
		}

		mObjectList.FreeItem(i);
	}
}

void ObjectManager::PrintActive(uint32 limit)
{
	ObjectGpuEntry* buffer = reinterpret_cast<ObjectGpuEntry*>(mObjectGpuBuffer.pMappedBuffer);

	for (int i = 0; i < std::min(limit, mObjectList.Capacity); i++) {
		if (mObjectList.SlotsInUse.Get(i)) {
			LogInfo(LC_CORE, "Object [{}]", i);

			float* model1 = buffer[i].ModelMatrix;

			for (int j = 0; j < 4; j++) {
				LogInfo(LC_CORE, "[{}, {}, {}, {}]", model1[j * 4 + 0], model1[j * 4 + 1], model1[j * 4 + 2],
						model1[j * 4 + 3]);
			}
		}
	}
}

ObjectGpuEntry* ObjectManager::ReserveInstanceSlots(uint32 count, uint32* out_first_slot)
{
	const uint32 frame = renderer::gGraphics->GetElapsedFrameCount();

	if (mInstanceSlotsFrame != frame) {
		mInstanceSlotsFrame = frame;
		mInstanceSlotsUsed = 0;
	}

	if (count == 0 || count > scMaxInstances - mInstanceSlotsUsed) {
		return nullptr;
	}

	const uint32 first_slot = scMaxObjects + mInstanceSlotsUsed;
	mInstanceSlotsUsed += count;

	*out_first_slot = first_slot;

	return GetBufferAtFrame(first_slot);
}


void ObjectManager::QueueInstanceUpdate(ObjectID id) { mPendingInstanceUpdates.Insert(id); }

void ObjectManager::FlushInstanceUpdates()
{
	for (ObjectID id : mPendingInstanceUpdates) {
		Object* object = GetObject(id);

		if (object != nullptr && object->mbInstancesPending) {
			object->ApplyInstanceChanges();
		}
	}

	mPendingInstanceUpdates.Clear();
}


void ObjectManager::Destroy() {}

void DestroyObjectTree(ObjectID root_id)
{
	Object* root = gObjectManager->GetObject(root_id);

	if (root == nullptr) {
		return;
	}

	DynArray<ObjectID> children;
	for (const ObjectID& child_id : root->AttachedNodes) {
		children.Insert(child_id);
	}

	root->AttachedNodes.Clear();

	for (const ObjectID& child_id : children) {
		DestroyObjectTree(child_id);
	}

	gWorld->Detach(root_id);
	gObjectManager->DestroyObject(root_id);
}

void CollectObjectTree(Object* root, DynArray<Object*>& out_parts)
{
	out_parts.Insert(root);

	for (ObjectID child_id : root->AttachedNodes) {
		Object* child = gObjectManager->GetObject(child_id);

		if (child != nullptr) {
			CollectObjectTree(child, out_parts);
		}
	}
}

} // namespace fx
