#include "FxMaterial.hpp"
#include <Renderer/Backend/RxPipeline.hpp>
#include "vulkan/vulkan_core.h"

#include <Core/FxDefines.hpp>
#include <Renderer/Renderer.hpp>
#include <Renderer/FxDeferred.hpp>
#include <Renderer/Backend/RxDevice.hpp>

#include <Renderer/Backend/RxCommands.hpp>

#include <Core/FxStackArray.hpp>

#include <vulkan/vulkan.h>

FX_SET_MODULE_NAME("FxMaterial")

FxMaterialManager& FxMaterialManager::GetGlobalManager()
{
    static FxMaterialManager global_manager;
    return global_manager;
}

void FxMaterialManager::Create(uint32 entities_per_page)
{
    if (mInitialized) {
        return;
    }

    GetGlobalManager().mMaterials.Create(entities_per_page);

    RxDescriptorPool& dp = mDescriptorPool;

    dp.AddPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 3);
    dp.AddPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3);
    dp.Create(Renderer->GetDevice(), MaxMaterials);


    AlbedoSampler = FxMakeRef<RxSampler>();
    AlbedoSampler->Create();


    const uint32 material_buffer_size = FX_MAX_MATERIALS * RendererFramesInFlight;

    Log::Info("Creating material buffer of size %u", material_buffer_size);

    MaterialPropertiesBuffer.Create(
        material_buffer_size,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VMA_MEMORY_USAGE_CPU_TO_GPU,
        RxGpuBufferFlags::PersistentMapped
    );

    mInitialized = true;
}

FxRef<FxMaterial> FxMaterialManager::New(const std::string& name, RxGraphicsPipeline* pipeline)
{
    FxMaterialManager& gm = GetGlobalManager();

    if (!gm.mMaterials.IsInited()) {
        gm.Create();
    }

    FxRef<FxMaterial> ref = FxRef<FxMaterial>::New();

    ref->NameHash = FxHashStr(name.c_str());
    ref->Name = name;
    ref->Pipeline = pipeline;

    return ref;
}

void FxMaterialManager::Destroy()
{
    if (!mInitialized) {
        return;
    }

    AlbedoSampler->Destroy();

    MaterialPropertiesBuffer.Destroy();

    // if (mDescriptorPool) {
        // vkDestroyDescriptorPool(Renderer->GetDevice()->Device, mDescriptorPool.Pool, nullptr);
    // }
    //
    mDescriptorPool.Destroy();

    mInitialized = false;
}


// VkDescriptorSetLayout FxMaterial::BuildLayout()
// {
//     VkDescriptorSetLayoutBinding image_layout_binding {
//         .binding = 1,
//         .descriptorCount = 1,
//         .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
//         .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
//         .pImmutableSamplers = nullptr,
//     };

//     VkDescriptorSetLayoutBinding bindings[] = {
//         image_layout_binding,
//     };

//     VkDescriptorSetLayoutCreateInfo ds_info {
//         .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
//         .bindingCount = sizeof(bindings) / sizeof(bindings[0]),
//         .pBindings = bindings,
//     };

//     VkResult status = vkCreateDescriptorSetLayout(RendererVulkan->GetDevice()->Device, &ds_info, nullptr, &mSetLayout);
//     if (status != VK_SUCCESS) {
//         FxModulePanic("Failed to create material descriptor set layout", status);
//     }

//     return mSetLayout;
// }
//
bool FxMaterial::IsReady()
{
    if (mIsReady) {
        return true;
    }

    if (!DiffuseTexture.Texture || !DiffuseTexture.Texture->IsLoaded()) {
        return false;
    }

    return (mIsReady = true);
}

bool FxMaterial::Bind(RxCommandBuffer* cmd)
{
    if (!IsBuilt.load()) {
        Build();
    }

    if (!IsReady() || !mDescriptorSet) {
        return false;
    }


    if (!cmd) {
        cmd = &Renderer->GetFrame()->CommandBuffer;
    }

    // VkDescriptorSet sets_to_bind[] = {
    //     mDescriptorSet.Set,
    //     mMaterialPropertiesDS.Set
    // };

    // RxDescriptorSet::BindMultiple(*cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, *Pipeline, sets_to_bind, 2);
    mDescriptorSet.Bind(*cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, *Pipeline);

    // mMaterialPropertiesDS.Bind(*cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Renderer->DeferredRenderer->GPassPipeline);

    return true;
}

