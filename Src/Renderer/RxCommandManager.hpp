#pragma once

#include <Core/FxPagedArray.hpp>

class RxCommandBuffer;

enum class RxCommandUsage
{
    ePerFrame = 1,
    eManual = 2,
};


class RxCommandManager
{
public:
    RxCommandManager() {}

    RxCommandBuffer& Request(RxCommandUsage usage);

private:
    // FxPagedArray<>
};
