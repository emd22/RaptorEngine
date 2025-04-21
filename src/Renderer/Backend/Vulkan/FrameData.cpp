#include "FrameData.hpp"

namespace vulkan {

void FrameData::Create(GPUDevice *device)
{
    ImageAvailable.Create(device);
    RenderFinished.Create(device);

    InFlight.Create(device);
}

void FrameData::Destroy()
{
    ImageAvailable.Destroy();
    RenderFinished.Destroy();

    InFlight.Destroy();

};

} // namespace vulkan
