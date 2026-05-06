#include "Material.hpp"

#include <vulkan/vulkan.h>

#include <Asset/AxManager.hpp>
#include <Core/Defines.hpp>
#include <Core/StackArray.hpp>
#include <ObjectManager.hpp>
#include <Renderer/Backend/Commands.hpp>
#include <Renderer/Backend/Device.hpp>
#include <Renderer/Backend/Pipeline.hpp>
#include <Renderer/DeferredRenderer.hpp>
#include <Renderer/Globals.hpp>
#include <Renderer/RenderBackend.hpp>

FX_SET_MODULE_NAME("Material")

namespace fx {

using namespace renderer;

void MaterialManager::Create(uint32 entities_per_page)
{
    if (mbInitialized) {
        return;
    }

    mMaterials.Create(entities_per_page);

    MaterialsInUse.InitZero(FX_MAX_MATERIALS);

    DescriptorPool& dp = mDescriptorPool;

    if (!dp.Pool) {
        dp.AddPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 512);
        dp.AddPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4);
        dp.AddPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 3);
        dp.Create(gRenderer->GetDevice(), FX_MAX_MATERIALS);
    }

    // Material properties buffer descriptors
    const uint32 material_buffer_size = FX_MAX_MATERIALS;

    MaterialPropertiesBuffer.Create(eGpuBufferType::Storage, sizeof(MaterialProperties) * material_buffer_size,
                                    VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, eGpuBufferFlags::PersistentMapped);


    if (!mMaterialPropertiesDS.IsInited()) {
        Assert(gRenderer->pDeferredRenderer->DsLayoutLightingMaterialProperties != nullptr);
        mMaterialPropertiesDS.Create(dp, gRenderer->pDeferredRenderer->DsLayoutLightingMaterialProperties, false);
    }

    mMaterialPropertiesDS.AddBuffer(0, &MaterialPropertiesBuffer, 0, VK_WHOLE_SIZE);
    mMaterialPropertiesDS.Build();

    Util::SetDebugLabel("Material Properties DS", VK_OBJECT_TYPE_DESCRIPTOR_SET, mMaterialPropertiesDS.Get());

    mbInitialized = true;
}

TSRef<Material> MaterialManager::New(const String& name, Pipeline* pipeline, bool supports_skinning)
{
    std::lock_guard guard(mInUse);

    if (!mMaterials.IsInited()) {
        Create();
    }

    uint32 free_material_index = MaterialsInUse.FindNextFreeBit();
    LogInfo("Free mat index: {}", free_material_index);
    Assert(free_material_index != Bitset::scNoFreeBits);

    TSRef<Material> ref = TSRef<Material>::New();

    ref->Name = name.Str();
    ref->pPipeline = pipeline;
    ref->mMaterialPropertiesIndex = free_material_index;
    ref->SetSupportsSkinning(supports_skinning);

    MaterialsInUse.Set(free_material_index);

    return ref;
}

void MaterialManager::Destroy()
{
    if (!mbInitialized) {
        return;
    }

    MaterialPropertiesBuffer.Destroy();

    mDescriptorPool.Destroy();

    mbInitialized = false;
}

#define CHECK_COMPONENT_READY(component_)                                                                              \
    if (component_.Exists() && !component_.pAssetImage->IsLoaded()) {                                                  \
        return false;                                                                                                  \
    }

bool Material::IsReady()
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

DescriptorSet& Material::GetDescriptorSetAlbedoOnly()
{
    if (mDsAlbedoOnly.IsInited()) {
        return mDsAlbedoOnly;
    }

    mDsAlbedoOnly.Create(gMaterialManager->GetDescriptorPool(),
                         gRenderer->pDeferredRenderer->DsLayoutGPassMaterialAlbedoOnly, false, 1);


    mDsAlbedoOnly.AddImage(0, &Diffuse.pAssetImage->Image, &gRenderer->Swapchain.ColorSampler);
    mDsAlbedoOnly.Build();

    return mDsAlbedoOnly;
}


bool Material::Bind(CommandBuffer* cmd) { return BindWithPipeline(*cmd, *pPipeline, false); }

