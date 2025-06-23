#include "RvkGpuBuffer.hpp"

#include <Renderer/Renderer.hpp>


void RvkGpuBufferSubmitUploadCmd(std::function<void(RvkCommandBuffer &)> func)
{
    Renderer->SubmitUploadCmd(func);
}
