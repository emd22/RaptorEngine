#include "RxGpuBuffer.hpp"

#include <FxEngine.hpp>
#include <Renderer/RxRenderBackend.hpp>

void RxGpuBufferSubmitUploadCmd(std::function<void(RxCommandBuffer&)> func) { gRenderer->SubmitUploadCmd(func); }
