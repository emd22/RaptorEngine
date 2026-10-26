#pragma once

#include <Controller/BaseController.hpp>

namespace fx {

class PlayerController : public BaseController
{
public:
	Vec3f Update(double delta_time, const Vec3f input, const PerspectiveCamera2& camera, bool sprinting) override;
};

} // namespace fx
