#include "BoundsEditor.hpp"

#ifdef FX_IS_EDITOR

#include "EditOperation.hpp"
#include "EditorSelection.hpp"
#include "RaptorEditor.hpp"

#include <Color.hpp>
#include <Engine.hpp>
#include <Object/Object.hpp>
#include <Object/ObjectManager.hpp>
#include <Renderer/DebugDraw.hpp>
#include <Renderer/Globals.hpp>
#include <World.hpp>
#include <algorithm>
#include <cmath>

namespace fx::editor {

static constexpr float32 scMinExtent = 0.05f;
static constexpr float32 scEditTolerance = 0.00001f;

static const Color scBoundsColor = Color::FromRGBA(80, 220, 255, 255);
static const Color scFaceColor = Color::FromRGBA(255, 120, 40, 255);

static bool HasVolume(const AABB& bounds)
{
	return (bounds.Max.X > bounds.Min.X) || (bounds.Max.Y > bounds.Min.Y) || (bounds.Max.Z > bounds.Min.Z);
}

static bool BoundsEqual(const AABB& a, const AABB& b)
{
	for (uint32 axis = 0; axis < 3; axis++) {
		if (std::fabs(a.Min.mData[axis] - b.Min.mData[axis]) > scEditTolerance ||
			std::fabs(a.Max.mData[axis] - b.Max.mData[axis]) > scEditTolerance) {
			return false;
		}
	}

	return true;
}

static uint32 FaceAxis(const Vec3f& face)
{
	return (std::fabs(face.X) > 0.5f) ? 0 : ((std::fabs(face.Y) > 0.5f) ? 1 : 2);
}

static void FindNearestNode(Object& node, const Vec3f& origin, const Vec3f& direction, float32& nearest_distance,
							Object*& out_object, Vec3f& out_face)
{
	if (node.pMesh.IsValid() && HasVolume(node.Bounds)) {
		Vec3f face;
		const float32 distance = node.RaycastBounds(origin, direction, face);

		if (distance >= 0.0f && distance < nearest_distance) {
			nearest_distance = distance;
			out_object = &node;
			out_face = face;
		}
	}

	for (ObjectID attached_id : node.AttachedNodes) {
		Object* attached = gObjectManager->GetObject(attached_id);

		if (attached != nullptr) {
			FindNearestNode(*attached, origin, direction, nearest_distance, out_object, out_face);
		}
	}
}

BoundsEditor::Target BoundsEditor::FindTarget() const
{
	PerspectiveCamera& camera = *gWorld->Player.pCamera;
	const Vec3f direction = camera.GetForwardVector();

	const EditorSelection& selection = gEditor->GetSelection();

	Target target;
	float32 nearest_distance = std::numeric_limits<float32>::max();

	for (uint32 i = 0; i < selection.GetCount(); i++) {
		Object* root = selection.GetObject(i);

		if (root->HasTags(eObjectTag::Blockout)) {
			continue;
		}

		FindNearestNode(*root, camera.Position, direction, nearest_distance, target.pObject, target.Face);
	}

	return target;
}

Object* BoundsEditor::ResolveTarget() const
{
	if (!mbActive || mpObject == nullptr || mObjectID.IsInvalid()) {
		return nullptr;
	}

	Object* live = gObjectManager->GetObject(mObjectID);
	return (live == mpObject) ? live : nullptr;
}

void BoundsEditor::DrawTarget(Object& object, const Vec3f& face) const
{
	const Mat4f& world = object.GetWorldMatrix();

	const Vec3f half_extent = (object.Bounds.Max - object.Bounds.Min) * 0.5f;
	const Vec3f center = (object.Bounds.Max + object.Bounds.Min) * 0.5f;

	renderer::gDebugDraw->WireBox(Mat4f::AsScale(half_extent) * Mat4f::AsTranslation(center) * world, scBoundsColor);

	const uint32 axis = FaceAxis(face);
	const float32 sign = (face.mData[axis] > 0.0f) ? 1.0f : -1.0f;

	Vec3f face_half_extent = half_extent;
	face_half_extent.mData[axis] = 0.0f;

	Vec3f face_center = center;
	face_center.mData[axis] += sign * half_extent.mData[axis];

	renderer::gDebugDraw->WireBox(Mat4f::AsScale(face_half_extent) * Mat4f::AsTranslation(face_center) * world,
								  scFaceColor);
}

void BoundsEditor::Enter() { mbActive = false; }

void BoundsEditor::Leave() { mbActive = false; }

void BoundsEditor::Controls()
{
	const Target target = FindTarget();

	if (target.pObject != nullptr) {
		DrawTarget(*target.pObject, target.Face);
	}
}

void BoundsEditor::Begin()
{
	mbActive = false;

	const Target target = FindTarget();

	if (target.pObject == nullptr) {
		return;
	}

	mpObject = target.pObject;
	mObjectID = target.pObject->ID;
	mBoundsBefore = target.pObject->Bounds;

	mAxis = FaceAxis(target.Face);
	mSign = (target.Face.mData[mAxis] > 0.0f) ? 1.0f : -1.0f;

	const Vec4f world_axis = mpObject->GetWorldMatrix() *
							 Vec4f(target.Face.X, target.Face.Y, target.Face.Z, 0.0f);
	const Vec3f axis_vector(world_axis.X, world_axis.Y, world_axis.Z);

	mAxisScale = std::max(axis_vector.Length(), 1e-4f);
	mWorldNormal = axis_vector / mAxisScale;

	mPlayerOrigin = gWorld->Player.Position;

	mbActive = true;
}

void BoundsEditor::Update(float32 delta_time)
{
	Object* object = ResolveTarget();

	if (object == nullptr) {
		mbActive = false;
		return;
	}

	const float32 projected = (gWorld->Player.Position - mPlayerOrigin).Dot(mWorldNormal);
	const float32 snapped = gEditor->SnapToGrid(Vec3f(projected)).X;

	const float32 extent_before = mBoundsBefore.Max.mData[mAxis] - mBoundsBefore.Min.mData[mAxis];
	const float32 amount = std::max(snapped / mAxisScale, scMinExtent - extent_before);

	AABB bounds = mBoundsBefore;

	if (mSign > 0.0f) {
		bounds.Max.mData[mAxis] += amount;
	}
	else {
		bounds.Min.mData[mAxis] -= amount;
	}

	if (!BoundsEqual(object->Bounds, bounds)) {
		object->SetBounds(bounds);
	}

	Vec3f face = Vec3f::sZero;
	face.mData[mAxis] = mSign;
	DrawTarget(*object, face);
}

void BoundsEditor::Finalize()
{
	Object* object = ResolveTarget();

	mbActive = false;

	if (object == nullptr || BoundsEqual(object->Bounds, mBoundsBefore)) {
		return;
	}

	EditOperation op {
		.Type = EditOperation::eType::BoundsEdit,
		.pObject = object,
	};

	op.BoundsBefore = mBoundsBefore;
	op.BoundsAfter = object->Bounds;

	gEditor->PushEditOperation(op);
}

void BoundsEditor::Cancel()
{
	Object* object = ResolveTarget();

	mbActive = false;

	if (object != nullptr) {
		object->SetBounds(mBoundsBefore);
	}
}

} // namespace fx::editor

#endif
