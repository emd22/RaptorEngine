#pragma once

#include "Backend/Commands.hpp"

#include <Core/PagedArray.hpp>

namespace fx::renderer {

enum class eCommandUsage
{
    Graphics,
    Transfer
};

using CommandHandle = Handle;

class CommandManager
{
public:
    CommandManager() {}

    CommandHandle Request(CommandUsage usage);

    void Release(CommandHandle handle);

private:
};

} // namespace fx::renderer
