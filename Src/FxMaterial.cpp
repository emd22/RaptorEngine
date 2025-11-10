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


    const uint32 material_buffer_size = FX_MAX_MATERIALS;

    FxLogInfo("Creating material buffer of size {:d}", material_buffer_size);

    MaterialPropertiesBuffer.Create(material_buffer_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                    VMA_MEMORY_USAGE_CPU_TO_GPU, RxGpuBufferFlags::PersistentMapped);


    if (!mMaterialPropertiesDS.IsInited()) {
        assert(gRenderer->DeferredRenderer->DsLayoutLightingMaterialProperties != nullptr);
        mMaterialPropertiesDS.Create(dp, gRenderer->DeferredRenderer->DsLayoutLightingMaterialProperties);
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

    ref->NameHash = FxHashStr(name.c_str());
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
//
bool FxMaterial::IsReady()
{
    if (mbIsReady) {
        return true;
    }

    if (!DiffuseComponent.pTexture || !DiffuseComponent.pTexture->IsLoaded()) {
        return false;
    }

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
    if (DiffuseComponent.pTexture) {
        DiffuseComponent.pTexture->Destroy();
    }
}


bool FxMaterialComponent::CheckIfReady()
{
    // If there is data passed in and the image has not been loaded yet, load it using the
    // asset manager. This will be validated on the next call of this function. (when attempting to build the material)
    if (!pTexture && pDataToLoad) {
        FxSlice<const uint8>& image_data = pDataToLoad;

        pTexture = FxAssetManager::LoadImageFromMemory(image_data.Ptr, image_data.Size);
        return false;
    }

    // If there is no texture and we are not loaded, return not loaded.
    if (!pTexture || !pTexture->IsLoaded()) {
        return false;
    }

    return true;
}

bool FxMaterial::CheckComponentTextureLoaded(FxMaterialComponent& component)
{
    if (!component.pTexture && component.pDataToLoad) {
        FxSlice<const uint8>& image_data = component.pDataToLoad;

        component.pTexture = FxAssetManager::LoadImageFromMemory(image_data.Ptr, image_data.Size);
        return false;
    }

    if (!component.pTexture || !component.pTexture->IsLoaded()) {
        return false;
    }

    return true;
}

FxMaterialComponent::Status FxMaterialComponent::Build(const FxRef<RxSampler>& sampler)
{
    // There is no texture provided, we will use the base colours passed in and a dummy texture
    if (!pTexture && !pDataToLoad) {
        pTexture = FxAssetImage::GetEmptyImage();
    }

    if (!CheckIfReady()) {
        // The texture is not ready, return the status to the material build function
        return Status::NotReady;
    }


    pTexture->Texture.SetSampler(sampler);

    return Status::Ready;
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
    if (component_.Build(sampler_) == FxMaterialComponent::Status::NotReady) {                                         \
        return;                                                                                                        \
    }

void FxMaterial::Build()
{
    if (!mDescriptorSet.IsInited()) {
        mDescriptorSet.Create(FxMaterialManager::GetDescriptorPool(),
                              gRenderer->DeferredRenderer->DsLayoutGPassMaterial);
    }

    FxMaterialManager& manager = FxMaterialManager::GetGlobalManager();

    // Build components
    BUILD_MATERIAL_COMPONENT(DiffuseComponent, manager.pAlbedoSampler);

    // Update the material descriptor
    {
        constexpr int max_images = static_cast<int>(FxMaterial::ResourceType::MaxImages);
        FxStackArray<VkDescriptorImageInfo, max_images> write_image_infos;
        FxStackArray<VkWriteDescriptorSet, max_images> write_descriptor_sets;

        RxDescriptorSet& descriptor_set = mDescriptorSet;

        // Push material textures
        PUSH_IMAGE_IF_SET(DiffuseComponent.pTexture, 0);

        vkUpdateDescriptorSets(gRenderer->GetDevice()->Device, write_descriptor_sets.Size, write_descriptor_sets.Data,
                               0, nullptr);
    }

    mMaterialPropertiesIndex = (manager.NumMaterialsInBuffer++ /* * RendererFramesInFlight */);

    FxMaterialProperties* materials_buffer = reinterpret_cast<FxMaterialProperties*>(
        manager.MaterialPropertiesBuffer.MappedBuffer);

    FxMaterialProperties* material = &materials_buffer[mMaterialPropertiesIndex];
    material->BaseColor = Properties.BaseColor;
    printf("Base colour: %u\n", Properties.BaseColor.Value);

    // material->BaseColor = ;

    // Mark material as built
    bIsBuilt.store(true);
}
