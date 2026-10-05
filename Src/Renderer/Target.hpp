#pragma once

#include "Backend/Image.hpp"

#include <vulkan/vulkan.h>

#include <Core/Slice.hpp>


namespace fx::renderer {

enum class eLoadOp
{
	None = VK_ATTACHMENT_LOAD_OP_NONE,
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

/**
 * @brief A target that is already part of a render stage, which holds the target's image. This points into the stage,
 * so it is only good for as long as the stage is.
 */
class TargetRef
{
public:
	TargetRef() = default;
	TargetRef(const RxRenderStage* stage, uint32 index) : mpStage(stage), mIndex(index) {}

	FX_FORCE_INLINE bool IsValid() const { return mpStage != nullptr && mIndex != RX_NO_TARGET; }
	FX_FORCE_INLINE bool operator==(nullptr_t np) const { return !IsValid(); }
	FX_FORCE_INLINE bool operator!=(nullptr_t np) const { return IsValid(); }

	/// The state of the target's image, which is shared with every copy of it
	const RxImage* GetRecord() const { return rx_render_stage_target_image(mpStage, mIndex); }

	/// The target's image. It is the same image whether or not it is made yet.
	Image GetImage() const { return Image(GetRecord()); }

private:
	const RxRenderStage* mpStage = nullptr;
	uint32 mIndex = RX_NO_TARGET;
};

/**
 * @brief Describes a target to add to a render stage with `RenderStage::AddTarget()`, which makes its image.
 */
struct Target
{
public:
	static constexpr Vec2u scFullScreen = Vec2u(0U);

public:
	Target() = default;

	Target(eImageFormat format, const Vec2u& size, bool is_fullscreen);
	Target(eImageFormat format, const Vec2u& size, bool is_fullscreen, VkImageUsageFlags usage,
		   eImageAspectFlag aspect);
	Target(eImageFormat format, const Vec2u& size, bool is_fullscreen, eLoadOp load_op, eStoreOp store_op,
		   VkImageLayout initial_layout, VkImageLayout final_layout);

	/// Draws to the image of `ref_target`, which belongs to another stage, instead of making an image of its own.
	void UseImageFromTarget(const TargetRef& ref_target) { pReferenceImage = ref_target.GetRecord(); }

	bool IsDepth() const { return Aspect == eImageAspectFlag::Depth; }

	RxTargetConfig ToRust() const;

public:
	eImageFormat Format = eImageFormat::RGBA8_UNorm;
	Vec2u Size = Vec2u::sZero;

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

	/// The image the target draws to, if it is another stage's
	const RxImage* pReferenceImage = nullptr;

	bool bRenderPassOnly = false;

	/// True if the image size matches the size of the surface
	bool bIsFullscreen = false;
};

} // namespace fx::renderer
