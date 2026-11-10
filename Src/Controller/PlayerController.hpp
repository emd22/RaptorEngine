#pragma once

#include <Controller/BaseController.hpp>

namespace fx {

class PlayerController : public BaseController
{
public:
	FVec3 Update(double delta_time, const FVec3 input, const PerspectiveCamera& camera, bool sprinting) override;
};

} // namespace fx
