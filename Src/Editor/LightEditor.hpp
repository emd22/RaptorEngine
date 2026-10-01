#pragma once

#ifdef FX_IS_EDITOR

#include "EditorTool.hpp"

#include <Core/Types.hpp>
#include <Math/Vec3.hpp>
#include <Renderer/LightID.hpp>

namespace fx {

class LightSpot;

} // namespace fx

namespace fx::editor {

class LightEditor final : public NativeEditorTool
{
public:
	void Enter() override;
	void Leave() override;

	void Begin() override;
	void Update(float32 delta_time) override;
	void Finalize() override;
	void Cancel() override;

	void Controls() override;

	LightSpot* GetSelected();
	void CreateAtCrosshair();

private:
	void Tick(float32 delta_time);

	void DrawMarkers();
	LightSpot* PickLight();

	void Select(LightSpot* light);
	void Deselect();

	void DeleteSelected();
	void DuplicateSelected();

	void BeginEdit(LightSpot& light);
	void CommitEdit();

	void MoveSelected(LightSpot& light, float32 delta_time);
	void AimSelected(LightSpot& light);

private:
	LightID mSelectedID = LightID::scNull;

	/// The left mouse button is held with a light selected
	bool mbMoving = false;

	/// Where the light and the player were when the move started, and how far Q/E have raised the light since
	Vec3f mMoveOrigin = Vec3f::sZero;
	Vec3f mPlayerOrigin = Vec3f::sZero;
	float32 mHeightOffset = 0.0f;

	/// An edit of the selected light is being recorded, and how it was before it
	bool mbEditing = false;
	Vec3f mPositionBefore = Vec3f::sZero;
	Vec3f mDirectionBefore = Vec3f::sForward;
};

} // namespace fx::editor

#endif
