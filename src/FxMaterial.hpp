#pragma once

#include <Asset/FxImage.hpp>

class FxMaterial
{
public:
    FxMaterial() = default;


public:
    std::shared_ptr<FxImage> mDiffuse;
};
