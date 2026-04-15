#pragma once

#include "Backend/RxCommands.hpp"

#include <Core/PagedArray.hpp>

namespace fx::renderer {

enum class RxCommandUsage
{
    eGraphics,
    eTransfer
};

using RxCommandHandle = Handle;

class RxCommandManager
{
public:
    RxCommandManager() {}

    RxCommandHandle Request(RxCommandUsage usage);

    void Release(RxCommandHandle handle);

private:
};

} // namespace fx::renderer
