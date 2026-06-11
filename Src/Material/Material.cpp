#include "Material.hpp"

#include "MaterialManagerFwd.hpp"

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
#include <Renderer/PipelineCache.hpp>
#include <Renderer/RenderBackend.hpp>

FX_SET_MODULE_NAME("Material")

namespace fx {

using namespace renderer;


#define CHECK_COMPONENT_READY(component_)                                                                              \
    if (component_.Exists() && (!component_.pAssetImage || !component_.pAssetImage->IsLoaded())) {                     \
        return false;                                                                                                  \
    }

bool Material::IsReady()
{
    if (mbIsReady) {
        return true;
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

    mDsAlbedoOnly.Create(MaterialManagerFwd::GetDescriptorPool(),
                         gRenderer->pDeferredRenderer->DsLayoutGPassMaterialAlbedoOnly, false, 1);


    mDsAlbedoOnly.AddImage(0, &Diffuse.pAssetImage->Image, &gRenderer->Swapchain.ColorSampler);
    mDsAlbedoOnly.Build();

    return mDsAlbedoOnly;
}


bool Material::Bind(const CommandBuffer& cmd) { return BindWithPipeline(cmd, *mpPipeline, false); }

bool Material::BindWithPipeline(const CommandBuffer& cmd, Pipeline& pipeline, bool albedo_only)
{
    if (!bIsBuilt.load()) {
        Build();
    }

    if (!IsReady() || !mDsDefault) {
        // gMaterialManager->GetNullMaterial()->BindWithPipeline(cmd, pipeline);
        return false;
    }

    pipeline.Bind(cmd);

    VkDescriptorSet sets_to_bind[] = {
        mDsDefault.Get(),                             // Set 0: Textures (Albedo, Normal map, Metallic/Roughness)
        MaterialManagerFwd::GetDescriptorSet().Get(), // Set 1: Material Properties Buffer
    };

    StackArray<uint32, 2> offsets = { 0 };
    if (bSupportsSkinning) {
        offsets.Insert(gRenderer->BoneBuffer.GetBaseOffset());
    }


    DescriptorSet::BindMultipleOffset(0, cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline,
                                      MakeSlice(sets_to_bind, std::size(sets_to_bind)), Slice<uint32>(offsets));

    return true;
}

Material& Material::operator=(const Material& other)
{
    ID = other.ID;

    Diffuse = other.Diffuse;
    NormalMap = other.NormalMap;
    MetallicRoughness = other.MetallicRoughness;

    Properties = other.Properties;

    Name = other.Name;

    mpPipeline = other.mpPipeline;
    mPipelineName = other.mPipelineName;

    bIsBuilt = false;

    bSupportsSkinning = other.bSupportsSkinning;
    bNearestFiltering = other.bNearestFiltering;

    mbIsReady = false;
    mbIsBeingBuilt = false;

    return *this;
}

void Material::Destroy()
{
    if (bIsBuilt) {
        bIsBuilt.store(false);
    }

    MaterialManagerFwd::DestroyMaterial(ID);
}


// template <eImageFormat TFormat>
// static bool CheckComponentTextureLoaded(MaterialComponent<TFormat>& component)
// {
//     if (!component.pAssetImage && component.UploadSrc == eMaterialComponentUploadSrc::ProcessAndUpload) {
//         Slice<const uint8>& image_data = component.pDataToLoad;

//         component.pAssetImage = gAssetManager->LoadImageFromMemory(component.Format, image_data.pData,
//         image_data.Size); return false;
//     }
//     else if (!component.pAssetImage && component.UploadSrc == eMaterialComponentUploadSrc::DirectUpload) {
//         ImageInfo& image_data = component.ImageToUpload;

//         component.pAssetImage = gAssetManager->LoadImageFromPixels(component.Format, image_data.pData,
//         image_data.Size); return false;-
//     }

//     if (!component.pAssetImage || !component.pAssetImage.IsLoaded()) {
//         return false;
//     }

//     return true;
// }


#define BUILD_MATERIAL_COMPONENT(component_)                                                                           \
    {                                                                                                                  \
        if (component_.Build() == eMaterialComponentStatus::NotReady) {                                                \
            return;                                                                                                    \
        }                                                                                                              \
    }

void Material::SetDefaultPipeline()
{
    if (NormalMap.Exists()) {
        SetPipeline(ePipelineName::GeometryNormalMaps);

        if (bSupportsSkinning) {
            SetPipeline(ePipelineName::GeometrySkinned);
        }
    }
    else {
        SetPipeline(ePipelineName::Geometry);
    }
}

void Material::SubmitProperties(const MaterialProperties& properties)
{
    MaterialProperties* properties_buffer = static_cast<MaterialProperties*>(
        MaterialManagerFwd::GetMaterialPropertiesBuffer().pMappedBuffer);

    memcpy(&properties_buffer[ID.GetID()], &properties, sizeof(MaterialProperties));
}

void Material::SetPipeline(renderer::ePipelineName pl_name)
{
    mPipelineName = pl_name;
    mpPipeline = &gPipelineCache->Request(pl_name);
}

void Material::Build()
{
    if (!mDsDefault.IsInited()) {
        VkDescriptorSetLayout layout = gRenderer->pDeferredRenderer->DsLayoutGPassMaterial;

        if (bSupportsSkinning) {
            layout = gRenderer->pDeferredRenderer->DsLayoutGPassSkinned;
        }

        mDsDefault.Create(MaterialManagerFwd::GetDescriptorPool(), layout, false, 1);
    }

    // Build components
    BUILD_MATERIAL_COMPONENT(Diffuse);
    BUILD_MATERIAL_COMPONENT(NormalMap);
    BUILD_MATERIAL_COMPONENT(MetallicRoughness);

    // Fill material descriptor

    Sampler* diffuse_sampler = &gRenderer->Swapchain.ColorSampler;
    if (bNearestFiltering) {
        diffuse_sampler = &gRenderer->Swapchain.ColorSamplerNearest;
    }

    mDsDefault.AddImage(0, &Diffuse.pAssetImage->Image, diffuse_sampler);

    // When there is no normal map, we do not add it to the descriptor set. We should not bind extra garbage when we do
    // not need to.

    if (NormalMap.Exists()) {
        mDsDefault.AddImage(1, &NormalMap.pAssetImage->Image, &gRenderer->Swapchain.NormalsSampler);

        // To reduce permutations -- the metallic roughness map should only be
        // enabled if there is also a normal map.
        if (!MetallicRoughness.Exists()) {
            MetallicRoughness.pAssetImage = AxImage::GetEmptyImage<eImageFormat::RGBA8_UNorm>();
        }

        mDsDefault.AddImage(2, &MetallicRoughness.pAssetImage->Image, &gRenderer->Swapchain.ColorSampler);
    }

    if (bSupportsSkinning) {
        mDsDefault.AddBuffer(3, &gRenderer->BoneBuffer.GetGpuBuffer(), 0, gRenderer->BoneBuffer.PageSize);
    }

    mDsDefault.Build();

    SubmitProperties(Properties);

    if (!mpPipeline) {
        SetDefaultPipeline();
    }


    bIsBuilt.store(true);
}

} // namespace fx
