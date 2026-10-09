/*
 * File:        ArchitectView.cpp
 * Author:      emd22
 * Created:     09/10/2026
 * Description: A 2D viewport for editing either X, Y, or Z views of the map
 */


#include "RaptorEditor.hpp"

#ifdef FX_IS_EDITOR

#include "EditorFrame.hpp"
#include "EditorThread.hpp"

#include <Blockout.hpp>
#include <Controls.hpp>
#include <Core/DynArray.hpp>
#include <Core/Log.hpp>
#include <Core/SizedArray.hpp>
#include <Engine.hpp>
#include <Math/Quat.hpp>
#include <Math/Vec2.hpp>
#include <Object/ObjectManager.hpp>
#include <World.hpp>
#include <algorithm>
#include <cfloat>
#include <cmath>

namespace fx::editor {

namespace {

template <typename TElementType>
using DoublingArray = DynArray<TElementType, GrowthFunctions::Double>;

constexpr float32 scSegmentQuantum = 1000.0f;
constexpr float32 scMinSegmentLengthSq = 1e-8f;
constexpr float32 scHullMergeDistance = 1e-4f;
constexpr float32 scMinMoveDistance = 1e-5f;
constexpr float32 scMinRotateAngle = 1e-5f;
constexpr float32 scVerticalFaceTolerance = 0.02f;
constexpr float32 scMinPlanNormalLength = 0.5f;
constexpr float32 scMinFaceLength = 1e-3f;
constexpr float32 scFaceMatchDot = 0.999f;
constexpr float32 scVertexMatchDistance = 2e-3f;
constexpr float32 scMinBrushExtent = 0.01f;

float32 GetAxis(const Vec3f& vector, uint32 axis)
{
	return (axis == 0) ? vector.X : ((axis == 1) ? vector.Y : vector.Z);
}

Vec2f Project(const Vec3f& vector, uint32 axis_u, uint32 axis_v)
{
	return Vec2f(GetAxis(vector, axis_u), GetAxis(vector, axis_v));
}

Vec2f Project(const Vec3f& vector, const ViewPlaneAxes& axes) { return Project(vector, axes.U, axes.V); }

struct Segment
{
	Vec2i KeyA {};
	Vec2i KeyB {};
	Vec2f A {};
	Vec2f B {};
};

bool IsKeyLess(const Vec2i& a, const Vec2i& b) { return a.X < b.X || (a.X == b.X && a.Y < b.Y); }

bool IsSegmentKeyLess(const Segment& a, const Segment& b)
{
	return IsKeyLess(a.KeyA, b.KeyA) || (a.KeyA == b.KeyA && IsKeyLess(a.KeyB, b.KeyB));
}

bool IsSegmentKeyEqual(const Segment& a, const Segment& b) { return a.KeyA == b.KeyA && a.KeyB == b.KeyB; }

bool IsNonLeftTurn(const Vec2f& origin, const Vec2f& a, const Vec2f& b)
{
	return (a - origin).Cross(b - origin) <= 0.0f;
}

void ConvexHull(DoublingArray<Vec2f>& points, DoublingArray<Vec2f>& out_hull)
{
	std::sort(points.begin(), points.end(),
			  [](const Vec2f& a, const Vec2f& b) { return a.X < b.X || (a.X == b.X && a.Y < b.Y); });

	const Vec2f* const sorted = points.begin();
	const Vec2f* const unique_end = std::unique(points.begin(), points.end(), [](const Vec2f& a, const Vec2f& b)
												{ return a.IsCloseTo(b, scHullMergeDistance); });
	const size_t count = static_cast<size_t>(unique_end - sorted);

	out_hull.Clear();

	if (count < 3) {
		for (size_t i = 0; i < count; i++) {
			out_hull.Insert(sorted[i]);
		}

		return;
	}

	for (size_t i = 0; i < count; i++) {
		while (out_hull.Size >= 2 &&
			   IsNonLeftTurn(out_hull[out_hull.Size - 2], out_hull[out_hull.Size - 1], sorted[i])) {
			out_hull.RemoveLast();
		}

		out_hull.Insert(sorted[i]);
	}

	const uint32 lower_size = out_hull.Size + 1;

	for (size_t i = count - 1; i > 0; i--) {
		while (out_hull.Size >= lower_size &&
			   IsNonLeftTurn(out_hull[out_hull.Size - 2], out_hull[out_hull.Size - 1], sorted[i - 1])) {
			out_hull.RemoveLast();
		}

		out_hull.Insert(sorted[i - 1]);
	}

	out_hull.RemoveLast();
}

Vec2i Quantize(const Vec2f& point)
{
	return Vec2i(static_cast<int32>(std::lround(point.X * scSegmentQuantum)),
				 static_cast<int32>(std::lround(point.Y * scSegmentQuantum)));
}

Segment MakeSegment(const Vec2f& a, const Vec2f& b)
{
	const Vec2i key_a = Quantize(a);
	const Vec2i key_b = Quantize(b);

	if (IsKeyLess(key_b, key_a)) {
		return Segment { key_b, key_a, b, a };
	}

	return Segment { key_a, key_b, a, b };
}

eTopViewBrushKind GetBrushKind(const Object& object)
{
	if (object.IsReflectionProbe()) {
		return eTopViewBrushKind::ReflectionProbe;
	}

	if (object.IsProbeVolume()) {
		return eTopViewBrushKind::ProbeVolume;
	}

	if (object.IsTrigger()) {
		return eTopViewBrushKind::Volume;
	}

	return eTopViewBrushKind::Geometry;
}

} // namespace


/////////////////////////////////////
// Snapshot
/////////////////////////////////////

TopViewState RaptorEditor::BuildTopViewState()
{
	TopViewState view;

	view.bActive = true;
	view.bDataMode = IsDataMode();
	view.bSnapEnabled = mToolState.ToolSnapEnabled;
	view.bCanCreate = IsToolAvailable(eEditorTool::Create);
	view.bCanRotate = IsToolAvailable(eEditorTool::Rotate);
	view.bCanFace = IsToolAvailable(eEditorTool::Face);
	view.MinFaceThickness = Blockout::scMinThickness;
	view.SnapStep = GetSnapStep();
	view.AngleSnapDegrees = GetAngleSnapStep();
	view.CommandSerial = mTopViewCommandSerial;

	const Ref<PerspectiveCamera>& camera = gWorld->Player.pCamera;

	if (camera.IsValid()) {
		const Vec3f forward = camera->GetForwardVector();

		view.bHasPlayer = true;
		view.PlayerPosition = { camera->Position.X, camera->Position.Y, camera->Position.Z };
		view.PlayerDirection = { forward.X, forward.Y, forward.Z };
	}

	Blockout* blockout = gWorld->pBlockout;

	if (blockout == nullptr) {
		return view;
	}

	struct WorldFace
	{
		Vec3f Normal;
		SizedArray<Vec3f> Points;
	};

	DoublingArray<WorldFace> faces;
	DoublingArray<Vec3f> corners;
	DoublingArray<Vec2f> projected;
	DoublingArray<Vec2f> hull;
	DoublingArray<Segment> segments;

	for (ObjectID id : blockout->BlockoutObjects) {
		Object* object = gObjectManager->GetObject(id);

		if (object == nullptr || object == blockout->pXFormObject || object == blockout->pPreviewObject) {
			continue;
		}

		const Brush* brush = blockout->GetBrush(object);

		if (brush == nullptr || !brush->IsValid()) {
			continue;
		}

		const Mat4f world = object->GetWorldMatrix();

		TopViewBrush& out = view.Brushes.emplace_back();

		out.ObjectId = object->ID.ID;
		out.Kind = GetBrushKind(*object);
		out.bSelected = mSelection.Contains(object);
		out.bSelectable = IsObjectSelectable(object);

		const Vec3f pivot = object->GetPosition() + brush->GetCenter();

		corners.Clear();

		for (const Vec3f vertex : brush->GetVertices()) {
			const Vec4f point = world * Vec4f(vertex.X, vertex.Y, vertex.Z, 1.0f);
			corners.Insert(Vec3f(point.X, point.Y, point.Z));
		}

		faces.Clear();

		for (uint32 f = 0; f < brush->Faces.Size; f++) {
			const Brush::Face& face = brush->Faces[f];
			const Vec3f local_normal = brush->Planes[face.PlaneIndex].Normal;
			const Vec4f world_normal = world * Vec4f(local_normal.X, local_normal.Y, local_normal.Z, 0.0f);
			const Vec3f normal(world_normal.X, world_normal.Y, world_normal.Z);

			WorldFace& out_face = faces.Emplace();
			out_face.Normal = normal * (1.0f / std::max(normal.Length(), 1e-6f));
			out_face.Points.InitCapacity(face.Vertices.Size);

			for (uint32 i = 0; i < face.Vertices.Size; i++) {
				const Vec3f vertex = face.Vertices[i];
				const Vec4f point = world * Vec4f(vertex.X, vertex.Y, vertex.Z, 1.0f);
				out_face.Points.Insert(Vec3f(point.X, point.Y, point.Z));
			}
		}

		for (uint32 plane_index = 0; plane_index < scViewPlaneCount; plane_index++) {
			const ViewPlaneAxes& axes = scViewPlaneAxes[plane_index];
			TopViewProjection& projection = out.Views[plane_index];

			projection.PivotU = GetAxis(pivot, axes.U);
			projection.PivotV = GetAxis(pivot, axes.V);

			projected.Clear();

			float32 near_depth = -FLT_MAX;

			for (const Vec3f& corner : corners) {
				projected.Insert(Project(corner, axes));
				near_depth = std::max(near_depth, axes.DepthSign * GetAxis(corner, axes.W));
			}

			projection.NearDepth = near_depth;

			ConvexHull(projected, hull);

			for (const Vec2f& point : hull) {
				projection.Hull.push_back(point.X);
				projection.Hull.push_back(point.Y);
			}

			segments.Clear();

			for (const WorldFace& face : faces) {
				const size_t point_count = face.Points.Size;

				for (size_t i = 0; i < point_count; i++) {
					const Vec2f from = Project(face.Points[i], axes);
					const Vec2f to = Project(face.Points[(i + 1) % point_count], axes);

					if ((to - from).LengthSquared() < scMinSegmentLengthSq) {
						continue;
					}

					segments.Insert(MakeSegment(from, to));
				}

				const Vec2f plan_normal = Project(face.Normal, axes);
				const float32 plan_length = plan_normal.Length();

				if (std::fabs(GetAxis(face.Normal, axes.W)) > scVerticalFaceTolerance * plan_length ||
					plan_length < scMinPlanNormalLength) {
					continue;
				}

				const Vec2f unit = plan_normal / plan_length;
				const Vec2f along_axis = unit.Perpendicular();

				float32 min_along = FLT_MAX;
				float32 max_along = -FLT_MAX;
				Vec2f min_point(0.0f);
				Vec2f max_point(0.0f);

				for (const Vec3f& world_point : face.Points) {
					const Vec2f point = Project(world_point, axes);
					const float32 along = point.Dot(along_axis);

					if (along < min_along) {
						min_along = along;
						min_point = point;
					}

					if (along > max_along) {
						max_along = along;
						max_point = point;
					}
				}

				if (max_along - min_along >= scMinFaceLength) {
					projection.Faces.push_back(TopViewFace { .AU = min_point.X,
															 .AV = min_point.Y,
															 .BU = max_point.X,
															 .BV = max_point.Y,
															 .NormalU = unit.X,
															 .NormalV = unit.Y });
				}
			}

			std::sort(segments.begin(), segments.end(), IsSegmentKeyLess);

			const Segment* const unique_end = std::unique(segments.begin(), segments.end(), IsSegmentKeyEqual);

			for (const Segment* segment = segments.begin(); segment != unique_end; segment++) {
				projection.Edges.push_back(segment->A.X);
				projection.Edges.push_back(segment->A.Y);
				projection.Edges.push_back(segment->B.X);
				projection.Edges.push_back(segment->B.Y);
			}
		}
	}

	return view;
}


/////////////////////////////////////
// View
/////////////////////////////////////

void RaptorEditor::SetView(eEditorView view)
{
	if (view != mView) {
		EndDrag();

		mView = view;

		if (view != eEditorView::Perspective) {
			if (ControlManager::IsMouseLocked()) {
				ControlManager::ReleaseMouse();
			}

			if (IsSimulationMode() || HasFlag(mpCurrentTool->Flags, eEditorToolFlags::ClearsSelection)) {
				SetTool(eEditorTool::Translate);
			}
		}

		mbForcePanelSync = true;
	}

	if (mpMainFrame != nullptr) {
		thread::PostToUI([this, shown = mView] { mpMainFrame->ShowView(shown); });
	}
}


/////////////////////////////////////
// Top view commands
/////////////////////////////////////

Object* RaptorEditor::FindTopViewObject(uint32 object_id)
{
	Blockout* blockout = gWorld->pBlockout;

	if (blockout == nullptr) {
		return nullptr;
	}

	Object* object = gObjectManager->GetObject(ObjectID(object_id));

	if (object == nullptr || object == blockout->pXFormObject || object == blockout->pPreviewObject) {
		return nullptr;
	}

	if (!object->HasTags(eObjectTag::Blockout) || blockout->GetBrush(object) == nullptr) {
		return nullptr;
	}

	return object;
}

uint32 RaptorEditor::CollectTopViewTargets(Object** out_targets)
{
	uint32 count = 0;

	for (uint32 i = 0; i < mSelection.GetCount(); i++) {
		Object* object = mSelection.GetObject(i);

		if (object == nullptr || object->HasTags(eObjectTag::LockTransform) ||
			gWorld->pBlockout->GetBrush(object) == nullptr) {
			continue;
		}

		out_targets[count++] = object;
	}

	return count;
}

void RaptorEditor::TopViewSelect(const std::vector<uint32>& object_ids, eTopViewSelectMode mode)
{
	if (IsSimulationMode()) {
		return;
	}

	if (mode == eTopViewSelectMode::Replace) {
		ClearSelection();
	}

	for (const uint32 object_id : object_ids) {
		Object* object = FindTopViewObject(object_id);

		if (object == nullptr) {
			continue;
		}

		if (mode == eTopViewSelectMode::Toggle && mSelection.Contains(object)) {
			mSelection.Remove(object);
			SyncSelection();
			continue;
		}

		SelectObject(object, true);
	}
}

void RaptorEditor::TopViewMove(const Vec3f offset)
{
	if (IsSimulationMode() || offset.Length() < scMinMoveDistance) {
		return;
	}

	Object* targets[scMaxSelectedObjects];
	const uint32 count = CollectTopViewTargets(targets);

	for (uint32 i = 0; i < count; i++) {
		const Vec3f before = targets[i]->GetPosition();

		EmplaceEditOperation<MoveOperation>(targets[i], before, before + offset, static_cast<int32>(count));
	}
}

void RaptorEditor::TopViewRotate(const Vec3f axis, float32 angle, const Vec3f pivot)
{
	if (IsSimulationMode() || !IsToolAvailable(eEditorTool::Rotate) || std::fabs(angle) < scMinRotateAngle ||
		axis.Length() < scMinMoveDistance) {
		return;
	}

	struct Change
	{
		Object* pObject = nullptr;
		Quat Before;
		Quat After;
		Vec3f PositionBefore;
		Vec3f PositionAfter;
		bool bMoves = false;
	};

	Object* targets[scMaxSelectedObjects];
	const uint32 count = CollectTopViewTargets(targets);

	const Quat rotation = Quat::FromAxisAngle(axis.Normalize(), angle);

	DoublingArray<Change> changes;
	int32 operation_count = 0;

	for (uint32 i = 0; i < count; i++) {
		Object* object = targets[i];
		const Brush* brush = gWorld->pBlockout->GetBrush(object);

		const Vec3f center = object->GetPosition() + brush->GetCenter();
		const Vec3f new_center = pivot + (center - pivot).Rotate(rotation);

		Change& change = changes.Emplace();
		change.pObject = object;
		change.Before = object->mRotation;
		change.After = (rotation * object->mRotation).Normalize();
		change.PositionBefore = object->GetPosition();
		change.PositionAfter = object->GetPosition() + (new_center - center);
		change.bMoves = (change.PositionAfter - change.PositionBefore).Length() >= scMinMoveDistance;

		operation_count += change.bMoves ? 2 : 1;
	}

	for (const Change& change : changes) {
		EmplaceEditOperation<RotateOperation>(change.pObject, change.Before, change.After, operation_count);

		if (change.bMoves) {
			EmplaceEditOperation<MoveOperation>(change.pObject, change.PositionBefore, change.PositionAfter,
												operation_count);
		}
	}
}

void RaptorEditor::TopViewMoveFace(const Vec3f world_normal, float32 distance)
{
	if (IsSimulationMode() || !IsToolAvailable(eEditorTool::Face) || std::fabs(distance) < scMinMoveDistance) {
		return;
	}

	struct FaceMove
	{
		Object* pObject = nullptr;
		Vec3f LocalNormal;
	};

	Object* targets[scMaxSelectedObjects];
	const uint32 count = CollectTopViewTargets(targets);

	DoublingArray<FaceMove> moves;

	for (uint32 i = 0; i < count; i++) {
		const Brush* brush = gWorld->pBlockout->GetBrush(targets[i]);
		const Vec3f local_direction = world_normal.Rotate(targets[i]->mRotation.Conjugate());
		const int32 plane_index = brush->FindPlane(local_direction);

		if (plane_index == Brush::scNoPlane) {
			continue;
		}

		const Vec3f plane_normal = brush->Planes[plane_index].Normal;

		if (plane_normal.Dot(local_direction) < scFaceMatchDot) {
			continue;
		}

		moves.Insert(FaceMove { targets[i], plane_normal });
	}

	for (const FaceMove& move : moves) {
		EmplaceEditOperation<ScaleFaceOperation>(move.pObject, move.LocalNormal, distance,
												 static_cast<int32>(moves.Size));
	}
}

void RaptorEditor::TopViewMoveVertices(uint32 axis_u, uint32 axis_v, const std::vector<TopViewVertexHandle>& handles,
									   const Vec3f offset)
{
	if (IsSimulationMode() || !IsToolAvailable(eEditorTool::Face) || offset.Length() < scMinMoveDistance ||
		axis_u > 2 || axis_v > 2) {
		return;
	}

	struct Edit
	{
		Object* pObject = nullptr;
		Brush::PlaneList Before;
		Brush::PlaneList After;
	};

	DoublingArray<uint32> object_ids;

	for (const TopViewVertexHandle& handle : handles) {
		if (std::find(object_ids.begin(), object_ids.end(), handle.ObjectId) == object_ids.end()) {
			object_ids.Insert(handle.ObjectId);
		}
	}

	DoublingArray<Edit> edits;

	for (const uint32 object_id : object_ids) {
		Object* object = FindTopViewObject(object_id);

		if (object == nullptr || object->HasTags(eObjectTag::LockTransform)) {
			continue;
		}

		const Brush* brush = gWorld->pBlockout->GetBrush(object);

		const Mat4f world = object->GetWorldMatrix();
		const Vec4f local_offset = world.Inverse() * Vec4f(offset.X, offset.Y, offset.Z, 0.0f);
		const Vec3f local_move(local_offset.X, local_offset.Y, local_offset.Z);

		SizedArray<Vec3f> points;
		points.InitCapacity(brush->GetVertices().Size);

		uint32 moved_count = 0;

		for (const Vec3f vertex : brush->GetVertices()) {
			const Vec4f world_point = world * Vec4f(vertex.X, vertex.Y, vertex.Z, 1.0f);
			const Vec2f projected_position = Project(Vec3f(world_point.X, world_point.Y, world_point.Z), axis_u,
													 axis_v);

			bool is_moved = false;

			for (const TopViewVertexHandle& handle : handles) {
				if (handle.ObjectId == object_id &&
					projected_position.IsCloseTo(Vec2f(handle.U, handle.V), scVertexMatchDistance)) {
					is_moved = true;
					break;
				}
			}

			points.Insert(is_moved ? vertex + local_move : vertex);
			moved_count += is_moved ? 1 : 0;
		}

		if (moved_count == 0) {
			continue;
		}

		const Brush moved = Brush::FromVertices(points, brush->Planes);
		const Vec3f extent = moved.GetBoundsMax() - moved.GetBoundsMin();

		if (!moved.IsValid() || extent.X < scMinBrushExtent || extent.Y < scMinBrushExtent ||
			extent.Z < scMinBrushExtent) {
			LogWarning(LC_CORE, "Cannot move those vertices of '{}', the brush would collapse or have too many faces",
					   object->Name.Get());
			continue;
		}

		edits.Insert(Edit { object, brush->Planes, moved.Planes });
	}

	for (const Edit& edit : edits) {
		EmplaceEditOperation<BrushEditOperation>(edit.pObject, edit.Before, edit.After, static_cast<int32>(edits.Size));
	}
}

Object* RaptorEditor::CreateWorldBox(const Vec3f min, const Vec3f max)
{
	if (IsSimulationMode() || gWorld->pBlockout == nullptr) {
		return nullptr;
	}

	Vec3f position;
	const Brush brush = gWorld->pBlockout->MakeWorldBox(min, max, position);

	if (!brush.IsValid()) {
		return nullptr;
	}

	if (IsDataMode()) {
		return CreateDataBrush(brush.Planes, position);
	}

	ObjectSnapshot snapshot;
	snapshot.Position = position;
	snapshot.Material = GetNewBrushMaterial();

	return EmplaceEditOperation<CreateBrushOperation>(brush.Planes, snapshot).GetCreated();
}

void RaptorEditor::TopViewCreate(const Vec3f min, const Vec3f max)
{
	if (IsSimulationMode() || !IsToolAvailable(eEditorTool::Create)) {
		return;
	}

	Object* created = CreateWorldBox(min, max);

	if (created == nullptr) {
		if (IsDataMode()) {
			LogWarning(LC_CORE, "Pick Probe Volumes, Reflection Probes or Volumes in the Data filter to create one");
		}

		return;
	}

	SelectObject(created, false);
}

void RaptorEditor::TopViewDelete()
{
	if (!IsSimulationMode()) {
		DeleteSelection();
	}
}

void RaptorEditor::TopViewDuplicate()
{
	if (!IsSimulationMode()) {
		DupeSelection();
	}
}

void RaptorEditor::TopViewToggleSnap()
{
	mToolState.ToolSnapEnabled = !mToolState.ToolSnapEnabled;

	SyncCurrentTool();
}

void RaptorEditor::TopViewAdjustSnap(int32 direction) { AdjustSnapLevel(direction); }

} // namespace fx::editor

#endif
