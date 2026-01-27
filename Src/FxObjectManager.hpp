#pragma once

#include <Core/FxBitset.hpp>
#include <Renderer/Backend/RxDescriptors.hpp>
#include <Renderer/Backend/RxGpuBuffer.hpp>

struct alignas(16) FxObjectGpuEntry
{
    float ModelMatrix[16];
};

using FxObjectId = uint32;

class FxObjectManager
{
public:
    static constexpr uint32 scMaxObjects = 128;


public:
    void Create();

    FxObjectId GenerateObjectId();
    void Submit(FxObjectId id, FxObjectGpuEntry& entry);
    void Submit(FxObjectId id, FxMat4f& model_matrix);
    void FreeObjectId(FxObjectId id);


    void PrintActive(int limit = 20);

    void Destroy();

    uint32 GetOffsetObjectIndex(uint32 object_id) const;
    uint32 GetBaseOffset() const;

private:
    FxObjectGpuEntry* GetBufferAtFrame(uint32 object_id);

public:
    // RxDescriptorSet mObjectBufferDS {};

    RxDescriptorPool mDescriptorPool {};

public:
    RxRawGpuBuffer mObjectGpuBuffer;
    FxBitset mObjectSlotsInUse;

    RxDescriptorSet mObjectBufferDS {};
    VkDescriptorSetLayout DsLayoutObjectBuffer = nullptr;
};
