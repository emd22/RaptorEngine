#include "ObjectManager.hpp"

#include <Engine.hpp>
#include <Math/Mat4.hpp>
#include <Object.hpp>
#include <Renderer/Backend/DsLayoutBuilder.hpp>
#include <Renderer/Globals.hpp>
#include <Renderer/RenderBackend.hpp>

namespace fx {

void ObjectManager::Create()
{
    if (!mDescriptorPool.Pool) {
        mDescriptorPool.AddPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 4);
        mDescriptorPool.Create(renderer::gRenderer->GetDevice(), 2);
    }

    renderer::DsLayoutBuilder builder {};
    builder.AddBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, eShaderType::Vertex);
    DsLayoutObjectBuffer = builder.Build();

    uint32 buffer_size = (sizeof(ObjectGpuEntry) * scMaxObjects) * renderer::FramesInFlight;

    mObjectGpuBuffer.Create(renderer::eGpuBufferType::StorageWithOffset, buffer_size, VMA_MEMORY_USAGE_CPU_ONLY,
                            eGpuBufferFlags::PersistentMapped);


    mObjectSlotsInUse.InitZero(scMaxObjects);

    if (!mObjectBufferDS.IsInited()) {
        Assert(DsLayoutObjectBuffer != nullptr);
        mObjectBufferDS.Create(mDescriptorPool, DsLayoutObjectBuffer, true);
    }

    static constexpr uint32 bound_size = scMaxObjects * sizeof(ObjectGpuEntry);
    mObjectBufferDS.AddBuffer(0, &mObjectGpuBuffer, 0, bound_size);
    mObjectBufferDS.Build();
}


uint32 ObjectManager::GetBaseOffset() const
{
    return (renderer::gRenderer->GetFrameNumber() * scMaxObjects * sizeof(ObjectGpuEntry));
}

uint32 ObjectManager::GetOffsetObjectIndex(uint32 object_id) const
{
    return GetBaseOffset() + (object_id * sizeof(ObjectGpuEntry));
}

ObjectGpuEntry* ObjectManager::GetBufferAtFrame(uint32 object_id)
{
    uint8* entry_buffer = reinterpret_cast<uint8*>(mObjectGpuBuffer.pMappedBuffer);

    return reinterpret_cast<ObjectGpuEntry*>(entry_buffer + GetOffsetObjectIndex(object_id));
}


ObjectId ObjectManager::GenerateObjectId()
{
    std::lock_guard<std::mutex> guard(mInUse);

    ObjectId free_object_id = mObjectSlotsInUse.FindNextFreeBit();
    mObjectSlotsInUse.Set(free_object_id);

    return free_object_id;
}

void ObjectManager::Submit(ObjectId object_id, ObjectGpuEntry& entry)
{
    Assert(object_id < scMaxObjects);

    memcpy(GetBufferAtFrame(object_id), &entry, sizeof(ObjectGpuEntry));

    // mObjectGpuBuffer.FlushToGpu(GetOffsetObjectIndex(object_id), sizeof(ObjectGpuEntry));
}

void ObjectManager::Submit(ObjectId object_id, const Mat4f& model_matrix)
{
    static_assert(offsetof(ObjectGpuEntry, ModelMatrix) == 0);

    Assert(object_id < scMaxObjects);

    memcpy(GetBufferAtFrame(object_id), model_matrix.RawData, sizeof(Mat4f));

    // mObjectGpuBuffer.FlushToGpu(GetOffsetObjectIndex(object_id), sizeof(ObjectGpuEntry));
}


void ObjectManager::ReleaseObject(ObjectId id)
{
    if (id == UINT32_MAX) {
        return;
    }

    std::lock_guard<std::mutex> guard(mInUse);

    LogInfo(LC_CORE, "Freeing object {} from object manager", id);

    mObjectSlotsInUse.Unset(id);
}

void ObjectManager::ReleaseAllObjects()
{
    std::lock_guard<std::mutex> guard(mInUse);
    mObjectSlotsInUse.ClearAll();
}

void ObjectManager::PrintActive(int limit)
{
    ObjectGpuEntry* buffer = reinterpret_cast<ObjectGpuEntry*>(mObjectGpuBuffer.pMappedBuffer);

    for (int i = 0; i < limit; i++) {
        if (mObjectSlotsInUse.Get(i)) {
            LogInfo(LC_CORE, "Object [{}]", i);

            float* model1 = buffer[i].ModelMatrix;

            for (int j = 0; j < 4; j++) {
                LogInfo(LC_CORE, "[{}, {}, {}, {}]", model1[j * 4 + 0], model1[j * 4 + 1], model1[j * 4 + 2],
                        model1[j * 4 + 3]);
            }
        }
    }
}

ObjectId ObjectManager::ReserveInstances(ObjectId object_id, uint32 num_instances)
{
    // Unset the current object to determine if the current span of slots can contain the instances
    mObjectSlotsInUse.Unset(object_id);

    // Find the new bit group (+1 for the current object)
    uint32 start_index = mObjectSlotsInUse.FindNextFreeBitGroup(num_instances + 1);
    Assert(start_index != Bit::scBitNotFound);

    // There is not enough room after our current object, move it
    if (start_index != object_id) {
        Submit(start_index, *GetBufferAtFrame(object_id));

        // The object id is already 'freed', so we do not need to call FreeObjectId.
    }

    // Mark the following bits as 'in use' to prevent other future objects from snatching them.
    for (uint32 i = start_index; i < start_index + num_instances; i++) {
        mObjectSlotsInUse.Set(i);
    }

    // Return the new slot block
    return start_index;
}


void ObjectManager::Destroy()
{
    mObjectBufferDS.Destroy();
    mDescriptorPool.Destroy();
    mObjectGpuBuffer.Destroy();

    if (DsLayoutObjectBuffer) {
        vkDestroyDescriptorSetLayout(renderer::gRenderer->GetDevice()->Device, DsLayoutObjectBuffer, nullptr);
        DsLayoutObjectBuffer = nullptr;
    }
}

} // namespace fx
