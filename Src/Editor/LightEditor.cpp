#include "LightEditor.hpp"

#ifdef FX_IS_EDITOR

#include "RaptorEditor.hpp"

#include <Color.hpp>
#include <Core/String.hpp>
#include <Engine.hpp>
#include <Renderer/DebugDraw.hpp>
#include <Renderer/Globals.hpp>
#include <Renderer/Light.hpp>
#include <Renderer/LightManager.hpp>
#include <World.hpp>
#include <algorithm>
#include <cmath>

namespace fx::editor {

static constexpr float32 scMarkerHalfExtent = 0.12f;
static constexpr float32 scDirectionLength = 1.5f;
static constexpr float32 scDirectionTipHalfExtent = 0.05f;

static constexpr float32 scEditTolerance = 0.0005f;

static constexpr float32 scNewLightRadius = 8.0f;
static const float32 scNewLightInnerAngle = MathUtil::DegreesToRadians(30.0f);
static const float32 scNewLightOuterAngle = MathUtil::DegreesToRadians(40.0f);


Vec3f SnapDirection(const Vec3f direction)
{
	const float32 step_degrees = gEditor->GetAngleSnapStep();

	if (step_degrees <= 0.0f) {
		return direction;
	}

	const Vec3f unit = direction.Normalize();
	const float32 step = MathUtil::DegreesToRadians(step_degrees);

	const float32 yaw = std::round(std::atan2(unit.X, unit.Z) / step) * step;
	const float32 pitch = std::round(std::asin(std::clamp(unit.Y, -1.0f, 1.0f)) / step) * step;
	const float32 horizontal = std::cos(pitch);

	return Vec3f(std::sin(yaw) * horizontal, std::sin(pitch), std::cos(yaw) * horizontal);
}

LightSpot* LightEditor::GetSelected()
{
	if (mSelectedID.IsNull() || mSelectedID.IsInvalid()) {
		return nullptr;
	}

	LightBase* light = gLightManager->GetLight(mSelectedID);

	if (light == nullptr || light->Type != eLightType::Spot) {
		mSelectedID = LightID::scNull;
		return nullptr;
	}

	return static_cast<LightSpot*>(light);
}

void LightEditor::Select(LightID id) { mSelectedID = id; }

void LightEditor::Deselect() { mSelectedID = LightID::scNull; }

void LightEditor::MoveSelected(const Vec3f offset)
{
	LightSpot* light = GetSelected();

	if (light == nullptr) {
		return;
	}

	const Vec3f before = light->GetPosition();
	const Vec3f after = before + offset;

	if (after.IsCloseTo(before, scEditTolerance)) {
		return;
	}

	const Vec3f direction = light->GetDirection().Normalize();

	gEditor->EmplaceEditOperation<LightTransformOperation>(light, before, after, direction, direction);
}

void LightEditor::AimSelected(const Vec3f direction)
{
	LightSpot* light = GetSelected();

	if (light == nullptr || direction.Length() < scEditTolerance) {
		return;
	}

	const Vec3f before = light->GetDirection().Normalize();
	const Vec3f after = SnapDirection(direction).Normalize();

	if (after.IsCloseTo(before, scEditTolerance)) {
		return;
	}

	const Vec3f position = light->GetPosition();

	gEditor->EmplaceEditOperation<LightTransformOperation>(light, position, position, before, after);
}

LightSpot* LightEditor::Create(const Vec3f position)
{
	uint32 index = gLightManager->GetCache().Size;
	Name name;

	do {
		name = String::Fmt("Light_{}", index++).Str();
	} while (gLightManager->FindLight(name.GetHash()) != nullptr);

	LightSnapshot snapshot;

	snapshot.Position = position;
	snapshot.Direction = SnapDirection(Vec3f(0.0f, -1.0f, 0.0f));

	snapshot.LightName = name;
	snapshot.Radius = scNewLightRadius;
	snapshot.InnerAngle = scNewLightInnerAngle;
	snapshot.OuterAngle = scNewLightOuterAngle;
	snapshot.bCastShadows = true;

	LightSpot* created = gEditor->EmplaceEditOperation<LightCreateOperation>(snapshot).GetCreated();

	if (created != nullptr) {
		Select(created->ID);
	}

	return created;
}

void LightEditor::DeleteSelected()
{
	LightSpot* light = GetSelected();

	if (light == nullptr) {
		return;
	}

	gEditor->EmplaceEditOperation<LightDeleteOperation>(light);

	Deselect();
}

void LightEditor::DuplicateSelected()
{
	LightSpot* light = GetSelected();

	if (light == nullptr) {
		return;
	}

	uint32 index = gLightManager->GetCache().Size;
	Name name;

	do {
		name = String::Fmt("Light_{}", index++).Str();
	} while (gLightManager->FindLight(name.GetHash()) != nullptr);

	LightSnapshot snapshot = LightSnapshot::Capture(*light);
	snapshot.LightName = name;

	LightSpot* created = gEditor->EmplaceEditOperation<LightCreateOperation>(snapshot).GetCreated();

	if (created != nullptr) {
		Select(created->ID);
	}
}

void LightEditor::DrawMarkers()
{
	const EditorTheme::MarkerColors& colors = gEditor->GetTheme().Marker;

	for (const Ref<LightBase>& base : gLightManager->GetCache()) {
		if (!base.IsValid() || base->Type != eLightType::Spot) {
			continue;
		}

		LightSpot& light = static_cast<LightSpot&>(*base);

		const Color color = (light.ID == mSelectedID) ? colors.LightSelected : colors.Light;
		const Vec3f position = light.GetPosition();
		const Vec3f tip = position + (light.GetDirection().Normalize() * scDirectionLength);

		renderer::gDebugDraw->SolidBox(position, Vec3f(scMarkerHalfExtent), Quat::scIdentity, color);
		renderer::gDebugDraw->Line(position, tip, color);
		renderer::gDebugDraw->SolidBox(tip, Vec3f(scDirectionTipHalfExtent), Quat::scIdentity, color);
	}
}

} // namespace fx::editor

#endif
