#include "Device.hpp"
#include "FrameData.hpp"
#include "GraphicsBackendFwd.hpp"

#include <Renderer/Globals.hpp>
#include <Renderer/GraphicsBackend.hpp>

namespace fx::renderer {
namespace GraphicsBackendFwd {

GpuDevice* GetDevice() { return gGraphics->GetDevice(); }
FrameData* GetFrame() { return gGraphics->GetFrame(); }

CommandBuffer& GetUploadCmd() { return gGraphics->UploadContext.CmdBuffer; }

void SubmitImmediateUploadCmd(GraphicsBackend::SubmitFunc upload_func)
{
	gGraphics->SubmitImmediateUploadCmd(upload_func);
}

CommandBuffer& GetCmd() { return gGraphics->GetFrame()->CmdBuffer; }

} // namespace GraphicsBackendFwd
} // namespace fx::renderer
