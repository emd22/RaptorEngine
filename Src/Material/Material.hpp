/*
 * File:        Material.hpp
 * Author:      emd22
 * Created:     15/07/2025 by emd22
 * Description:
 */

#pragma once

#include "MaterialID.hpp"

#include <Asset/AssetManagerFwd.hpp>
#include <Color.hpp>
#include <Core/Bitset.hpp>
#include <Core/FreeArray.hpp>
#include <Core/Name.hpp>
#include <Core/PagedArray.hpp>
#include <Renderer/Backend/Descriptors.hpp>
#include <Renderer/Backend/GpuBuffer.hpp>
#include <Renderer/PipelineNames.hpp>


namespace fx {


enum class eMaterialFlags : uint32
{
	None = 0,
	Unlit = (1 << 0),
	/// The MetallicRoughness component holds a KHR_materials_pbrSpecularGlossiness texture (specular RGB,
	/// glossiness A) instead of glTF metallic/roughness (roughness G, metallic B).
	SpecularGlossiness = (1 << 1),
	/// The material is alpha tested (glTF MASK), so shadow passes have to discard on its albedo alpha too.
	AlphaMask = (1 << 2),
	/// Both faces are drawn (glTF doubleSided), with the back face lit with a flipped normal.
	DoubleSided = (1 << 3),
};

FxEnumFlags(eMaterialFlags);


/////////////////////////////////////
// Material Component
/////////////////////////////////////

enum class eMaterialComponentStatus
{
	Ready,
	MissingComponent,
	NotReady,
};

enum class eMaterialComponentUploadSrc
{
	None,
	/// Load image (from memory), decompress to pixels and upload
	ProcessAndUpload,
	/// Upload raw pixel data
	DirectUpload,
};

struct MaterialComponent
{
public:
	using Status = eMaterialComponentStatus;

public:
	MaterialComponent(eImageFormat format) : ImageFormat(format) {}

	MaterialComponent& operator=(const MaterialComponent& other);

	MaterialComponent::Status Build();
	FX_FORCE_INLINE void RequireUpdate() { mbRequiresUpdate = true; }

	FX_FORCE_INLINE bool Exists() const
	{
		return (pImage != nullptr) || (pDataToLoad != nullptr) || (ImageToUpload.ImageData.pData != nullptr);
	}

	void SetTicket(AssetTicket& ticket);
	void SetTicket(AssetTicket&& ticket);

	~MaterialComponent() = default;

private:
	bool CheckIfReady();

public:
	AssetTicket Ticket { nullptr };
	Image* pImage = nullptr;


	/// The texture cache ID used to load this image from.
	Hash32 TextureCacheID = HashNull32;

	eMaterialComponentUploadSrc UploadSrc = eMaterialComponentUploadSrc::None;

	/// Image data (including format containers) that needs to be parsed and uploaded by a loader.
	Slice<const uint8> pDataToLoad { nullptr };

	/// The image info used to _directly_ upload pixel data to an image. This would be used when uploading from
	/// pregenerated texture cache files.
	ImageInfo ImageToUpload {};

	eImageFormat ImageFormat = eImageFormat::RGBA8_UNorm;

	bool bUpdateAndReupload = false;

private:
	bool mbRequiresUpdate = false;
};

/////////////////////////////////////
// Material
/////////////////////////////////////

/**
 * @brief Per-material constants, uploaded as-is to the material properties buffer.
 */
struct MaterialProperties
{
	eMaterialFlags Flags = eMaterialFlags::None;
	float32 Alpha = 1.0f;

	/// Scales the metallic (B) and roughness (G) channels of the MetallicRoughness texture. With no texture bound these
	/// are the surface values themselves.
	float32 MetallicFactor = 0.0f;
	float32 RoughnessFactor = 0.5f;

	/// Scales the specular (RGB) and glossiness (A) channels when `eMaterialFlags::SpecularGlossiness` is set.
	float32 SpecularFactor[3] = { 1.0f, 1.0f, 1.0f };
	float32 GlossinessFactor = 1.0f;
};

static_assert(sizeof(MaterialProperties) == 32,
			  "MaterialProperties must match `Material` in Shaders/MaterialDef.hlsli");

/**
 * @brief Renderer material class.
 */
class Material
{
public:
	enum class eResourceType : uint32
	{
		Diffuse,
		Normal,
		MetallicRoughness,

