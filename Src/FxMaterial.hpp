#pragma once

#include <Asset/FxAssetImage.hpp>

#include <Core/FxPagedArray.hpp>
#include <Core/FxHash.hpp>
#include <Core/Log.hpp>


#include <Renderer/Backend/RxDescriptors.hpp>

#include <vulkan/vulkan.h>

struct FxMaterialComponent
{
    FxRef<FxAssetImage> Texture{nullptr};
    FxSlice<const uint8> DataToLoad{nullptr};
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
    
    void CheckComponentTextureLoaded(FxMaterialComponent& component);

public:
//    FxRef<FxAssetImage> DiffuseTexture{nullptr};
    FxMaterialComponent DiffuseTexture;

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

private:
    FxPagedArray<FxMaterial> mMaterials;
    RxDescriptorPool mDescriptorPool;

    bool mInitialized = false;
};
