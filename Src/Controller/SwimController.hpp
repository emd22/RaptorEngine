#pragma once

#include <Controller/BaseController.hpp>

namespace fx {

class SwimController : public BaseController
{
public:
	SwimController();

	FVec3 Update(double delta_time, const FVec3 input, const PerspectiveCamera& camera, bool sprinting) override;

public:
};

} // namespace fx