		MaxImages,
	};

private:
	friend class MaterialManager;
	friend class FreeArray<Material>; // Use internally by MaterialManager
	Material() = default;

public:
	void Attach(eResourceType type, AssetTicket& ticket)
	{
		Image* image = static_cast<Image*>(ticket.Get());

		int32 mip_level = image->GetInfo().MipLevel;
		if (mip_level < QualityLevel) {
			QualityLevel = mip_level;
		}

		MaterialComponent* component = nullptr;

		switch (type) {
		case eResourceType::Diffuse:
			component = &Diffuse;
			break;
		case eResourceType::Normal:
			component = &NormalMap;
			break;
		case eResourceType::MetallicRoughness:
			component = &MetallicRoughness;
			break;

		default:
			LogError(LC_CORE, "Unsupported resource type to attach to material!");
			break;
		}

		if (component) {
			component->Ticket = ticket;
			component->pImage = image;
		}
	}

	bool IsReady();

	void SetSupportsSkinning(bool value) { bSupportsSkinning = value; }

	FX_FORCE_INLINE MaterialID GetID() const { return ID; }

	/**
	 * Binds the material to be used in the given command buffer.
	 * @returns True if the material was bound successfully.
	 */
	bool BindWithPipeline(const renderer::CommandBuffer& cmd, const renderer::Pipeline& pipeline);


	void RequestQuality(uint32 quality);

	void Build();

	FX_FORCE_INLINE renderer::DescriptorSet* GetDescriptorSet() { return mpDescriptorSet; }

	/**
	 * @brief Returns the pipeline that is required by the material.
	 */
	renderer::ePipelineName GetRequiredPipeline() const;

	void SetUnlit(bool value);
	void SetAlphaMask(bool value);
	void SetDoubleSided(bool value);
	void SetAlpha(float32 alpha);
	void SetMetallicRoughness(float32 metallic, float32 roughness);

	/**
	 * @brief Switches the material to the specular/glossiness workflow. The MetallicRoughness component is then read
	 * as specular (RGB) and glossiness (A).
	 */
	void SetSpecularGlossiness(const float32 specular[3], float32 glossiness);

	bool IsAlbedoOnly() const { return (NormalMap.Exists() == false); }

	void Finalize() { bReadyToCheck.test_and_set(); }

	Material& operator=(const Material& other);

	void Destroy();
	~Material() { Destroy(); }

private:
	/**
	 * @brief If requested by the `Bind` functions, generate albedo only descriptor sets to be bound.
	 */
	renderer::DescriptorSet* RequestAlbedoOnlyDescriptors();

	/**
	 * @brief Generates a descriptor set with a bone buffer binding for materials that don't otherwise support
	 * skinning (e.g. the null material), so they remain layout-compatible when bound as a fallback for a skinned
	 * pipeline.
	 */
	renderer::DescriptorSet* RequestSkinnedFallbackDescriptors();

public:
	MaterialID ID = MaterialID::scNull;

	/// Albedo is authored in sRGB, so it is sampled through an _SRGB view and the hardware linearises it. The other
	/// two carry data rather than colour (a direction, a metallic/roughness pair) and stay linear.
	MaterialComponent Diffuse { eImageFormat::RGBA8_SRGB };
	MaterialComponent NormalMap { eImageFormat::RGBA8_UNorm };
	MaterialComponent MetallicRoughness { eImageFormat::RGBA8_UNorm };

	MaterialProperties Properties {};

	Name Name;

	std::atomic_bool bIsBuilt = { false };
	std::atomic_flag bReadyToCheck = ATOMIC_FLAG_INIT;

	bool bSupportsSkinning : 1 = false;
	bool bNearestFiltering : 1 = false;

	int32 QualityLevel = 3;

private:
	renderer::DescriptorSet* mpDescriptorSet = nullptr;
	renderer::DescriptorSet* mpAlbedoOnlyDescriptorSet = nullptr;
	renderer::DescriptorSet* mpSkinnedFallbackDescriptorSet = nullptr;

	bool mbIsReady : 1 = false;
	bool mbIsBeingBuilt : 1 = false;
	bool mbRequiresSync : 1 = false;
};


} // namespace fx
