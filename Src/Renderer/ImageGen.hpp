#pragma once

#include <Math/Vec2.hpp>

namespace fx {

class Image;

namespace renderer::ImageGen {

Image* Random(UVec2 size);

Image* DfgLut(uint32 size);

} // namespace renderer::ImageGen

} // namespace fx
