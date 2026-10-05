#include "Target.hpp"

#include <Renderer/Globals.hpp>
#include <Renderer/GraphicsBackend.hpp>

namespace fx::renderer {

Target::Target(eImageFormat format, const Vec2u& size, bool is_fullscreen) : Format(format), Size(size)
{
	if (is_fullscreen) {
		Size = gGraphics->Swapchain.Extent;
		bIsFullscreen = true;
	}
}

Target::Target(eImageFormat format, const Vec2u& size, bool is_fullscreen, eLoadOp load_op, eStoreOp store_op,
			   VkImageLayout initial_layout, VkImageLayout final_layout)
	: Format(format), Size(size), LoadOp(load_op), StoreOp(store_op), InitialLayout(initial_layout),
	  FinalLayout(final_layout)
{
	if (is_fullscreen) {
		Size = gGraphics->Swapchain.Extent;
		bIsFullscreen = true;
	}
}

Target::Target(eImageFormat format, const Vec2u& size, bool is_fullscreen, VkImageUsageFlags usage,
			   eImageAspectFlag aspect)
	: Format(format), Size(size), Usage(usage), Aspect(aspect)
{
	if (is_fullscreen) {
		Size = gGraphics->Swapchain.Extent;
		bIsFullscreen = true;
	}
}

RxTargetConfig Target::ToRust() const
{
	Assert(Format != eImageFormat::None);

	return RxTargetConfig {
		.format = static_cast<uint16>(Format),
		.image_type = static_cast<uint16>(ImageType),
		.usage = Usage,
		.aspect = static_cast<uint32>(Aspect),
		.samples = static_cast<int32>(Samples),
		.load_op = static_cast<int32>(LoadOp),
		.store_op = static_cast<int32>(StoreOp),
		.stencil_load_op = static_cast<int32>(StencilLoadOp),
		.stencil_store_op = static_cast<int32>(StencilStoreOp),
		.initial_layout = static_cast<int32>(InitialLayout),
		.final_layout = static_cast<int32>(FinalLayout),
		.width = Size.X,
		.height = Size.Y,
		.render_pass_only = bRenderPassOnly,
	};
}

} // namespace fx::renderer
