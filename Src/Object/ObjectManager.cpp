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

namespace fx {

const ObjectID ObjectID::scNull = ObjectID(UINT32_MAX);

void ObjectManager::Create()
{
	using namespace renderer;

	mObjectList.Init(scMaxObjects);

	uint32 buffer_size = (sizeof(ObjectGpuEntry) * scMaxObjects) * renderer::FramesInFlight;

	// TODO: replace with DescriptorCache'd version
	mObjectGpuBuffer.Create(eGpuBufferType::StorageWithOffset, buffer_size, VMA_MEMORY_USAGE_CPU_ONLY,
							eGpuBufferFlags::PersistentMapped);
}

ObjectID ObjectManager::NewObjectID(const std::string& name)
{
	std::lock_guard<std::mutex> guard(mInUse);

	uint32 index;
	Object* obj = mObjectList.NewItem(&index);
	obj->Name = Name(name);
	obj->ID = ObjectID(index);

	return obj->ID;
}


Object* ObjectManager::NewObject(const std::string& name)
{
	std::lock_guard<std::mutex> guard(mInUse);

	uint32 index;
	Object* obj = mObjectList.NewItem(&index);
	obj->ID = ObjectID(index);
	obj->Name = name;

	return obj;
}

Object* ObjectManager::GetObject(ObjectID id)
{
	if (id.IsInvalid()) {
		return nullptr;
	}

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

		if (object->Name.GetHash() == name_hash) {
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
	return (renderer::gGraphics->GetFrameNumber() * scMaxObjects * sizeof(ObjectGpuEntry));
}

uint32 ObjectManager::GetPageSize() const { return scMaxObjects * sizeof(ObjectGpuEntry); }

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

	// mObjectGpuBuffer.FlushToGpu(GetOffsetObjectIndex(object_id), sizeof(ObjectGpuEntry));
}

void ObjectManager::Submit(const ObjectID& object_id, const Mat4f& model_matrix)
{
	static_assert(offsetof(ObjectGpuEntry, ModelMatrix) == 0);

	Assert(object_id.GetID() < scMaxObjects);

	memcpy(GetBufferAtFrame(object_id.GetID()), model_matrix.RawData, sizeof(Mat4f));

	// mObjectGpuBuffer.FlushToGpu(GetOffsetObjectIndex(object_id), sizeof(ObjectGpuEntry));
}


SizedArray<Object*> ObjectManager::CollectObjects()
{
	std::lock_guard<std::mutex> guard(mInUse);

	SizedArray<Object*> object_list(mObjectList.Size + 1);

	for (uint32 i = 0; i < mObjectList.Capacity; i++) {
		if (!mObjectList.SlotsInUse.Get(i)) {
			continue;
		}

		object_list.Insert(mObjectList.GetItem(i));
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

ObjectID ObjectManager::ReserveInstances(const ObjectID& object_id, uint32 num_instances)
{
	// Unset the current object to determine if the current span of slots can contain the instances
	mObjectList.SlotsInUse.Unset(object_id.GetID());

	// Find the new bit group (+1 for the current object)
	uint32 start_index = mObjectList.SlotsInUse.FindNextFreeBitGroup(num_instances + 1);
	Assert(start_index != Bit::scBitNotFound);

	// There is not enough room after our current object, move it
	if (start_index != object_id.GetID()) {
		Submit(start_index, *GetBufferAtFrame(object_id.GetID()));

		// The object id is already 'freed', so we do not need to call FreeObjectId.
	}

	// Mark the following bits as 'in use' to prevent other future objects from snatching them.
	for (uint32 i = start_index; i < start_index + num_instances; i++) {
		mObjectList.SlotsInUse.Set(i);
	}

	// Return the new slot block
	return ObjectID(start_index);
}


void ObjectManager::Destroy() {}

} // namespace fx
