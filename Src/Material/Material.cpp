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
#include <Renderer/PSOBuild.hpp>
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
	ImageToUpload = other.ImageToUpload;
	// Shallow, like the rest of this assignment: `other` stays the owner of the pixel buffer
	ImageToUpload.bOwnsData = false;

	return *this;
}

MaterialComponent::Status MaterialComponent::Build()
{
	// There is no texture provided, we will use the base colours passed in and a dummy texture
	if (!pImage && !ImageToUpload.ImageData.pData) {
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

		if (UploadSrc == eMaterialComponentUploadSrc::DirectUpload) {
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

bool Material::BindWithPipeline(const CommandBuffer& cmd, const Pipeline& pipeline)
{
	if (!bIsBuilt.load()) {
		Build();
	}

	if (!IsReady()) {
		return false;
	}

	// Every material's set has the same layout, so it fits any pipeline. Dynamic offsets go in binding order.
	const uint32 offsets[] = { gGraphics->BoneBuffer.GetBaseOffset(), gGraphics->LightBuffer.GetBaseOffset() };

	mpDescriptorSet->Bind(1, cmd, pipeline, Slice<const uint32>(offsets, std::size(offsets)));

	// Culling is dynamic state, so every draw that binds a material sets it
	pipeline.SetDoubleSided(cmd, HasFlag(Properties.Flags, eMaterialFlags::DoubleSided));

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

ePipelineFeatures Material::GetPipelineFeatures() const
{
	ePipelineFeatures features = ePipelineFeatures::None;

	if (NormalMap.Exists()) {
		features |= ePipelineFeatures::NormalMap;
	}

	if (bSupportsSkinning) {
		features |= ePipelineFeatures::Skinned;
	}

	return features;
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
	if (value) {
		SetFlag(Properties.Flags, eMaterialFlags::Unlit);
	}
	else {
		ClearFlag(Properties.Flags, eMaterialFlags::Unlit);
	}
	mbRequiresSync = true;
}

void Material::SetAlphaMask(bool value)
{
	if (value) {
		SetFlag(Properties.Flags, eMaterialFlags::AlphaMask);
	}
	else {
		ClearFlag(Properties.Flags, eMaterialFlags::AlphaMask);
	}
	mbRequiresSync = true;
}

void Material::SetDoubleSided(bool value)
{
	if (value) {
		SetFlag(Properties.Flags, eMaterialFlags::DoubleSided);
	}
	else {
		ClearFlag(Properties.Flags, eMaterialFlags::DoubleSided);
	}
	mbRequiresSync = true;
}

void Material::SetAlpha(float32 alpha)
{
	Properties.Alpha = alpha;
	mbRequiresSync = true;
}

void Material::SetMetallicRoughness(float32 metallic, float32 roughness)
{
	ClearFlag(Properties.Flags, eMaterialFlags::SpecularGlossiness);

	Properties.MetallicFactor = metallic;
	Properties.RoughnessFactor = roughness;
	mbRequiresSync = true;
}

void Material::SetSpecularGlossiness(const float32 specular[3], float32 glossiness)
{
	SetFlag(Properties.Flags, eMaterialFlags::SpecularGlossiness);

	memcpy(Properties.SpecularFactor, specular, sizeof(Properties.SpecularFactor));
	Properties.GlossinessFactor = glossiness;
	mbRequiresSync = true;
}

void Material::SetBaseColorFactor(const float32 color[3])
{
	memcpy(Properties.BaseColorFactor, color, sizeof(Properties.BaseColorFactor));
	mbRequiresSync = true;
}

void Material::DeclareDescriptors(renderer::PSOBuild& pso)
{
	// Set 1 (Object local). The images are null stand-ins that give the pipeline its layout, the real ones are bound at
	// render time. This has to stay in step with Build().

	// tAlbedo
	pso.AddImage(0, 1, eShaderType::Pixel, gAssetManager->GetNullImage(eImageFormat::RGBA8_UNorm),
				 gSamplerCache->Request({}));
	// tNormalMap
	pso.AddImage(1, 1, eShaderType::Pixel, gAssetManager->GetNullImage(eImageFormat::RGBA8_UNorm),
				 gSamplerCache->Request({}));
	// tMetallicRoughness
	pso.AddImage(2, 1, eShaderType::Pixel, gAssetManager->GetNullImage(eImageFormat::RGBA8_UNorm),
				 gSamplerCache->Request({}));
	// bBoneBuffer
	pso.AddBuffer(3, 1, eShaderType::Vertex, &gGraphics->BoneBuffer.GetGpuBuffer(), 0, gGraphics->BoneBuffer.PageSize);
	// FSLightBuffer
	pso.AddBuffer(4, 1, eShaderType::Pixel, &gGraphics->LightBuffer.GetGpuBuffer(), 0, gGraphics->LightBuffer.PageSize);
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

	// The null image is white, so the shader's texture * factor falls back to the material factors alone
	// (Properties.MetallicFactor etc.), which is also how glTF defines a missing metallic/roughness texture.
	if (NormalMap.Exists() && !MetallicRoughness.Exists()) {
		MetallicRoughness.SetTicket(gAssetManager->GetNullImageTicket(eImageFormat::RGBA8_UNorm));
	}


	if (mpDescriptorSet == nullptr) {
		SizedArray<DescriptorEntry> ds_entries(6);

		LogInfo(LC_RENDER, "** Building Material ({}) descriptor set", ID);

		Image* normal_image = NormalMap.Exists() ? NormalMap.pImage : gAssetManager->GetFlatNormalImage();
		Image* mr_image = MetallicRoughness.Exists() ? MetallicRoughness.pImage
													 : gAssetManager->GetNullImage(eImageFormat::RGBA8_UNorm);

		if (NormalMap.Exists()) {
			LogInfo(LC_RENDER, "\tHas Normal maps");
		}

		if (bSupportsSkinning) {
			LogInfo(LC_RENDER, "\tHas Skinning");
		}

		ds_entries.Emplace(DescriptorEntry::AsImage(0, eShaderType::Pixel, Diffuse.pImage,
													gSamplerCache->Request(diffuse_sampler_props)));
		ds_entries.Emplace(DescriptorEntry::AsImage(1, eShaderType::Pixel, normal_image,
													gSamplerCache->Request(diffuse_sampler_props)));
		ds_entries.Emplace(
			DescriptorEntry::AsImage(2, eShaderType::Pixel, mr_image, gSamplerCache->Request(diffuse_sampler_props)));
		ds_entries.Emplace(DescriptorEntry::AsBuffer(3, eShaderType::Vertex, &gGraphics->BoneBuffer.GetGpuBuffer(), 0,
													 gGraphics->BoneBuffer.PageSize));
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
