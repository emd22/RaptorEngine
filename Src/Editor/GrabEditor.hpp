#pragma once

#ifdef FX_IS_EDITOR

#include <ThirdParty/Jolt/Jolt.h>
#include <ThirdParty/Jolt/Physics/Body/BodyID.h>

#include <Core/Types.hpp>
#include <Math/Vec3.hpp>

namespace fx::editor {

class GrabEditor
{
public:
	void Update(float32 delta_time);
	void Release();

	FX_FORCE_INLINE bool IsHolding() const { return mbHolding; }

private:
	void Grab();
	void Hold(float32 delta_time);
	void Launch();

private:
	bool mbHolding = false;

	JPH::BodyID mBody {};

	Vec3f mLocalPoint = Vec3f::sZero;
	float32 mDistance = 0.0f;
};

} // namespace fx::editor

#endif
