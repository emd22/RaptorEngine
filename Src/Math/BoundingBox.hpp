#pragma once

#include "Vec3.hpp"

namespace fx {

class BoundingBox
{
public:
    BoundingBox() = default;
    BoundingBox(Vec3f min, Vec3f max);

    BoundingBox operator+(const BoundingBox& other) const;
    BoundingBox& operator+=(const BoundingBox& other);

    BoundingBox& operator=(const BoundingBox& other);

public:
    Vec3f Min = Vec3f::sZero;
    Vec3f Max = Vec3f::sZero;
};

} // namespace fx
