#include "PortalVisibility.hpp"

#include <Math/SIMDHelper.hpp>
#include <algorithm>
#include <cfloat>
#include <cmath>

namespace fx {

namespace {

constexpr float32 scMinClipW = 1e-3f;

void AddToRect(const Vec4f& clip, ScreenRect& rect)
{
	const float32 inverse_w = 1.0f / std::max(clip.W, scMinClipW);
	const float32 x = clip.X * inverse_w;
	const float32 y = clip.Y * inverse_w;

	rect.MinX = std::min(rect.MinX, x);
	rect.MinY = std::min(rect.MinY, y);

	rect.MaxX = std::max(rect.MaxX, x);
	rect.MaxY = std::max(rect.MaxY, y);
}

ScreenRect MakeEmptyRect() { return ScreenRect { FLT_MAX, FLT_MAX, -FLT_MAX, -FLT_MAX }; }

float32 GetNearDistance(const Vec4f& clip) { return clip.W - scMinClipW; }

} // namespace

ScreenRect ScreenRect::Intersect(const ScreenRect& other) const
{
	return ScreenRect { std::max(MinX, other.MinX), std::max(MinY, other.MinY), std::min(MaxX, other.MaxX),
						std::min(MaxY, other.MaxY) };
}

ScreenRect ScreenRect::Union(const ScreenRect& other) const
{
	if (IsEmpty()) {
		return other;
	}

	if (other.IsEmpty()) {
		return *this;
	}

	return ScreenRect { std::min(MinX, other.MinX), std::min(MinY, other.MinY), std::max(MaxX, other.MaxX),
						std::max(MaxY, other.MaxY) };
}

void PortalVisibility::Disable()
{
	mbActive = false;
	mCameraArea = -1;
	mpGraph = nullptr;

	VisibleAreaCount = 0;
	PortalsTested = 0;
	PortalsPassed = 0;
}

void PortalVisibility::Update(const AreaGraph& graph, const Mat4f& view_projection, const Vec3f& camera_position)
{
	Disable();

	if (graph.IsEmpty()) {
		return;
	}

	mCameraArea = graph.FindArea(camera_position);

	if (mCameraArea < 0) {
		return;
	}

	mpGraph = &graph;
	mViewProjection = view_projection;
	mCameraPosition = camera_position;

	const uint32 area_count = graph.GetAreaCount();

	if (mAreaRects.Capacity != area_count) {
		mAreaRects.InitCapacity(area_count);
	}

	mAreaRects.Clear();

	for (uint32 area = 0; area < area_count; area++) {
		mAreaRects.Insert(ScreenRect {});
	}

	const uint32 portal_count = graph.GetPortalCount();

	if (mPortalStates.Capacity != portal_count) {
		mPortalStates.InitCapacity(portal_count);
	}

	mPortalStates.Clear();

	for (uint32 portal = 0; portal < portal_count; portal++) {
		mPortalStates.Insert(ePortalState::Unreached);
	}

	uint32 max_polygon_size = 0;

	for (uint32 portal = 0; portal < graph.GetPortalCount(); portal++) {
		for (const uint32 size : graph.GetPortal(portal).PolygonSizes) {
			max_polygon_size = std::max(max_polygon_size, size);
		}
	}

	if (mClipPoints.Capacity < max_polygon_size) {
		mClipPoints.InitCapacity(max_polygon_size);
		mClippedPoints.InitCapacity(max_polygon_size + 1);
	}

	mbActive = true;

	Flow(static_cast<uint32>(mCameraArea), ScreenRect::MakeFull(), 0);

	for (const ScreenRect& rect : mAreaRects) {
		if (!rect.IsEmpty()) {
			VisibleAreaCount++;
		}
	}
}

void PortalVisibility::Flow(uint32 area, const ScreenRect& rect, uint32 depth)
{
	ScreenRect& area_rect = mAreaRects[area];

	if (area_rect.Contains(rect)) {
		return;
	}

	area_rect = area_rect.Union(rect);

	if (depth >= scMaxDepth) {
		return;
	}

	const MapArea& map_area = mpGraph->GetArea(area);

	for (const uint32 portal_index : map_area.Portals) {
		const MapPortal& portal = mpGraph->GetPortal(portal_index);

		if (!portal.bOpen) {
			mPortalStates[portal_index] = ePortalState::Closed;
			continue;
		}

		PortalsTested++;

		ScreenRect portal_rect;

		if (!GetPortalRect(portal, area, rect, portal_rect)) {
			if (mPortalStates[portal_index] == ePortalState::Unreached) {
				mPortalStates[portal_index] = ePortalState::Rejected;
			}

			continue;
		}

		PortalsPassed++;
		mPortalStates[portal_index] = ePortalState::Passed;

		Flow(portal.GetOtherArea(area), portal_rect, depth + 1);
	}
}

Vec4f PortalVisibility::ToClip(const Vec3f& point) const
{
	return mViewProjection * Vec4f(point.X, point.Y, point.Z, 1.0f);
}

bool PortalVisibility::GetPortalRect(const MapPortal& portal, uint32 from_area, const ScreenRect& parent_rect,
									 ScreenRect& out_rect)
{
	const bool is_from_back = (portal.BackArea == from_area);

	ScreenRect rect = MakeEmptyRect();
	uint32 first_point = 0;

	for (size_t polygon = 0; polygon < portal.PolygonSizes.Size; polygon++) {
		const uint32 size = portal.PolygonSizes[polygon];
		const MapPortalPlane& plane = portal.PolygonPlanes[polygon];

		const float32 distance = plane.Normal.Dot(mCameraPosition) - plane.Distance;

		if (std::abs(distance) <= scNearMargin) {
			const BBox padded_bounds = portal.Bounds.AddMargin(Vec3f(scNearMargin));

			const bool is_inside = mCameraPosition >= padded_bounds.Min && mCameraPosition <= padded_bounds.Max;

			if (is_inside) {
				out_rect = parent_rect;
				return true;
			}
		}

		bool is_facing = (distance > 0.0f);

		if (is_from_back) {
			is_facing = (distance < 0.0f);
		}

		if (!is_facing) {
			first_point += size;
			continue;
		}

		mClipPoints.Clear();

		for (uint32 point = 0; point < size; point++) {
			mClipPoints.Insert(ToClip(portal.Points[first_point + point]));
		}

		first_point += size;

		mClippedPoints.Clear();

		for (uint32 point = 0; point < size; point++) {
			const Vec4f& current = mClipPoints[point];
			const Vec4f& next = mClipPoints[(point + 1) % size];

			const float32 current_distance = GetNearDistance(current);
			const float32 next_distance = GetNearDistance(next);

			if (current_distance >= 0.0f) {
				mClippedPoints.Insert(current);
			}

			if ((current_distance >= 0.0f) != (next_distance >= 0.0f)) {
				const float32 t = current_distance / (current_distance - next_distance);

				// const Vec3f current_xzw =

				mClippedPoints.Insert(Vec4f(current.X + (next.X - current.X) * t, current.Y + (next.Y - current.Y) * t,
											current.Z + (next.Z - current.Z) * t,
											current.W + (next.W - current.W) * t));
			}
		}

		if (mClippedPoints.Size < 3) {
			continue;
		}

		for (const Vec4f& clip : mClippedPoints) {
			AddToRect(clip, rect);
		}
	}

	if (rect.IsEmpty()) {
		return false;
	}

	out_rect = rect.Intersect(parent_rect);

	return !out_rect.IsEmpty();
}

bool PortalVisibility::ProjectBounds(const BBox& bounds, ScreenRect& out_rect) const
{
	out_rect = MakeEmptyRect();

	for (uint32 corner = 0; corner < 8; corner++) {
		Vec3f point = bounds.Min;

		if ((corner & 1) != 0) {
			point.X = bounds.Max.X;
		}

		if ((corner & 2) != 0) {
			point.Y = bounds.Max.Y;
		}

		if ((corner & 4) != 0) {
			point.Z = bounds.Max.Z;
		}

		const Vec4f clip = ToClip(point);

		if (GetNearDistance(clip) < 0.0f) {
			return false;
		}

		AddToRect(clip, out_rect);
	}

	return true;
}

bool PortalVisibility::IsVisible(const AreaSet& areas, const BBox& bounds) const
{
	if (!mbActive || areas.bEverywhere) {
		return true;
	}

	bool has_rect = false;
	bool is_projected = false;
	ScreenRect bounds_rect;

	for (uint32 i = 0; i < areas.Count; i++) {
		const uint32 area = areas.Areas[i];

		if (area >= mAreaRects.Size) {
			return true;
		}

		const ScreenRect& area_rect = mAreaRects[area];

		if (area_rect.IsEmpty()) {
			continue;
		}

		if (!is_projected) {
			has_rect = ProjectBounds(bounds, bounds_rect);
			is_projected = true;
		}

		if (!has_rect || area_rect.Overlaps(bounds_rect)) {
			return true;
		}
	}

	return false;
}

} // namespace fx
