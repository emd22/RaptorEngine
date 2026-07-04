#include "Material.hpp"

#include "MaterialManagerFwd.hpp"

#include <vulkan/vulkan.h>

#include <Asset/AxManager.hpp>
#include <Asset/MipmapGen.hpp>
#include <Core/Defines.hpp>
#include <Core/StackArray.hpp>
#include <Object/ObjectManager.hpp>
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

/////////////////////////////////////
// Material Component
/////////////////////////////////////

MaterialComponent& MaterialComponent::operator=(const MaterialComponent& other)
{
    pAssetImage = other.pAssetImage;

    UploadSrc = other.UploadSrc;
    pDataToLoad = other.pDataToLoad;
    ImageToUpload = other.ImageToUpload;

    TextureCacheID = other.TextureCacheID;

    return *this;
}

MaterialComponent::Status MaterialComponent::Build()
{
    // There is no texture provided, we will use the base colours passed in and a dummy texture
    if (!pAssetImage.IsValid() && !pDataToLoad.pData && !ImageToUpload.ImageData.pData) {
        return Status::MissingComponent;
    }

    if (!CheckIfReady()) {
        // The texture is not ready, return the status to the material build function
        return Status::NotReady;
    }

    return Status::Ready;
}


bool MaterialComponent::CheckIfReady()
{
    // If there is data passed in and the image has not been loaded yet, load it using the
    // asset manager. This will be validated on the next call of this function. (when attempting to build the
    // material)
    if (!pAssetImage || mbRequiresUpdate) {
        AssertMsg(UploadSrc != eMaterialComponentUploadSrc::None, "UploadSrc has not been set!");

        LogInfo("CHECKING IF READY");

        if (UploadSrc == eMaterialComponentUploadSrc::ProcessAndUpload) {
            pAssetImage = AssetManagerFwd::LoadImageFromMemory(ImageFormat, pDataToLoad.pData, pDataToLoad.Size);
        }
        else if (UploadSrc == eMaterialComponentUploadSrc::DirectUpload) {
            // If the image has not been created yet, create the reference.
            if (!pAssetImage.IsValid()) {
                pAssetImage = TSRef<AxImage>::New();
            }

            // If the image is previously initialized, we want to update the image with the new mip.
            /// @see DoDirectUpload() in AxManager.cpp
            AssetManagerFwd::LoadImageFromPixels(pAssetImage, ImageToUpload);
        }

        mbRequiresUpdate = false;

        return false;
    }

    // If there is no texture and we are not loaded, return not loaded.
    if (!pAssetImage->IsLoaded()) {
        return false;
    }

    return true;
}

/////////////////////////////////////
// Material
/////////////////////////////////////

#define CHECK_COMPONENT_READY(component_)                                                                              \
    if (component_.Exists() && (!component_.pAssetImage || !component_.pAssetImage->IsLoaded())) {                     \
        return false;                                                                                                  \
    }

bool Material::IsReady()
{
    if (!bReadyToCheck.test()) {
        return false;
    }

    if (mbIsReady) {
        return true;
    }


    CHECK_COMPONENT_READY(Diffuse);
    CHECK_COMPONENT_READY(NormalMap);
    CHECK_COMPONENT_READY(MetallicRoughness);

    return (mbIsReady = true);
}

#define REQUEST_COMPONENT_HIGHER_DETAIL(component_)                                                                    \
    if (component_.Exists()) {                                                                                         \
        MipmapLoader loader {};                                                                                        \
        String texture_cache_path = String::Fmt("{}/Models/TGen/{}.ftx", gAssetManager->GetScenePath(),                \
                                                component_.TextureCacheID);                                            \
        loader.Open(texture_cache_path.CStr());                                                                        \
        component_.ImageToUpload = loader.GetMip(quality);                                                             \
        component_.pAssetImage->InvalidateLoaded();                                                                    \
        component_.UploadSrc = eMaterialComponentUploadSrc::DirectUpload;                                              \
        component_.RequireUpdate();                                                                                    \
    }


void Material::RequestQuality(uint32 quality)
{
    REQUEST_COMPONENT_HIGHER_DETAIL(Diffuse);

    bReadyToCheck.test_and_set();
    bIsBuilt.store(false);
    mbIsReady = false;

    // REQUEST_COMPONENT_HIGHER_DETAIL(NormalMap);
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


bool Material::Bind(const CommandBuffer& cmd) { return BindWithPipeline(cmd, *mpPipeline); }

bool Material::BindWithPipeline(const CommandBuffer& cmd, const Pipeline& pipeline)
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

#define BUILD_REQUIRED_MATERIAL_COMPONENT(component_)                                                                  \
    {                                                                                                                  \
        if (component_.Build() != eMaterialComponentStatus::Ready) {                                                   \
            return;                                                                                                    \
        }                                                                                                              \
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
    // Build components
    BUILD_REQUIRED_MATERIAL_COMPONENT(Diffuse);
    BUILD_MATERIAL_COMPONENT(NormalMap);
    BUILD_MATERIAL_COMPONENT(MetallicRoughness);

    if (!mDsDefault.IsInited()) {
        VkDescriptorSetLayout layout = gRenderer->pDeferredRenderer->DsLayoutGPassMaterial;

        if (bSupportsSkinning) {
            layout = gRenderer->pDeferredRenderer->DsLayoutGPassSkinned;
        }

        mDsDefault.Create(MaterialManagerFwd::GetDescriptorPool(), layout, false, 1);
    }


    // Fill material descriptor

    Sampler* diffuse_sampler = &gRenderer->Swapchain.ColorSampler;
    if (bNearestFiltering) {
        diffuse_sampler = &gRenderer->Swapchain.ColorSamplerNearest;
    }

    AssertMsg(Diffuse.pAssetImage.IsValid(), "Diffuse texture must be valid");

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
