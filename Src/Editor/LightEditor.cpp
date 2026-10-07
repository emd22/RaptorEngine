#include "LightEditor.hpp"

#ifdef FX_IS_EDITOR

#include "RaptorEditor.hpp"

#include <Color.hpp>
#include <Controls.hpp>
#include <Core/String.hpp>
#include <Engine.hpp>
#include <Physics/JoltPhysicsBackend.hpp>
#include <Physics/PhysicsManager.hpp>
#include <Renderer/DebugDraw.hpp>
#include <Renderer/Globals.hpp>
#include <Renderer/Light.hpp>
#include <Renderer/LightManager.hpp>
#include <World.hpp>
#include <algorithm>
#include <cmath>

namespace fx::editor {

static constexpr float32 scLightPickRange = 40.0f;
static constexpr float32 scLightPickRadius = 0.3f;

static constexpr float32 scAimFallbackDistance = 10.0f;
static constexpr float32 scAimRange = 100.0f;

static constexpr float32 scMarkerHalfExtent = 0.12f;
static constexpr float32 scDirectionLength = 1.5f;
static constexpr float32 scDirectionTipHalfExtent = 0.05f;

static constexpr float32 scEditTolerance = 0.0005f;

static constexpr float32 scHeightSnapStepsPerSecond = 8.0f;

static constexpr float32 scNewLightRadius = 8.0f;
static const float32 scNewLightInnerAngle = MathUtil::DegreesToRadians(30.0f);
static const float32 scNewLightOuterAngle = MathUtil::DegreesToRadians(40.0f);

static const Color scLightColor = Color::FromRGBA(255, 170, 40, 255);
static const Color scSelectedLightColor = Color::FromRGBA(255, 255, 90, 255);

template <typename TFunc>
static void ForEachSpotLight(TFunc&& func)
{
	for (const Ref<LightBase>& light : gLightManager->GetCache()) {
		if (light.IsValid() && light->Type == eLightType::Spot) {
			func(static_cast<LightSpot&>(*light));
		}
	}
}

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

/// The distance along a ray to where it enters a sphere, or a negative number if it misses. `direction` is normalized.
static float32 RaySphereDistance(const Vec3f origin, const Vec3f direction, const Vec3f center, float32 radius)
{
	const Vec3f to_center = center - origin;
	const float32 along = to_center.Dot(direction);

	if (along < 0.0f) {
		return -1.0f;
	}

	const float32 closest_sq = to_center.Dot(to_center) - (along * along);
	const float32 radius_sq = radius * radius;

	if (closest_sq > radius_sq) {
		return -1.0f;
	}

	return along - std::sqrt(radius_sq - closest_sq);
}


/////////////////////////////////////
// Light Editor
/////////////////////////////////////

void LightEditor::Enter() { Deselect(); }

void LightEditor::Leave()
{
	mbMoving = false;

	// Nothing is left showing that a light is selected once the tool is gone
	Deselect();
}

void LightEditor::Begin()
{
	// Clicking a light picks it, and clicking anywhere else moves the light that is already selected
	LightSpot* picked = PickLight();

	if (picked != nullptr && picked->ID != mSelectedID) {
		Select(picked);
	}

	LightSpot* light = GetSelected();

	if (light == nullptr) {
		return;
	}

	mbMoving = true;
	mMoveOrigin = light->GetPosition();
	mPlayerOrigin = gWorld->Player.Position;
	mHeightOffset = 0.0f;

	BeginEdit(*light);
}

void LightEditor::Update(float32 delta_time) { Tick(delta_time); }

void LightEditor::Finalize() { mbMoving = false; }

void LightEditor::Cancel()
{
	mbMoving = false;
	mbEditing = false;
}

void LightEditor::Controls() { Tick(0.0f); }

LightSpot* LightEditor::GetSelected()
{
	if (mSelectedID.IsInvalid()) {
		return nullptr;
	}

	LightBase* light = gLightManager->GetLight(mSelectedID);

	// Require a spotlight (for now)
	if (light == nullptr || light->Type != eLightType::Spot) {
		mSelectedID.Invalidate();
		mbMoving = false;
		mbEditing = false;
	}

	return static_cast<LightSpot*>(light);
}

void LightEditor::Tick(float32 delta_time)
{
	LightSpot* light = GetSelected();

	// Deselect the light
	if (light != nullptr && ControlManager::IsKeyPressed(eKey::FX_KEY_TAB)) {
		CommitEdit();
		Deselect();
		light = nullptr;
	}

	if (light != nullptr && ControlManager::IsComboPressed(eKey::FX_KEY_LCTRL, eKey::FX_KEY_D)) {
		CommitEdit();
		DuplicateSelected();
		light = GetSelected();
	}

	// Delete the light
	if (light != nullptr && ControlManager::IsKeyPressed(eKey::FX_KEY_BACKSPACE)) {
		mbEditing = false;
		DeleteSelected();
		light = nullptr;
	}

	// Check for directional aiming or translation
	if (light != nullptr) {
		const bool aiming = ControlManager::IsKeyDown(eKey::FX_MOUSE_RIGHT);

		if (aiming) {
			BeginEdit(*light);
			AimSelected(*light);
		}

		if (mbMoving) {
			MoveSelected(*light, delta_time);
		}

		// When the LMOUSE or RMOUSE is up, commit the edit
		if (!aiming && !mbMoving) {
			CommitEdit();
		}
	}

	DrawMarkers();
}


/////////////////////////////////////
// Markers
/////////////////////////////////////

void LightEditor::DrawMarkers()
{
	ForEachSpotLight(
		[&](LightSpot& light)
		{
			const Color color = (light.ID == mSelectedID) ? scSelectedLightColor : scLightColor;
			const Vec3f position = light.GetPosition();
			const Vec3f tip = position + (light.GetDirection().Normalize() * scDirectionLength);

			// Box for the light, icospheres are for the dogs
			renderer::gDebugDraw->SolidBox(position, Vec3f(scMarkerHalfExtent), Quat::scIdentity, color);
			renderer::gDebugDraw->Line(position, tip, color);

			// The Tip(TM)
			renderer::gDebugDraw->SolidBox(tip, Vec3f(scDirectionTipHalfExtent), Quat::scIdentity, color);
		});
}

LightSpot* LightEditor::PickLight()
{
	Ref<PerspectiveCamera>& camera = gWorld->Player.pCamera;

	const Vec3f origin = camera->Position;
	const Vec3f direction = camera->GetForwardVector().Normalize();

	LightSpot* nearest = nullptr;
	float32 nearest_distance = scLightPickRange;


	ForEachSpotLight(
		[&](LightSpot& light)
		{
			const float32 distance = RaySphereDistance(origin, direction, light.GetPosition(), scLightPickRadius);

			if (distance >= 0.0f && distance < nearest_distance) {
				nearest = &light;
				nearest_distance = distance;
			}
		});

	return nearest;
}

void LightEditor::CreateAtCrosshair()
{
	Ref<PerspectiveCamera>& camera = gWorld->Player.pCamera;

	const Vec3f origin = camera->Position;
	const Vec3f forward = camera->GetForwardVector().Normalize();

	const physics::RayResult hit = gPhysics->pBackend->Raycast(origin, forward * scAimRange);
	const Vec3f position = gEditor->SnapToGrid(hit.bHit ? hit.Point : (origin + (forward * scAimFallbackDistance)));

	EditOperation op {
		.Type = EditOperation::eType::LightCreate,
	};

	op.LightSnap.Position = position;
	// Aim er down
	op.LightSnap.Direction = SnapDirection(Vec3f(0.0f, -1.0f, 0.0f));

	op.LightSnap.LightName = String::Fmt("Light_{}", gLightManager->GetCache().Size).Str();
	op.LightSnap.Radius = scNewLightRadius;
	op.LightSnap.InnerAngle = scNewLightInnerAngle;
	op.LightSnap.OuterAngle = scNewLightOuterAngle;
	op.LightSnap.bCastShadows = true;

	const EditOperationValue created = gEditor->PushEditOperation(op);

	if (created.Type == EditOperationValue::eValueType::Light && created.pLight != nullptr) {
		Select(static_cast<LightSpot*>(created.pLight));
	}
}


/////////////////////////////////////
// Selection
/////////////////////////////////////

void LightEditor::Select(LightSpot* light)
{
	CommitEdit();

	mSelectedID = (light != nullptr) ? light->ID : LightID::scNull;
	mbMoving = false;
}

void LightEditor::Deselect()
{
	mSelectedID = LightID::scNull;
	mbMoving = false;
	mbEditing = false;
}

void LightEditor::DeleteSelected()
{
	LightSpot* light = GetSelected();

	if (light == nullptr) {
		return;
	}

	EditOperation op {
		.Type = EditOperation::eType::LightDelete,
	};

	// Snapshot everything Undo needs before the Execute step destroys the light, same as RaptorEditor::DeleteObject
	op.Light.pLight = light;
	op.LightSnap = EditOperation::LightSnapshot::Capture(*light);

	gEditor->PushEditOperation(op);

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

	EditOperation op {
		.Type = EditOperation::eType::LightCreate,
	};

	op.LightSnap = EditOperation::LightSnapshot::Capture(*light);
	op.LightSnap.LightName = name;

	const EditOperationValue created = gEditor->PushEditOperation(op);

	if (created.Type == EditOperationValue::eValueType::Light && created.pLight != nullptr) {
		Select(static_cast<LightSpot*>(created.pLight));
	}
}


/////////////////////////////////////
// Editing
/////////////////////////////////////

void LightEditor::BeginEdit(LightSpot& light)
{
	if (mbEditing) {
		return;
	}

	mbEditing = true;
	mPositionBefore = light.GetPosition();
	mDirectionBefore = light.GetDirection().Normalize();
}

void LightEditor::CommitEdit()
{
	LightSpot* light = mbEditing ? GetSelected() : nullptr;

	mbEditing = false;

	if (light == nullptr) {
		return;
	}

	const Vec3f position_after = light->GetPosition();
	const Vec3f direction_after = light->GetDirection().Normalize();

	// Clicks without any movement are not worth an undo step
	if (position_after.IsCloseTo(mPositionBefore, scEditTolerance) &&
		direction_after.IsCloseTo(mDirectionBefore, scEditTolerance)) {
		return;
	}

	EditOperation op {
		.Type = EditOperation::eType::LightTransform,
		.ValueA = EditOperationValue(mPositionBefore),
		.ValueB = EditOperationValue(position_after),
	};

	op.Light.pLight = light;
	op.Light.DirectionBefore = mDirectionBefore;
	op.Light.DirectionAfter = direction_after;

	gEditor->PushEditOperation(op);
}

void LightEditor::MoveSelected(LightSpot& light, float32 delta_time)
{
	const Vec3f player_delta = gWorld->Player.Position - mPlayerOrigin;

	const float32 height_speed = delta_time * gEditor->GetSnapStep() * scHeightSnapStepsPerSecond;

	if (ControlManager::IsKeyDown(eKey::FX_KEY_E)) {
		mHeightOffset += height_speed;
	}
	if (ControlManager::IsKeyDown(eKey::FX_KEY_Q)) {
		mHeightOffset -= height_speed;
	}

	const Vec3f movement = gEditor->SnapToGrid(Vec3f(player_delta.X, mHeightOffset, player_delta.Z));

	light.SetPosition(mMoveOrigin + movement);
}

void LightEditor::AimSelected(LightSpot& light)
{
	Ref<PerspectiveCamera>& camera = gWorld->Player.pCamera;

	const Vec3f origin = camera->Position;
	const Vec3f forward = camera->GetForwardVector().Normalize();

	const physics::RayResult hit = gPhysics->pBackend->Raycast(origin, forward * scAimRange);
	const Vec3f target = hit.bHit ? hit.Point : (origin + (forward * scAimFallbackDistance));

	const Vec3f to_target = target - light.GetPosition();

	// The light is on top of the target, so there is no direction to point in
	if (to_target.Length() < 0.05f) {
		return;
	}

	light.SetDirection(SnapDirection(to_target));
}

} // namespace fx::editor

#endif
