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

/////////////////////////////////////
// Material Component
/////////////////////////////////////

struct MaterialComponent
{
public:
    using Status = eMaterialComponentStatus;

public:
    MaterialComponent(eImageFormat format) : ImageFormat(format) {}

    MaterialComponent::Status Build()
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

    MaterialComponent& operator=(const MaterialComponent& other)
    {
        pAssetImage = other.pAssetImage;

        UploadSrc = other.UploadSrc;
        pDataToLoad = other.pDataToLoad;
        ImageToUpload = other.ImageToUpload;

        TextureCacheID = other.TextureCacheID;

        return *this;
    }

    FX_FORCE_INLINE void RequireUpdate() { mbRequiresUpdate = true; }

    bool Exists() const
    {
        return (pAssetImage != nullptr) || (pDataToLoad != nullptr) || (ImageToUpload.ImageData.pData != nullptr);
    }

    ~MaterialComponent() = default;

private:
    bool CheckIfReady()
    {
        // If there is data passed in and the image has not been loaded yet, load it using the
        // asset manager. This will be validated on the next call of this function. (when attempting to build the
        // material)
        if (!pAssetImage || mbRequiresUpdate) {
            AssertMsg(UploadSrc != eMaterialComponentUploadSrc::None, "UploadSrc has not been set!");

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

public:
    TSRef<AxImage> pAssetImage { nullptr };

    Hash32 TextureCacheID = HashNull32;

    eMaterialComponentUploadSrc UploadSrc = eMaterialComponentUploadSrc::None;

    /// Image data (including format containers) that needs to be parsed and uploaded by a loader.
    Slice<const uint8> pDataToLoad { nullptr };

    ImageInfo ImageToUpload {};

    eImageFormat ImageFormat = eImageFormat::RGBA8_UNorm;

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

    void Attach(eResourceType type, const TSRef<AxImage>& image)
    {
        switch (type) {
        case eResourceType::Diffuse:
            Diffuse.pAssetImage = image;
            break;
        case eResourceType::Normal:
            NormalMap.pAssetImage = image;
            break;
        case eResourceType::MetallicRoughness:
            MetallicRoughness.pAssetImage = image;
            break;

        default:
            LogError(LC_CORE, "Unsupported resource type to attach to material!");
            break;
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
    renderer::DescriptorSet& GetDescriptorSetAlbedoOnly();

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

    bool bSupportsSkinning = false;

    /**
     * @brief Offset into `MaterialPropertiesBuffer` for this material.
     */
    // uint32 mMaterialPropertiesIndex = UINT32_MAX;

    bool bNearestFiltering : 1 = false;

private:
    renderer::DescriptorSet mDsDefault;
    renderer::DescriptorSet mDsAlbedoOnly;


    renderer::Pipeline* mpPipeline = nullptr;
    renderer::ePipelineName mPipelineName = renderer::ePipelineName::Geometry;


    bool mbIsReady : 1 = false;
    bool mbIsBeingBuilt : 1 = false;
};


} // namespace fx
