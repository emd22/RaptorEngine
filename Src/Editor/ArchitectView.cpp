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
#include <Object/ObjectManager.hpp>
#include <Renderer/Globals.hpp>
#include <Renderer/Light.hpp>
#include <Renderer/LightManager.hpp>
#include <World.hpp>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <vector>

/////////////////////////////////////
// Operation includes
/////////////////////////////////////

#include "Operation/History.hpp"
#include "Operation/LightOperation.hpp"
#include "Operation/ObjectOperation.hpp"
#include "Operation/SubviewOperation.hpp"
#include "Operation/WorldOperation.hpp"


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
constexpr float32 scHullEdgeTolerance = 1e-3f;
constexpr float32 scGroundFootprintInset = 1e-3f;
constexpr float32 scGroundFitTolerance = 1e-3f;
constexpr float32 scDBoundThickness = 1.0f;
constexpr uint32 scDBoundSideCount = 6;
constexpr float32 scDecalDepth = 0.5f;
constexpr float32 scDecalRayReach = 1000.0f;
constexpr float32 scMinTextureScale = 1.0f / 64.0f;
constexpr float32 scMinPatchTextureScale = 1e-3f;
constexpr float32 scMaxTextureScale = 1024.0f;
constexpr float32 scDegreesPerTurn = 360.0f;

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
	if (object.IsPatch()) {
		return eTopViewBrushKind::Patch;
	}

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

