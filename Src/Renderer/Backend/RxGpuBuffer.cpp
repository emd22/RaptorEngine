#include "RxGpuBuffer.hpp"

#include <Renderer/Renderer.hpp>


void RxGpuBufferSubmitUploadCmd(std::function<void(RxCommandBuffer &)> func)
{
    Renderer->SubmitUploadCmd(func);
}
