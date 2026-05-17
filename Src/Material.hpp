#pragma once

#include <vulkan/vulkan.h>

#include <Asset/AxImage.hpp>
#include <Asset/Fwd/Ax_Fwd_Manager.hpp>
#include <Color.hpp>
#include <Core/Bitset.hpp>
#include <Core/Name.hpp>
#include <Core/PagedArray.hpp>
#include <Renderer/Backend/Descriptors.hpp>
#include <Renderer/Backend/GpuBuffer.hpp>


namespace fx {

#define FX_MAX_MATERIALS 512

enum class eMaterialComponentStatus
{
    Ready,
    MissingComponent,
    NotReady,
};

/////////////////////////////////////
// Material Component
/////////////////////////////////////

template <renderer::eImageFormat TFormat>
struct MaterialComponent
{
public:
public:
    TSRef<AxImage> pAssetImage { nullptr };
    Slice<const uint8> pDataToLoad { nullptr };

    using Status = eMaterialComponentStatus;

public:
    MaterialComponent::Status Build()
    {
        // There is no texture provided, we will use the base colours passed in and a dummy texture
        if (!pAssetImage && !pDataToLoad) {
            // pAssetImage = AxImage::GetEmptyImage<TFormat>();
            return Status::MissingComponent;
        }

        if (!CheckIfReady()) {
            // The texture is not ready, return the status to the material build function
            return Status::NotReady;
        }

        return Status::Ready;
    }

    bool Exists() const { return (pAssetImage != nullptr) || (pDataToLoad != nullptr); }

    ~MaterialComponent() = default;

private:
    bool CheckIfReady()
    {
        // If there is data passed in and the image has not been loaded yet, load it using the
        // asset manager. This will be validated on the next call of this function. (when attempting to build the
        // material)
        if (!pAssetImage && pDataToLoad) {
            Slice<const uint8>& image_data = pDataToLoad;
            pAssetImage = Fwd::AssetManager::LoadImageFromMemory(TFormat, image_data.pData, image_data.Size);

            return false;
        }

        // If there is no texture and we are not loaded, return not loaded.
        if (!pAssetImage || !pAssetImage->IsLoaded()) {
            return false;
        }

        return true;
    }
};

/////////////////////////////////////
// Material
/////////////////////////////////////

struct MaterialProperties
{
    alignas(16) Color BaseColor = 0xFF010101u;
};

class Material
{
public:
    enum class eResourceType : uint32
    {
        Diffuse,
        Normal,
        MetallicRoughness,

        MaxImages,
    };

public:
    Material() = default;

    void Attach(eResourceType type, const TSRef<AxImage>& image)
    {
        switch (type) {
        case eResourceType::Diffuse:
            Diffuse.pAssetImage = image;
            break;
        case eResourceType::Normal:
            NormalMap.pAssetImage = image;
            break;
        case eResourceType::MetallicRoughness:
            MetallicRoughness.pAssetImage = image;
            break;

        default:
            LogError(LC_CORE, "Unsupported resource type to attach to material!");
            break;
        }
    }

    bool IsReady();

    void SetSupportsSkinning(bool value) { bSupportsSkinning = value; }
    inline uint32 GetMaterialIndex() { return mMaterialPropertiesIndex; }

    /**
     * Binds the material to be used in the given command buffer.
     * @returns True if the material was bound succesfully.
     */
    bool Bind(renderer::CommandBuffer* cmd);
    bool BindWithPipeline(renderer::CommandBuffer& cmd, renderer::Pipeline& pipeline, bool albedo_only = false);

    void Build();

    renderer::DescriptorSet& GetDescriptorSet() { return mDsDefault; }
    renderer::DescriptorSet& GetDescriptorSetAlbedoOnly();

    void SetDefaultPipeline();

    void Destroy();
    ~Material() { Destroy(); }

public:
    //    Ref<AssetImage> DiffuseTexture{nullptr};
    MaterialComponent<renderer::eImageFormat::RGBA8_UNorm> Diffuse {};
    MaterialComponent<renderer::eImageFormat::RGBA8_UNorm> NormalMap {};
    MaterialComponent<renderer::eImageFormat::RGBA8_UNorm> MetallicRoughness {};

    MaterialProperties Properties {};

    Name Name;

    renderer::Pipeline* pPipeline = nullptr;

    std::atomic_bool bIsBuilt { false };

    bool bSupportsSkinning = false;

    /**
     * @brief Offset into `MaterialPropertiesBuffer` for this material.
     */
    uint32 mMaterialPropertiesIndex = UINT32_MAX;

    bool bNearestFiltering : 1 = false;

private:
    renderer::DescriptorSet mDsDefault;
    renderer::DescriptorSet mDsAlbedoOnly;


    bool mbIsReady : 1 = false;
    bool mbIsBeingBuilt : 1 = false;
};

/////////////////////////////////////
// Material Manager
/////////////////////////////////////

class MaterialManager
{
public:
    void Create(uint32 materials_per_page = 64);
    TSRef<Material> New(const String& name, renderer::Pipeline* pipeline, bool supports_skinning);

    renderer::DescriptorPool& GetDescriptorPool() { return mDescriptorPool; }
    TSRef<Material> GetNullMaterial();

    void Destroy();

    ~MaterialManager() { Destroy(); }

private:
    uint32 GetNewMaterialIndex();

public:
    /**
     * @brief A large GPU buffer containing all loaded in material properties.
     */
    renderer::RawGpuBuffer MaterialPropertiesBuffer {};
    uint32 NumMaterialsInBuffer = 0;

    Bitset MaterialsInUse;

    /**
     * @brief Descriptor set for material properties. Used in the light pass.
     */
    renderer::DescriptorSet mMaterialPropertiesDS {};


private:
    PagedArray<Material> mMaterials;
    renderer::DescriptorPool mDescriptorPool;

    TSRef<Material> mNullMaterial { nullptr };

    bool mbInitialized : 1 = false;

    std::mutex mInUse;
};

} // namespace fx
