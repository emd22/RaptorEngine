#pragma once

#include <Asset/FxImage.hpp>

class FxMaterial
{
public:
    FxMaterial() = default;


public:
    FxRef<FxImage> mDiffuse;
};
