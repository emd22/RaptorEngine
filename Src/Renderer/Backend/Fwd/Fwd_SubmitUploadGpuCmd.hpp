#pragma once

#include "../Commands.hpp"

#include <functional>

namespace fx::renderer {


void Fx_Fwd_SubmitUploadCmd(std::function<void(CommandBuffer&)> func);

}
