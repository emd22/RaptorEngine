#pragma once

#ifdef FX_IS_EDITOR

#include "EditorTool.hpp"

#include <ThirdParty/Jolt/Jolt.h>
#include <ThirdParty/Jolt/Physics/Body/BodyID.h>

#include <Core/Types.hpp>
#include <Math/Vec3.hpp>

namespace fx::editor {

class GrabEditor final : public NativeEditorTool
{
public:
	void Enter() override;
	void Leave() override;

	void Begin() override;
	void Update(float32 delta_time) override;
	void Finalize() override;
	void Cancel() override;

private:
	void Release();

private:
	bool mbHolding = false;

	JPH::BodyID mBody {};

	Vec3f mLocalPoint = Vec3f::sZero;
	float32 mDistance = 0.0f;
};

} // namespace fx::editor

#endif
