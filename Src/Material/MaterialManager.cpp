#include "MaterialManager.hpp"

#include "Material.hpp"

#include <Asset/AssetManager.hpp>
#include <Renderer/Backend/Descriptors.hpp>
#include <Renderer/Backend/Pipeline.hpp>
#include <Renderer/Globals.hpp>
#include <Renderer/PipelineCache.hpp>
#include <Renderer/RenderBackend.hpp>
#include <Texture/TextureManager.hpp>

namespace fx {

const MaterialID MaterialID::Null = MaterialID(0);

void MaterialManager::Create()
{
	std::lock_guard guard(mInUse);

	if (mbInitialized) {
		return;
	}

	mMaterialList.Init(FX_MAX_BOUND_MATERIALS);

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

	Material* material = mMaterialList.GetItem(id.GetID());
	if (material == nullptr) {
		LogError(LC_CORE, "Could not bind material {}", id.GetID());
		return false;
	}

	return material->Bind(cmd);
}

bool MaterialManager::BindWithPipeline(const renderer::CommandBuffer& cmd, const renderer::Pipeline& pipeline,
									   const MaterialID& id)
{
	Material* material = mMaterialList.GetItem(id.GetID());
	if (material == nullptr) {
		LogError(LC_CORE, "Could not bind material {}", id.GetID());
		return false;
	}

	return material->BindWithPipeline(cmd, pipeline);
}

#define NM_PINK	 255, 80, 203, 255
#define NM_BLACK 0, 0, 0, 255

void MaterialManager::MakeNullMaterial()
{
	Material* material = mMaterialList.NewItem();

	material->ID = MaterialID(0);
	material->Name = "NullMaterial";
	material->SetPipeline(renderer::ePipelineName::Geometry);
	material->SetSupportsSkinning(false);

	SizedArray<uint8> diffuse_data = {
		NM_PINK,  NM_PINK,	NM_BLACK, NM_BLACK, /* 0 */
		NM_PINK,  NM_PINK,	NM_BLACK, NM_BLACK, /* 1 */
		NM_BLACK, NM_BLACK, NM_PINK,  NM_PINK,	/* 2 */
		NM_BLACK, NM_BLACK, NM_PINK,  NM_PINK,	/* 3 */
	};


	fx::Image* diffuse_image = gTextureManager->NewTexture();
	AssetTicket diffuse_ticket(diffuse_image);

	// Upload the null material diffuse texture to GPU
	renderer::gRenderer->SubmitImmediateUploadCmd(
		[&](renderer::CommandBuffer& cmd)
		{
			ImageInfo image_info { Vec2u(4, 4), eImageFormat::RGBA8_UNorm, 0, 1,
								   Slice<const uint8>(diffuse_data.pData, diffuse_data.Size) };
			diffuse_image->CreateFromData(cmd, image_info, eImageCreateFlags::None);
			diffuse_ticket.MarkAndSignalLoaded();
		});


	AssetTicket null_image_ticket = gAssetManager->GetNullImageTicket(eImageFormat::RGBA8_UNorm);


	material->Attach(Material::eResourceType::Diffuse, diffuse_ticket);
	material->Attach(Material::eResourceType::Normal, null_image_ticket);
	material->Attach(Material::eResourceType::MetallicRoughness, null_image_ticket);

	material->bNearestFiltering = true;

	material->bReadyToCheck.test_and_set();

	material->Build();

	LogInfo("Created null material (Id={})", material->GetID());
}

Material* MaterialManager::GetNewMaterial()
{
	// Do not lock mutex here, this is called by NewMaterial.

	uint32 material_index = 0;
	Material* material = mMaterialList.NewItem(&material_index);

	// Ensure that the null material does not get overwritten
	Assert(material_index != 0);
	material->ID = MaterialID(material_index);

	return material;
}

Material* MaterialManager::GetMaterial(const MaterialID& id)
{
	std::lock_guard guard(mInUse);
	return mMaterialList.GetItem(id.GetID());
}

MaterialID MaterialManager::NewMaterial(const String& name, renderer::ePipelineName pl_name, bool supports_skinning)
{
	if (!mMaterialList.IsInited()) {
		Create();
	}

	std::lock_guard guard(mInUse);

	Material* material = GetNewMaterial();
	material->Name = name.Str();
	material->SetPipeline(pl_name);
	material->SetSupportsSkinning(supports_skinning);

	return material->ID;
}

void MaterialManager::DestroyMaterial(const MaterialID& id)
{
	std::lock_guard guard(mInUse);
	mMaterialList.MarkItemFree(id.GetID());
}

void MaterialManager::Destroy()
{
	if (!mbInitialized) {
		return;
	}

	mMaterialList.Free();

	MaterialPropertiesBuffer.Destroy();
	mDescriptorPool.Destroy();

	mbInitialized = false;
}

MaterialManager::~MaterialManager() { Destroy(); }

} // namespace fx
