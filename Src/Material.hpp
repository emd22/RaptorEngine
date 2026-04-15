#pragma once

#include <vulkan/vulkan.h>

#include <Asset/AxImage.hpp>
#include <Asset/Fwd/Ax_Fwd_Manager.hpp>
#include <Color.hpp>
#include <Core/Bitset.hpp>
#include <Core/Name.hpp>
#include <Core/PagedArray.hpp>
#include <Renderer/Backend/RxDescriptors.hpp>
#include <Renderer/Backend/RxGpuBuffer.hpp>


namespace fx {

#define FX_MAX_MATERIALS 128

enum class MaterialComponentStatus
{
    eReady,
    eMissingComponent,
    eNotReady,
};

/////////////////////////////////////
// Material Component
/////////////////////////////////////

template <renderer::RxImageFormat TFormat>
struct MaterialComponent
{
public:
public:
    TSRef<AxImage> pAssetImage { nullptr };
    Slice<const uint8> pDataToLoad { nullptr };

    using Status = MaterialComponentStatus;

public:
    MaterialComponent::Status Build()
    {
        // There is no texture provided, we will use the base colours passed in and a dummy texture
        if (!pAssetImage && !pDataToLoad) {
            // pAssetImage = AxImage::GetEmptyImage<TFormat>();
            return Status::eMissingComponent;
        }

        if (!CheckIfReady()) {
            // The texture is not ready, return the status to the material build function
            return Status::eNotReady;
        }

        return Status::eReady;
    }

    bool Exists() const { return (pAssetImage != nullptr); }

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
    alignas(16) Color BaseColor = 0xFFFFFFFFu;
};

class Material
{
public:
    enum ResourceType : uint32
    {
        eDiffuse,
        eNormal,
        eMetallicRoughness,

        eMaxImages,
    };

public:
    Material() = default;

    void Attach(ResourceType type, const TSRef<AxImage>& image)
    {
        switch (type) {
        case ResourceType::eDiffuse:
            Diffuse.pAssetImage = image;
            break;
        case ResourceType::eNormal:
            NormalMap.pAssetImage = image;
            break;
        case ResourceType::eMetallicRoughness:
            MetallicRoughness.pAssetImage = image;
            break;

        default:
            LogError("Unsupported resource type to attach to material!");
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
    bool Bind(renderer::RxCommandBuffer* cmd);
    bool BindWithPipeline(renderer::RxCommandBuffer& cmd, renderer::RxPipeline& pipeline, bool albedo_only = false);

    void Build();

    renderer::RxDescriptorSet& GetDescriptorSet() { return mDsDefault; }
    renderer::RxDescriptorSet& GetDescriptorSetAlbedoOnly();

    void Destroy();
    ~Material() { Destroy(); }

public:
    //    Ref<AssetImage> DiffuseTexture{nullptr};
    MaterialComponent<renderer::RxImageFormat::eRGBA8_UNorm> Diffuse {};
    MaterialComponent<renderer::RxImageFormat::eRGBA8_UNorm> NormalMap {};
    MaterialComponent<renderer::RxImageFormat::eRGBA8_UNorm> MetallicRoughness {};

    MaterialProperties Properties {};

    Name Name;

    renderer::RxPipeline* pPipeline = nullptr;

    std::atomic_bool bIsBuilt { false };

    bool bSupportsSkinning = false;

    /**
     * @brief Offset into `MaterialPropertiesBuffer` for this material.
     */
    uint32 mMaterialPropertiesIndex = UINT32_MAX;

private:
    renderer::RxDescriptorSet mDsDefault;
    renderer::RxDescriptorSet mDsAlbedoOnly;


    bool mbIsReady : 1 = false;
};

/////////////////////////////////////
// Material Manager
/////////////////////////////////////

class MaterialManager
{
public:
    void Create(uint32 materials_per_page = 64);
    TSRef<Material> New(const std::string& name, renderer::RxPipeline* pipeline, bool supports_skinning);

    renderer::RxDescriptorPool& GetDescriptorPool() { return mDescriptorPool; }

    void Destroy();

    ~MaterialManager() { Destroy(); }

    /**
     * @brief A large GPU buffer containing all loaded in material properties.
     */
    renderer::RxRawGpuBuffer MaterialPropertiesBuffer {};
    uint32 NumMaterialsInBuffer = 0;

    Bitset MaterialsInUse;

    /**
     * @brief Descriptor set for material properties. Used in the light pass.
     */
    renderer::RxDescriptorSet mMaterialPropertiesDS {};

private:
    PagedArray<Material> mMaterials;
    renderer::RxDescriptorPool mDescriptorPool;

    bool mbInitialized : 1 = false;

    std::mutex mInUse;
};

} // namespace fx
