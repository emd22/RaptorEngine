#pragma once

#include <vulkan/vulkan.h>

#include <Asset/FxAssetImage.hpp>
#include <Core/FxHash.hpp>
#include <Core/FxPagedArray.hpp>
#include <Core/Log.hpp>
#include <Renderer/Backend/RxDescriptors.hpp>
#include <Renderer/Backend/RxGpuBuffer.hpp>

// TODO: Replace this, make dynamic
#define FX_MAX_MATERIALS 256

struct FxMaterialComponent
{
    FxRef<FxAssetImage> Texture { nullptr };
    FxSlice<const uint8> DataToLoad { nullptr };

    ~FxMaterialComponent() = default;
};

struct FxMaterialProperties
{
    alignas(16) uint32 BaseColor = 0xFFFFFFFF;
};

class FxMaterial
{
public:
    enum ResourceType
    {
        Diffuse,

        MaxImages,
    };

public:
    FxMaterial() = default;

    void Attach(ResourceType type, const FxRef<FxAssetImage>& image)
    {
        switch (type) {
        case ResourceType::Diffuse:
            DiffuseTexture.Texture = image;
            break;
        default:
            Log::Error("Unsupported resource type to attach to material!", 0);
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
    FxMaterialComponent DiffuseTexture;

    FxMaterialProperties Properties {};

    FxHash NameHash { 0 };
    std::string Name = "";

    RxDescriptorSet mDescriptorSet {};

    RxGraphicsPipeline* Pipeline = nullptr;

    std::atomic_bool IsBuilt { false };

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

    bool mIsReady = false;
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
    FxRef<RxSampler> AlbedoSampler { nullptr };
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

    bool mInitialized = false;
};
