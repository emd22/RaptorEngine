#pragma once

#ifdef FX_IS_EDITOR

#include "EditorTool.hpp"

#include <Core/Types.hpp>
#include <Math/Vec3.hpp>

namespace fx::editor {

class SpawnEditor final : public NativeEditorTool
{
public:
	void Enter() override;
	void Leave() override;

	void Begin() override;
	void Update(float32 delta_time) override;
	void Finalize() override;
	void Cancel() override;

	void Controls() override;

	void PlaceAtCrosshair();

	void DrawMarker(bool selected);

	bool Raycast(const Vec3f origin, const Vec3f direction, float32 max_distance, float32& out_distance) const;

private:
	void Tick(float32 delta_time);

	void BeginEdit();
	void CommitEdit();

	void MoveSpawn(float32 delta_time);
	void AimSpawn();

	void ResetToDefault();

private:
	bool mbMoving = false;

	Vec3f mMoveOrigin = Vec3f::sZero;
	Vec3f mPlayerOrigin = Vec3f::sZero;
	float32 mHeightOffset = 0.0f;

	bool mbEditing = false;
	Vec3f mPositionBefore = Vec3f::sZero;
	Vec3f mDirectionBefore = Vec3f::sForward;
	bool mbCustomBefore = false;
};

} // namespace fx::editor

#endif
