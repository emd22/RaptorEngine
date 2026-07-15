/*
 * File:        Material.hpp
 * Author:      emd22
 * Created:     15/07/2025 by emd22
 * Description:
 */

#pragma once

#include "MaterialID.hpp"

#include <Asset/AssetManagerFwd.hpp>
#include <Asset/AxImage.hpp>
#include <Color.hpp>
#include <Core/Bitset.hpp>
#include <Core/Name.hpp>
#include <Core/PagedArray.hpp>
#include <Renderer/Backend/Descriptors.hpp>
#include <Renderer/Backend/GpuBuffer.hpp>
#include <Renderer/PipelineNames.hpp>


namespace fx {


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
	ProcessAndUpload,
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
	// TSRef<AxImage> pAssetImage { nullptr };

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

struct MaterialProperties
{
	alignas(16) Color BaseColor = 0xFF010101u;
};

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

public:
	Material() { Properties.BaseColor = 0xFF010101u; }

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
	bool Bind(const renderer::CommandBuffer& cmd);
	bool BindWithPipeline(const renderer::CommandBuffer& cmd, const renderer::Pipeline& pipeline);

	void RequestQuality(uint32 quality);

	void Build();

	renderer::DescriptorSet& GetDescriptorSet() { return mDsDefault; }

	void SetDefaultPipeline();
	void SubmitProperties(const MaterialProperties& properties);

	void SetPipeline(renderer::ePipelineName pl_name);
	renderer::Pipeline& GetPipeline() { return *mpPipeline; }
	renderer::ePipelineName GetPipelineName() const { return mPipelineName; }


	Material& operator=(const Material& other);

	void Destroy();
	~Material() { Destroy(); }

public:
	MaterialID ID = MaterialID::Null;

	MaterialComponent Diffuse { eImageFormat::RGBA8_UNorm };
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
	renderer::DescriptorSet mDsDefault;

	renderer::Pipeline* mpPipeline = nullptr;
	renderer::ePipelineName mPipelineName = renderer::ePipelineName::Geometry;

	bool mbIsReady : 1 = false;
	bool mbIsBeingBuilt : 1 = false;
};


} // namespace fx
