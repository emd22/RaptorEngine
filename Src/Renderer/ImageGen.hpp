#pragma once

#include <Math/Vec2.hpp>

namespace fx {

class Image;

namespace renderer::ImageGen {

Image* Random(Vec2u size);

Image* DfgLut(uint32 size);

}

} // namespace fx
