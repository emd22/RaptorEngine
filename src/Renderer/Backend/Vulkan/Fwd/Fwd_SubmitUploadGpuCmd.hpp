#pragma once

#include <functional>
#include "../FxCommandBuffer.hpp"

namespace vulkan {

void Fx_Fwd_SubmitUploadCmd(std::function<void (FxCommandBuffer &)> func);

}
