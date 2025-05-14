#pragma once

#include <functional>
#include "../RvkCommands.hpp"

namespace vulkan {

void Fx_Fwd_SubmitUploadCmd(std::function<void (RvkCommandBuffer &)> func);

}