bool Material::BindWithPipeline(CommandBuffer& cmd, Pipeline& pipeline, bool albedo_only)
{
    if (!bIsBuilt.load()) {
        Build();
    }

    if (!IsReady() || !mDsDefault) {
        return false;
    }

    pipeline.Bind(cmd);

    VkDescriptorSet sets_to_bind[] = {
        mDsDefault.Get(),                              // Set 0: Textures (Albedo, Normal map, Metallic/Roughness)
        gMaterialManager->mMaterialPropertiesDS.Get(), // Set 1: Material Properties Buffer
    };

    StackArray<uint32, 1> offsets;
    if (bSupportsSkinning) {
        offsets.Insert(gRenderer->BoneBuffer.GetBaseOffset());
    }


    DescriptorSet::BindMultipleOffset(0, cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline,
                                      MakeSlice(sets_to_bind, std::size(sets_to_bind)), Slice<uint32>(offsets));

    return true;
}

void Material::Destroy()
{
    if (bIsBuilt) {
        bIsBuilt.store(false);
    }

    if (mMaterialPropertiesIndex != UINT32_MAX) {
        gMaterialManager->MaterialsInUse.Unset(mMaterialPropertiesIndex);
    }
}


template <eImageFormat TFormat>
static bool CheckComponentTextureLoaded(MaterialComponent<TFormat>& component)
{
    if (!component.pAssetImage && component.pDataToLoad) {
        Slice<const uint8>& image_data = component.pDataToLoad;

        component.pAssetImage = gAssetManager->LoadImageFromMemory(component.Format, image_data.pData, image_data.Size);
        return false;
    }

    if (!component.pAssetImage || !component.pAssetImage.IsLoaded()) {
        return false;
    }

    return true;
}


#define BUILD_MATERIAL_COMPONENT(component_)                                                                           \
    {                                                                                                                  \
        if (component_.Build() == eMaterialComponentStatus::NotReady) {                                                \
            return;                                                                                                    \
        }                                                                                                              \
    }

void Material::SetDefaultPipeline()
{
    if (NormalMap.Exists()) {
        pPipeline = &gRenderer->pDeferredRenderer->PlGeometryWithNormalMaps;
    }
    else {
        pPipeline = &gRenderer->pDeferredRenderer->PlGeometry;
    }

    if (bSupportsSkinning) {
        pPipeline = &gRenderer->pDeferredRenderer->PlGeometrySkinned;
    }
}

void Material::Build()
{
    if (!mDsDefault.IsInited()) {
        VkDescriptorSetLayout layout = gRenderer->pDeferredRenderer->DsLayoutGPassMaterial;

        if (bSupportsSkinning) {
            layout = gRenderer->pDeferredRenderer->DsLayoutGPassSkinned;
        }

        mDsDefault.Create(gMaterialManager->GetDescriptorPool(), layout, false, 1);
    }

    // Build components
    BUILD_MATERIAL_COMPONENT(Diffuse);

    BUILD_MATERIAL_COMPONENT(NormalMap);

    // MetallicRoughness.Build();
    BUILD_MATERIAL_COMPONENT(MetallicRoughness);

    // Fill material descriptor

    mDsDefault.AddImage(0, &Diffuse.pAssetImage->Image, &gRenderer->Swapchain.ColorSampler);

    // When there is no normal map, we do not add it to the descriptor set. We should not bind extra garbage when we do
    // not need to.

    if (NormalMap.Exists()) {
        mDsDefault.AddImage(1, &NormalMap.pAssetImage->Image, &gRenderer->Swapchain.NormalsSampler);

        // To reduce permutations -- the metallic roughness map should only be
        // enabled if there is also a normal map.
        if (!MetallicRoughness.Exists()) {
            MetallicRoughness.pAssetImage = AxImage::GetEmptyImage<eImageFormat::RGBA8_SRGB>();
        }

        mDsDefault.AddImage(2, &MetallicRoughness.pAssetImage->Image, &gRenderer->Swapchain.ColorSampler);
    }

    if (bSupportsSkinning) {
        mDsDefault.AddBuffer(3, &gRenderer->BoneBuffer.GetGpuBuffer(), 0, gRenderer->BoneBuffer.Size);
    }

    mDsDefault.Build();

    Assert(mMaterialPropertiesIndex != UINT32_MAX);

    MaterialProperties* materials_buffer = static_cast<MaterialProperties*>(
        gMaterialManager->MaterialPropertiesBuffer.pMappedBuffer);

    MaterialProperties* material = &materials_buffer[mMaterialPropertiesIndex];
    material->BaseColor = Properties.BaseColor;

    if (!pPipeline) {
        SetDefaultPipeline();
    }


    bIsBuilt.store(true);
}

} // namespace fx
