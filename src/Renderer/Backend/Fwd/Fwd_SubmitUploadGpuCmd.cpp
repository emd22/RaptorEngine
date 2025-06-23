#include "Fwd_SubmitUploadGpuCmd.hpp"

#include <Renderer/Renderer.hpp>


void Fx_Fwd_SubmitUploadCmd(std::function<void (RvkCommandBuffer &)> func)
{
    Renderer->SubmitUploadCmd(func);
}
