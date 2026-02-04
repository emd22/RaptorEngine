#include "FxMaterial.hpp"

#include <vulkan/vulkan.h>

#include <Asset/AxManager.hpp>
#include <Core/FxDefines.hpp>
#include <Core/FxStackArray.hpp>
#include <FxEngine.hpp>
#include <FxObjectManager.hpp>
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

    MaterialsInUse.InitZero(FX_MAX_MATERIALS);

    RxDescriptorPool& dp = mDescriptorPool;

    if (!dp.Pool) {
        dp.AddPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 6);
        dp.AddPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3);
        dp.Create(gRenderer->GetDevice(), FX_MAX_MATERIALS);
    }

    // Material properties buffer descriptors

    pAlbedoSampler = FxMakeRef<RxSampler>();
    pAlbedoSampler->Create();

    pNormalMapSampler = FxMakeRef<RxSampler>();
    pNormalMapSampler->Create();

    pMetallicRoughnessSampler = FxMakeRef<RxSampler>();
    pMetallicRoughnessSampler->Create();

    const uint32 material_buffer_size = FX_MAX_MATERIALS;

    FxLogInfo("Creating material buffer of size {:d}", material_buffer_size);

    MaterialPropertiesBuffer.Create(sizeof(FxMaterialProperties) * material_buffer_size,
                                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
                                    RxGpuBufferFlags::ePersistentMapped);


    if (!mMaterialPropertiesDS.IsInited()) {
        FxAssert(gRenderer->pDeferredRenderer->DsLayoutLightingMaterialProperties != nullptr);
        mMaterialPropertiesDS.Create(dp, gRenderer->pDeferredRenderer->DsLayoutLightingMaterialProperties);
    }


    {
        FxStackArray<VkWriteDescriptorSet, FxMaterial::ResourceType::eMaxImages> write_descriptor_sets;
        FxStackArray<VkDescriptorBufferInfo, FxMaterial::ResourceType::eMaxImages> write_buffer_infos;

        {
            VkDescriptorBufferInfo info {
                .buffer = MaterialPropertiesBuffer.Buffer,
                .offset = 0,
                .range = VK_WHOLE_SIZE,
            };

            VkWriteDescriptorSet buffer_write {
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = mMaterialPropertiesDS.Get(),
                .dstBinding = 0,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .pBufferInfo = write_buffer_infos.Insert(info),
            };

            write_descriptor_sets.Insert(buffer_write);
        }

        vkUpdateDescriptorSets(gRenderer->GetDevice()->Device, write_descriptor_sets.Size, write_descriptor_sets.pData,
                               0, nullptr);
    }

    RxUtil::SetDebugLabel("Material Properties DS", VK_OBJECT_TYPE_DESCRIPTOR_SET, mMaterialPropertiesDS.Get());

    mbInitialized = true;
}

FxRef<FxMaterial> FxMaterialManager::New(const std::string& name, RxPipeline* pipeline)
{
    FxMaterialManager& gm = GetGlobalManager();

    if (!gm.mMaterials.IsInited()) {
        gm.Create();
    }

    int free_material_index = gm.MaterialsInUse.FindNextFreeBit();
    FxAssert(free_material_index != FxBitset::scNoFreeBits);

    FxRef<FxMaterial> ref = FxMakeRef<FxMaterial>();

    ref->NameHash = FxHashStr32(name.c_str());
    ref->Name = name;
    ref->pPipeline = pipeline;
    ref->mMaterialPropertiesIndex = free_material_index;

    gm.MaterialsInUse.Set(free_material_index);

    return ref;
}

void FxMaterialManager::Destroy()
{
    if (!mbInitialized) {
        return;
    }

    pAlbedoSampler->Destroy();

    pNormalMapSampler->Destroy();
    pMetallicRoughnessSampler->Destroy();

    MaterialPropertiesBuffer.Destroy();

    // if (mDescriptorPool) {
    // vkDestroyDescriptorPool(gRenderer->GetDevice()->Device, mDescriptorPool.Pool, nullptr);
    // }
    //
    mDescriptorPool.Destroy();

    mbInitialized = false;
}

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
    CHECK_COMPONENT_READY(MetallicRoughness);

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

    VkDescriptorSet sets_to_bind[] = {
        mDescriptorSet.Get(),                // Set 0
        manager.mMaterialPropertiesDS.Get(), // Set 1: Material Properties Buffer
    };


    RxDescriptorSet::BindMultiple(0, *cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, *pPipeline,
                                  FxMakeSlice(sets_to_bind, FxSizeofArray(sets_to_bind)));

    return true;
}

