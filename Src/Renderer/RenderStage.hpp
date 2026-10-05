#pragma once

#include "Backend/Descriptors.hpp"
#include "Backend/Image.hpp"
#include "Backend/Sampler/Sampler.hpp"

#include <Core/SizedArray.hpp>
#include <vector>
#include <Renderer/Target.hpp>

namespace fx::renderer {

class RenderStage
{
public:
	RenderStage();
	RenderStage(const RenderStage&) = delete;
	RenderStage& operator=(const RenderStage&) = delete;

	void Create(const char* name, const Vec2u& size, eSizeDivisor size_divisor);

	void AddTarget(eImageFormat format, VkImageUsageFlags usage, eImageAspectFlag aspect);
	void AddTarget(const Target& attachment);

	/**
	 * @brief Returns the output target with a given format, which is not valid if there is none. The optional argument
	 * `sub_index` returns the n'th target of a given format.
	 */
	TargetRef GetTarget(eImageFormat format, int32 sub_index = 0) const;

	/**
	 * @brief Returns the index of an output target with a given format, or -1. The optional argument `sub_index` returns
	 * the index of the n'th target of a given format.
	 */
	int32 GetTargetIndex(eImageFormat format, int32 sub_index = 0) const
	{
		const uint32 index = rx_render_stage_find_target(mpStage, static_cast<uint16>(format), sub_index);

		return (index == RX_NO_TARGET) ? -1 : static_cast<int32>(index);
	}

	RxRenderStage* GetRust() const { return mpStage; }

	uint32 GetTargetCount() const { return rx_render_stage_target_count(mpStage); }

	/// The formats of the targets that are not depth, which are the ones that blend
	std::vector<uint16> GetColorTargetFormats() const;

	/// The render pass attachment of each target
	Slice<VkAttachmentDescription> GetDescriptions();

	void MarkFinalStage();

	FX_FORCE_INLINE VkRenderPass GetRenderPass() const { return reinterpret_cast<VkRenderPass>(mpStage->render_pass); }

	/**
	 * @brief Builds the Vulkan objects for the render stage. This is deferred until the user requests a renderpass or
	 * attachment. This is to reduce unused render stages, allow changes before the stage is built, etc.
	 */
	void BuildRenderStage();

	void Rebuild(const Vec2u& size);
	FX_FORCE_INLINE bool IsBuilt() const { return mbIsBuilt; }

	/// Begins the stage and points the viewport and scissor at all of its targets
	void Begin(CommandBuffer& cmd);

	/// Begins the stage over part of its targets, see RenderPass::Begin(). The viewport and scissor cover that part.
	void Begin(CommandBuffer& cmd, const VkRect2D& render_area);

	/// Like above, for when what is drawn to is not what gets cleared: the render area is cleared, and the viewport and
	/// scissor cover `draw_area`
	void Begin(CommandBuffer& cmd, const VkRect2D& render_area, const VkRect2D& draw_area);

	/**
	 * @brief Ends the render pass.
	 *
	 * vkCmdEndRenderPass transitions every attachment to its FinalLayout, so the tracked layout of each follows.
	 * Without this the next explicit barrier on a target reports a stale oldLayout -- and a stale UNDEFINED lets the
	 * driver legally discard everything the pass just rendered.
	 */
	void End();

	FX_FORCE_INLINE uint32 GetSizeDivisor() const { return mSizeDivisor; }

	~RenderStage();

private:
	/// Makes the stage's images, render pass and framebuffers, or makes them again at a new size if `recreate`
	void Build(const Vec2u& final_size, bool recreate);

	void AddPresentTarget();

private:
	const char* pcName = "Unnamed";

	/// The targets, render pass, framebuffers and clear values live in a record owned by Rust.
	RxRenderStage* mpStage = nullptr;
	Vec2u mSize = Vec2u::sZero;

	/// The command buffer the stage was last begun on, which `End()` ends the render pass on
	CommandBuffer* mpEndCommandBuffer = nullptr;

	bool mbIsFullscreen = false;

	uint32 mSizeDivisor = 1U;

	bool mbIsBuilt : 1 = false;
	bool mbIsFinalStage : 1 = false;
};

} // namespace fx::renderer
