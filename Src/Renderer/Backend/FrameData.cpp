#include "FrameData.hpp"

#include <vulkan/vulkan.h>

namespace fx::renderer {

void FrameData::Create(GpuDevice* device)
{
	ImageAvailable.Create(eSemaphoreType::Binary);
	RenderFinished.Create(eSemaphoreType::Binary);
}

void FrameData::Destroy()
{
	ImageAvailable.Destroy();
	RenderFinished.Destroy();
	InFlight.Destroy();
};

} // namespace fx::renderer
