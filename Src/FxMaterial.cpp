#include "FxMaterial.hpp"

#include <vulkan/vulkan.h>

#include <Asset/FxAssetManager.hpp>
#include <Core/FxDefines.hpp>
#include <Core/FxStackArray.hpp>
#include <FxEngine.hpp>
#include <Renderer/Backend/RxCommands.hpp>
#include <Renderer/Backend/RxDevice.hpp>
#include <Renderer/Backend/RxPipeline.hpp>
#include <Renderer/RxDeferred.hpp>
#include <Renderer/RxRenderBackend.hpp>

FX_SET_MODULE_NAME("FxMaterial")

FxMaterialManager& FxMaterialManager::GetGlobalManager()
{
    static FxMaterialManager global_manager;
    return global_manager;
}

void FxMaterialManager::Create(uint32 entities_per_page)
{
    if (mbInitialized) {
        return;
    }

    GetGlobalManager().mMaterials.Create(entities_per_page);

    RxDescriptorPool& dp = mDescriptorPool;

    if (!dp.Pool) {
        dp.AddPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 3);
        dp.AddPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 3);
        dp.Create(gRenderer->GetDevice(), MaxMaterials);
    }


    // Material properties buffer descriptors

    pAlbedoSampler = FxMakeRef<RxSampler>();
    pAlbedoSampler->Create();

    pNormalMapSampler = FxMakeRef<RxSampler>();
    pNormalMapSampler->Create();

    const uint32 material_buffer_size = FX_MAX_MATERIALS;

    FxLogInfo("Creating material buffer of size {:d}", material_buffer_size);

    MaterialPropertiesBuffer.Create(material_buffer_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                    VMA_MEMORY_USAGE_CPU_TO_GPU, RxGpuBufferFlags::PersistentMapped);


    if (!mMaterialPropertiesDS.IsInited()) {
        assert(gRenderer->pDeferredRenderer->DsLayoutLightingMaterialProperties != nullptr);
        mMaterialPropertiesDS.Create(dp, gRenderer->pDeferredRenderer->DsLayoutLightingMaterialProperties);
    }


    {
        //        FxStackArray<VkWriteDescriptorSet, 1> write_descriptor_sets;

        VkDescriptorBufferInfo info {
            .buffer = MaterialPropertiesBuffer.Buffer,
            .offset = 0,
            .range = sizeof(FxMaterialProperties),
        };

        VkWriteDescriptorSet buffer_write {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC,
            .descriptorCount = 1,
            .dstSet = mMaterialPropertiesDS.Set,
            .dstBinding = 0,
            .dstArrayElement = 0,
            .pBufferInfo = &info,
        };

        vkUpdateDescriptorSets(gRenderer->GetDevice()->Device, 1, &buffer_write, 0, nullptr);
    }

    RxUtil::SetDebugLabel("Material Properties DS", VK_OBJECT_TYPE_DESCRIPTOR_SET, mMaterialPropertiesDS.Set);

    mbInitialized = true;
}

FxRef<FxMaterial> FxMaterialManager::New(const std::string& name, RxGraphicsPipeline* pipeline)
{
    FxMaterialManager& gm = GetGlobalManager();

    if (!gm.mMaterials.IsInited()) {
        gm.Create();
    }

    FxRef<FxMaterial> ref = FxMakeRef<FxMaterial>();

    ref->NameHash = FxHashStr32(name.c_str());
    ref->Name = name;
    ref->pPipeline = pipeline;

    return ref;
}

