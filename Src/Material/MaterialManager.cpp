#include "MaterialManager.hpp"

#include "Material.hpp"

#include <Renderer/Backend/Descriptors.hpp>
#include <Renderer/Backend/Pipeline.hpp>
#include <Renderer/Globals.hpp>
#include <Renderer/PipelineCache.hpp>
#include <Renderer/RenderBackend.hpp>

namespace fx {

const MaterialID MaterialID::Null = MaterialID(0);

void MaterialManager::Create()
{
    std::lock_guard guard(mInUse);

    if (mbInitialized) {
        return;
    }

    mMaterials.InitCapacity(FX_MAX_BOUND_MATERIALS);
    // mMaterialUsages.InitSize(FX_MAX_BOUND_MATERIALS);
    // memset(mMaterialUsages.pData, 0, mMaterialUsages.GetSizeInBytes());

    MaterialsInUse.InitZero(FX_MAX_BOUND_MATERIALS);

    // The null material should always be set

    renderer::DescriptorPool& dp = mDescriptorPool;

    if (!dp.Pool) {
        dp.AddPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 512);
        dp.AddPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 10);
        dp.AddPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 10);
        dp.Create(renderer::gRenderer->GetDevice(), FX_MAX_BOUND_MATERIALS);
    }

    // Material properties buffer descriptors
    const uint32 material_buffer_size = FX_MAX_BOUND_MATERIALS;

    MaterialPropertiesBuffer.Create(renderer::eGpuBufferType::Storage,
                                    sizeof(MaterialProperties) * material_buffer_size,
                                    VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, eGpuBufferFlags::PersistentMapped);


    if (!mMaterialPropertiesDS.IsInited()) {
        Assert(renderer::gRenderer->pDeferredRenderer->DsLayoutLightingMaterialProperties != nullptr);
        mMaterialPropertiesDS.Create(dp, renderer::gRenderer->pDeferredRenderer->DsLayoutLightingMaterialProperties,
                                     false);
    }

    mMaterialPropertiesDS.AddBuffer(0, &MaterialPropertiesBuffer, 0, VK_WHOLE_SIZE);
    mMaterialPropertiesDS.Build();

    MakeNullMaterial();

    mbInitialized = true;
}

bool MaterialManager::Bind(const renderer::CommandBuffer& cmd, const MaterialID& id)
{
    // Note that if the id is zero, this binds the null material.
    return mMaterials[id.GetID()].Bind(cmd);
}

#define NM_PINK  255, 80, 203, 255
#define NM_BLACK 0, 0, 0, 255

void MaterialManager::MakeNullMaterial()
{
    MaterialsInUse.Set(0);

    Material* material = mMaterials.Insert();
    material->ID = MaterialID(0);
    material->Name = "NullMaterial";
    material->pPipeline = &renderer::gPipelineCache->Request(renderer::ePipelineName::Geometry);
    material->SetSupportsSkinning(false);

    SizedArray<uint8> diffuse_data = {
        NM_PINK,  NM_PINK,  NM_BLACK, NM_BLACK, /* 0 */
        NM_PINK,  NM_PINK,  NM_BLACK, NM_BLACK, /* 1 */
        NM_BLACK, NM_BLACK, NM_PINK,  NM_PINK,  /* 2 */
        NM_BLACK, NM_BLACK, NM_PINK,  NM_PINK,  /* 3 */
    };

    TSRef<AxImage> diffuse = TSRef<AxImage>::New();
    renderer::gRenderer->SubmitImmediateUploadCmd(
        [&](renderer::CommandBuffer& cmd)
        {
            diffuse->Image.CreateFromData(cmd, renderer::eImageType::Flat, Vec2u(4, 4), 1,
                                          renderer::eImageFormat::RGBA8_UNorm, diffuse_data, eImageCreateFlags::None);

            diffuse->MarkAndSignalLoaded();
        });


    material->Attach(Material::eResourceType::Diffuse, diffuse);
    material->Attach(Material::eResourceType::Normal, AxImage::GetEmptyImage<renderer::eImageFormat::RGBA8_UNorm>());
    material->Attach(Material::eResourceType::MetallicRoughness,
                     AxImage::GetEmptyImage<renderer::eImageFormat::RGBA8_UNorm>());

    material->bNearestFiltering = true;

    material->Build();

    LogInfo("Created null material (Id={})", material->GetID());
}

Material* MaterialManager::GetNewMaterial()
{
    uint32 material_index = MaterialsInUse.FindNextFreeBit();
    Assert(material_index != Bitset::scNoFreeBits);
    Assert(material_index != 0);

    Material* material;

    if (material_index >= mMaterials.Size) {
        LogInfo(LC_CORE, "Adding new material {}", material_index);
        Assert(mMaterials.Size == material_index);
        material = mMaterials.Insert();
    }
    else {
        material = &mMaterials[material_index];
        new (material) Material;
    }

    MaterialsInUse.Set(material_index);
    material->ID = MaterialID(material_index);

    return material;
}

Material* MaterialManager::GetMaterial(const MaterialID& id)
{
    std::lock_guard guard(mInUse);

    Assert(MaterialsInUse.Get(id.GetID()) == true);

    return &mMaterials[id.GetID()];
}

MaterialID MaterialManager::NewMaterial(const String& name, renderer::Pipeline* pipeline, bool supports_skinning)
{
    std::lock_guard guard(mInUse);

    if (!mMaterials.IsInited()) {
        Create();
    }

    Material* material = GetNewMaterial();
    material->Name = name.Str();
    material->pPipeline = pipeline;
    material->SetSupportsSkinning(supports_skinning);

    return material->ID;
}

void MaterialManager::DestroyMaterial(const MaterialID& id)
{
    if (id.IsNull()) {
        return;
    }

    std::lock_guard guard(mInUse);

    // If the material exists, destroy it
    if (MaterialsInUse.Get(id.GetID())) {
        Material* mat = GetMaterial(id);
        mat->~Material();
    }

    MaterialsInUse.Unset(id.GetID());
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

MaterialManager::~MaterialManager() { Destroy(); }

} // namespace fx
