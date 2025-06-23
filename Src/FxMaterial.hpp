#pragma once

#include <Asset/FxImage.hpp>

#include <Core/FxHash.hpp>
#include <Core/Log.hpp>

#include <Renderer/Backend/RvkDescriptors.hpp>

#include <vulkan/vulkan.h>

class FxMaterial
{
public:
    enum class ResourceType {
        Diffuse,

        MaxImages,
    };
public:
    FxMaterial() = default;

    void Attach(ResourceType type, FxRef<FxImage>& image)
    {
        switch (type) {
            case ResourceType::Diffuse:
                Diffuse = image;
                break;
            default:
                Log::Error("Unsupported resource type to attach to material!", 0);
                break;
        }
    }

    void Bind(RvkCommandBuffer* cmd);

    void Build(RvkGraphicsPipeline* pipeline);
    void Destroy();

    ~FxMaterial()
    {
        Destroy();
    }

private:
    VkDescriptorSetLayout BuildLayout();

public:
    FxRef<FxImage> Diffuse{nullptr};

    FxHash NameHash{0};
    std::string Name = "";

    RvkDescriptorSet mDescriptorSet;
    RvkGraphicsPipeline* Pipeline = nullptr;

private:
    VkDescriptorSetLayout mSetLayout = nullptr;

    std::atomic_bool mIsBuilt = false;
};


class FxMaterialManager
{
    // TODO: replace with a calculated material count
    const uint32 MaxMaterials = 32;

public:
    static FxMaterialManager& GetGlobalManager();

    void Create(uint32 materials_per_page=32);

    static FxRef<FxMaterial> New(const std::string& name);


    static RvkDescriptorPool& GetPool()
    {
        return GetGlobalManager().mDescriptorPool;
    }

    void Destroy();

private:
    FxPagedArray<FxMaterial> mMaterials;
    RvkDescriptorPool mDescriptorPool;

    bool mInitialized = false;
};
