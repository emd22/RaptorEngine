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
#include <Math/MathConsts.hpp>
#include <Math/Quat.hpp>
#include <Math/Vec2.hpp>
#include <Math/Vec3d.hpp>
#include <Object/ObjectManager.hpp>
#include <Renderer/Globals.hpp>
#include <Renderer/Light.hpp>
#include <Renderer/LightManager.hpp>
#include <World.hpp>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <vector>

namespace fx::editor {

namespace {

template <typename TElementType>
using DoublingArray = DynArray<TElementType, GrowthFunctions::Double>;

constexpr float32 scSegmentQuantum = 1000.0f;
constexpr float32 scMinSegmentLengthSq = 1e-8f;
constexpr float32 scHullMergeDistance = 1e-4f;
constexpr float32 scMinMoveDistance = 1e-5f;
constexpr float32 scMinRotateAngle = 1e-5f;
constexpr float32 scGridSnapTolerance = 1e-4f;
constexpr float32 scVerticalFaceTolerance = 0.02f;
constexpr float32 scMinPlanNormalLength = 0.5f;
constexpr float32 scMinFaceLength = 1e-3f;
constexpr float32 scFaceMatchDot = 0.999f;
constexpr float32 scVertexMatchDistance = 2e-3f;
constexpr float32 scMinBrushExtent = 0.01f;
constexpr float32 scGroundFootprintInset = 1e-3f;
constexpr float32 scGroundFitTolerance = 1e-3f;
constexpr float32 scDBoundThickness = 1.0f;
constexpr uint32 scDBoundSideCount = 6;
constexpr float32 scDecalDepth = 0.5f;
constexpr float32 scDecalRayReach = 1000.0f;
constexpr double scBendOverlap = 1e-4;
constexpr double scBendPointMergeSq = 1e-10;

Vec2f Project(const Vec3f& vector, const ViewPlaneAxes& axes)
{
	return Vec2f(GetAxis(vector, axes.U) * axes.USign, GetAxis(vector, axes.V));
}

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

	if (object.IsNvBrush()) {
		return eTopViewBrushKind::NvBrush;
	}

	if (object.IsDBound()) {
		return eTopViewBrushKind::DBound;
	}

	if (object.IsDecal()) {
		return eTopViewBrushKind::Decal;
	}

