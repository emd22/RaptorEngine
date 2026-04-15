#pragma once

#include "Backend/RxImage.hpp"

#include <vulkan/vulkan.h>

#include <Core/SizedArray.hpp>
#include <Core/Slice.hpp>

namespace fx::renderer {


enum class RxLoadOp
{
    eClear = VK_ATTACHMENT_LOAD_OP_CLEAR,
    eLoad = VK_ATTACHMENT_LOAD_OP_LOAD,
    eDontCare = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
};

enum class RxStoreOp
{
    eNone = VK_ATTACHMENT_STORE_OP_NONE,
    eStore = VK_ATTACHMENT_STORE_OP_STORE,
    eDontCare = VK_ATTACHMENT_STORE_OP_DONT_CARE,
};


struct RxTarget
{
public:
    static constexpr Vec2u scFullScreen = Vec2u(0U);

public:
    RxTarget() = default;

    RxTarget(RxImageFormat format, const Vec2u& size);
    RxTarget(RxImageFormat format, const Vec2u& size, VkImageUsageFlags usage, RxImageAspectFlag aspect);
    RxTarget(RxImageFormat format, const Vec2u& size, RxLoadOp load_op, RxStoreOp store_op,
             VkImageLayout initial_layout, VkImageLayout final_layout);

    VkAttachmentDescription BuildDescription() const;
    void CreateImage();

    RxImage& GetImage() { return Image; }
    VkImageView& GetImageView() { return Image.View; }

    void SetImage(const RxImage& image)
    {
        Image = image;
        bReuseImage = true;
    }

    bool IsDepth() const { return Aspect == RxImageAspectFlag::eDepth; }

public:
    RxImageType ImageType = RxImageType::e2d;

    VkImageUsageFlags Usage = (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
    RxImageAspectFlag Aspect = RxImageAspectFlag::eColor;

    VkSampleCountFlagBits Samples = VK_SAMPLE_COUNT_1_BIT;

    RxLoadOp LoadOp = RxLoadOp::eClear;
    RxStoreOp StoreOp = RxStoreOp::eStore;

    RxLoadOp StencilLoadOp = RxLoadOp::eClear;
    RxStoreOp StencilStoreOp = RxStoreOp::eStore;

    VkImageLayout InitialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImageLayout FinalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    RxImage Image {};

    bool bReuseImage : 1 = false;
    bool bRenderPassOnly : 1 = false;
};

class RxTargetList
{
public:
    RxTargetList() = default;
    RxTargetList(uint32 max_targets) : mMaxTargets(max_targets) {}
    RxTargetList(const RxTargetList& other) { Targets.InitAsCopyOf(other.Targets); }
    RxTargetList(RxTargetList&& other) noexcept { Targets = std::move(other.Targets); }

    static RxTargetList New() { return {}; }

    RxTargetList& Add(const RxTarget* attachment);
    RxTargetList& Add(const RxTarget& attachment);

    bool IsCompatible(const RxTargetList& other) const;

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
    SizedArray<RxTarget> Targets;


private:
    bool mbDescriptionsBuilt : 1 = false;
    bool mbImageViewsBuilt : 1 = false;
    bool mbImagesCreated : 1 = false;

    SizedArray<VkAttachmentDescription> mBuiltAttachmentDescriptions;
    SizedArray<VkImageView> mBuiltImageViews;

    uint32 mMaxTargets = 10;
};

} // namespace fx::renderer
