/*
 * File:        Material.cpp
 * Author:      emd22
 * Created:     23/06/2025
 * Description: Material modification and loading functions
 */

#include "Material.hpp"

#include "MaterialManagerFwd.hpp"

#include <vulkan/vulkan.h>

#include <Asset/AssetManager.hpp>
#include <Asset/MipmapGen.hpp>
#include <Core/Defines.hpp>
#include <Core/StackArray.hpp>
#include <Object/ObjectManager.hpp>
#include <Renderer/Backend/Commands.hpp>
#include <Renderer/Backend/DescriptorCache.hpp>
#include <Renderer/Backend/Device.hpp>
#include <Renderer/Backend/Image.hpp>
#include <Renderer/Backend/Pipeline.hpp>
#include <Renderer/Backend/Sampler/SamplerCache.hpp>
#include <Renderer/DeferredRenderer.hpp>
#include <Renderer/Globals.hpp>
#include <Renderer/PipelineCache.hpp>
#include <Renderer/RenderBackend.hpp>
#include <Texture/TextureManager.hpp>

FX_SET_MODULE_NAME("Material")

namespace fx {

using namespace renderer;

/////////////////////////////////////
// Material Component
/////////////////////////////////////

MaterialComponent& MaterialComponent::operator=(const MaterialComponent& other)
{
	Ticket = other.Ticket;
	pImage = other.pImage;

	UploadSrc = other.UploadSrc;
	pDataToLoad = other.pDataToLoad;
	ImageToUpload = other.ImageToUpload;

	TextureCacheID = other.TextureCacheID;

	return *this;
}

MaterialComponent::Status MaterialComponent::Build()
{
	// There is no texture provided, we will use the base colours passed in and a dummy texture
	if (!pImage && !pDataToLoad.pData && !ImageToUpload.ImageData.pData) {
		return Status::MissingComponent;
	}

	if (!CheckIfReady()) {
		// The texture is not ready, return the status to the material build function
		return Status::NotReady;
	}

	return Status::Ready;
}

void MaterialComponent::SetTicket(AssetTicket& ticket)
{
	Ticket = ticket;
	pImage = static_cast<fx::Image*>(ticket.Get());
}

void MaterialComponent::SetTicket(AssetTicket&& ticket)
{
	Ticket = std::move(ticket);
	pImage = static_cast<fx::Image*>(Ticket.Get());
}


bool MaterialComponent::CheckIfReady()
{
	// If there is data passed in and the image has not been loaded yet, load it using the
	// asset manager. This will be validated on the next call of this function. (when attempting to build the
	// material)
	if (!pImage || mbRequiresUpdate) {
		AssertMsg(UploadSrc != eMaterialComponentUploadSrc::None, "UploadSrc has not been set!");

		if (UploadSrc == eMaterialComponentUploadSrc::ProcessAndUpload) {
			AssetTicket ticket = gAssetManager->LoadImageFromMemory(eImageType::Flat, eImageFormat::RGBA8_UNorm,
																	pDataToLoad, eImageCreateFlags::None);
			SetTicket(ticket);
		}
		else if (UploadSrc == eMaterialComponentUploadSrc::DirectUpload) {
			// If the image has not been created yet, create the reference.
			// if (!pImage && !Ticket.IsInvalid()) {
			// 	Ticket = AssetTicket(gTextureManager->NewTexture());
			// }

			AssetTicket ticket = gAssetManager->UploadImage(ImageToUpload);
			SetTicket(ticket);

			// If the image is previously initialized, we want to update the image with the new mip.
			/// @see DoDirectUpload() in AxManager.cpp
			// AssetManagerFwd::LoadImageFromPixels(pAssetImage, ImageToUpload);
		}

		mbRequiresUpdate = false;

		return false;
	}

	// If there is no texture and we are not loaded, return not loaded.
	if (!Ticket.IsLoaded()) {
		return false;
	}

	return true;
}

/////////////////////////////////////
// Material
/////////////////////////////////////

#define CHECK_COMPONENT_READY(component_)                                                                              \
	if (component_.Exists() && (!component_.pImage || !component_.Ticket.IsLoaded())) {                                \
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
		if (loader.Pack.IsOpen()) {                                                                                    \
			component_.ImageToUpload = loader.GetQuality(quality);                                                     \
			component_.pAssetImage->InvalidateLoaded();                                                                \
			component_.UploadSrc = eMaterialComponentUploadSrc::DirectUpload;                                          \
			component_.RequireUpdate();                                                                                \
		}                                                                                                              \
	}


void Material::RequestQuality(uint32 quality)
{
	// REQUEST_COMPONENT_HIGHER_DETAIL(Diffuse);
	// REQUEST_COMPONENT_HIGHER_DETAIL(NormalMap);
	// REQUEST_COMPONENT_HIGHER_DETAIL(MetallicRoughness);

	bReadyToCheck.test_and_set();
	bIsBuilt.store(false);
	mbIsReady = false;
}


bool Material::Bind(const CommandBuffer& cmd) { return BindWithPipeline(cmd, *mpPipeline); }

bool Material::BindWithPipeline(const CommandBuffer& cmd, const Pipeline& pipeline)
{
	if (!bIsBuilt.load()) {
		Build();
	}

	if (!IsReady() || !mDescriptorSet) {
		return false;
	}

	pipeline.Bind(cmd);

	VkDescriptorSet sets_to_bind[] = {
		mDescriptorSet->Get(), // Set 0: Textures (Albedo, Normal map, Metallic/Roughness)
	};

	StackArray<uint32, 2> offsets;
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

static float32 GetComponentMaxLOD(const MaterialComponent& component)
{
	if (!component.pImage) {
		return 0.0f;
	}

	const ImageInfo& info = component.pImage->GetInfo();

	return static_cast<float32>(info.MipCount);
}

static float32 GetComponentMinLOD(const MaterialComponent& component)
{
	if (!component.pImage) {
		return 0.0f;
	}

	const ImageInfo& info = component.pImage->GetInfo();

	return static_cast<float32>(info.MipLevel);
}


void Material::Build()
{
	// Build components
	BUILD_REQUIRED_MATERIAL_COMPONENT(Diffuse);
	BUILD_MATERIAL_COMPONENT(NormalMap);
	BUILD_MATERIAL_COMPONENT(MetallicRoughness);

	AssertMsg(Diffuse.Ticket.IsValid(), "Diffuse texture must be valid");

	SamplerProps diffuse_sampler_props { .MinLOD = GetComponentMinLOD(Diffuse), .MaxLOD = GetComponentMaxLOD(Diffuse) };

	if (bNearestFiltering) {
		diffuse_sampler_props.SetNearest();
	}

	if (NormalMap.Exists() && !MetallicRoughness.Exists()) {
		MetallicRoughness.SetTicket(gAssetManager->GetNullImageTicket(eImageFormat::RGBA8_UNorm));
	}

	if (mDescriptorSet == nullptr) {
		SizedArray<DescriptorEntry> ds_entries(6);
		ds_entries.Emplace(DescriptorEntry::AsImage(0, eShaderType::Pixel, Diffuse.pImage,
													gSamplerCache->Request(diffuse_sampler_props)));

		if (NormalMap.Exists()) {
			ds_entries.Emplace(DescriptorEntry::AsImage(1, eShaderType::Pixel, NormalMap.pImage,
														gSamplerCache->Request(diffuse_sampler_props)));
			ds_entries.Emplace(DescriptorEntry::AsImage(2, eShaderType::Pixel, MetallicRoughness.pImage,
														gSamplerCache->Request(diffuse_sampler_props)));
		}

		if (bSupportsSkinning) {
			ds_entries.Emplace(DescriptorEntry::AsBuffer(3, eShaderType::Pixel, &gRenderer->BoneBuffer.GetGpuBuffer(),
														 0, gRenderer->BoneBuffer.PageSize));
		}


		std::pair<Hash32, DescriptorSet*> result = gDescriptorCache->Request(ds_entries);
		mDescriptorSet = result.second;


		// VkDescriptorSetLayout layout = gRenderer->pDeferredRenderer->DsLayoutGPassMaterial;

		// if (bSupportsSkinning) {
		// 	layout = gRenderer->pDeferredRenderer->DsLayoutGPassSkinned;
		// }

		// mDescriptorSet.Create(MaterialManagerFwd::GetDescriptorPool(), layout, false, 1);
	}


	// mDsDefault.AddImage(0, Diffuse.pImage, gSamplerCache->Request(diffuse_sampler_props));

	// // When there is no normal map, we do not add it to the descriptor set. We should not bind extra garbage when we
	// do
	// // not need to.

	// if (NormalMap.Exists()) {
	// 	mDsDefault.AddImage(1, NormalMap.pImage,
	// 						gSamplerCache->Request(SamplerProps { .MaxLOD = GetComponentMaxLOD(NormalMap) }));

	// 	// To reduce permutations -- the metallic roughness map should only be
	// 	// enabled if there is also a normal map.
	// 	if (!MetallicRoughness.Exists()) {
	// 		MetallicRoughness.SetTicket(gAssetManager->GetNullImageTicket(eImageFormat::RGBA8_UNorm));
	// 	}

	// 	mDsDefault.AddImage(2, MetallicRoughness.pImage,
	// 						gSamplerCache->Request(SamplerProps { .MaxLOD = GetComponentMaxLOD(MetallicRoughness) }));
	// }

	// if (bSupportsSkinning) {
	// 	mDsDefault.AddBuffer(3, &gRenderer->BoneBuffer.GetGpuBuffer(), 0, gRenderer->BoneBuffer.PageSize);
	// }

	// mDsDefault.Build();

	SubmitProperties(Properties);

	if (!mpPipeline) {
		SetDefaultPipeline();
	}


	bIsBuilt.store(true);
}

} // namespace fx