	return eTopViewBrushKind::Geometry;
}

struct HeightSpan
{
	float32 Bottom;
	float32 Top;
};

void ClipToFootprintEdge(DoublingArray<Vec3f>& points, DoublingArray<Vec3f>& scratch, uint32 axis, float32 limit,
						 float32 sign)
{
	scratch.Clear();

	for (uint32 i = 0; i < points.Size; i++) {
		const Vec3f current = points[i];
		const Vec3f next = points[(i + 1) % points.Size];

		const float32 current_distance = (GetAxis(current, axis) - limit) * sign;
		const float32 next_distance = (GetAxis(next, axis) - limit) * sign;

		if (current_distance >= 0.0f) {
			scratch.Insert(current);
		}

		if ((current_distance >= 0.0f) != (next_distance >= 0.0f)) {
			const float32 t = current_distance / (current_distance - next_distance);

			scratch.Insert(current + (next - current) * t);
		}
	}

	std::swap(points, scratch);
}

struct BrushSource
{
	Object* pObject = nullptr;
	const Brush* pBrush = nullptr;
	Mat4f World = Mat4f::scIdentity;
	Vec3f Pivot = Vec3f::sZero;
	eTopViewBrushKind Kind = eTopViewBrushKind::Geometry;
};

void GatherBrushSources(Blockout& blockout, std::vector<BrushSource>& out_sources)
{
	for (ObjectID id : blockout.BlockoutObjects) {
		Object* object = gObjectManager->GetObject(id);

		if (object == nullptr || object == blockout.pXFormObject || object == blockout.pPreviewObject) {
			continue;
		}

		const Brush* brush = blockout.GetBrush(object);

		if (brush == nullptr || !brush->IsValid()) {
			continue;
		}

		out_sources.push_back(BrushSource {
			.pObject = object,
			.pBrush = brush,
			.World = object->GetWorldMatrix(),
			.Pivot = object->GetPosition() + brush->GetCenter(),
			.Kind = GetBrushKind(*object),
		});
	}

	std::vector<Blockout::SubviewSolid> solids;
	blockout.GetSubviewSolids(solids);

	for (const Blockout::SubviewSolid& solid : solids) {
		out_sources.push_back(BrushSource {
			.pObject = solid.pOwner,
			.pBrush = solid.pBrush,
			.World = solid.World,
			.Pivot = solid.pOwner->GetPosition(),
			.Kind = eTopViewBrushKind::Geometry,
		});
	}
}

void ClipToHalfSpace(DoublingArray<Vec3d>& points, DoublingArray<Vec3d>& scratch, const Vec3d& normal, double limit)
{
	scratch.Clear();

	for (uint32 i = 0; i < points.Size; i++) {
		const Vec3d& current = points[i];
		const Vec3d& next = points[(i + 1) % points.Size];

		const double current_side = normal.Dot(current) - limit;
		const double next_side = normal.Dot(next) - limit;

		if (current_side <= 0.0) {
			scratch.Insert(current);
		}

		if ((current_side <= 0.0) != (next_side <= 0.0)) {
			const double t = current_side / (current_side - next_side);

			scratch.Insert(current + (next - current) * t);
		}
	}

	std::swap(points, scratch);
}

void InsertUniquePoint(DoublingArray<Vec3d>& points, const Vec3d& point)
{
	for (const Vec3d& existing : points) {
		const Vec3d delta = existing - point;

		if (delta.Dot(delta) < scBendPointMergeSq) {
			return;
		}
	}

	points.Insert(point);
}

Vec3d BendWorldPoint(const BendFrame& frame, const Vec3d& along_axis, const Vec3d& across_axis, const Vec3d& point)
{
	const double along = along_axis.Dot(point);
	const double across = across_axis.Dot(point);
	const Vec2d bent = BendCoordinates(frame, along, across);

	return point + along_axis * (bent.X - along) + across_axis * (bent.Y - across);
}

Vec3f DepthAxisVector(const eViewPlane plane, const float32 value)
{
	float32 components[3] = { 0.0f, 0.0f, 0.0f };
	components[GetViewPlaneAxes(plane).W] = value;

	return Vec3f(components[0], components[1], components[2]);
}

bool FindGroundOffset(const Vec3f min, const Vec3f max, const eViewPlane plane, float32& out_offset)
{
	Blockout* blockout = gWorld->pPrototype;

	if (blockout == nullptr) {
		return false;
	}

	const ViewPlaneAxes& axes = GetViewPlaneAxes(plane);

	const float32 min_u = GetAxis(min, axes.U) + scGroundFootprintInset;
	const float32 max_u = GetAxis(max, axes.U) - scGroundFootprintInset;
	const float32 min_v = GetAxis(min, axes.V) + scGroundFootprintInset;
	const float32 max_v = GetAxis(max, axes.V) - scGroundFootprintInset;

	if (min_u >= max_u || min_v >= max_v) {
		return false;
	}

	DoublingArray<HeightSpan> spans;
	DoublingArray<Vec3f> points;
	DoublingArray<Vec3f> scratch;

	std::vector<BrushSource> sources;
	GatherBrushSources(*blockout, sources);

	for (const BrushSource& source : sources) {
		if (source.Kind != eTopViewBrushKind::Geometry) {
			continue;
		}

		const Brush* brush = source.pBrush;
		const Mat4f world = source.World;

		HeightSpan span { FLT_MAX, -FLT_MAX };

		for (uint32 f = 0; f < brush->Faces.Size; f++) {
			const Brush::Face& face = brush->Faces[f];

			points.Clear();

			for (uint32 i = 0; i < face.Vertices.Size; i++) {
				const Vec3f vertex = face.Vertices[i];
				const Vec4f point = world * Vec4f(vertex.X, vertex.Y, vertex.Z, 1.0f);

				points.Insert(Vec3f(point.X, point.Y, point.Z));
			}

			ClipToFootprintEdge(points, scratch, axes.U, min_u, 1.0f);
			ClipToFootprintEdge(points, scratch, axes.U, max_u, -1.0f);
			ClipToFootprintEdge(points, scratch, axes.V, min_v, 1.0f);
			ClipToFootprintEdge(points, scratch, axes.V, max_v, -1.0f);

			for (const Vec3f point : points) {
				const float32 height = axes.DepthSign * GetAxis(point, axes.W);

				span.Bottom = std::min<float32>(span.Bottom, height);
				span.Top = std::max<float32>(span.Top, height);
			}
		}

		if (span.Top >= span.Bottom) {
			spans.Insert(span);
		}
	}

	if (spans.IsEmpty()) {
		return false;
	}

	const float32 near_height = axes.DepthSign * GetAxis(min, axes.W);
	const float32 far_height = axes.DepthSign * GetAxis(max, axes.W);
	const float32 bottom = std::min(near_height, far_height);
	const float32 height = std::abs(far_height - near_height);

	bool found = false;
	float32 best_base = 0.0f;

	for (const HeightSpan& support : spans) {
		const float32 base = support.Top;

		if (found && base >= best_base) {
			continue;
		}

		bool fits = true;

		for (const HeightSpan& other : spans) {
			fits = fits &&
				   !(base + height > other.Bottom + scGroundFitTolerance && base < other.Top - scGroundFitTolerance);
		}

		if (fits) {
			best_base = base;
			found = true;
		}
	}

	if (found) {
		out_offset = axes.DepthSign * (best_base - bottom);
	}

	return found;
}

bool FindDecalSurfaceOffset(const Vec3f min, const Vec3f max, const eViewPlane plane, float32& out_offset)
{
	Blockout* blockout = gWorld->pPrototype;

	if (blockout == nullptr) {
		return false;
	}

	const ViewPlaneAxes& axes = GetViewPlaneAxes(plane);
	const Vec3f toward_viewer = DepthAxisVector(plane, axes.DepthSign);
	const float32 base_height = axes.DepthSign * GetAxis(min, axes.W);

	const auto sample_point = [&](float32 u_fraction, float32 v_fraction)
	{
		const float32 min_u = GetAxis(min, axes.U);
		const float32 min_v = GetAxis(min, axes.V);

		float32 components[3];
		components[axes.U] = min_u + (GetAxis(max, axes.U) - min_u) * u_fraction;
		components[axes.V] = min_v + (GetAxis(max, axes.V) - min_v) * v_fraction;
		components[axes.W] = GetAxis(min, axes.W);

		return Vec3f(components[0], components[1], components[2]);
	};

	const Vec3f samples[] = {
		sample_point(0.5f, 0.5f),	sample_point(0.25f, 0.25f), sample_point(0.75f, 0.25f),
		sample_point(0.25f, 0.75f), sample_point(0.75f, 0.75f),
	};

	std::vector<float32> heights;
	std::vector<float32> distances;

	for (uint32 i = 0; i < std::size(samples); i++) {
		if (i == 1 && !heights.empty()) {
			break;
		}

		const Vec3f origin = samples[i] + toward_viewer * scDecalRayReach;
		blockout->RaycastDecalSurfaces(origin, toward_viewer * -1.0f, scDecalRayReach * 2.0f, distances);

		for (const float32 distance : distances) {
			heights.push_back(base_height + scDecalRayReach - distance);
		}
	}

	if (heights.empty()) {
		return false;
	}

	const float32 limit = base_height + scDecalDepth * 0.5f;

	float32 below = -FLT_MAX;
	float32 above = FLT_MAX;

	for (const float32 height : heights) {
		if (height <= limit) {
			below = std::max(below, height);
		}
		else {
			above = std::min(above, height);
		}
	}

	float32 surface = above;

	if (below > -FLT_MAX) {
		surface = below;
	}

	out_offset = axes.DepthSign * (surface - base_height);

	return true;
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
	view.bSimulating = IsSimulationMode();
	view.bSnapEnabled = mToolState.ToolSnapEnabled;
	view.AvailableTools = GetAvailableToolMask();
	view.MinFaceThickness = Blockout::scMinThickness;
	view.SnapStep = GetSnapStep();
	view.AngleSnapDegrees = GetAngleSnapStep();
	view.CommandSerial = mTopViewCommandSerial;

	const Ref<PerspectiveCamera>& camera = gWorld->Player.pCamera;

	if (camera.IsValid()) {
		const Vec3f forward = camera->GetForwardVector();

		view.bHasPlayer = true;
		view.PlayerPosition = camera->Position;
		view.PlayerDirection = forward;
	}

	const LightSpot* selected_light = mLightEditor.GetSelected();

	for (const Ref<LightBase>& base : gLightManager->GetCache()) {
		if (!base.IsValid() || base->Type != eLightType::Spot) {
			continue;
		}

		LightSpot& light = static_cast<LightSpot&>(*base);
		const Vec3f position = light.GetPosition();

		TopViewMarker& marker = view.Markers.Emplace();

		marker.Kind = eTopViewMarkerKind::Light;
		marker.LightId = light.ID.ID;
		marker.Position = position;
		marker.AimOrigin = position;
		marker.Direction = light.GetDirection().Normalize();
		marker.BoundsMin = position;
		marker.BoundsMax = position;
		marker.bSelected = (selected_light == &light);
		marker.bSelectable = (mCurrentTool == eEditorTool::Light);
	}

	if (IsSpawnPointShown()) {
		const World::PlayerSpawnPoint& spawn = gWorld->PlayerSpawn;
		const Vec3f half_extent(SpawnEditor::scHalfWidth, 0.0f, SpawnEditor::scHalfWidth);

		TopViewMarker& marker = view.Markers.Emplace();

		marker.Kind = eTopViewMarkerKind::Spawn;
		marker.Position = spawn.Position;
		marker.AimOrigin = spawn.Position + Vec3f(0.0f, SpawnEditor::GetEyeHeight(), 0.0f);
		marker.Direction = spawn.Direction.Normalize();
		marker.BoundsMin = spawn.Position - half_extent;
		marker.BoundsMax = spawn.Position + half_extent + Vec3f(0.0f, SpawnEditor::GetBodyHeight(), 0.0f);
		marker.bSelected = mbSpawnSelected;
		marker.bSelectable = (mCurrentTool == eEditorTool::Select);
	}

	Blockout* blockout = gWorld->pPrototype;

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

	std::vector<BrushSource> sources;
	GatherBrushSources(*blockout, sources);

	Object* last_hidden = nullptr;

	for (const BrushSource& source : sources) {
		Object* object = source.pObject;
		const Brush* brush = source.pBrush;

		if (object->bHiddenInTopView) {
			if (object != last_hidden) {
				view.HiddenCount++;
			}

			last_hidden = object;
			continue;
		}

		const Mat4f world = source.World;

		TopViewBrush& out = view.Brushes.Emplace();

		out.ObjectId = object->ID;
		out.Kind = source.Kind;
		out.bSelected = mSelection.Contains(object);
		out.bSelectable = IsObjectSelectable(object);

		const Vec3f pivot = source.Pivot;

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

			projection.Pivot = Project(pivot, axes);

			projected.Clear();

			float32 near_depth = -FLT_MAX;

			for (const Vec3f& corner : corners) {
				projected.Insert(Project(corner, axes));
				near_depth = std::max(near_depth, axes.DepthSign * GetAxis(corner, axes.W));
			}

			projection.NearDepth = near_depth;

			ConvexHull(projected, hull);

			projection.Hull.Assign(hull.pData, hull.Size);

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
					projection.Faces.Insert(TopViewFace { .A = min_point, .B = max_point, .Normal = unit });
				}
			}

