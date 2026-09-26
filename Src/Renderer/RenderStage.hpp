#pragma once

#include "Backend/Descriptors.hpp"
#include "Backend/Framebuffer.hpp"
#include "Backend/Image.hpp"
#include "Backend/RenderPass.hpp"
#include "Backend/Sampler/Sampler.hpp"

#include <Core/SizedArray.hpp>

namespace fx::renderer {

class RenderStage
{
	static constexpr uint32 scMaxInputAttachments = 6;
	static constexpr uint32 scMaxOutputTargets = 8;
	static constexpr uint32 scMaxInputBuffers = 2;

	struct InputTarget
	{
		uint32 BindIndex = 0;
		Target* pTarget = nullptr;
		Sampler* pSampler = nullptr;
		RawGpuBuffer* pBuffer = nullptr;

		uint32 BufferOffset = 0;
		uint32 BufferRange = 0;
	};

public:
	RenderStage() = default;

	void Create(const char* name, const Vec2u& size, eSizeDivisor size_divisor);

	void AddTarget(eImageFormat format, VkImageUsageFlags usage, eImageAspectFlag aspect);
	void AddTarget(const Target& attachment);

	TargetList& GetTargets() { return mOutputTargets; }

	/**
	 * @brief Returns the output target with a given format. The optional argument `sub_index` returns the
	 * n'th target of a given format.
	 */
	Target* GetTarget(eImageFormat format, int32 sub_index = 0);

	/**
	 * @brief Returns the index of an output target with a given format. The optional argument `sub_index` returns the
	 * index of the n'th target of a given format.
	 */
	int32 GetTargetIndex(eImageFormat format, int32 sub_index = 0);

	void MarkFinalStage();

	RenderPass& GetRenderPass() { return mRenderPass; }
	Framebuffer& GetFramebuffer() { return mFramebuffer; }

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
	 * vkCmdEndRenderPass transitions every attachment to its FinalLayout, so the
	 * tracked layout has to follow. Without this the next explicit barrier on a
	 * target reports a stale oldLayout -- and a stale UNDEFINED lets the driver
	 * legally discard everything the pass just rendered.
	 */
	void End()
	{
		mRenderPass.End();

		for (Target& target : mOutputTargets.Targets) {
			// Present targets carry a placeholder image; the swapchain owns the real one.
			if (target.Image.InternalImage == nullptr) {
				continue;
			}

			target.Image.ImageLayout = target.FinalLayout;
		}
	}

	FX_FORCE_INLINE uint32 GetSizeDivisor() const { return mSizeDivisor; }

	~RenderStage() = default;

private:
	void MakeClearValues();
	void CreateFinalStageFramebuffers();

	void AddPresentTarget();

public:
	SizedArray<VkClearValue> ClearValues;

private:
	const char* pcName = "Unnamed";

	TargetList mOutputTargets;
	SizedArray<InputTarget> mInputTargets;

	Framebuffer mFramebuffer;
	RenderPass mRenderPass;
	Vec2u mSize = Vec2u::sZero;

	bool mbIsFullscreen = false;

	uint32 mSizeDivisor = 1U;

	SizedArray<Framebuffer> mFinalStageFramebuffers;

	bool mbIsBuilt : 1 = false;
	bool mbIsFinalStage : 1 = false;
};

} // namespace fx::renderer
