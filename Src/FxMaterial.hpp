#pragma once

#include <vulkan/vulkan.h>

#include <Asset/AxImage.hpp>
#include <Asset/Fwd/Ax_Fwd_Manager.hpp>
#include <Core/FxBitset.hpp>
#include <Core/FxHash.hpp>
#include <Core/FxPagedArray.hpp>
#include <Core/Log.hpp>
#include <FxColor.hpp>
#include <Renderer/Backend/RxDescriptors.hpp>
#include <Renderer/Backend/RxGpuBuffer.hpp>

#define FX_MAX_MATERIALS 128

enum class FxMaterialComponentStatus
{
    eReady,
    eNotReady,
};

template <RxImageFormat TFormat>
struct FxMaterialComponent
{
public:
public:
    FxRef<AxImage> pImage { nullptr };
    FxSlice<const uint8> pDataToLoad { nullptr };

    using Status = FxMaterialComponentStatus;

public:
    FxMaterialComponent::Status Build(const FxRef<RxSampler>& sampler)
    {
        // There is no texture provided, we will use the base colours passed in and a dummy texture
        if (!pImage && !pDataToLoad) {
            pImage = AxImage::GetEmptyImage<TFormat>();
        }

        if (!CheckIfReady()) {
            // The texture is not ready, return the status to the material build function
            return Status::eNotReady;
        }

        pImage->Texture.SetSampler(sampler);

        return Status::eReady;
    }

    ~FxMaterialComponent() = default;

private:
    bool CheckIfReady()
    {
        // If there is data passed in and the image has not been loaded yet, load it using the
        // asset manager. This will be validated on the next call of this function. (when attempting to build the
        // material)
        if (!pImage && pDataToLoad) {
            FxSlice<const uint8>& image_data = pDataToLoad;

            pImage = Fwd::AssetManager::LoadImageFromMemory(TFormat, image_data.pData, image_data.Size);
            // pImage = FxAssetManager::LoadImageFromMemory(Format, image_data.pData, image_data.Size);
            return false;
        }

        // If there is no texture and we are not loaded, return not loaded.
        if (!pImage || !pImage->IsLoaded()) {
            return false;
        }

        return true;
    }
};

struct FxMaterialProperties
{
    alignas(16) FxColor BaseColor = 0xFFFFFFFFu;
};

class FxMaterial
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
    FxMaterial() = default;

    void Attach(ResourceType type, const FxRef<AxImage>& image)
    {
        switch (type) {
        case ResourceType::eDiffuse:
            Diffuse.pImage = image;
            break;
        case ResourceType::eNormal:
            NormalMap.pImage = image;
            break;
        case ResourceType::eMetallicRoughness:
            MetallicRoughness.pImage = image;
            break;

        default:
            FxLogError("Unsupported resource type to attach to material!");
            break;
        }
    }

    bool IsReady();

    inline uint32 GetMaterialIndex() { return mMaterialPropertiesIndex; }

    /**
     * Binds the material to be used in the given command buffer.
     * @returns True if the material was bound succesfully.
     */
    bool Bind(RxCommandBuffer* cmd);

    void Build();
    void Destroy();

    ~FxMaterial() { Destroy(); }

private:
    VkDescriptorSetLayout BuildLayout();

    // bool CheckComponentTextureLoaded(FxMaterialComponent& component);

public:
    //    FxRef<FxAssetImage> DiffuseTexture{nullptr};
    FxMaterialComponent<RxImageFormat::eRGBA8_UNorm> Diffuse;
    FxMaterialComponent<RxImageFormat::eRGBA8_UNorm> NormalMap;
    FxMaterialComponent<RxImageFormat::eRGBA8_UNorm> MetallicRoughness;

    FxMaterialProperties Properties {};

    FxHash32 NameHash { 0 };
    std::string Name = "";

    RxDescriptorSet mDescriptorSet {};

    RxPipeline* pPipeline = nullptr;

    std::atomic_bool bIsBuilt { false };

    /**
     * @brief Offset into `MaterialPropertiesBuffer` for this material.
     */
    uint32 mMaterialPropertiesIndex = UINT32_MAX;

private:
    // VkDescriptorSetLayout mSetLayout = nullptr;

    /**
     * @brief Descriptor set layout for material properties, used in the light pass fragment shader.
     */
    // VkDescriptorSetLayout mMaterialPropertiesLayoutDS = nullptr;

    bool mbIsReady : 1 = false;
};

class FxMaterialManager
{
public:
    static FxMaterialManager& GetGlobalManager();

    void Create(uint32 materials_per_page = 64);
    static FxRef<FxMaterial> New(const std::string& name, RxPipeline* pipeline);

    static RxDescriptorPool& GetDescriptorPool() { return GetGlobalManager().mDescriptorPool; }

    void Destroy();

    ~FxMaterialManager() { Destroy(); }

public:
    FxRef<RxSampler> pAlbedoSampler { nullptr };
    FxRef<RxSampler> pNormalMapSampler { nullptr };
    FxRef<RxSampler> pMetallicRoughnessSampler { nullptr };
    // RxRawGpuBuffer<FxMaterialProperties> MaterialPropertiesUbo;

    /**
     * @brief A large GPU buffer containing all loaded in material properties.
     */
    RxRawGpuBuffer MaterialPropertiesBuffer;
    uint32 NumMaterialsInBuffer = 0;

    FxBitset MaterialsInUse;

    /**
     * @brief Descriptor set for material properties. Used in the light pass.
     */
    RxDescriptorSet mMaterialPropertiesDS {};

private:
    FxPagedArray<FxMaterial> mMaterials;
    RxDescriptorPool mDescriptorPool;

    bool mbInitialized : 1 = false;
};
