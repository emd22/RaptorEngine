#pragma once

#include <functional>
#include "../RvkCommands.hpp"


void Fx_Fwd_SubmitUploadCmd(std::function<void (RvkCommandBuffer &)> func);
