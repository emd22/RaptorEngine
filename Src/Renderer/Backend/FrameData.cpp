#include "FrameData.hpp"

#include <vulkan/vulkan.h>

namespace fx::renderer {

void FrameData::Create(GpuDevice* device)
{
    ImageAvailable.Create();
    RenderFinished.Create();
}

void FrameData::Destroy()
{
    ImageAvailable.Destroy();
    RenderFinished.Destroy();
    InFlight.Destroy();
};

} // namespace fx::renderer
