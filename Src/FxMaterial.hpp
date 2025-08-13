#pragma once

#include <Asset/FxAssetImage.hpp>

#include <Core/FxPagedArray.hpp>
#include <Core/FxHash.hpp>
#include <Core/Log.hpp>


#include <Renderer/Backend/RxDescriptors.hpp>
#include <Renderer/Backend/RxGpuBuffer.hpp>

#include <vulkan/vulkan.h>

// TODO: Replace this, make dynamic
#define FX_MAX_MATERIALS 64

struct FxMaterialComponent
{
    FxRef<FxAssetImage> Texture{nullptr};
    FxSlice<const uint8> DataToLoad{nullptr};

    ~FxMaterialComponent() = default;
};

struct FxMaterialProperties
{
    alignas(16) Vec4f BaseColor = Vec4f(0);
};

class FxMaterial
{
public:
    enum ResourceType {
        Diffuse,

        MaxImages,
    };
public:
    FxMaterial() = default;

    void Attach(ResourceType type, FxRef<FxAssetImage>& image)
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

    /**
     * Binds the material to be used in the given command buffer.
     * @returns True if the material was bound succesfully.
     */
    bool Bind(RxCommandBuffer* cmd);

    void Build();
    void Destroy();

    ~FxMaterial()
    {
        Destroy();
    }

private:
    VkDescriptorSetLayout BuildLayout();

    bool CheckComponentTextureLoaded(FxMaterialComponent& component);

public:
//    FxRef<FxAssetImage> DiffuseTexture{nullptr};
    FxMaterialComponent DiffuseTexture;

    FxMaterialProperties Properties{};

    FxHash NameHash{0};
    std::string Name = "";

    RxDescriptorSet mDescriptorSet{};
    RxGraphicsPipeline* Pipeline = nullptr;

    std::atomic_bool IsBuilt {false};
private:
    VkDescriptorSetLayout mSetLayout = nullptr;

    bool mIsReady = false;
};



class FxMaterialManager
{
    // TODO: replace with a calculated material count
    const uint32 MaxMaterials = 32;

public:
    static FxMaterialManager& GetGlobalManager();

    void Create(uint32 materials_per_page=32);
    static FxRef<FxMaterial> New(const std::string& name, RxGraphicsPipeline* pipeline);

    static RxDescriptorPool& GetDescriptorPool()
    {
        return GetGlobalManager().mDescriptorPool;
    }

    void Destroy();

    ~FxMaterialManager()
    {
        Destroy();
    }

public:
    FxRef<RxSampler> AlbedoSampler{nullptr};

    RxRawGpuBuffer<FxMaterialProperties> MaterialPropertiesUbo;

    /**
     * @brief A large GPU buffer containing all material properties for the frame.
     */
    RxRawGpuBuffer<FxMaterialProperties> MaterialPropertiesBuffer;
    uint32 NumMaterialsInBuffer = 0;

private:
    FxPagedArray<FxMaterial> mMaterials;
    RxDescriptorPool mDescriptorPool;



    bool mInitialized = false;
};