			std::sort(segments.begin(), segments.end(), IsSegmentKeyLess);

			const Segment* const unique_end = std::unique(segments.begin(), segments.end(), IsSegmentKeyEqual);

			for (const Segment* segment = segments.begin(); segment != unique_end; segment++) {
				projection.Edges.Insert(TopViewEdge { .A = segment->A, .B = segment->B });
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
		mView = view;

		if (view != eEditorView::Perspective && ControlManager::IsMouseLocked()) {
			ControlManager::ReleaseMouse();
		}

		if (view == eEditorView::Top) {
			SetSimulating(false);
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

Object* RaptorEditor::FindTopViewObject(ObjectID object_id)
{
	Blockout* blockout = gWorld->pPrototype;

	if (blockout == nullptr) {
		return nullptr;
	}

	Object* object = gObjectManager->GetObject(object_id);

	if (object == nullptr || object == blockout->pXFormObject || object == blockout->pPreviewObject) {
		return nullptr;
	}

	if (object->bHiddenInTopView) {
		return nullptr;
	}

	if (blockout->IsSubviewObject(object)) {
		return object;
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

		if (object == nullptr || object->HasTags(eObjectTag::LockTransform)) {
			continue;
		}

		if (gWorld->pPrototype->GetBrush(object) == nullptr && !gWorld->pPrototype->IsSubviewObject(object)) {
			continue;
		}

		out_targets[count++] = object;
	}

	return count;
}

void RaptorEditor::TopViewSelect(const DynArray<ObjectID>& object_ids, eTopViewSelectMode mode)
{
	if (IsSimulationMode()) {
		return;
	}

	if (mode == eTopViewSelectMode::Replace) {
		ClearSelection();
	}

	DoublingArray<ObjectID> seen_ids;

	for (const ObjectID object_id : object_ids) {
		if (std::find(seen_ids.begin(), seen_ids.end(), object_id) != seen_ids.end()) {
			continue;
		}

		seen_ids.Insert(object_id);

		Object* object = FindTopViewObject(object_id);

		if (object == nullptr) {
			continue;
		}

		if (mode == eTopViewSelectMode::Toggle && mSelection.Contains(object)) {
			DeselectObject(object);
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

void RaptorEditor::TopViewRotate(const Vec3f axis, float32 angle, const Vec3f pivot, const Vec3f offset)
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
		const Brush* brush = gWorld->pPrototype->GetBrush(object);

		Vec3f center = object->GetPosition();

		if (brush != nullptr) {
			center = center + brush->GetCenter();
		}

		const Vec3f new_center = pivot + (center - pivot).Rotate(rotation);

		Change& change = changes.Emplace();
		change.pObject = object;
		change.Before = object->mRotation;
		change.After = (rotation * object->mRotation).Normalize();
		change.PositionBefore = object->GetPosition();
		change.PositionAfter = object->GetPosition() + (new_center - center) + offset;
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
		if (!CanEditObject(targets[i])) {
			continue;
		}

		const Brush* brush = gWorld->pPrototype->GetBrush(targets[i]);
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

void RaptorEditor::TopViewMoveVertices(eViewPlane plane, const DynArray<TopViewVertexHandle>& handles,
									   const Vec3f offset)
{
	if (IsSimulationMode() || !IsToolAvailable(eEditorTool::Vertex) || offset.Length() < scMinMoveDistance ||
		static_cast<uint32>(plane) >= scViewPlaneCount) {
		return;
	}

	struct Edit
	{
		Object* pObject = nullptr;
		Brush::PlaneList Before;
		Brush::PlaneList After;
	};

	DoublingArray<ObjectID> object_ids;

	for (const TopViewVertexHandle& handle : handles) {
		if (std::find(object_ids.begin(), object_ids.end(), handle.ObjectId) == object_ids.end()) {
			object_ids.Insert(handle.ObjectId);
		}
	}

	DoublingArray<Edit> edits;

	for (const ObjectID object_id : object_ids) {
		Object* object = FindTopViewObject(object_id);

		if (object == nullptr || !CanEditObject(object) || object->HasTags(eObjectTag::LockTransform)) {
			continue;
		}

		const Brush* brush = gWorld->pPrototype->GetBrush(object);

		const Mat4f world = object->GetWorldMatrix();
		const Vec4f local_offset = world.Inverse() * Vec4f(offset.X, offset.Y, offset.Z, 0.0f);
		const Vec3f local_move(local_offset.X, local_offset.Y, local_offset.Z);

		SizedArray<Vec3f> points;
		points.InitCapacity(brush->GetVertices().Size);

		uint32 moved_count = 0;

		for (const Vec3f vertex : brush->GetVertices()) {
			const Vec4f world_point = world * Vec4f(vertex.X, vertex.Y, vertex.Z, 1.0f);
			const Vec2f projected_position = Project(Vec3f(world_point.X, world_point.Y, world_point.Z),
													 GetViewPlaneAxes(plane));

			bool is_moved = false;

			for (const TopViewVertexHandle& handle : handles) {
				if (handle.ObjectId == object_id &&
					projected_position.IsCloseTo(handle.Position, scVertexMatchDistance)) {
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
	if (IsSimulationMode() || gWorld->pPrototype == nullptr) {
		return nullptr;
	}

	Vec3f position;
	const Brush brush = gWorld->pPrototype->MakeWorldBox(min, max, position);

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

void RaptorEditor::TopViewCreate(eViewPlane plane, Vec3f min, Vec3f max, bool drop_to_ground)
{
	if (IsSimulationMode() || !IsToolAvailable(eEditorTool::Create)) {
		return;
	}

	float32 offset = 0.0f;

	if (drop_to_ground && FindGroundOffset(min, max, plane, offset)) {
		const Vec3f shift = DepthAxisVector(plane, offset);

		min += shift;
		max += shift;
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

static Vec3f UnitAxis(uint32 axis, float32 sign)
{
	float32 components[3] = { 0.0f, 0.0f, 0.0f };
	components[axis] = sign;

	return Vec3f(components[0], components[1], components[2]);
}

static Quat QuatFromBasis(const Vec3f x, const Vec3f y, const Vec3f z)
{
	const float32 trace = x.X + y.Y + z.Z;

	float32 qx, qy, qz, qw;

	if (trace > 0.0f) {
		const float32 s = std::sqrt(trace + 1.0f) * 2.0f;
		qw = 0.25f * s;
		qx = (y.Z - z.Y) / s;
		qy = (z.X - x.Z) / s;
		qz = (x.Y - y.X) / s;
	}
	else if (x.X > y.Y && x.X > z.Z) {
		const float32 s = std::sqrt(1.0f + x.X - y.Y - z.Z) * 2.0f;
		qw = (y.Z - z.Y) / s;
		qx = 0.25f * s;
		qy = (y.X + x.Y) / s;
		qz = (z.X + x.Z) / s;
	}
	else if (y.Y > z.Z) {
		const float32 s = std::sqrt(1.0f + y.Y - x.X - z.Z) * 2.0f;
		qw = (z.X - x.Z) / s;
		qx = (y.X + x.Y) / s;
		qy = 0.25f * s;
		qz = (z.Y + y.Z) / s;
	}
	else {
		const float32 s = std::sqrt(1.0f + z.Z - x.X - y.Y) * 2.0f;
		qw = (x.Y - y.X) / s;
		qx = (z.X + x.Z) / s;
		qy = (z.Y + y.Z) / s;
		qz = 0.25f * s;
	}

	return Quat(qx, qy, qz, qw).Normalize();
}

void RaptorEditor::TopViewCreateDecal(eViewPlane plane, Vec3f min, Vec3f max, bool drop_to_ground)
{
	if (IsSimulationMode() || !IsToolAvailable(eEditorTool::Decal) || gWorld->pPrototype == nullptr) {
		return;
	}

	float32 offset = 0.0f;

	if (drop_to_ground) {
		if (FindDecalSurfaceOffset(min, max, plane, offset)) {
			const Vec3f shift = DepthAxisVector(plane, offset);

			min += shift;
			max += shift;
		}
		else {
			LogWarning(LC_CORE, "There is no surface under the decal to project onto, it was left at the base");
		}
	}

	const ViewPlaneAxes& axes = GetViewPlaneAxes(plane);

	const float32 width = GetAxis(max, axes.U) - GetAxis(min, axes.U);
	const float32 height = GetAxis(max, axes.V) - GetAxis(min, axes.V);

	float32 center[3];
	center[axes.U] = (GetAxis(min, axes.U) + GetAxis(max, axes.U)) * 0.5f;
	center[axes.V] = (GetAxis(min, axes.V) + GetAxis(max, axes.V)) * 0.5f;
	center[axes.W] = (axes.DepthSign > 0.0f) ? GetAxis(min, axes.W) : GetAxis(max, axes.W);

	const Vec3f half_extent(width * 0.5f, scDecalDepth * 0.5f, height * 0.5f);

	ObjectSnapshot snapshot;
	snapshot.Position = Vec3f(center[0], center[1], center[2]);
	snapshot.Rotation = QuatFromBasis(UnitAxis(axes.U, axes.USign), UnitAxis(axes.W, axes.DepthSign),
									  UnitAxis(axes.V, 1.0f));
	snapshot.Material = GetNewDecalMaterial();
	snapshot.bIsDecal = true;

	Object* created = EmplaceEditOperation<CreateBrushOperation>(Brush::FromBox(half_extent * -1.0f, half_extent).Planes,
																 snapshot)
						  .GetCreated();

	if (created != nullptr) {
		SelectObject(created, false);
	}
}

void RaptorEditor::TopViewCreateArch(eViewPlane plane, DynArray<Vec3f> points, bool drop_to_ground)
{
	if (IsSimulationMode() || !IsToolAvailable(eEditorTool::Arch) || gWorld->pPrototype == nullptr) {
		return;
	}

	const uint32 segment_count = points.Size / scArchPointsPerSegment;

	if (segment_count == 0) {
		return;
	}

	if (drop_to_ground) {
		Vec3f min = points[0];
		Vec3f max = points[0];

		for (const Vec3f& point : points) {
			min = Vec3f::Min(min, point);
			max = Vec3f::Max(max, point);
		}

		float32 offset = 0.0f;

		if (FindGroundOffset(min, max, plane, offset)) {
			const Vec3f shift = DepthAxisVector(plane, offset);

			for (Vec3f& point : points) {
				point += shift;
			}
		}
	}

	struct Piece
	{
		Brush::PlaneList Planes;
		Vec3f Position;
	};

	DoublingArray<Piece> pieces;

	for (uint32 segment = 0; segment < segment_count; segment++) {
		const Vec3f* segment_points = &points[segment * scArchPointsPerSegment];

		Vec3f min = segment_points[0];
		Vec3f max = segment_points[0];

		for (uint32 i = 1; i < scArchPointsPerSegment; i++) {
			min = Vec3f::Min(min, segment_points[i]);
			max = Vec3f::Max(max, segment_points[i]);
		}

		const Vec3f position = (min + max) * 0.5f;

		SizedArray<Vec3f> local_points;
		local_points.InitCapacity(scArchPointsPerSegment);

		for (uint32 i = 0; i < scArchPointsPerSegment; i++) {
			local_points.Insert(segment_points[i] - position);
		}

		Brush brush = Brush::FromVertices(local_points, Brush::PlaneList());
		const Vec3f extent = brush.GetBoundsMax() - brush.GetBoundsMin();

		if (!brush.IsValid() || extent.X < scMinBrushExtent || extent.Y < scMinBrushExtent ||
			extent.Z < scMinBrushExtent) {
			continue;
		}

		brush.AlignTexturesToWorld(position);
		pieces.Insert(Piece { brush.Planes, position });
	}

	if (pieces.Size < segment_count) {
		LogWarning(LC_CORE, "Skipped {} arch segments that were too thin to make into brushes",
				   segment_count - pieces.Size);
	}

	if (pieces.IsEmpty()) {
		return;
	}

	const MaterialID material = GetNewBrushMaterial();
	bool append = false;

	for (const Piece& piece : pieces) {
		ObjectSnapshot snapshot;
		snapshot.Position = piece.Position;
		snapshot.Material = material;

		Object* created = EmplaceEditOperation<CreateBrushOperation>(piece.Planes, snapshot,
																	 static_cast<int32>(pieces.Size))
							  .GetCreated();

		if (created != nullptr) {
			SelectObject(created, append);
			append = true;
		}
	}
}

void RaptorEditor::TopViewBend(const BendFrame& frame)
{
	Blockout* blockout = gWorld->pPrototype;

	if (IsSimulationMode() || !IsToolAvailable(eEditorTool::Bend) || blockout == nullptr) {
		return;
	}

	if (frame.Segments == 0 || frame.Segments > scMaxBendSegments || frame.Length < scMinBrushExtent ||
		IsBendStraight(frame)) {
		return;
	}

	if (std::fabs(frame.Angle) / frame.Segments >= scMaxBendSegmentAngle) {
		LogWarning(LC_CORE, "Bending by {:.0f} degrees needs more than {} segments",
				   std::fabs(frame.Angle) * 180.0f / FX_PI, frame.Segments);
		return;
	}

	struct Source
	{
		Object* pObject = nullptr;
		ObjectSnapshot Snapshot;
	};

	struct Piece
	{
		uint32 SourceIndex = 0;
		Brush::PlaneList Planes;
		Vec3f Position;
	};

	const Vec3d along_axis(frame.Along);
	const Vec3d across_axis(frame.Across);
	const uint32 slice_count = GetBendSliceCount(frame);

	DoublingArray<Source> sources;
	DoublingArray<Piece> pieces;
	DoublingArray<Vec3d> world_vertices;
	DoublingArray<Vec3d> polygon;
	DoublingArray<Vec3d> scratch;
	DoublingArray<Vec3d> slice;
	uint32 failed_count = 0;

	for (uint32 i = 0; i < mSelection.GetCount(); i++) {
		Object* object = mSelection.GetObject(i);

		if (object == nullptr || !CanEditObject(object) || object->HasTags(eObjectTag::LockTransform) ||
			GetBrushKind(*object) != eTopViewBrushKind::Geometry) {
			continue;
		}

		const Brush* brush = blockout->GetBrush(object);

		if (brush == nullptr || !brush->IsValid()) {
			continue;
		}

		const Mat4f world = object->GetWorldMatrix();

		const auto to_world = [&world](const Vec3f& local)
		{
			const Vec4f point = world * Vec4f(local.X, local.Y, local.Z, 1.0f);
			return Vec3d(point.X, point.Y, point.Z);
		};

		double brush_low = DBL_MAX;
		double brush_high = -DBL_MAX;

		for (const Vec3f& vertex : brush->GetVertices()) {
			const double along = along_axis.Dot(to_world(vertex));

			brush_low = std::min(brush_low, along);
			brush_high = std::max(brush_high, along);
		}

		if (IsUnbentByBend(frame, brush_low, brush_high)) {
			continue;
		}

		const uint32 first_piece = pieces.Size;
		bool failed = false;

		for (uint32 slice_index = 0; slice_index < slice_count && !failed; slice_index++) {
			const BendSlice bend_slice = GetBendSlice(frame, slice_index);
			const double low = bend_slice.Low;
			const double high = bend_slice.High;
			const bool has_low = bend_slice.bHasLow;
			const bool has_high = bend_slice.bHasHigh;

			if ((has_high && high <= brush_low + scBendOverlap) || (has_low && low >= brush_high - scBendOverlap)) {
				continue;
			}

			slice.Clear();

			for (const Brush::Face& face : brush->Faces) {
				polygon.Clear();

				for (const Vec3f& vertex : face.Vertices) {
					polygon.Insert(to_world(vertex));
				}

				if (has_low) {
					ClipToHalfSpace(polygon, scratch, along_axis * -1.0, -low);
				}

				if (has_high) {
					ClipToHalfSpace(polygon, scratch, along_axis, high);
				}

				for (const Vec3d& point : polygon) {
					InsertUniquePoint(slice, BendWorldPoint(frame, along_axis, across_axis, point));
				}
			}

			if (slice.Size < 4) {
				continue;
			}

			Vec3d min = slice[0];
			Vec3d max = slice[0];

			for (const Vec3d& point : slice) {
				min = Vec3d(std::min(min.X, point.X), std::min(min.Y, point.Y), std::min(min.Z, point.Z));
				max = Vec3d(std::max(max.X, point.X), std::max(max.Y, point.Y), std::max(max.Z, point.Z));
			}

			const Vec3f position = ((min + max) * 0.5).ToVec3f();

			SizedArray<Vec3f> local_points;
			local_points.InitCapacity(slice.Size);

			for (const Vec3d& point : slice) {
				local_points.Insert(point.ToVec3f() - position);
			}

			Brush piece = Brush::FromVertices(local_points, Brush::PlaneList());
			const Vec3f extent = piece.GetBoundsMax() - piece.GetBoundsMin();

			if (!piece.IsValid()) {
				failed = true;
				break;
			}

			if (extent.X < scMinBrushExtent || extent.Y < scMinBrushExtent || extent.Z < scMinBrushExtent) {
				continue;
			}

			piece.AlignTexturesToWorld(position);
			pieces.Insert(Piece { sources.Size, piece.Planes, position });
		}

		if (failed || pieces.Size == first_piece) {
			while (pieces.Size > first_piece) {
				pieces.RemoveLast();
			}

			if (failed) {
				failed_count++;
			}

			continue;
		}

		sources.Insert(Source { object, ObjectSnapshot::Capture(*object, mSelection.GetStoredMaterial(object)) });
	}

	if (failed_count > 0) {
		LogWarning(LC_CORE, "Left {} brushes unbent, their segments would need more than {} faces", failed_count,
				   Brush::scMaxPlanes);
	}

	if (pieces.IsEmpty()) {
		if (failed_count == 0) {
			LogWarning(LC_CORE, "Select the brushes to bend first");
		}

		return;
	}

	const int32 group_size = static_cast<int32>(sources.Size + pieces.Size);

	ClearSelection();

	for (const Source& source : sources) {
		DeleteObject(source.pObject, group_size);
	}

	DoublingArray<bool> named;

	for (uint32 i = 0; i < sources.Size; i++) {
		named.Insert(false);
	}

	bool append = false;

	for (const Piece& piece : pieces) {
		ObjectSnapshot snapshot = sources[piece.SourceIndex].Snapshot;
		snapshot.Position = piece.Position;
		snapshot.Rotation = Quat::scIdentity;

		if (named[piece.SourceIndex]) {
			snapshot.ObjectName = Name();
		}

		named[piece.SourceIndex] = true;

		Object* created = EmplaceEditOperation<CreateBrushOperation>(piece.Planes, snapshot, group_size).GetCreated();

		if (created != nullptr) {
			SelectObject(created, append);
			append = true;
		}
	}
}

void RaptorEditor::FitDBoundsToLevel()
{
	Blockout* blockout = gWorld->pPrototype;

	if (IsSimulationMode() || blockout == nullptr) {
		return;
	}

	std::vector<BrushSource> sources;
	GatherBrushSources(*blockout, sources);

	Vec3f level_min(FLT_MAX);
	Vec3f level_max(-FLT_MAX);
	bool has_geometry = false;

	DoublingArray<Object*> old_bounds;

	for (const BrushSource& source : sources) {
		if (source.Kind == eTopViewBrushKind::DBound) {
			if (source.pObject->SubviewInstance == Blockout::scNoSubview && CanEditObject(source.pObject)) {
				old_bounds.Insert(source.pObject);
			}

			continue;
		}

		if (source.Kind != eTopViewBrushKind::Geometry) {
			continue;
		}

		for (const Vec3f vertex : source.pBrush->GetVertices()) {
			const Vec4f point = source.World * Vec4f(vertex.X, vertex.Y, vertex.Z, 1.0f);
			const Vec3f world_point(point.X, point.Y, point.Z);

			level_min = Vec3f::Min(level_min, world_point);
			level_max = Vec3f::Max(level_max, world_point);
			has_geometry = true;
		}
	}

	if (!has_geometry) {
		LogWarning(LC_CORE, "There is no level geometry to fit the map bounds to");
		return;
	}

	const Vec3f outer_min = level_min - Vec3f(scDBoundThickness);
	const Vec3f outer_max = level_max + Vec3f(scDBoundThickness);

	const Vec3f boxes[scDBoundSideCount][2] = {
		{ Vec3f(outer_min.X, outer_min.Y, outer_min.Z), Vec3f(outer_max.X, level_min.Y, outer_max.Z) },
		{ Vec3f(outer_min.X, level_max.Y, outer_min.Z), Vec3f(outer_max.X, outer_max.Y, outer_max.Z) },
		{ Vec3f(outer_min.X, level_min.Y, outer_min.Z), Vec3f(level_min.X, level_max.Y, outer_max.Z) },
		{ Vec3f(level_max.X, level_min.Y, outer_min.Z), Vec3f(outer_max.X, level_max.Y, outer_max.Z) },
		{ Vec3f(level_min.X, level_min.Y, outer_min.Z), Vec3f(level_max.X, level_max.Y, level_min.Z) },
		{ Vec3f(level_min.X, level_min.Y, level_max.Z), Vec3f(level_max.X, level_max.Y, outer_max.Z) },
	};

	ClearSelection();

	const int32 group_size = static_cast<int32>(old_bounds.Size + scDBoundSideCount);

	for (Object* object : old_bounds) {
		DeleteObject(object, group_size);
	}

	for (const auto& box : boxes) {
		Vec3f position;
		const Brush brush = blockout->MakeWorldBox(box[0], box[1], position);

		ObjectSnapshot snapshot;
		snapshot.Position = position;
		snapshot.Material = blockout->GetDBoundMaterial();
		snapshot.bIsDBound = true;

		Object* created = EmplaceEditOperation<CreateBrushOperation>(brush.Planes, snapshot, group_size).GetCreated();

		if (created != nullptr && IsDataMode()) {
			SelectObject(created, true);
		}
	}

	LogInfo("Fitted the map bounds to the level, {} to {} (replaced {} old map bounds)", level_min, level_max,
			old_bounds.Size);

	mbForcePanelSync = true;
}

void RaptorEditor::TopViewClip(const Vec3f point_a, const Vec3f point_b, const Vec3f view_axis)
{
	Blockout* blockout = gWorld->pPrototype;

	if (IsSimulationMode() || !IsToolAvailable(eEditorTool::Clip) || blockout == nullptr ||
		(point_b - point_a).Length() < scMinMoveDistance) {
		return;
	}

	struct Clip
	{
		Object* pObject = nullptr;
		Brush::PlaneList Before;
		Brush::PlaneList Kept;
		Brush::PlaneList Split;
		ObjectSnapshot SplitSnapshot;
	};

	DoublingArray<Clip> clips;

	for (uint32 i = 0; i < mSelection.GetCount(); i++) {
		Object* object = mSelection.GetObject(i);

		if (object == nullptr || !CanEditObject(object) || object->HasTags(eObjectTag::LockTransform)) {
			continue;
		}

		const Brush* brush = blockout->GetBrush(object);

		if (brush == nullptr) {
			continue;
		}

		Clip clip;
		clip.pObject = object;
		clip.Before = brush->Planes;
		clip.SplitSnapshot = ObjectSnapshot::Capture(*object, mSelection.GetStoredMaterial(object));
		clip.SplitSnapshot.ObjectName = Name();

		if (blockout->GetClipPieces(object, point_a, point_b, view_axis, clip.Kept, clip.Split,
									clip.SplitSnapshot.Position)) {
			clips.Insert(std::move(clip));
		}
	}

	if (clips.IsEmpty()) {
		LogWarning(LC_CORE, "The clip line does not cross any of the selected brushes");
		return;
	}

	const int32 group_size = static_cast<int32>(clips.Size * 2);

	DoublingArray<Object*> pieces;

	for (const Clip& clip : clips) {
		EmplaceEditOperation<BrushEditOperation>(clip.pObject, clip.Before, clip.Kept, group_size);
		pieces.Insert(clip.pObject);
	}

	for (const Clip& clip : clips) {
		Object* created = EmplaceEditOperation<CreateBrushOperation>(clip.Split, clip.SplitSnapshot, group_size)
							  .GetCreated();

		if (created != nullptr) {
			pieces.Insert(created);
		}
	}

	for (Object* piece : pieces) {
		SelectObject(piece, true);
	}
}

void RaptorEditor::TopViewSubtract(bool keep_cutters)
{
	if (IsSimulationMode() || IsDataMode()) {
		return;
	}

	if (mSelection.IsEmpty()) {
		LogWarning(LC_CORE, "Select the brushes to subtract first");
		return;
	}

	if (SubtractSelection(keep_cutters) == 0) {
		LogWarning(LC_CORE, "The selected brushes do not overlap any other brushes");
	}
}

void RaptorEditor::TopViewSelectLight(uint32 light_id)
{
	if (IsSimulationMode() || mCurrentTool != eEditorTool::Light) {
		return;
	}

	if (light_id == LightID::scNull.ID) {
		mLightEditor.Deselect();
	}
	else {
		mLightEditor.Select(LightID(light_id));
	}

	mbForcePanelSync = true;
}

void RaptorEditor::TopViewMoveLight(const Vec3f offset)
{
	if (!IsSimulationMode() && mCurrentTool == eEditorTool::Light) {
		mLightEditor.MoveSelected(offset);
	}
}

void RaptorEditor::TopViewAimLight(const Vec3f direction)
{
	if (!IsSimulationMode() && mCurrentTool == eEditorTool::Light) {
		mLightEditor.AimSelected(direction);
	}
}

void RaptorEditor::TopViewCreateLight(const Vec3f position)
{
	if (!IsSimulationMode() && mCurrentTool == eEditorTool::Light) {
		mLightEditor.Create(position);
		mbForcePanelSync = true;
	}
}

void RaptorEditor::TopViewSelectSpawn()
{
	if (!IsSimulationMode()) {
		SelectSpawn();
	}
}

void RaptorEditor::TopViewMoveSpawn(const Vec3f offset)
{
	if (!IsSimulationMode() && mbSpawnSelected && offset.Length() >= scMinMoveDistance) {
		mSpawnEditor.Move(offset);
	}
}

void RaptorEditor::TopViewAimSpawn(const Vec3f direction)
{
	if (!IsSimulationMode() && mbSpawnSelected) {
		mSpawnEditor.Aim(direction);
	}
}

void RaptorEditor::TopViewRespawn()
{
	if (!IsSimulationMode()) {
		gWorld->RespawnPlayer();
	}
}

void RaptorEditor::TopViewToggleSubview()
{
	if (IsEditingSubview()) {
		LeaveSubview();
	}
	else {
		EnterSubview();
	}
}

void RaptorEditor::TopViewDelete()
{
	if (IsSimulationMode()) {
		return;
	}

	if (mCurrentTool == eEditorTool::Light) {
		mLightEditor.DeleteSelected();
		mbForcePanelSync = true;
	}
	else if (mbSpawnSelected) {
		mSpawnEditor.ResetToDefault();
	}
	else {
		DeleteSelection();
	}
}

void RaptorEditor::TopViewDuplicate()
{
	if (IsSimulationMode()) {
		return;
	}

	if (mCurrentTool == eEditorTool::Light) {
		mLightEditor.DuplicateSelected();
		mbForcePanelSync = true;
	}
	else {
		DupeSelection();
	}
}

void RaptorEditor::TopViewHideSelection()
{
	if (IsSimulationMode() || mSelection.IsEmpty()) {
		return;
	}

	for (uint32 i = 0; i < mSelection.GetCount(); i++) {
		mSelection.GetObject(i)->bHiddenInTopView = true;
	}

	ClearSelection();

	mbForcePanelSync = true;
}

void RaptorEditor::TopViewShowAll()
{
	Blockout* blockout = gWorld->pPrototype;

	if (blockout == nullptr) {
		return;
	}

	for (ObjectID id : blockout->BlockoutObjects) {
		Object* object = gObjectManager->GetObject(id);

		if (object != nullptr) {
			object->bHiddenInTopView = false;
		}
	}

	mbForcePanelSync = true;
}

void RaptorEditor::TopViewToggleSnap()
{
	mToolState.ToolSnapEnabled = !mToolState.ToolSnapEnabled;
}

void RaptorEditor::TopViewAdjustSnap(int32 direction) { AdjustSnapLevel(direction); }

uint32 RaptorEditor::SnapSelectionToGrid()
{
	if (IsSimulationMode() || gWorld->pPrototype == nullptr || mSelection.IsEmpty()) {
		return 0;
	}

	struct Snap
	{
		Object* pObject = nullptr;
		Vec3f Before;
		Vec3f After;
	};

	std::vector<BrushSource> sources;
	GatherBrushSources(*gWorld->pPrototype, sources);

	DoublingArray<Snap> snaps;

	for (uint32 i = 0; i < mSelection.GetCount(); i++) {
		Object* object = mSelection.GetObject(i);

		if (object == nullptr || object->HasTags(eObjectTag::LockTransform)) {
			continue;
		}

		Vec3f anchor = object->GetPosition();
		bool has_bounds = false;

		for (const BrushSource& source : sources) {
			if (source.pObject != object) {
				continue;
			}

			for (const Vec3f vertex : source.pBrush->GetVertices()) {
				const Vec4f point = source.World * Vec4f(vertex.X, vertex.Y, vertex.Z, 1.0f);
				const Vec3f world_point(point.X, point.Y, point.Z);

				if (has_bounds) {
					anchor = Vec3f::Min(anchor, world_point);
				}
				else {
					anchor = world_point;
					has_bounds = true;
				}
			}
		}

		const Vec3f offset = RoundToGrid(anchor) - anchor;

		if (offset.Length() < scGridSnapTolerance) {
			continue;
		}

		const Vec3f before = object->GetPosition();
		snaps.Insert(Snap { object, before, before + offset });
	}

	for (const Snap& snap : snaps) {
		EmplaceEditOperation<MoveOperation>(snap.pObject, snap.Before, snap.After, static_cast<int32>(snaps.Size));
	}

	LogInfo(LC_CORE, "Snapped {} object(s) to the {} grid", snaps.Size, GetSnapStep());

	return static_cast<uint32>(snaps.Size);
}


} // namespace fx::editor

#endif
