/*
 * File:        Occluders.hpp
 * Author:      emd22
 * Created:     25/10/2026
 * Description: Bounding volume hierarchy over the brushes and patch triangles that cast sun shadows
 */

#pragma once

#include "Patches.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace fx::mapc {

constexpr double scParallelEpsilon = 1e-14;
constexpr double scTriangleMinDistance = 1e-6;

constexpr double scNoHit = std::numeric_limits<double>::infinity();
constexpr uint32 scLeafSize = 4;
constexpr uint32 scMaxStackDepth = 64;

struct OccluderPlane
{
	Vec3d Normal = Vec3d(0.0);
	double Distance = 0.0;
};

struct Occluder
{
	uint32 FirstPlane = 0;
	uint32 PlaneCount = 0;

	int32 FirstCorner = -1;
	Vec3d Facing = Vec3d(0.0);

	Vec3d Min = Vec3d(0.0);
	Vec3d Max = Vec3d(0.0);
};

struct BvhNode
{
	Vec3d Min = Vec3d(0.0);
	Vec3d Max = Vec3d(0.0);

	int32 Left = -1;
	int32 Right = -1;

	uint32 First = 0;
	uint32 Count = 0;
};

class OccluderSet
{
public:
	void Build(const PlaneSet& planes, const MapSource& source, const PatchBuilder* patches)
	{
		for (const SourceBrush& brush : source.Brushes) {
			if (!brush.bVisible || !brush.bCastsShadow) {
				continue;
			}

			Occluder occluder;
			occluder.FirstPlane = mPlanes.Size;
			occluder.PlaneCount = brush.PlaneIndices.Size;
			occluder.Min = brush.Shape.Min;
			occluder.Max = brush.Shape.Max;

			for (const int32 plane_index : brush.PlaneIndices) {
				const Plane& plane = planes[plane_index];
				mPlanes.Emplace(OccluderPlane { plane.Normal, plane.Distance });
			}

			mOccluders.Emplace(occluder);
		}

		if (patches != nullptr) {
			for (const PatchSurface& surface : patches->Surfaces) {
				if (!surface.bCastsShadow) {
					continue;
				}

				for (size_t index = 0; index + 2 < surface.Indices.Size; index += 3) {
					Occluder occluder;
					occluder.FirstCorner = static_cast<int32>(mCorners.Size);

					for (uint32 corner = 0; corner < 3; corner++) {
						const PatchSurfaceVertex& vertex =
							surface.Vertices[static_cast<size_t>(surface.Indices[index + corner])];
						const Vec3d& point = vertex.Position;

						mCorners.Emplace(point);
						occluder.Facing = occluder.Facing + vertex.Normal;

						if (corner == 0) {
							occluder.Min = point;
							occluder.Max = point;
						}

						occluder.Min = MinVec(occluder.Min, point);
						occluder.Max = MaxVec(occluder.Max, point);
					}

					mOccluders.Emplace(occluder);
					mTriangleCount++;
				}
			}
		}

		if (mOccluders.Size > 0) {
			BuildNode(0, mOccluders.Size);
		}
	}

	uint32 GetCount() const { return mOccluders.Size - mTriangleCount; }
	uint32 GetTriangleCount() const { return mTriangleCount; }

	bool GetBounds(Vec3d& out_min, Vec3d& out_max) const
	{
		if (mNodes.Size == 0) {
			return false;
		}

		out_min = mNodes[0].Min;
		out_max = mNodes[0].Max;

		return true;
	}

	double TraceClosest(const Vec3d& origin, const Vec3d& direction, double max_time) const
	{
		if (mNodes.Size == 0) {
			return scNoHit;
		}

		int32 stack[scMaxStackDepth];
		uint32 stack_size = 0;

		stack[stack_size++] = 0;

		double closest = max_time;

		while (stack_size > 0) {
			const BvhNode& node = mNodes[static_cast<uint32>(stack[--stack_size])];

			if (!HitsBox(node, origin, direction, closest)) {
				continue;
			}

			if (node.Left < 0) {
				for (uint32 i = 0; i < node.Count; i++) {
					double time = scNoHit;

					if (IntersectOccluder(mOccluders[node.First + i], origin, direction, time) && time < closest) {
						closest = time;
					}
				}

				continue;
			}

			if (stack_size + 2 <= scMaxStackDepth) {
				stack[stack_size++] = node.Left;
				stack[stack_size++] = node.Right;
			}
		}

		if (closest >= max_time) {
			return scNoHit;
		}

		return closest;
	}

