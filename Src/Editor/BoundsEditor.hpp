#pragma once

#ifdef FX_IS_EDITOR

#include "EditorTool.hpp"

#include <Core/Types.hpp>
#include <Math/BoundingBox.hpp>
#include <Math/Vec3.hpp>
#include <Object/ObjectID.hpp>

namespace fx {

class Object;

} // namespace fx

namespace fx::editor {

class BoundsEditor final : public NativeEditorTool
{
public:
	void Enter() override;
	void Leave() override;

	void Begin() override;
	void Update(float32 delta_time) override;
	void Finalize() override;
	void Cancel() override;

	void Controls() override;

private:
	struct Target
	{
		Object* pObject = nullptr;
		Vec3f Face = Vec3f::sZero;
	};

	Target FindTarget() const;

	Object* ResolveTarget() const;

	void DrawTarget(Object& object, const Vec3f& face) const;

private:
	bool mbActive = false;

	Object* mpObject = nullptr;
	ObjectID mObjectID = ObjectID::scNull;

	AABB mBoundsBefore;

	uint32 mAxis = 0;
	float32 mSign = 1.0f;

	Vec3f mPlayerOrigin = Vec3f::sZero;
	Vec3f mWorldNormal = Vec3f::sZero;
	float32 mAxisScale = 1.0f;
};

} // namespace fx::editor

#endif
