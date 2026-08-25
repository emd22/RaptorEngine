#pragma once

#include <functional>

namespace fx {
namespace renderer {

class FrameData;
class CommandBuffer;
class GpuDevice;

namespace GraphicsBackendFwd {

FrameData* GetFrame();
GpuDevice* GetDevice();

CommandBuffer& GetUploadCmd();
void SubmitImmediateUploadCmd(std::function<void(CommandBuffer&)> upload_func);
CommandBuffer& GetCmd();

} // namespace GraphicsBackendFwd

} // namespace renderer
} // namespace fx