	bool IsOccluded(const Vec3d& origin, const Vec3d& direction) const
	{
		if (mNodes.Size == 0) {
			return false;
		}

		int32 stack[scMaxStackDepth];
		uint32 stack_size = 0;

		stack[stack_size++] = 0;

		while (stack_size > 0) {
			const BvhNode& node = mNodes[static_cast<uint32>(stack[--stack_size])];

			if (!HitsBox(node, origin, direction)) {
				continue;
			}

			if (node.Left < 0) {
				for (uint32 i = 0; i < node.Count; i++) {
					if (HitsOccluder(mOccluders[node.First + i], origin, direction)) {
						return true;
					}
				}

				continue;
			}

			if (stack_size + 2 <= scMaxStackDepth) {
				stack[stack_size++] = node.Left;
				stack[stack_size++] = node.Right;
			}
		}

		return false;
	}

	bool IsInsideBrush(const Vec3d& point) const
	{
		if (mNodes.Size == 0) {
			return false;
		}

		int32 stack[scMaxStackDepth];
		uint32 stack_size = 0;

		stack[stack_size++] = 0;

		while (stack_size > 0) {
			const BvhNode& node = mNodes[static_cast<uint32>(stack[--stack_size])];

			if (!ContainsPoint(node.Min, node.Max, point)) {
				continue;
			}

			if (node.Left < 0) {
				for (uint32 i = 0; i < node.Count; i++) {
					const Occluder& occluder = mOccluders[node.First + i];

					if (occluder.FirstCorner < 0 && ContainsPoint(occluder.Min, occluder.Max, point) &&
						IsInsideOccluder(occluder, point)) {
						return true;
					}
				}

				continue;
			}

			if (stack_size + 2 <= scMaxStackDepth) {
				stack[stack_size++] = node.Left;
				stack[stack_size++] = node.Right;
			}
		}

		return false;
	}

	bool HitsBackFace(const Vec3d& origin, const Vec3d& direction) const
	{
		if (mNodes.Size == 0) {
			return false;
		}

		int32 stack[scMaxStackDepth];
		uint32 stack_size = 0;

		stack[stack_size++] = 0;

		double closest = scNoHit;
		bool is_back_face = false;

		while (stack_size > 0) {
			const BvhNode& node = mNodes[static_cast<uint32>(stack[--stack_size])];

			if (!HitsBox(node, origin, direction, closest)) {
				continue;
			}

			if (node.Left < 0) {
				for (uint32 i = 0; i < node.Count; i++) {
					const Occluder& occluder = mOccluders[node.First + i];

					double time = scNoHit;

					if (!IntersectOccluder(occluder, origin, direction, time) || time >= closest) {
						continue;
					}

					closest = time;
					is_back_face = occluder.FirstCorner >= 0 && occluder.Facing.Dot(direction) > 0.0;
				}

				continue;
			}

			if (stack_size + 2 <= scMaxStackDepth) {
				stack[stack_size++] = node.Left;
				stack[stack_size++] = node.Right;
			}
		}

		return is_back_face;
	}

	bool IsBuried(const Vec3d& point, const Vec3d& normal, const Vec3d* local_directions, uint32 direction_count) const
	{
		if (IsInsideBrush(point)) {
			return true;
		}

		if (mTriangleCount == 0) {
			return false;
		}

		const Vec3d reference = (std::abs(normal.Y) < 0.9) ? Vec3d(0.0, 1.0, 0.0) : Vec3d(1.0, 0.0, 0.0);
		const Vec3d tangent = reference.Cross(normal).Normalized();
		const Vec3d bitangent = normal.Cross(tangent);

		uint32 back_faces = 0;

		for (uint32 i = 0; i < direction_count; i++) {
			const Vec3d& local = local_directions[i];
			const Vec3d direction = tangent * local.X + bitangent * local.Y + normal * local.Z;

			if (HitsBackFace(point, direction)) {
				back_faces++;
			}
		}

		return back_faces * 2 > direction_count;
	}

private:
	static bool ContainsPoint(const Vec3d& min, const Vec3d& max, const Vec3d& point)
	{
		for (int32 axis = 0; axis < 3; axis++) {
			if (point.mData[axis] < min.mData[axis] || point.mData[axis] > max.mData[axis]) {
				return false;
			}
		}

		return true;
	}

	bool IsInsideOccluder(const Occluder& occluder, const Vec3d& point) const
	{
		for (uint32 i = 0; i < occluder.PlaneCount; i++) {
			const OccluderPlane& plane = mPlanes[occluder.FirstPlane + i];

			if (plane.Normal.Dot(point) - plane.Distance >= 0.0) {
				return false;
			}
		}

		return true;
	}

