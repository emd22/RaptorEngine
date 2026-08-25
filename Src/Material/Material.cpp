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
#include <Material/MaterialManager.hpp>
#include <Object/ObjectManager.hpp>
#include <Renderer/Backend/Commands.hpp>
#include <Renderer/Backend/DescriptorCache.hpp>
#include <Renderer/Backend/Device.hpp>
#include <Renderer/Backend/Image.hpp>
#include <Renderer/Backend/Pipeline.hpp>
#include <Renderer/Backend/Sampler/SamplerCache.hpp>
#include <Renderer/Globals.hpp>
#include <Renderer/GraphicsBackend.hpp>
#include <Renderer/PipelineCache.hpp>
#include <Renderer/TiledForwardRenderer.hpp>
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


bool Material::BindWithPipeline(const CommandBuffer& cmd, const Pipeline& pipeline)
{
	if (!bIsBuilt.load()) {
		Build();
	}

	if (!IsReady()) {
		return false;
	}

	renderer::DescriptorSet* descriptor_set = mpDescriptorSet;
	const renderer::PipelineNameInfo& pl_info = GetPipelineNameInfo(pipeline.Name);

	// This is only for materials that were defined as a 'full' material originally, but will be replaced into a
	// different material later. Since this function is only really called from the segmented RenderList, this is likely
	// only called for the NullMaterial.
	if (HasFlag(pl_info.Flags, ePipelineNameFlags::AlbedoOnly) && !IsAlbedoOnly()) {
		descriptor_set = RequestAlbedoOnlyDescriptors();
	}

	// Buffer offsets
	StackArray<uint32, 2> offsets;
	if (bSupportsSkinning) {
		offsets.Insert(gGraphics->BoneBuffer.GetBaseOffset());
	}
	offsets.Insert(gGraphics->LightBuffer.GetBaseOffset());

	// Bind the descriptor set
	descriptor_set->Bind(1, cmd, pipeline, Slice<uint32>(offsets));

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

renderer::ePipelineName Material::GetRequiredPipeline() const
{
	if (NormalMap.Exists()) {
		return ePipelineName::GeometryNormalMaps;

		if (bSupportsSkinning) {
			return ePipelineName::GeometrySkinned;
		}
	}
	else {
		return ePipelineName::Geometry;
	}
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

void Material::SetUnlit(bool value)
{
	SetFlag(Properties.Flags, eMaterialFlags::Unlit);
	mbRequiresSync = true;
}

renderer::DescriptorSet* Material::RequestAlbedoOnlyDescriptors()
{
	if (mpAlbedoOnlyDescriptorSet != nullptr) {
		return mpAlbedoOnlyDescriptorSet;
	}

	SamplerProps sampler_props { .MinLOD = GetComponentMinLOD(Diffuse), .MaxLOD = GetComponentMaxLOD(Diffuse) };

	// Build entries list
	SizedArray<DescriptorEntry> ds_entries(4);
	ds_entries.Emplace(
		DescriptorEntry::AsImage(0, eShaderType::Pixel, Diffuse.pImage, gSamplerCache->Request(sampler_props)));

	ds_entries.Emplace(DescriptorEntry::AsBuffer(4, eShaderType::Pixel, &gGraphics->LightBuffer.GetGpuBuffer(), 0,
												 gGraphics->LightBuffer.PageSize));

	std::pair<DescriptorID, renderer::DescriptorSet*> ds_result = gDescriptorCache->Request(ds_entries);
	mpAlbedoOnlyDescriptorSet = ds_result.second;

	return mpAlbedoOnlyDescriptorSet;
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


	if (mpDescriptorSet == nullptr) {
		SizedArray<DescriptorEntry> ds_entries(6);

		LogInfo(LC_RENDER, "** Building Material ({}) descriptor set", ID);

		ds_entries.Emplace(DescriptorEntry::AsImage(0, eShaderType::Pixel, Diffuse.pImage,
													gSamplerCache->Request(diffuse_sampler_props)));

		if (NormalMap.Exists()) {
			LogInfo(LC_RENDER, "\tHas Normal maps");

			ds_entries.Emplace(DescriptorEntry::AsImage(1, eShaderType::Pixel, NormalMap.pImage,
														gSamplerCache->Request(diffuse_sampler_props)));
			ds_entries.Emplace(DescriptorEntry::AsImage(2, eShaderType::Pixel, MetallicRoughness.pImage,
														gSamplerCache->Request(diffuse_sampler_props)));
		}
		else {
			LogInfo(LC_RENDER, "\tAlbedo Only");
		}

		if (bSupportsSkinning) {
			LogInfo(LC_RENDER, "\tHas Skinning");

			ds_entries.Emplace(DescriptorEntry::AsBuffer(3, eShaderType::Vertex, &gGraphics->BoneBuffer.GetGpuBuffer(),
														 0, gGraphics->BoneBuffer.PageSize));
		}

		ds_entries.Emplace(DescriptorEntry::AsBuffer(4, eShaderType::Pixel, &gGraphics->LightBuffer.GetGpuBuffer(), 0,
													 gGraphics->LightBuffer.PageSize));


		std::pair<DescriptorID, DescriptorSet*> result = gDescriptorCache->Request(ds_entries);
		mpDescriptorSet = result.second;

		Assert(mpDescriptorSet != nullptr);

		// VkDescriptorSetLayout layout = gRenderer->pDeferredRenderer->DsLayoutGPassMaterial;

		// if (bSupportsSkinning) {
		// 	layout = gRenderer->pDeferredRenderer->DsLayoutGPassSkinned;
		// }

		// mDescriptorSet.Create(MaterialManagerFwd::GetDescriptorPool(), layout, false, 1);
	}

	bIsBuilt.store(true);
}

} // namespace fx
