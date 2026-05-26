#include "RenderBackendFwd.hpp"

#include "Device.hpp"
#include "FrameData.hpp"

#include <Renderer/Globals.hpp>
#include <Renderer/RenderBackend.hpp>

namespace fx::renderer {
namespace RenderBackendFwd {

GpuDevice* GetDevice() { return gRenderer->GetDevice(); }
FrameData* GetFrame() { return gRenderer->GetFrame(); }

CommandBuffer& GetUploadCmd() { return gRenderer->UploadContext.CmdBuffer; }

void SubmitImmediateUploadCmd(RenderBackend::SubmitFunc upload_func)
{
    gRenderer->SubmitImmediateUploadCmd(upload_func);
}

CommandBuffer& GetCmd() { return gRenderer->GetFrame()->CmdBuffer; }

} // namespace RenderBackendFwd
} // namespace fx::renderer