void FxMaterial::Destroy()
{
    if (IsBuilt) {
        // if (mSetLayout) {
        //     vkDestroyDescriptorSetLayout(Renderer->GetDevice()->Device, mSetLayout, nullptr);
        // }

        IsBuilt.store(false);
    }

    // TODO: figure out why the FxRef isn't destroying the object...
    if (DiffuseTexture.Texture) {
        DiffuseTexture.Texture->Destroy();
    }
}

#include <Asset/FxAssetManager.hpp>

bool FxMaterial::CheckComponentTextureLoaded(FxMaterialComponent& component)
{
    if (!component.Texture && component.DataToLoad) {
        FxSlice<const uint8>& image_data = component.DataToLoad;

        component.Texture = FxAssetManager::LoadFromMemory<FxAssetImage>(image_data.Ptr, image_data.Size);
        return false;
    }

    if (!component.Texture || !component.Texture->IsLoaded()) {
        return false;
    }
    return true;

}

#define PUSH_IMAGE_IF_SET(img, binding) \
    if (img != nullptr) { \
        VkDescriptorImageInfo image_info { \
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, \
            .imageView = img->Texture.Image.View, \
            .sampler = img->Texture.Sampler->Sampler, \
        }; \
        VkWriteDescriptorSet image_write { \
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, \
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, \
            .descriptorCount = 1, \
            .dstSet = descriptor_set.Set, \
            .dstBinding = binding, \
            .dstArrayElement = 0, \
            .pImageInfo = &image_info, \
        }; \
        write_descriptor_sets.Insert(image_write); \
    }

void FxMaterial::Build()
{
    if (!mDescriptorSet.IsInited()) {
        mDescriptorSet.Create(FxMaterialManager::GetDescriptorPool(), Renderer->DeferredRenderer->DsLayoutGPassMaterial);
    }

    FxMaterialManager& manager = FxMaterialManager::GetGlobalManager();

    if (!CheckComponentTextureLoaded(DiffuseTexture)) {
        return;
    }

    if (DiffuseTexture.Texture) {
        DiffuseTexture.Texture->Texture.SetSampler(manager.AlbedoSampler);
    }


    // Material properties buffer descriptors

    if (!mMaterialPropertiesDS.IsInited()) {
        mMaterialPropertiesDS.Create(FxMaterialManager::GetDescriptorPool(), Renderer->DeferredRenderer->DsLayoutLightingMaterialProperties);
    }

    {
        constexpr const int max_images = static_cast<int>(FxMaterial::ResourceType::MaxImages);
        FxStackArray<VkWriteDescriptorSet, max_images> write_descriptor_sets;

        RxDescriptorSet& descriptor_set = mDescriptorSet;

        // Push material textures
        PUSH_IMAGE_IF_SET(DiffuseTexture.Texture, 0);

        vkUpdateDescriptorSets(Renderer->GetDevice()->Device, write_descriptor_sets.Size, write_descriptor_sets.Data, 0, nullptr);
    }

    mMaterialPropertiesIndex = (manager.NumMaterialsInBuffer /* * RendererFramesInFlight */);

    {
        FxStackArray<VkWriteDescriptorSet, 1> write_descriptor_sets;

        {
            VkDescriptorBufferInfo info{
                .buffer = manager.MaterialPropertiesBuffer.Buffer,
                .offset = mMaterialPropertiesIndex,
                .range = sizeof(FxMaterialProperties)
            };

            VkWriteDescriptorSet buffer_write{
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .descriptorCount = 1,
                .dstSet = mMaterialPropertiesDS.Set,
                .dstBinding = 0,
                .dstArrayElement = 0,
                .pBufferInfo = &info
            };

            write_descriptor_sets.Insert(buffer_write);
        }

        vkUpdateDescriptorSets(Renderer->GetDevice()->Device, write_descriptor_sets.Size, write_descriptor_sets.Data, 0, nullptr);
    }

    FxMaterialProperties* materials_buffer = reinterpret_cast<FxMaterialProperties*>(manager.MaterialPropertiesBuffer.MappedBuffer);

    FxMaterialProperties* material = &materials_buffer[mMaterialPropertiesIndex];
    material->BaseColor = Properties.BaseColor;

    // material->BaseColor = ;

    // Mark material as built
    IsBuilt.store(true);
}
