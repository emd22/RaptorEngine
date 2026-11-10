#pragma once

#ifdef FX_IS_EDITOR

#include <Core/Types.hpp>
#include <Math/FVec3.hpp>
#include <Renderer/LightID.hpp>

namespace fx {

class LightSpot;

} // namespace fx

namespace fx::editor {

/// Rounds a direction's yaw and pitch to the editor's angle snap step
FVec3 SnapDirection(const FVec3 direction);

class LightEditor
{
public:
	LightSpot* GetSelected();

	void Select(LightID id);
	void Deselect();

	void MoveSelected(const FVec3 offset);
	void AimSelected(const FVec3 direction);

	LightSpot* Create(const FVec3 position);
	void DeleteSelected();
	void DuplicateSelected();

	void DrawMarkers();

private:
	LightID mSelectedID = LightID::scNull;
};

} // namespace fx::editor

#endif
