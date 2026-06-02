/*
 * File:        Material.hpp
 * Author:      emd22
 * Created:     15/07/2025 by emd22
 * Description:
 */

#pragma once

#include "MaterialID.hpp"

#include <Asset/AxImage.hpp>
#include <Asset/Fwd/Ax_Fwd_Manager.hpp>
#include <Color.hpp>
#include <Core/Bitset.hpp>
#include <Core/Name.hpp>
#include <Core/PagedArray.hpp>
#include <Renderer/Backend/Descriptors.hpp>
#include <Renderer/Backend/GpuBuffer.hpp>


namespace fx {

enum class eMaterialComponentStatus
{
    Ready,
    MissingComponent,
    NotReady,
};

/////////////////////////////////////
// Material Component
/////////////////////////////////////

template <renderer::eImageFormat TFormat>
struct MaterialComponent
{
public:
    using Status = eMaterialComponentStatus;

public:
    MaterialComponent::Status Build()
    {
        // There is no texture provided, we will use the base colours passed in and a dummy texture
        if (!pAssetImage && !pDataToLoad) {
            // pAssetImage = AxImage::GetEmptyImage<TFormat>();
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

        pDataToLoad = other.pDataToLoad;
        pPixelData = other.pPixelData;

        return *this;
    }

    bool Exists() const { return (pAssetImage != nullptr) || (pDataToLoad != nullptr) || (pPixelData != nullptr); }

    ~MaterialComponent() = default;

private:
    bool CheckIfReady()
    {
        // If there is data passed in and the image has not been loaded yet, load it using the
        // asset manager. This will be validated on the next call of this function. (when attempting to build the
        // material)
        if (!pAssetImage) {
            if (pDataToLoad) {
                Slice<const uint8>& image_data = pDataToLoad;
                pAssetImage = Fwd::AssetManager::LoadImageFromMemory(TFormat, image_data.pData, image_data.Size);
            }
            else if (pPixelData) {
            }
            return false;
        }

        // If there is no texture and we are not loaded, return not loaded.
        if (!pAssetImage || !pAssetImage->IsLoaded()) {
            return false;
        }

        return true;
    }

public:
    TSRef<AxImage> pAssetImage { nullptr };

    /// Image data (including format containers) that needs to be parsed and uploaded by a loader.
    Slice<const uint8> pDataToLoad { nullptr };

    /// Pixel data to be uploaded;
    Slice<const uint8> pPixelData { nullptr };
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
    bool BindWithPipeline(const renderer::CommandBuffer& cmd, renderer::Pipeline& pipeline, bool albedo_only = false);

    void Build();

    renderer::DescriptorSet& GetDescriptorSet() { return mDsDefault; }
    renderer::DescriptorSet& GetDescriptorSetAlbedoOnly();

    void SetDefaultPipeline();
    void SubmitProperties(const MaterialProperties& properties);

    Material& operator=(const Material& other);

    void Destroy();
    ~Material() { Destroy(); }

public:
    MaterialID ID = MaterialID::Null;

    MaterialComponent<renderer::eImageFormat::RGBA8_UNorm> Diffuse {};
    MaterialComponent<renderer::eImageFormat::RGBA8_UNorm> NormalMap {};
    MaterialComponent<renderer::eImageFormat::RGBA8_UNorm> MetallicRoughness {};

    MaterialProperties Properties {};

    Name Name;

    renderer::Pipeline* pPipeline = nullptr;

    std::atomic_bool bIsBuilt = { false };

    bool bSupportsSkinning = false;

    /**
     * @brief Offset into `MaterialPropertiesBuffer` for this material.
     */
    // uint32 mMaterialPropertiesIndex = UINT32_MAX;

    bool bNearestFiltering : 1 = false;

private:
    renderer::DescriptorSet mDsDefault;
    renderer::DescriptorSet mDsAlbedoOnly;


    bool mbIsReady : 1 = false;
    bool mbIsBeingBuilt : 1 = false;
};


} // namespace fx