void FxMaterialManager::Destroy()
{
    if (!mbInitialized) {
        return;
    }

    pAlbedoSampler->Destroy();

    pNormalMapSampler->Destroy();

    MaterialPropertiesBuffer.Destroy();

    // if (mDescriptorPool) {
    // vkDestroyDescriptorPool(gRenderer->GetDevice()->Device, mDescriptorPool.Pool, nullptr);
    // }
    //
    mDescriptorPool.Destroy();

    mbInitialized = false;
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

//     VkResult status = vkCreateDescriptorSetLayout(RendererVulkan->GetDevice()->Device, &ds_info, nullptr,
//     &mSetLayout); if (status != VK_SUCCESS) {
//         FxModulePanic("Failed to create material descriptor set layout", status);
//     }

//     return mSetLayout;
// }

#define CHECK_COMPONENT_READY(component_)                                                                              \
    if (!component_.pImage || !component_.pImage->IsLoaded()) {                                                        \
        return false;                                                                                                  \
    }

bool FxMaterial::IsReady()
{
    if (mbIsReady) {
        return true;
    }

    CHECK_COMPONENT_READY(Diffuse);
    CHECK_COMPONENT_READY(NormalMap);

    return (mbIsReady = true);
}

bool FxMaterial::Bind(RxCommandBuffer* cmd)
{
    if (!bIsBuilt.load()) {
        Build();
    }

    if (!IsReady() || !mDescriptorSet) {
        return false;
    }

    if (!cmd) {
        cmd = &gRenderer->GetFrame()->CommandBuffer;
    }

    if (pPipeline) {
        pPipeline->Bind(*cmd);
    }

    FxMaterialManager& manager = FxMaterialManager::GetGlobalManager();

    VkDescriptorSet sets_to_bind[] = { mDescriptorSet.Set, manager.mMaterialPropertiesDS.Set };

    const uint32 num_sets = FxSizeofArray(sets_to_bind);
    const uint32 properties_offset = static_cast<uint32>(mMaterialPropertiesIndex * sizeof(FxMaterialProperties));

    uint32 dynamic_offsets[] = { properties_offset };

    RxDescriptorSet::BindMultipleOffset(*cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, *pPipeline,
                                        FxMakeSlice(sets_to_bind, num_sets), FxMakeSlice(dynamic_offsets, 1));
    // mDescriptorSet.Bind(*cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, *Pipeline);

    // mMaterialPropertiesDS.Bind(*cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, gRenderer->DeferredgRenderer->GPassPipeline);

    return true;
}

void FxMaterial::Destroy()
{
    if (bIsBuilt) {
        // if (mSetLayout) {
        //     vkDestroyDescriptorSetLayout(gRenderer->GetDevice()->Device, mSetLayout, nullptr);
        // }

        bIsBuilt.store(false);
    }

    // TODO: figure out why the FxRef isn't destroying the object...
    // if (Diffuse.pImage) {
    //     Diffuse.pImage->Destroy();
    // }
}


template <VkFormat TFormat>
static bool CheckComponentTextureLoaded(FxMaterialComponent<TFormat>& component)
{
    if (!component.pImage && component.pDataToLoad) {
        FxSlice<const uint8>& image_data = component.pDataToLoad;

        component.pImage = FxAssetManager::LoadImageFromMemory(component.Format, image_data.pData, image_data.Size);
        return false;
    }

    if (!component.pImage || !component.pImage->IsLoaded()) {
        return false;
    }

    return true;
}


#define PUSH_IMAGE_IF_SET(img, binding)                                                                                \
    if (img != nullptr) {                                                                                              \
        const VkDescriptorImageInfo image_info {                                                                       \
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,                                                   \
            .imageView = img->Texture.Image.View,                                                                      \
            .sampler = img->Texture.Sampler->Sampler,                                                                  \
        };                                                                                                             \
        const VkWriteDescriptorSet image_write {                                                                       \
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,                                                           \
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,                                               \
            .descriptorCount = 1,                                                                                      \
            .dstSet = descriptor_set.Set,                                                                              \
            .dstBinding = binding,                                                                                     \
            .dstArrayElement = 0,                                                                                      \
            .pImageInfo = write_image_infos.Insert(image_info),                                                        \
        };                                                                                                             \
        write_descriptor_sets.Insert(image_write);                                                                     \
    }

#define BUILD_MATERIAL_COMPONENT(component_, sampler_)                                                                 \
    {                                                                                                                  \
        if (component_.Build(sampler_) == FxMaterialComponentStatus::eNotReady) {                                      \
            return;                                                                                                    \
        }                                                                                                              \
    }

void FxMaterial::Build()
{
    if (!mDescriptorSet.IsInited()) {
        mDescriptorSet.Create(FxMaterialManager::GetDescriptorPool(),
                              gRenderer->pDeferredRenderer->DsLayoutGPassMaterial, 2);
    }

    bool has_normal_map = false;

    FxMaterialManager& manager = FxMaterialManager::GetGlobalManager();

    // Build components
    BUILD_MATERIAL_COMPONENT(Diffuse, manager.pAlbedoSampler);

    // if (Normal.Build(manager.pNormalSampler) == FxMaterialComponent::Status::NotReady) {
    // }
    if (NormalMap.pImage) {
        has_normal_map = true;

        BUILD_MATERIAL_COMPONENT(NormalMap, manager.pNormalMapSampler);
    }
    BUILD_MATERIAL_COMPONENT(NormalMap, manager.pNormalMapSampler);

    // Update the material descriptor
    {
        constexpr int max_images = static_cast<int>(FxMaterial::ResourceType::eMaxImages);
        FxStackArray<VkDescriptorImageInfo, max_images> write_image_infos;
        FxStackArray<VkWriteDescriptorSet, max_images> write_descriptor_sets;

        RxDescriptorSet& descriptor_set = mDescriptorSet;

        // Push material textures
        PUSH_IMAGE_IF_SET(Diffuse.pImage, 0);
        PUSH_IMAGE_IF_SET(NormalMap.pImage, 1);

        vkUpdateDescriptorSets(gRenderer->GetDevice()->Device, write_descriptor_sets.Size, write_descriptor_sets.pData,
                               0, nullptr);
    }

    mMaterialPropertiesIndex = (manager.NumMaterialsInBuffer++ /* * RendererFramesInFlight */);

    FxMaterialProperties* materials_buffer = static_cast<FxMaterialProperties*>(
        manager.MaterialPropertiesBuffer.pMappedBuffer);

    FxMaterialProperties* material = &materials_buffer[mMaterialPropertiesIndex];
    material->BaseColor = Properties.BaseColor;
    printf("Base colour: %u\n", Properties.BaseColor.Value);

    if (has_normal_map) {
        pPipeline = &gRenderer->pDeferredRenderer->PlGeometryWithNormalMaps;
    }
    else {
        pPipeline = &gRenderer->pDeferredRenderer->PlGeometry;
    }

    // material->BaseColor = ;

    // Mark material as built
    bIsBuilt.store(true);
}