	static bool HitsBox(const BvhNode& node, const Vec3d& origin, const Vec3d& direction, double max_time = scNoHit)
	{
		double enter = 0.0;
		double exit = max_time;

		for (int32 axis = 0; axis < 3; axis++) {
			const double start = origin.mData[axis];
			const double speed = direction.mData[axis];

			if (std::abs(speed) < scParallelEpsilon) {
				if (start < node.Min.mData[axis] || start > node.Max.mData[axis]) {
					return false;
				}

				continue;
			}

			const double near_time = (node.Min.mData[axis] - start) / speed;
			const double far_time = (node.Max.mData[axis] - start) / speed;

			enter = std::fmax(enter, std::fmin(near_time, far_time));
			exit = std::fmin(exit, std::fmax(near_time, far_time));
		}

		return enter <= exit;
	}

	bool HitsTriangle(const Occluder& occluder, const Vec3d& origin, const Vec3d& direction, double& out_time) const
	{
		const Vec3d& a = mCorners[static_cast<uint32>(occluder.FirstCorner)];
		const Vec3d edge_ab = mCorners[static_cast<uint32>(occluder.FirstCorner) + 1] - a;
		const Vec3d edge_ac = mCorners[static_cast<uint32>(occluder.FirstCorner) + 2] - a;

		const Vec3d p = direction.Cross(edge_ac);
		const double determinant = edge_ab.Dot(p);

		if (std::abs(determinant) < scParallelEpsilon) {
			return false;
		}

		const double inverse = 1.0 / determinant;
		const Vec3d offset = origin - a;
		const double u = offset.Dot(p) * inverse;

		if (u < 0.0 || u > 1.0) {
			return false;
		}

		const Vec3d q = offset.Cross(edge_ab);
		const double v = direction.Dot(q) * inverse;

		if (v < 0.0 || u + v > 1.0) {
			return false;
		}

		out_time = edge_ac.Dot(q) * inverse;

		return out_time > scTriangleMinDistance;
	}

	bool HitsOccluder(const Occluder& occluder, const Vec3d& origin, const Vec3d& direction) const
	{
		double time = scNoHit;

		return IntersectOccluder(occluder, origin, direction, time);
	}

	bool IntersectOccluder(const Occluder& occluder, const Vec3d& origin, const Vec3d& direction,
						   double& out_time) const
	{
		if (occluder.FirstCorner >= 0) {
			return HitsTriangle(occluder, origin, direction, out_time);
		}

		double enter = 0.0;
		double exit = scNoHit;

		for (uint32 i = 0; i < occluder.PlaneCount; i++) {
			const OccluderPlane& plane = mPlanes[occluder.FirstPlane + i];

			const double distance = plane.Normal.Dot(origin) - plane.Distance;
			const double speed = plane.Normal.Dot(direction);

			if (std::abs(speed) < scParallelEpsilon) {
				if (distance > 0.0) {
					return false;
				}

				continue;
			}

			const double time = -distance / speed;

			if (speed < 0.0) {
				enter = std::fmax(enter, time);
			}
			else {
				exit = std::fmin(exit, time);
			}

			if (enter >= exit) {
				return false;
			}
		}

		out_time = enter;

		return true;
	}

	int32 BuildNode(uint32 first, uint32 count)
	{
		const int32 node_index = static_cast<int32>(mNodes.Size);
		mNodes.Emplace();

		Vec3d min = mOccluders[first].Min;
		Vec3d max = mOccluders[first].Max;

		Vec3d center_min = (min + max) * 0.5;
		Vec3d center_max = center_min;

		for (uint32 i = 1; i < count; i++) {
			const Occluder& occluder = mOccluders[first + i];
			const Vec3d center = (occluder.Min + occluder.Max) * 0.5;

			min = MinVec(min, occluder.Min);
			max = MaxVec(max, occluder.Max);
			center_min = MinVec(center_min, center);
			center_max = MaxVec(center_max, center);
		}

		int32 left = -1;
		int32 right = -1;

		if (count > scLeafSize) {
			const Vec3d extent = center_max - center_min;

			int32 axis = 0;

			for (int32 other = 1; other < 3; other++) {
				if (extent.mData[other] > extent.mData[axis]) {
					axis = other;
				}
			}

			Occluder* begin = mOccluders.pData + first;
			const uint32 half = count / 2;

			std::nth_element(
				begin, begin + half, begin + count, [axis](const Occluder& a, const Occluder& b)
				{ return (a.Min.mData[axis] + a.Max.mData[axis]) < (b.Min.mData[axis] + b.Max.mData[axis]); });

			left = BuildNode(first, half);
			right = BuildNode(first + half, count - half);
		}

		BvhNode& node = mNodes[static_cast<uint32>(node_index)];
		node.Min = min;
		node.Max = max;
		node.Left = left;
		node.Right = right;
		node.First = first;
		node.Count = count;

		return node_index;
	}

private:
	Array<OccluderPlane> mPlanes;
	Array<Vec3d> mCorners;
	Array<Occluder> mOccluders;
	Array<BvhNode> mNodes;
	uint32 mTriangleCount = 0;
};

} // namespace fx::mapc
