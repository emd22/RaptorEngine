#include "FxObjectManager.hpp"

#include <FxEngine.hpp>
#include <FxObject.hpp>
#include <Renderer/Backend/RxDsLayoutBuilder.hpp>
#include <Renderer/RxRenderBackend.hpp>

void FxObjectManager::Create()
{
    if (!mDescriptorPool.Pool) {
        mDescriptorPool.AddPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1);
        mDescriptorPool.Create(gRenderer->GetDevice(), 2);
    }

    RxDsLayoutBuilder builder {};
    builder.AddBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, RxShaderType::eVertex);
    DsLayoutObjectBuffer = builder.Build();

    RxUtil::SetDebugLabel("Object Buffer", VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, DsLayoutObjectBuffer);

    mObjectGpuBuffer.Create(FX_MAX_GPU_OBJECTS, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU,
                            RxGpuBufferFlags::ePersistentMapped);
    mObjectSlotsInUse.InitZero(FX_MAX_GPU_OBJECTS);

    if (!mObjectBufferDS.IsInited()) {
        FxAssert(DsLayoutObjectBuffer != nullptr);
        mObjectBufferDS.Create(mDescriptorPool, DsLayoutObjectBuffer);
    }

    VkDescriptorBufferInfo info {
        .buffer = mObjectGpuBuffer.Buffer,
        .offset = 0,
        .range = VK_WHOLE_SIZE,
    };

    VkWriteDescriptorSet buffer_write {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = mObjectBufferDS.Set,
        .dstBinding = 0,
        .dstArrayElement = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .pBufferInfo = &info,
    };


    vkUpdateDescriptorSets(gRenderer->GetDevice()->Device, 1, &buffer_write, 0, nullptr);

    // FxStackArray<VkWriteDescriptorSet, 2> write_descriptor_sets;
    // FxStackArray<VkDescriptorBufferInfo, 2> write_buffer_infos;

    // VkDescriptorBufferInfo info {
    //     .buffer = gObjectManager->mObjectGpuBuffer.Buffer,
    //     .offset = 0,
    //     .range = VK_WHOLE_SIZE,
    // };

    // VkWriteDescriptorSet buffer_write {
    //     .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
    //     .dstSet = mObjectBufferDS.Set,
    //     .dstBinding = 1,
    //     .dstArrayElement = 0,
    //     .descriptorCount = 1,
    //     .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
    //     .pBufferInfo = write_buffer_infos.Insert(info),
    // };

    // write_descriptor_sets.Insert(buffer_write);

    // vkUpdateDescriptorSets(gRenderer->GetDevice()->Device, write_descriptor_sets.Size, write_descriptor_sets.pData,
    // 0,
    //                        nullptr);

    // RxUtil::SetDebugLabel("Object Buffer DS", VK_OBJECT_TYPE_DESCRIPTOR_SET, mObjectBufferDS.Set);
}

FxObjectId FxObjectManager::GenerateObjectId()
{
    FxObjectId free_object_id = mObjectSlotsInUse.FindNextFreeBit();
    mObjectSlotsInUse.Set(free_object_id);

    return free_object_id;

    // static uint32 sObjectId = 0;
    // return (sObjectId++);
}

void FxObjectManager::Submit(FxObjectId object_id, FxObjectGpuEntry& entry)
{
    FxAssert(object_id < FX_MAX_GPU_OBJECTS);
    FxObjectGpuEntry* buffer = reinterpret_cast<FxObjectGpuEntry*>(mObjectGpuBuffer.pMappedBuffer);


    memcpy(&buffer[object_id], &entry, sizeof(FxObjectGpuEntry));

    // mObjectGpuBuffer.FlushToGpu(object_id * sizeof(FxObjectGpuEntry), sizeof(FxObjectGpuEntry));
}

void FxObjectManager::Submit(FxObjectId object_id, FxMat4f& model_matrix)
{
    static_assert(offsetof(FxObjectGpuEntry, ModelMatrix) == 0);

    FxAssert(object_id < FX_MAX_GPU_OBJECTS);
    FxObjectGpuEntry* buffer = reinterpret_cast<FxObjectGpuEntry*>(mObjectGpuBuffer.pMappedBuffer);

    // FxObjectGpuEntry entry;

    memcpy(&buffer[object_id], model_matrix.RawData, sizeof(FxMat4f));
}


void FxObjectManager::FreeObjectId(FxObjectId id)
{
    if (id == UINT32_MAX) {
        return;
    }
    mObjectSlotsInUse.Unset(id);
}

void FxObjectManager::PrintActive(int limit)
{
    FxObjectGpuEntry* buffer = reinterpret_cast<FxObjectGpuEntry*>(mObjectGpuBuffer.pMappedBuffer);

    for (int i = 0; i < limit; i++) {
        if (mObjectSlotsInUse.Get(i)) {
            FxLogInfo("Object [{}]", i);

            float* model1 = buffer[i].ModelMatrix;

            for (int j = 0; j < 4; j++) {
                FxLogInfo("[{}, {}, {}, {}]", model1[j * 4 + 0], model1[j * 4 + 1], model1[j * 4 + 2],
                          model1[j * 4 + 3]);
            }
        }
    }
}


void FxObjectManager::Destroy()
{
    // mObjectBufferDS.Destroy();
    mDescriptorPool.Destroy();
    mObjectGpuBuffer.Destroy();
}
