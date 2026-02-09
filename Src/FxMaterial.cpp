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
    const uint32 material_buffer_size = FX_MAX_MATERIALS;

    FxLogInfo("Creating material buffer of size {:d}", material_buffer_size);

    MaterialPropertiesBuffer.Create(RxGpuBufferType::eStorage, sizeof(FxMaterialProperties) * material_buffer_size,
                                    VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, RxGpuBufferFlags::ePersistentMapped);


    if (!mMaterialPropertiesDS.IsInited()) {
        FxAssert(gRenderer->pDeferredRenderer->DsLayoutLightingMaterialProperties != nullptr);
        mMaterialPropertiesDS.Create(dp, gRenderer->pDeferredRenderer->DsLayoutLightingMaterialProperties);
    }

    mMaterialPropertiesDS.AddBuffer(0, &MaterialPropertiesBuffer, 0, VK_WHOLE_SIZE);
    mMaterialPropertiesDS.Build();

    RxUtil::SetDebugLabel("Material Properties DS", VK_OBJECT_TYPE_DESCRIPTOR_SET, mMaterialPropertiesDS.Get());

    FxLogInfo("Done!");

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

    MaterialPropertiesBuffer.Destroy();

    mDescriptorPool.Destroy();

    mbInitialized = false;
}

#define CHECK_COMPONENT_READY(component_)                                                                              \
    if (component_.Exists() && !component_.pImage->IsLoaded()) {                                                       \
        return false;                                                                                                  \
    }

bool FxMaterial::IsReady()
{
    if (mbIsReady) {
        return true;
    }

    if (!Diffuse.Exists()) {
        return false;
    }

    CHECK_COMPONENT_READY(Diffuse);
    CHECK_COMPONENT_READY(NormalMap);
    CHECK_COMPONENT_READY(MetallicRoughness);

    return (mbIsReady = true);
}

RxDescriptorSet& FxMaterial::GetDescriptorSetAlbedoOnly()
{
    if (mDsAlbedoOnly.IsInited()) {
        return mDsAlbedoOnly;
    }

    mDsAlbedoOnly.Create(FxMaterialManager::GetDescriptorPool(),
                         gRenderer->pDeferredRenderer->DsLayoutGPassMaterialAlbedoOnly, 1);


    mDsAlbedoOnly.AddImage(0, &Diffuse.pImage->Image, &gRenderer->Swapchain.ColorSampler);
    mDsAlbedoOnly.Build();

    return mDsAlbedoOnly;
}


bool FxMaterial::Bind(RxCommandBuffer* cmd)
{
    if (!bIsBuilt.load()) {
        Build();
    }

    if (!IsReady() || !mDsDefault) {
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
        mDsDefault.Get(),                    // Set 0
        manager.mMaterialPropertiesDS.Get(), // Set 1: Material Properties Buffer
    };


    RxDescriptorSet::BindMultiple(0, *cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, *pPipeline,
                                  FxMakeSlice(sets_to_bind, FxSizeofArray(sets_to_bind)));

    return true;
}

bool FxMaterial::BindWithPipeline(RxCommandBuffer& cmd, RxPipeline& pipeline, bool albedo_only)
{
    if (!bIsBuilt.load()) {
        Build();
    }

    if (!IsReady() || !mDsDefault) {
        return false;
    }

    pipeline.Bind(cmd);

    FxMaterialManager& manager = FxMaterialManager::GetGlobalManager();

    RxDescriptorSet* descriptor_set = &mDsDefault;
    if (albedo_only) {
        descriptor_set = &GetDescriptorSetAlbedoOnly();
    }

    VkDescriptorSet sets_to_bind[] = {
        descriptor_set->Get(),               // Set 0: Textures (Albedo, Normal map, Metallic/Roughness)
        manager.mMaterialPropertiesDS.Get(), // Set 1: Material Properties Buffer
    };

    RxDescriptorSet::BindMultiple(0, cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline,
                                  FxMakeSlice(sets_to_bind, FxSizeofArray(sets_to_bind)));

    return true;
}

void FxMaterial::Destroy()
{
    if (bIsBuilt) {
        bIsBuilt.store(false);
    }

    if (mMaterialPropertiesIndex != UINT32_MAX) {
        FxMaterialManager::GetGlobalManager().MaterialsInUse.Unset(mMaterialPropertiesIndex);
    }
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


#define BUILD_MATERIAL_COMPONENT(component_)                                                                           \
    {                                                                                                                  \
        if (component_.Build() == FxMaterialComponentStatus::eNotReady) {                                              \
            return;                                                                                                    \
        }                                                                                                              \
    }

void FxMaterial::Build()
{
    if (!mDsDefault.IsInited()) {
        mDsDefault.Create(FxMaterialManager::GetDescriptorPool(), gRenderer->pDeferredRenderer->DsLayoutGPassMaterial,
                          2);
    }

    FxMaterialManager& manager = FxMaterialManager::GetGlobalManager();

    // Build components
    BUILD_MATERIAL_COMPONENT(Diffuse);

    BUILD_MATERIAL_COMPONENT(NormalMap);

    MetallicRoughness.Build();
    BUILD_MATERIAL_COMPONENT(MetallicRoughness);

    // Fill material descriptor

    mDsDefault.AddImage(0, &Diffuse.pImage->Image, &gRenderer->Swapchain.ColorSampler);

    // When there is no normal map, we do not add it to the descriptor set. We should not bind extra garbage when we do
    // not need to.

    if (NormalMap.Exists()) {
        mDsDefault.AddImage(1, &NormalMap.pImage->Image, &gRenderer->Swapchain.NormalsSampler);

        // To reduce permutations -- the metallic roughness map should only be
        // enabled if there is also a normal map.
        if (!MetallicRoughness.Exists()) {
            MetallicRoughness.pImage = AxImage::GetEmptyImage<RxImageFormat::eRGBA8_SRGB>();
        }

        mDsDefault.AddImage(2, &MetallicRoughness.pImage->Image, &gRenderer->Swapchain.ColorSampler);
    }

    mDsDefault.Build();


    FxAssert(mMaterialPropertiesIndex != UINT32_MAX);

    FxMaterialProperties* materials_buffer = static_cast<FxMaterialProperties*>(
        manager.MaterialPropertiesBuffer.pMappedBuffer);

    FxMaterialProperties* material = &materials_buffer[mMaterialPropertiesIndex];
    material->BaseColor = Properties.BaseColor;

    if (NormalMap.Exists()) {
        pPipeline = &gRenderer->pDeferredRenderer->PlGeometryWithNormalMaps;
    }
    else {
        pPipeline = &gRenderer->pDeferredRenderer->PlGeometry;
    }

    bIsBuilt.store(true);
}
