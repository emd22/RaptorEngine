#include "ObjectManager.hpp"

#include <Core/Hash.hpp>
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

	mpStorage = static_cast<uint8*>(std::malloc(sizeof(Object) * scMaxObjects));

	uint32 buffer_size = (sizeof(ObjectGpuEntry) * scMaxObjects) * renderer::FramesInFlight;

	// TODO: replace with DescriptorCache'd version
	mObjectGpuBuffer.Create(eGpuBufferType::StorageWithOffset, buffer_size, RX_MEMORY_CPU_ONLY,
							eGpuBufferFlags::PersistentMapped);
}

Object* ObjectManager::NewObject(const std::string& name, MaterialID material, eObjectTag tags)
{
	RxEntityCore* entity_core = nullptr;
	RxObjectCore* object_core = nullptr;

	const uint32 index = rx_object_store_alloc(mpStore, HashStr32(name.c_str()), &entity_core, &object_core);
	Assert(index != UINT32_MAX);

	Object* obj = new (Slot(index)) Object(entity_core, object_core);

	obj->SetMaterialDirect(material);
	obj->Name = name;
	obj->Tags = tags;

	return obj;
}

Object* ObjectManager::GetObject(ObjectID id)
{
	if (id.IsInvalid() || id.GetID() >= scMaxObjects || rx_object_store_is_used(mpStore, id.GetID()) == 0) {
		return nullptr;
	}

	return Slot(id.GetID());
}

Object* ObjectManager::FindObject(const Hash32 name_hash)
{
	const uint32 id = rx_object_store_find_by_name(mpStore, name_hash);

	return (id == UINT32_MAX) ? nullptr : Slot(id);
}


void ObjectManager::DestroyObject(ObjectID& id)
{
	if (id.IsInvalid()) {
		return;
	}

	// If the ID is passed in directly from an object (as it probably will be in some places), then we want to make sure
	// we dont invalidate our ID before freeing it.
	const uint32 index = id.GetID();

	Object* object = GetObject(id);

	if (object == nullptr) {
		id.Invalidate();
		return;
	}

	object->~Object();

	rx_object_store_release(mpStore, index);

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
	uint8* entry_buffer = reinterpret_cast<uint8*>(mObjectGpuBuffer.GetMapped());
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


std::vector<uint32> ObjectManager::UsedIds() const
{
	std::vector<uint32> ids(scMaxObjects);
	ids.resize(std::min<size_t>(rx_object_store_used_ids(mpStore, ids.data(), ids.size()), ids.size()));

	return ids;
}

ObjectManager::Range ObjectManager::GetCache() { return Range(reinterpret_cast<Object*>(mpStorage), UsedIds()); }

SizedArray<Object*> ObjectManager::CollectObjects()
{
	const std::vector<uint32> ids = UsedIds();

	SizedArray<Object*> object_list(ids.size() + 1);

	for (const uint32 id : ids) {
		object_list.Insert(Slot(id));
	}

	return object_list;
}

SizedArray<Object*> ObjectManager::CollectWithTags(eObjectTag tags)
{
	std::vector<uint32> ids(scMaxObjects);
	ids.resize(std::min<size_t>(
		rx_object_store_ids_with_tags(mpStore, static_cast<uint32>(tags), ids.data(), ids.size()), ids.size()));

	SizedArray<Object*> object_list(ids.size() + 1);

	for (const uint32 id : ids) {
		object_list.Insert(Slot(id));
	}

	return object_list;
}

void ObjectManager::ReleaseAllObjects()
{
	for (const uint32 id : UsedIds()) {
		Slot(id)->~Object();
		rx_object_store_release(mpStore, id);
	}
}

void ObjectManager::PrintActive(uint32 limit)
{
	ObjectGpuEntry* buffer = reinterpret_cast<ObjectGpuEntry*>(mObjectGpuBuffer.GetMapped());

	for (const uint32 i : UsedIds()) {
		if (i >= limit) {
			break;
		}

		LogInfo(LC_CORE, "Object [{}]", i);

		float* model1 = buffer[i].ModelMatrix;

		for (int j = 0; j < 4; j++) {
			LogInfo(LC_CORE, "[{}, {}, {}, {}]", model1[j * 4 + 0], model1[j * 4 + 1], model1[j * 4 + 2],
					model1[j * 4 + 3]);
		}
	}
}

void ObjectManager::Destroy()
{
	if (mpStorage == nullptr) {
		return;
	}

	ReleaseAllObjects();

	std::free(mpStorage);
	mpStorage = nullptr;
}

} // namespace fx
