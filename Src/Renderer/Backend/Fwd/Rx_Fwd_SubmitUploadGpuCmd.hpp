#pragma once

#include "../RxCommands.hpp"

#include <functional>

namespace fx::renderer {


void Fx_Fwd_SubmitUploadCmd(std::function<void(RxCommandBuffer&)> func);

}
