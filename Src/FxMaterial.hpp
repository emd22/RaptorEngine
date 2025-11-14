#pragma once

#include <vulkan/vulkan.h>

#include <Asset/FxAssetImage.hpp>
#include <Core/FxHash.hpp>
#include <Core/FxPagedArray.hpp>
#include <Core/Log.hpp>
#include <FxColor.hpp>
#include <Renderer/Backend/RxDescriptors.hpp>
#include <Renderer/Backend/RxGpuBuffer.hpp>

// TODO: Replace this, make dynamic
#define FX_MAX_MATERIALS 256

struct FxMaterialComponent
{
public:
    enum class Status
    {
        Ready,
        NotReady,
    };

public:
    FxRef<FxAssetImage> pTexture { nullptr };
    FxSlice<const uint8> pDataToLoad { nullptr };

    ~FxMaterialComponent() = default;

public:
    Status Build(const FxRef<RxSampler>& sampler);

private:
    bool CheckIfReady();
};

struct FxMaterialProperties
{
    alignas(16) FxColor BaseColor = 0xFFFFFFFFu;
};

class FxMaterial
{
public:
    enum class ResourceType
    {
        eDiffuse,
        eNormal,

        eMaxImages,
    };

public:
    FxMaterial() = default;

    void Attach(ResourceType type, const FxRef<FxAssetImage>& image)
    {
        switch (type) {
        case ResourceType::eDiffuse:
            Diffuse.pTexture = image;
            break;
        case ResourceType::eNormal:
            Normal.pTexture = image;
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

    bool CheckComponentTextureLoaded(FxMaterialComponent& component);

public:
    //    FxRef<FxAssetImage> DiffuseTexture{nullptr};
    FxMaterialComponent Diffuse;
    FxMaterialComponent Normal;

    FxMaterialProperties Properties {};

    FxHash NameHash { 0 };
    std::string Name = "";

    RxDescriptorSet mDescriptorSet {};

    RxGraphicsPipeline* pPipeline = nullptr;

    std::atomic_bool bIsBuilt { false };

    /**
     * @brief Offset into `MaterialPropertiesBuffer` for this material.
     */
    uint32 mMaterialPropertiesIndex = 0;

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
    // TODO: replace with a calculated material count
    const uint32 MaxMaterials = 64;

public:
    static FxMaterialManager& GetGlobalManager();

    void Create(uint32 materials_per_page = 64);
    static FxRef<FxMaterial> New(const std::string& name, RxGraphicsPipeline* pipeline);

    static RxDescriptorPool& GetDescriptorPool() { return GetGlobalManager().mDescriptorPool; }

    void Destroy();

    ~FxMaterialManager() { Destroy(); }

public:
    FxRef<RxSampler> pAlbedoSampler { nullptr };
    FxRef<RxSampler> pNormalSampler { nullptr };
    // RxRawGpuBuffer<FxMaterialProperties> MaterialPropertiesUbo;

    /**
     * @brief A large GPU buffer containing all loaded in material properties.
     */
    RxRawGpuBuffer<FxMaterialProperties> MaterialPropertiesBuffer;
    uint32 NumMaterialsInBuffer = 0;

    /**
     * @brief Descriptor set for material properties. Used in the light pass.
     */
    RxDescriptorSet mMaterialPropertiesDS {};

private:
    FxPagedArray<FxMaterial> mMaterials;
    RxDescriptorPool mDescriptorPool;

    bool mbInitialized : 1 = false;
};
