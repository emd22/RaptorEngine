#include "RxGpuBuffer.hpp"

#include <FxEngine.hpp>


void RxGpuBufferSubmitUploadCmd(std::function<void(RxCommandBuffer&)> func) { gRenderer->SubmitUploadCmd(func); }
