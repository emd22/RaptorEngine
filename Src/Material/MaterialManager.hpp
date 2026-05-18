#pragma once

#include "Material.hpp"
#include "MaterialID.hpp"

#include <Core/Bitset.hpp>
#include <Core/String.hpp>
#include <Core/Types.hpp>
#include <Renderer/Backend/Descriptors.hpp>

#define FX_MAX_BOUND_MATERIALS 64

namespace fx {

// Forward declarations
namespace renderer {
class Pipeline;
class CommandBuffer;
} // namespace renderer

class Material;

class MaterialManager
{
public:
    void Create();

    MaterialID NewMaterial(const String& name, renderer::Pipeline* pipeline, bool supports_skinning);
    Material* GetMaterial(const MaterialID& id);
    void DestroyMaterial(const MaterialID& id);

    bool Bind(const renderer::CommandBuffer& cmd, const MaterialID& id);

    renderer::DescriptorPool& GetDescriptorPool() { return mDescriptorPool; }

    void Destroy();

    ~MaterialManager();

private:
    Material* GetNewMaterial();

    void MakeNullMaterial();

public:
    /**
     * @brief A large GPU buffer containing all loaded in material properties.
     */
    renderer::RawGpuBuffer MaterialPropertiesBuffer {};

    Bitset MaterialsInUse;

    /**
     * @brief Descriptor set for material properties. Used in the light pass.
     */
    renderer::DescriptorSet mMaterialPropertiesDS {};


private:
    SizedArray<Material> mMaterials;
    // SizedArray<int16> mMaterialUsages;

    renderer::DescriptorPool mDescriptorPool;

    bool mbInitialized : 1 = false;

    std::mutex mInUse;
};

} // namespace fx
