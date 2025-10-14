#pragma once

#include <FxEntity.hpp>

class FxScene
{
public:
    FxScene() {}

    void Create();

    void Attach();

    void Destroy();

private:
    FxPagedArray<FxRef<FxEntity>> mAttachedEntities;
};
