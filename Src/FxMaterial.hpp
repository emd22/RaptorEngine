#pragma once

#include <Asset/FxImage.hpp>

#include <Core/FxPagedArray.hpp>
#include <Core/FxHash.hpp>
#include <Core/Log.hpp>

#include <Renderer/Backend/RvkDescriptors.hpp>

#include <vulkan/vulkan.h>

class FxMaterial
{
public:
    enum ResourceType {
        Diffuse,

        MaxImages,
    };
public:
    FxMaterial() = default;

    void Attach(ResourceType type, FxRef<FxImage>& image)
    {
        switch (type) {
            case ResourceType::Diffuse:
                DiffuseTexture = image;
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
    bool Bind(RvkCommandBuffer* cmd);

    void Build();
    void Destroy();

    ~FxMaterial()
    {
        Destroy();
    }

private:
    VkDescriptorSetLayout BuildLayout();

public:
    FxRef<FxImage> DiffuseTexture{nullptr};

    FxHash NameHash{0};
    std::string Name = "";

    RvkDescriptorSet mDescriptorSet{};
    RvkGraphicsPipeline* Pipeline = nullptr;

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
    static FxRef<FxMaterial> New(const std::string& name, RvkGraphicsPipeline* pipeline);

    static RvkDescriptorPool& GetDescriptorPool()
    {
        return GetGlobalManager().mDescriptorPool;
    }

    void Destroy();

    ~FxMaterialManager()
    {
        Destroy();
    }

public:
    FxRef<RvkSampler> AlbedoSampler{nullptr};

private:
    FxPagedArray<FxMaterial> mMaterials;
    RvkDescriptorPool mDescriptorPool;

    bool mInitialized = false;
};