bool IsOnBrushEdge(const Brush& brush, const Vec3f point)
{
	for (const Brush::Face& face : brush.Faces) {
		for (uint32 i = 0; i < face.Vertices.Size; i++) {
			const Vec3f start = face.Vertices[i];
			const Vec3f edge = face.Vertices[(i + 1) % face.Vertices.Size] - start;
			const float32 length_squared = edge.Dot(edge);

			float32 along = 0.0f;

			if (length_squared > 0.0f) {
				along = std::clamp((point - start).Dot(edge) / length_squared, 0.0f, 1.0f);
			}

			if ((start + edge * along - point).Length() <= scHullEdgeTolerance) {
				return true;
			}
		}
	}

	return false;
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

	for (ObjectID id : blockout->BlockoutObjects) {
		Object* object = gObjectManager->GetObject(id);
		const Patch* patch = blockout->GetPatch(object);

		if (patch == nullptr || !patch->IsValid()) {
			continue;
		}

		SizedArray<Vec3f> positions;
		SizedArray<Vec3f> normals;
		SizedArray<Vec3f> tangents;
		SizedArray<float32> handedness;
		SizedArray<Vec2f> texcoords;
		SizedArray<uint32> indices;

		patch->GenerateMesh(positions, normals, tangents, handedness, texcoords, indices);

		const Mat4f world = object->GetWorldMatrix();

		for (size_t index = 0; index + 2 < indices.Size; index += 3) {
			points.Clear();

			for (uint32 corner = 0; corner < 3; corner++) {
				const Vec3f vertex = positions[indices[index + corner]];
				const Vec4f point = world * Vec4f(vertex.X, vertex.Y, vertex.Z, 1.0f);

				points.Insert(Vec3f(point.X, point.Y, point.Z));
			}

			ClipToFootprintEdge(points, scratch, axes.U, min_u, 1.0f);
			ClipToFootprintEdge(points, scratch, axes.U, max_u, -1.0f);
			ClipToFootprintEdge(points, scratch, axes.V, min_v, 1.0f);
			ClipToFootprintEdge(points, scratch, axes.V, max_v, -1.0f);

			if (points.IsEmpty()) {
				continue;
			}

			HeightSpan span { FLT_MAX, -FLT_MAX };

			for (const Vec3f point : points) {
				const float32 height = axes.DepthSign * GetAxis(point, axes.W);

				span.Bottom = std::min<float32>(span.Bottom, height);
				span.Top = std::max<float32>(span.Top, height);
			}

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
	view.Notice = mNotice;
	view.NoticeSerial = mNoticeSerial;

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

	struct PatchGrid
	{
		uint32 Base = 0;
		uint32 Columns = 0;
		uint32 Rows = 0;
	};

	PatchMesh patch_mesh;
	DoublingArray<Vec3f> control_points;
	DoublingArray<PatchGrid> grids;

	for (ObjectID id : blockout->BlockoutObjects) {
		Object* object = gObjectManager->GetObject(id);
		const Patch* patch = blockout->GetPatch(object);

		if (patch == nullptr) {
			continue;
		}

		if (object->bHiddenInTopView) {
			view.HiddenCount++;
			continue;
		}

		const Mat4f world = object->GetWorldMatrix();

		Patch pieces[Patch::scMaxClosurePieces];
		const uint32 piece_count = patch->BuildClosure(pieces);

		corners.Clear();
		grids.Clear();

		for (uint32 piece = 0; piece <= piece_count; piece++) {
			const Patch* shape = patch;

			if (piece > 0) {
				shape = &pieces[piece - 1];
			}

			Patch grid = shape->Clone();
			grid.bSmooth = true;
			grid.Tessellate(patch_mesh);

			if (patch_mesh.Vertices.IsEmpty()) {
				continue;
			}

			grids.Insert(PatchGrid {
				.Base = static_cast<uint32>(corners.Size),
				.Columns = patch_mesh.Columns,
				.Rows = patch_mesh.Rows,
			});

			for (const PatchVertex& vertex : patch_mesh.Vertices) {
				const Vec4f point = world * Vec4f(static_cast<float32>(vertex.Position.X),
												  static_cast<float32>(vertex.Position.Y),
												  static_cast<float32>(vertex.Position.Z), 1.0f);
				corners.Insert(Vec3f(point.X, point.Y, point.Z));
			}
		}

		if (grids.IsEmpty()) {
			continue;
		}

		TopViewBrush& out = view.Brushes.Emplace();

		out.ObjectId = object->ID;
		out.Kind = eTopViewBrushKind::Patch;
		out.bSelected = mSelection.Contains(object);
		out.bSelectable = IsObjectSelectable(object);
		out.PatchWidth = static_cast<uint16>(patch->Width);
		out.PatchHeight = static_cast<uint16>(patch->Height);
		out.PatchSubdivisionsU = static_cast<uint16>(patch->SubdivisionsU);
		out.PatchSubdivisionsV = static_cast<uint16>(patch->SubdivisionsV);
		out.bPatchSmooth = patch->bSmooth;
		out.PatchClosure = patch->Closure;
		out.PatchThickness = patch->Thickness;

		control_points.Clear();

		for (const PatchPoint& point : patch->Points) {
			const Vec4f moved = world * Vec4f(point.Position.X, point.Position.Y, point.Position.Z, 1.0f);
			control_points.Insert(Vec3f(moved.X, moved.Y, moved.Z));
		}

		const Vec3f pivot = object->GetPosition() + patch->GetCenter();

		for (uint32 plane_index = 0; plane_index < scViewPlaneCount; plane_index++) {
			const ViewPlaneAxes& axes = scViewPlaneAxes[plane_index];
			TopViewProjection& projection = out.Views[plane_index];

			projection.Pivot = Project(pivot, axes);

			for (const Vec3f& control_point : control_points) {
				projection.ControlPoints.Insert(Project(control_point, axes));
			}

			for (const PatchPoint& point : patch->Points) {
				projection.ControlWeights.Insert(point.Weight);
			}

			projected.Clear();

			float32 near_depth = -FLT_MAX;

			for (const Vec3f& corner : corners) {
				projected.Insert(Project(corner, axes));
				near_depth = std::max(near_depth, axes.DepthSign * GetAxis(corner, axes.W));
			}

			projection.NearDepth = near_depth;

			segments.Clear();

			const auto add_edge = [&](uint32 from, uint32 to)
			{
				const Vec2f a = projected[from];
				const Vec2f b = projected[to];

				if ((b - a).LengthSquared() >= scMinSegmentLengthSq) {
					segments.Insert(MakeSegment(a, b));
				}
			};

			for (const PatchGrid& grid : grids) {
				const uint32 columns = grid.Columns;

				for (uint32 row = 0; row < grid.Rows; row++) {
					for (uint32 column = 0; column + 1 < columns; column++) {
						add_edge(grid.Base + row * columns + column, grid.Base + row * columns + column + 1);
					}
				}

				for (uint32 column = 0; column < columns; column++) {
					for (uint32 row = 0; row + 1 < grid.Rows; row++) {
						add_edge(grid.Base + row * columns + column, grid.Base + (row + 1) * columns + column);
					}
				}
			}

			std::sort(segments.begin(), segments.end(), IsSegmentKeyLess);

			const Segment* const unique_end = std::unique(segments.begin(), segments.end(), IsSegmentKeyEqual);

			for (const Segment* segment = segments.begin(); segment != unique_end; segment++) {
				projection.Edges.Insert(TopViewEdge { .A = segment->A, .B = segment->B });
			}

			ConvexHull(projected, hull);

			projection.Hull.Assign(hull.pData, hull.Size);
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

	if (!object->HasTags(eObjectTag::Blockout) ||
		(blockout->GetBrush(object) == nullptr && blockout->GetPatch(object) == nullptr)) {
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

		if (gWorld->pPrototype->GetBrush(object) == nullptr && gWorld->pPrototype->GetPatch(object) == nullptr &&
			!gWorld->pPrototype->IsSubviewObject(object)) {
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

		Vec3f center = object->GetPosition();
		Vec3f local_center;

		if (gWorld->pPrototype->GetLocalCenter(object, local_center)) {
			center = center + local_center;
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

		if (brush == nullptr) {
			continue;
		}

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
	if (IsSimulationMode() || (!IsToolAvailable(eEditorTool::Vertex) && !IsToolAvailable(eEditorTool::Curve)) ||
		offset.Length() < scMinMoveDistance || static_cast<uint32>(plane) >= scViewPlaneCount) {
		return;
	}

	struct Edit
	{
		Object* pObject = nullptr;
		Brush::PlaneList Before;
		Brush::PlaneList After;
	};

	struct PatchEdit
	{
		Object* pObject = nullptr;
		Patch Before;
		Patch After;
	};

	std::vector<PatchEdit> patch_edits;

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

		const Mat4f world = object->GetWorldMatrix();
		const Vec4f local_offset = world.Inverse() * Vec4f(offset.X, offset.Y, offset.Z, 0.0f);
		const Vec3f local_move(local_offset.X, local_offset.Y, local_offset.Z);

		const Patch* patch = gWorld->pPrototype->GetPatch(object);

		if (patch != nullptr) {
			Patch moved = patch->Clone();
			uint32 moved_count = 0;

			for (PatchPoint& point : moved.Points) {
				const Vec4f world_point = world * Vec4f(point.Position.X, point.Position.Y, point.Position.Z, 1.0f);
				const Vec2f projected_position = Project(Vec3f(world_point.X, world_point.Y, world_point.Z),
														 GetViewPlaneAxes(plane));

				for (const TopViewVertexHandle& handle : handles) {
					if (handle.ObjectId == object_id &&
						projected_position.IsCloseTo(handle.Position, scVertexMatchDistance)) {
						point.Position = point.Position + local_move;
						moved_count++;
						break;
					}
				}
			}

			if (moved_count > 0) {
				patch_edits.push_back(PatchEdit { object, patch->Clone(), std::move(moved) });
			}

			continue;
		}

		const Brush* brush = gWorld->pPrototype->GetBrush(object);

		if (brush == nullptr) {
			continue;
		}

		SizedArray<Vec3f> points;
		points.InitCapacity(brush->GetVertices().Size);

		DoublingArray<Vec3f> moved_points;
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

			if (is_moved) {
				points.Insert(vertex + local_move);
				moved_points.Insert(vertex + local_move);
				moved_count++;
			}
			else {
				points.Insert(vertex);
			}
		}

		if (moved_count == 0) {
			continue;
		}

		const Brush moved = Brush::FromVertices(points, brush->Planes);
		const Vec3f extent = moved.GetBoundsMax() - moved.GetBoundsMin();

		if (!moved.IsValid() || extent.X < scMinBrushExtent || extent.Y < scMinBrushExtent ||
			extent.Z < scMinBrushExtent) {
			Warn("Cannot move those vertices of '{}', the brush would collapse or have too many faces",
				 object->Name.Get());
			continue;
		}

		const bool is_concave = std::any_of(moved_points.begin(), moved_points.end(),
											[&](const Vec3f point) { return !IsOnBrushEdge(moved, point); });

		if (is_concave) {
			Warn("Cannot move those vertices of '{}', the brush would no longer be convex", object->Name.Get());
			continue;
		}

		edits.Insert(Edit { object, brush->Planes, moved.Planes });
	}

	const int32 group_size = static_cast<int32>(edits.Size + patch_edits.size());

	for (const Edit& edit : edits) {
		EmplaceEditOperation<BrushEditOperation>(edit.pObject, edit.Before, edit.After, group_size);
	}

	for (PatchEdit& edit : patch_edits) {
		EmplaceEditOperation<PatchEditOperation>(edit.pObject, std::move(edit.Before), std::move(edit.After),
												 group_size);
	}
}

void RaptorEditor::TopViewCreatePatch(eViewPlane plane, ePatchPrimitive primitive, Vec3f min, Vec3f max,
									  bool drop_to_ground, const PatchCommand& settings)
{
	if (IsSimulationMode() || !IsToolAvailable(eEditorTool::Curve) || static_cast<uint32>(plane) >= scViewPlaneCount) {
		return;
	}

	float32 offset = 0.0f;

	if (drop_to_ground && FindGroundOffset(min, max, plane, offset)) {
		const Vec3f shift = DepthAxisVector(plane, offset);

		min += shift;
		max += shift;
	}

	Patch patch = Patch::MakePrimitive(primitive, min, max, GetViewPlaneAxes(plane).W);

	if (!patch.IsValid()) {
		return;
	}

	patch.SubdivisionsU = std::clamp(settings.SubdivisionsU, Patch::scMinSubdivisions, Patch::scMaxSubdivisions);
	patch.SubdivisionsV = std::clamp(settings.SubdivisionsV, Patch::scMinSubdivisions, Patch::scMaxSubdivisions);
	patch.bSmooth = settings.bSmooth;
	patch.Closure = settings.Closure;
	patch.Thickness = settings.Thickness;

	Object* created = CreateWorldPatch(std::move(patch));

	if (created != nullptr) {
		SelectObject(created, false);
	}
}

static bool FindHandledControlPoint(const Patch& patch, const Mat4f& world, eViewPlane plane, ObjectID object_id,
									const DynArray<TopViewVertexHandle>& handles, uint32& out_column, uint32& out_row)
{
	for (const TopViewVertexHandle& handle : handles) {
		if (handle.ObjectId != object_id) {
			continue;
		}

		for (uint32 row = 0; row < patch.Height; row++) {
			for (uint32 column = 0; column < patch.Width; column++) {
				const Vec3f local = patch.GetPoint(column, row).Position;
				const Vec4f point = world * Vec4f(local.X, local.Y, local.Z, 1.0f);

				if (Project(Vec3f(point.X, point.Y, point.Z), GetViewPlaneAxes(plane))
						.IsCloseTo(handle.Position, scVertexMatchDistance)) {
					out_column = column;
					out_row = row;
					return true;
				}
			}
		}
	}

	return false;
}

static bool IsHandledControlPoint(const Patch& patch, const Mat4f& world, eViewPlane plane, ObjectID object_id,
								  const DynArray<TopViewVertexHandle>& handles, uint32 column, uint32 row)
{
	const Vec3f local = patch.GetPoint(column, row).Position;
	const Vec4f point = world * Vec4f(local.X, local.Y, local.Z, 1.0f);
	const Vec2f projected = Project(Vec3f(point.X, point.Y, point.Z), GetViewPlaneAxes(plane));

	for (const TopViewVertexHandle& handle : handles) {
		if (handle.ObjectId == object_id && projected.IsCloseTo(handle.Position, scVertexMatchDistance)) {
			return true;
		}
	}

	return false;
}

static float32 GetArcWeight(const Vec3f corner, const Vec3f start, const Vec3f end)
{
	const Vec3f to_start = start - corner;
	const Vec3f to_end = end - corner;
	const float32 lengths = to_start.Length() * to_end.Length();

	if (lengths <= 1e-8f) {
		return 1.0f;
	}

	const float32 cos_angle = std::clamp(to_start.Dot(to_end) / lengths, -1.0f, 1.0f);

	return std::max(std::sqrt((1.0f - cos_angle) * 0.5f), Patch::scMinWeight);
}

static float32 GetRoundWeight(const Patch& patch, uint32 column, uint32 row)
{
	const Vec3f corner = patch.GetPoint(column, row).Position;
	float32 weight = 1.0f;

	if ((column & 1) == 1) {
		weight *= GetArcWeight(corner, patch.GetPoint(column - 1, row).Position,
							   patch.GetPoint(column + 1, row).Position);
	}

	if ((row & 1) == 1) {
		weight *= GetArcWeight(corner, patch.GetPoint(column, row - 1).Position,
							   patch.GetPoint(column, row + 1).Position);
	}

	return weight;
}

static bool ApplyWeightCommand(Patch& patch, const PatchCommand& command, const Mat4f& world, eViewPlane plane,
							   ObjectID object_id, const DynArray<TopViewVertexHandle>& handles)
{
	bool has_handle = false;

	for (const TopViewVertexHandle& handle : handles) {
		has_handle = has_handle || handle.ObjectId == object_id;
	}

	if (!has_handle && command.Type == ePatchCommand::SetWeight) {
		return false;
	}

	const Patch source = patch.Clone();
	bool changed = false;

	for (uint32 row = 0; row < patch.Height; row++) {
		for (uint32 column = 0; column < patch.Width; column++) {
			if (has_handle && !IsHandledControlPoint(source, world, plane, object_id, handles, column, row)) {
				continue;
			}

			float32 weight = std::max(command.Weight, Patch::scMinWeight);

			if (command.Type == ePatchCommand::RoundWeights) {
				weight = GetRoundWeight(source, column, row);
			}

			patch.GetPoint(column, row).Weight = weight;
			changed = true;
		}
	}

	return changed;
}

static uint32 PickSubPatch(uint32 control_index, bool has_control, uint32 sub_patch_count, bool for_removal)
{
	uint32 index = sub_patch_count / 2;

	if (has_control) {
		index = std::min(control_index / 2, sub_patch_count - 1);
	}

	if (for_removal && index + 1 >= sub_patch_count && index > 0) {
		index--;
	}

	return index;
}

static bool IsSamePatch(const Patch& a, const Patch& b)
{
	if (a.Width != b.Width || a.Height != b.Height || a.SubdivisionsU != b.SubdivisionsU ||
		a.SubdivisionsV != b.SubdivisionsV || a.bSmooth != b.bSmooth || a.SmoothingGroup != b.SmoothingGroup ||
		a.Closure != b.Closure || a.Thickness != b.Thickness) {
		return false;
	}

	for (uint32 i = 0; i < a.Points.Size; i++) {
		if (!(a.Points[i].Position == b.Points[i].Position) || !(a.Points[i].Texcoord == b.Points[i].Texcoord) ||
			a.Points[i].Weight != b.Points[i].Weight) {
			return false;
		}
	}

	return true;
}

static bool ApplyTextureMapping(Patch& patch, const PatchTextureSettings& settings, const Mat4f& world)
{
	if (std::fabs(settings.Scale) < scMinPatchTextureScale) {
		return false;
	}

	switch (settings.Mapping) {
	case ePatchTextureMapping::Natural:
		patch.NaturalTexture(settings.Scale);
		return true;
	case ePatchTextureMapping::World: {
		uint32 axis = Patch::scAutoTextureAxis;

		if (settings.Axis != ePatchTextureAxis::Auto) {
			axis = static_cast<uint32>(settings.Axis) - static_cast<uint32>(ePatchTextureAxis::X);
		}

		patch.WorldTexture(world, settings.Scale, axis);
		return true;
	}
	case ePatchTextureMapping::Fit:
		if (std::fabs(settings.Repeats.X) < scMinPatchTextureScale ||
			std::fabs(settings.Repeats.Y) < scMinPatchTextureScale) {
			return false;
		}

		patch.FitTexture(settings.Repeats.X, settings.Repeats.Y);
		return true;
	default:
		return false;
	}
}

static bool ApplyPatchCommand(Patch& patch, const PatchCommand& command, const Mat4f& world, bool has_control,
							  uint32 column, uint32 row)
{
	switch (command.Type) {
	case ePatchCommand::Invert:
		patch.Invert();
		return true;
	case ePatchCommand::Transpose:
		patch.Transpose();
		return true;
	case ePatchCommand::InsertColumns:
		return patch.InsertColumns(PickSubPatch(column, has_control, patch.GetSubPatchColumns(), false));
	case ePatchCommand::InsertRows:
		return patch.InsertRows(PickSubPatch(row, has_control, patch.GetSubPatchRows(), false));
	case ePatchCommand::RemoveColumns:
		return patch.RemoveColumns(PickSubPatch(column, has_control, patch.GetSubPatchColumns(), true));
	case ePatchCommand::RemoveRows:
		return patch.RemoveRows(PickSubPatch(row, has_control, patch.GetSubPatchRows(), true));
	case ePatchCommand::MapTexture:
		return ApplyTextureMapping(patch, command.Texture, world);
	case ePatchCommand::SetSubdivisions:
		patch.SubdivisionsU = std::clamp(command.SubdivisionsU, Patch::scMinSubdivisions, Patch::scMaxSubdivisions);
		patch.SubdivisionsV = std::clamp(command.SubdivisionsV, Patch::scMinSubdivisions, Patch::scMaxSubdivisions);
		return true;
	case ePatchCommand::SetSmooth:
		patch.bSmooth = command.bSmooth;
		return true;
	case ePatchCommand::SetClosure:
		patch.Closure = command.Closure;

		if (command.Closure == ePatchClosure::Thicken) {
			patch.Thickness = command.Thickness;
		}

		return true;
	default:
		return false;
	}
}

void RaptorEditor::TopViewPatchCommand(eViewPlane plane, const PatchCommand& command,
									   const DynArray<TopViewVertexHandle>& handles)
{
	if (IsSimulationMode() || gWorld->pPrototype == nullptr || static_cast<uint32>(plane) >= scViewPlaneCount) {
		return;
	}

	struct PatchEdit
	{
		Object* pObject = nullptr;
		Patch Before;
		Patch After;
	};

	std::vector<PatchEdit> edits;
	uint32 patch_count = 0;

	for (uint32 i = 0; i < mSelection.GetCount(); i++) {
		Object* object = mSelection.GetObject(i);
		const Patch* patch = gWorld->pPrototype->GetPatch(object);

		if (patch == nullptr || !CanEditObject(object)) {
			continue;
		}

		patch_count++;

		uint32 column = 0;
		uint32 row = 0;
		const bool has_control = FindHandledControlPoint(*patch, object->GetWorldMatrix(), plane, object->ID, handles,
														 column, row);

		Patch changed = patch->Clone();

		if (command.Type == ePatchCommand::SetWeight || command.Type == ePatchCommand::RoundWeights) {
			if (!ApplyWeightCommand(changed, command, object->GetWorldMatrix(), plane, object->ID, handles)) {
				Warn("Select control points of patch '{}' to set their weight", object->Name.Get());
				continue;
			}
		}
		else if (!ApplyPatchCommand(changed, command, object->GetWorldMatrix(), has_control, column, row)) {
			if (command.Type == ePatchCommand::MapTexture) {
				Warn("The texture scale and repeats can't be 0");
				break;
			}

			Warn("Patch '{}' can't be changed that way, it is at its smallest or largest size", object->Name.Get());
			continue;
		}

		if (IsSamePatch(changed, *patch)) {
			continue;
		}

		if (command.Type == ePatchCommand::SetClosure && command.Closure == ePatchClosure::Cap) {
			Patch pieces[Patch::scMaxClosurePieces];

			if (changed.BuildClosure(pieces) == 0) {
				Warn("Patch '{}' has no curved open edge to cap", object->Name.Get());
			}
		}

		edits.push_back(PatchEdit { object, patch->Clone(), std::move(changed) });
	}

	if (patch_count == 0 && command.Type == ePatchCommand::MapTexture) {
		Warn("Select the patches to change the texture of first");
	}

	for (PatchEdit& edit : edits) {
		EmplaceEditOperation<PatchEditOperation>(edit.pObject, std::move(edit.Before), std::move(edit.After),
												 static_cast<int32>(edits.size()));
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

Object* RaptorEditor::CreateWorldPatch(Patch&& world_patch, int32 group_size, MaterialID material)
{
	if (IsSimulationMode() || gWorld->pPrototype == nullptr || !world_patch.IsValid()) {
		return nullptr;
	}

	const Vec3f position = world_patch.GetCenter();

	for (PatchPoint& point : world_patch.Points) {
		point.Position = point.Position - position;
	}

	ObjectSnapshot snapshot;
	snapshot.Position = position;
	snapshot.Material = material;
	snapshot.pPatch = std::make_shared<const Patch>(std::move(world_patch));

	if (material.IsNull()) {
		snapshot.Material = GetNewBrushMaterial();
	}

	return EmplaceEditOperation<CreateBrushOperation>(Brush::PlaneList {}, snapshot, group_size).GetCreated();
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
			Warn("Pick Probe Volumes, Reflection Probes or Volumes in the Data filter to create one");
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
			Warn("There is no surface under the decal to project onto, it was left at the base");
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

	Object* created = EmplaceEditOperation<CreateBrushOperation>(
						  Brush::FromBox(half_extent * -1.0f, half_extent).Planes, snapshot)
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
		Warn("Skipped {} arch segments that were too thin to make into brushes", segment_count - pieces.Size);
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

static bool ExtendBoundsByPatch(const Patch& patch, const Mat4f& world, Vec3f& min, Vec3f& max)
{
	Patch pieces[Patch::scMaxClosurePieces];
	const uint32 piece_count = patch.BuildClosure(pieces);

	bool extended = false;
	PatchMesh mesh;

	for (uint32 piece = 0; piece <= piece_count; piece++) {
		const Patch* shape = &patch;

		if (piece > 0) {
			shape = &pieces[piece - 1];
		}

		shape->Tessellate(mesh);

		for (const PatchVertex& vertex : mesh.Vertices) {
			const Vec4f point = world * Vec4f(static_cast<float32>(vertex.Position.X),
											  static_cast<float32>(vertex.Position.Y),
											  static_cast<float32>(vertex.Position.Z), 1.0f);
			const Vec3f world_point(point.X, point.Y, point.Z);

			min = Vec3f::Min(min, world_point);
			max = Vec3f::Max(max, world_point);
			extended = true;
		}
	}

	return extended;
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

	for (ObjectID id : blockout->BlockoutObjects) {
		Object* object = gObjectManager->GetObject(id);

		if (object == nullptr || object == blockout->pXFormObject || object == blockout->pPreviewObject) {
			continue;
		}

		const Patch* patch = blockout->GetPatch(object);

		if (patch != nullptr && patch->IsValid() &&
			ExtendBoundsByPatch(*patch, object->GetWorldMatrix(), level_min, level_max)) {
			has_geometry = true;
		}
	}

	if (!has_geometry) {
		Warn("There is no level geometry to fit the map bounds to");
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
		Warn("The clip line does not cross any of the selected brushes");
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
		Warn("Select the brushes to subtract first");
		return;
	}

	if (SubtractSelection(keep_cutters) == 0) {
		Warn("The selected brushes do not overlap any other brushes");
	}
}

static bool ApplyTextureCommand(Brush& brush, uint32 plane_index, const TextureCommand& command, float32 step,
								const Vec3f origin)
{
	BrushFaceTexture& texture = brush.Planes[plane_index].Texture;
	const BrushFaceTexture before = texture;

	switch (command.Type) {
	case eTextureCommand::Shift:
		texture.Offset = texture.Offset - command.Amount * step / texture.Scale;
		break;
	case eTextureCommand::Scale: {
		const Vec2f scale = texture.Scale * command.Amount;

		if (std::fabs(scale.X) < scMinTextureScale || std::fabs(scale.Y) < scMinTextureScale ||
			std::fabs(scale.X) > scMaxTextureScale || std::fabs(scale.Y) > scMaxTextureScale) {
			return false;
		}

		texture.Offset = texture.Offset * texture.Scale / scale;
		texture.Scale = scale;
		break;
	}
	case eTextureCommand::Rotate:
		texture.Rotation = std::fmod(texture.Rotation + command.Amount.X, scDegreesPerTurn);

		if (texture.Rotation < 0.0f) {
			texture.Rotation += scDegreesPerTurn;
		}
		break;
	case eTextureCommand::Reset:
		brush.ResetFaceTexture(plane_index);
		break;
	case eTextureCommand::WorldAlign:
		brush.AlignFaceTextureToWorld(plane_index, origin);
		break;
	}

	return !(texture.Offset == before.Offset) || !(texture.Scale == before.Scale) ||
		   texture.Rotation != before.Rotation;
}

void RaptorEditor::TopViewEditTextures(ObjectID face_object, const Vec3f face_normal, const TextureCommand& command)
{
	Blockout* blockout = gWorld->pPrototype;

	if (IsSimulationMode() || blockout == nullptr || !IsToolAvailable(eEditorTool::Face)) {
		return;
	}

	struct Edit
	{
		Object* pObject = nullptr;
		Brush::PlaneList Before;
		Brush::PlaneList After;
	};

	const bool has_face = !face_object.IsInvalid();

	DoublingArray<Object*> targets;

	if (has_face) {
		Object* object = FindTopViewObject(face_object);

		if (object != nullptr) {
			targets.Insert(object);
		}
	}
	else {
		for (uint32 i = 0; i < mSelection.GetCount(); i++) {
			targets.Insert(mSelection.GetObject(i));
		}
	}

	const float32 step = GetSnapStep();

	DoublingArray<Edit> edits;
	uint32 brush_count = 0;
	uint32 limited_count = 0;

	for (Object* object : targets) {
		const Brush* brush = blockout->GetBrush(object);

		if (brush == nullptr || object->IsDecal() || !CanEditObject(object)) {
			continue;
		}

		brush_count++;

		if (command.Type == eTextureCommand::WorldAlign && !object->mRotation.IsCloseTo(Quat::scIdentity)) {
			Warn("'{}' is rotated, so its textures can't be lined up with the world grid", object->Name.Get());
			continue;
		}

		Brush changed = Brush::FromPlanes(brush->Planes);

		uint32 first_plane = 0;
		uint32 end_plane = changed.Planes.Size;

		if (has_face) {
			const Vec3f local_direction = face_normal.Rotate(object->mRotation.Conjugate());
			const int32 plane_index = changed.FindPlane(local_direction);

			if (plane_index == Brush::scNoPlane ||
				changed.Planes[plane_index].Normal.Dot(local_direction.Normalize()) < scFaceMatchDot) {
				Warn("The picked face of '{}' is no longer there, pick it again", object->Name.Get());
				continue;
			}

			first_plane = static_cast<uint32>(plane_index);
			end_plane = first_plane + 1;
		}

		bool is_changed = false;

		for (uint32 plane = first_plane; plane < end_plane; plane++) {
			if (ApplyTextureCommand(changed, plane, command, step, object->GetPosition())) {
				is_changed = true;
			}
			else if (command.Type == eTextureCommand::Scale) {
				limited_count++;
			}
		}

		if (is_changed) {
			edits.Insert(Edit { object, brush->Planes, changed.Planes });
		}
	}

	if (brush_count == 0) {
		Warn("Pick a face with the Face tool, or select brushes, to change their textures");
		return;
	}

	if (limited_count > 0) {
		Warn("Some textures are already at their smallest or largest scale");
	}

	for (const Edit& edit : edits) {
		EmplaceEditOperation<BrushEditOperation>(edit.pObject, edit.Before, edit.After, static_cast<int32>(edits.Size));
	}
}

void RaptorEditor::TopViewSetFaceMaterial(ObjectID face_object, const Vec3f face_normal, MaterialID material)
{
	Blockout* blockout = gWorld->pPrototype;

	if (IsSimulationMode() || blockout == nullptr || !IsToolAvailable(eEditorTool::Face)) {
		return;
	}

	Object* object = FindTopViewObject(face_object);
	const Brush* brush = blockout->GetBrush(object);

	if (brush == nullptr || object->IsDecal() || !CanEditObject(object)) {
		Warn("Only the faces of brushes can have a material of their own");
		return;
	}

	Brush changed = Brush::FromPlanes(brush->Planes);

	const Vec3f local_direction = face_normal.Rotate(object->mRotation.Conjugate());
	const int32 plane_index = changed.FindPlane(local_direction);

	if (plane_index == Brush::scNoPlane ||
		changed.Planes[plane_index].Normal.Dot(local_direction.Normalize()) < scFaceMatchDot) {
		Warn("The picked face of '{}' is no longer there, pick it again", object->Name.Get());
		return;
	}

	const MaterialLibraryID library_id = blockout->GetIDForMaterial(material);

	if (!library_id.IsValid()) {
		return;
	}

	String name = blockout->GetMaterialName(library_id);

	if (blockout->GetIDForMaterial(mSelection.GetStoredMaterial(object)) == library_id) {
		name.Clear();
	}

	if (changed.Planes[plane_index].Material == name) {
		return;
	}

	changed.Planes[plane_index].Material = name;

	EmplaceEditOperation<BrushEditOperation>(object, brush->Planes, changed.Planes, 1);
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
	if (IsEditingSubview() && !IsSubviewSelected()) {
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

void RaptorEditor::TopViewToggleSnap() { mToolState.ToolSnapEnabled = !mToolState.ToolSnapEnabled; }

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

		const Patch* patch = gWorld->pPrototype->GetPatch(object);

		if (patch != nullptr) {
			const Mat4f world = object->GetWorldMatrix();

			for (const PatchPoint& control_point : patch->Points) {
				const Vec3f local = control_point.Position;
				const Vec4f point = world * Vec4f(local.X, local.Y, local.Z, 1.0f);
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