bool FxMaterial::BindWithPipeline(RxCommandBuffer& cmd, RxPipeline& pipeline)
{
    if (!bIsBuilt.load()) {
        Build();
    }

    if (!IsReady() || !mDescriptorSet) {
        return false;
    }

    FxMaterialManager& manager = FxMaterialManager::GetGlobalManager();

    VkDescriptorSet sets_to_bind[] = {
        mDescriptorSet.Get(),                // Set 0
        manager.mMaterialPropertiesDS.Get(), // Set 1: Material Properties Buffer
    };

    RxDescriptorSet::BindMultiple(0, cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline,
                                  FxMakeSlice(sets_to_bind, FxSizeofArray(sets_to_bind)));

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

    if (mMaterialPropertiesIndex != UINT32_MAX) {
        FxMaterialManager::GetGlobalManager().MaterialsInUse.Unset(mMaterialPropertiesIndex);
    }

    // TODO: figure out why the FxRef isn't destroying the object...
    // if (Diffuse.pImage) {
    //     Diffuse.pImage->Destroy();
    // }
}


template <RxImageFormat TFormat>
static bool CheckComponentTextureLoaded(FxMaterialComponent<TFormat>& component)
{
    if (!component.pImage && component.pDataToLoad) {
        FxSlice<const uint8>& image_data = component.pDataToLoad;

        component.pImage = AxManager::LoadImageFromMemory(component.Format, image_data.pData, image_data.Size);
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
            .sampler = img->Texture.Sampler->Sampler,                                                                  \
            .imageView = img->Texture.Image.View,                                                                      \
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,                                                   \
        };                                                                                                             \
        const VkWriteDescriptorSet image_write {                                                                       \
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,                                                           \
            .dstSet = descriptor_set.Set,                                                                              \
            .dstBinding = binding,                                                                                     \
            .dstArrayElement = 0,                                                                                      \
            .descriptorCount = 1,                                                                                      \
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,                                               \
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

    if (NormalMap.pImage) {
        has_normal_map = true;
    }

    BUILD_MATERIAL_COMPONENT(NormalMap, manager.pNormalMapSampler);

    MetallicRoughness.Build(manager.pMetallicRoughnessSampler);
    BUILD_MATERIAL_COMPONENT(MetallicRoughness, manager.pMetallicRoughnessSampler);

    // Update the material descriptor
    {
        // constexpr int max_images = static_cast<int>(FxMaterial::ResourceType::eMaxImages);
        // constexpr int max_buffers = 1;

        // FxStackArray<VkDescriptorImageInfo, max_images> write_image_infos;
        // FxStackArray<VkDescriptorBufferInfo, max_buffers> write_buffer_infos;
        // FxStackArray<VkWriteDescriptorSet, (max_images + max_buffers)> write_descriptor_sets;

        // RxDescriptorSet& descriptor_set = mDescriptorSet;

        mDescriptorSet.AddImage(0, &Diffuse.pImage->Texture.Image, Diffuse.pImage->Texture.Sampler.mpPtr);
        mDescriptorSet.AddImage(1, &NormalMap.pImage->Texture.Image, NormalMap.pImage->Texture.Sampler.mpPtr);
        mDescriptorSet.AddImage(2, &MetallicRoughness.pImage->Texture.Image,
                                MetallicRoughness.pImage->Texture.Sampler.mpPtr);

        mDescriptorSet.Build();

        // Push material textures
        // PUSH_IMAGE_IF_SET(Diffuse.pImage, 0);
        // PUSH_IMAGE_IF_SET(NormalMap.pImage, 1);
        // PUSH_IMAGE_IF_SET(MetallicRoughness.pImage, 2);

        // vkUpdateDescriptorSets(gRenderer->GetDevice()->Device, write_descriptor_sets.Size,
        // write_descriptor_sets.pData,
        //                        0, nullptr);
    }

    // mMaterialPropertiesIndex = (manager.NumMaterialsInBuffer++);

    FxAssert(mMaterialPropertiesIndex != UINT32_MAX);

    FxMaterialProperties* materials_buffer = static_cast<FxMaterialProperties*>(
        manager.MaterialPropertiesBuffer.pMappedBuffer);

    FxMaterialProperties* material = &materials_buffer[mMaterialPropertiesIndex];
    material->BaseColor = Properties.BaseColor;

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
