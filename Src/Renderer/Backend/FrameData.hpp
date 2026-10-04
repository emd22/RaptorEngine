#pragma once

#include "Commands.hpp"
#include "Descriptors.hpp"
#include "Synchro.hpp"

#include <Math/Mat4.hpp>

namespace fx::renderer {

struct alignas(16) UniformBufferObject
{
    Mat4f MvpMatrix;
};

struct FrameData
{
public:
    CommandPool CmdPool;
    CommandBuffer CmdBuffer;
};

} // namespace fx::renderer
