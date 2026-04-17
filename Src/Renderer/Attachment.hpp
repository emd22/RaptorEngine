#pragma once

#include "Backend/Image.hpp"

#include <vulkan/vulkan.h>

#include <Core/SizedArray.hpp>
#include <Core/Slice.hpp>


namespace fx::renderer {

enum class eLoadOp
{
    Clear = VK_ATTACHMENT_LOAD_OP_CLEAR,
    Load = VK_ATTACHMENT_LOAD_OP_LOAD,
    DontCare = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
};

enum class eStoreOp
{
    None = VK_ATTACHMENT_STORE_OP_NONE,
    Store = VK_ATTACHMENT_STORE_OP_STORE,
    DontCare = VK_ATTACHMENT_STORE_OP_DONT_CARE,
};

struct Target
{
public:
    static constexpr Vec2u scFullScreen = Vec2u(0U);

public:
    Target() = default;

    Target(eImageFormat format, const Vec2u& size);
    Target(eImageFormat format, const Vec2u& size, VkImageUsageFlags usage, eImageAspectFlag aspect);
    Target(eImageFormat format, const Vec2u& size, eLoadOp load_op, eStoreOp store_op, VkImageLayout initial_layout,
           VkImageLayout final_layout);

    VkAttachmentDescription BuildDescription() const;
    void CreateImage();

    Image& GetImage() { return Image; }
    VkImageView& GetImageView() { return Image.View; }

    void SetImage(const Image& image)
    {
        Image = image;
        bReuseImage = true;
    }

    bool IsDepth() const { return Aspect == eImageAspectFlag::Depth; }

public:
    eImageType ImageType = eImageType::Flat;

    VkImageUsageFlags Usage = (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
    eImageAspectFlag Aspect = eImageAspectFlag::Color;

    VkSampleCountFlagBits Samples = VK_SAMPLE_COUNT_1_BIT;

    eLoadOp LoadOp = eLoadOp::Clear;
    eStoreOp StoreOp = eStoreOp::Store;

    eLoadOp StencilLoadOp = eLoadOp::Clear;
    eStoreOp StencilStoreOp = eStoreOp::Store;

    VkImageLayout InitialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImageLayout FinalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    Image Image {};

    bool bReuseImage : 1 = false;
    bool bRenderPassOnly : 1 = false;
};

class TargetList
{
public:
    TargetList() = default;
    TargetList(uint32 max_targets) : mMaxTargets(max_targets) {}
    TargetList(const TargetList& other) { Targets.InitAsCopyOf(other.Targets); }
    TargetList(TargetList&& other) noexcept { Targets = std::move(other.Targets); }

    static TargetList New() { return {}; }

    TargetList& Add(const Target* attachment);
    TargetList& Add(const Target& attachment);

    bool IsCompatible(const TargetList& other) const;

    void CreateImages();
    SizedArray<VkAttachmentDescription>& GetDescriptions();
    SizedArray<VkImageView>& GetImageViews();

private:
    FX_FORCE_INLINE void CheckInited()
    {
        if (!Targets) {
            Targets.InitCapacity(mMaxTargets);
        }
    }


public:
    SizedArray<Target> Targets;


private:
    bool mbDescriptionsBuilt : 1 = false;
    bool mbImageViewsBuilt : 1 = false;
    bool mbImagesCreated : 1 = false;

    SizedArray<VkAttachmentDescription> mBuiltAttachmentDescriptions;
    SizedArray<VkImageView> mBuiltImageViews;

    uint32 mMaxTargets = 10;
};

} // namespace fx::renderer
