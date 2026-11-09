#pragma once

#include <Controller/BaseController.hpp>

namespace fx {

class SwimController : public BaseController
{
public:
	SwimController();

	Vec3f Update(double delta_time, const Vec3f input, const PerspectiveCamera& camera, bool sprinting) override;

public:
};

} // namespace fx
