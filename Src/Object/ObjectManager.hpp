#pragma once

#include "Object.hpp"
#include "ObjectID.hpp"

#include <Core/Bitset.hpp>
#include <Core/FreeArray.hpp>
#include <Renderer/Backend/Descriptors.hpp>
#include <Renderer/Backend/GpuBuffer.hpp>

namespace fx {

struct alignas(16) ObjectGpuEntry
{
    float ModelMatrix[16];
    float UvOffsets[4];
};

class Mat4f;

// using ObjectId = uint32;


class ObjectManager
{
public:
    static constexpr uint32 scMaxObjects = 512;


public:
    void Create();

    ObjectID NewObjectID(const std::string& name);
    Object* NewObject();
    Object* NewObject(const std::string& name);
    Object* GetObject(const ObjectID& id);
    void DestroyObject(ObjectID& id);

    void Submit(const ObjectID& id, ObjectGpuEntry& entry);
    void Submit(const ObjectID& id, const Mat4f& model_matrix);
    void ReleaseAllObjects();

    void PrintActive(uint32 limit = 20);

    uint32 GetOffsetObjectIndex(uint32 object_id) const;
    uint32 GetBaseOffset() const;

    /**
     * @brief Finds an object slot with `num_instances` free slots following.
     *
     * @note This does not update the object's object id or set the reserved instances counter, use
     * Object::ReserveInstances() for that!
     *
     * @param object_id An object id of the current object if it needs to be moved.
     * @returns The object id for the first slot of the block.
     */
    ObjectID ReserveInstances(const ObjectID& object_id, uint32 num_instances);

    void Destroy();

    ~ObjectManager() { Destroy(); }

private:
    ObjectGpuEntry* GetBufferAtFrame(uint32 object_id);

public:
    renderer::DescriptorPool mDescriptorPool {};

public:
    renderer::RawGpuBuffer mObjectGpuBuffer {};
    // Bitset mObjectSlotsInUse;

    renderer::DescriptorSet mObjectBufferDS {};
    VkDescriptorSetLayout DsLayoutObjectBuffer = nullptr;

    std::mutex mInUse;

private:
    FreeArray<Object> mObjectList;
};

} // namespace fx
