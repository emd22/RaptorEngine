#pragma once

#ifdef FX_IS_EDITOR

#include <Core/Types.hpp>
#include <Math/Vec3.hpp>
#include <Renderer/LightID.hpp>

namespace fx {

class LightSpot;

} // namespace fx

namespace fx::editor {

/// Rounds a direction's yaw and pitch to the editor's angle snap step
Vec3f SnapDirection(const Vec3f direction);

class LightEditor
{
public:
	LightSpot* GetSelected();

	void Select(LightID id);
	void Deselect();

	void MoveSelected(const Vec3f offset);
	void AimSelected(const Vec3f direction);

	LightSpot* Create(const Vec3f position);
	void DeleteSelected();
	void DuplicateSelected();

	void DrawMarkers();

private:
	LightID mSelectedID = LightID::scNull;
};

} // namespace fx::editor

#endif
