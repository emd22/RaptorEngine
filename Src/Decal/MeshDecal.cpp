#include "MeshDecal.hpp"

#include <Math/MathUtil.hpp>
#include <algorithm>
#include <cfloat>
#include <cmath>

namespace fx {

static constexpr uint32 scMaxClipPoints = 128;
static constexpr float32 scOverlapTolerance = 0.001f;
static constexpr float32 scMinExtent = 0.0001f;
static constexpr float32 scCoplanarTolerance = 0.001f;
static constexpr float32 scCoplanarDot = 0.999f;

static FVec3 TransformPoint(const FMat4& matrix, const FVec3 point)
{
	const FVec4 result = matrix * FVec4(point.X, point.Y, point.Z, 1.0f);
	return FVec3(result.X, result.Y, result.Z);
}

static FVec3 TransformDirection(const FMat4& matrix, const FVec3 direction)
{
	const FVec4 result = matrix * FVec4(direction.X, direction.Y, direction.Z, 0.0f);
	return FVec3(result.X, result.Y, result.Z);
}

static uint32 ClipToPlane(const FVec3* points, uint32 count, const BrushPlane& plane, FVec3* out_points)
{
	uint32 out_count = 0;

	for (uint32 i = 0; i < count; i++) {
		const FVec3 current = points[i];
		const FVec3 next = points[(i + 1) % count];

		const float32 current_side = plane.Normal.Dot(current) - plane.Distance;
		const float32 next_side = plane.Normal.Dot(next) - plane.Distance;

		if (current_side <= 0.0f && out_count < scMaxClipPoints) {
			out_points[out_count++] = current;
		}

		if ((current_side <= 0.0f) != (next_side <= 0.0f) && out_count < scMaxClipPoints) {
			const float32 t = current_side / (current_side - next_side);
			out_points[out_count++] = current + (next - current) * t;
		}
	}

	return out_count;
}

static void SplitPolygon(const std::vector<FVec3>& points, const FVec3 normal, float32 distance,
						 std::vector<FVec3>& out_front, std::vector<FVec3>& out_back)
{
	out_front.clear();
	out_back.clear();

	const size_t count = points.size();

	for (size_t i = 0; i < count; i++) {
		const FVec3 current = points[i];
		const FVec3 next = points[(i + 1) % count];

		const float32 current_side = normal.Dot(current) - distance;
		const float32 next_side = normal.Dot(next) - distance;

		if (current_side > 0.0f) {
			out_front.push_back(current);
		}
		else {
			out_back.push_back(current);
		}

		if ((current_side > 0.0f) != (next_side > 0.0f)) {
			const float32 t = current_side / (current_side - next_side);
			const FVec3 crossing = current + (next - current) * t;

			out_front.push_back(crossing);
			out_back.push_back(crossing);
		}
	}
}

static bool LiesOnPlane(const std::vector<FVec3>& points, const FVec3 normal, float32 distance)
{
	for (const FVec3 point : points) {
		if (std::fabs(normal.Dot(point) - distance) > scCoplanarTolerance) {
			return false;
		}
	}

	return true;
}

MeshDecalBuilder::MeshDecalBuilder(const Brush& volume, const FMat4& volume_to_world)
	: mVolume(volume), mWorldToLocal(volume_to_world.AffineInverse())
{
	mLocalMin = volume.GetBoundsMin();
	mLocalMax = volume.GetBoundsMax();

	const FVec3 size = mLocalMax - mLocalMin;

	mUGradient = FVec3(1.0f / std::max(size.X, scMinExtent), 0.0f, 0.0f);
	mVGradient = FVec3(0.0f, 0.0f, -1.0f / std::max(size.Z, scMinExtent));

	mMinFacing = std::cos(MathUtil::DegreesToRadians(scMaxAngleDegrees));
}

bool MeshDecalBuilder::OverlapsVolume(const FVec3 min, const FVec3 max) const
{
	return min.X <= mLocalMax.X + scOverlapTolerance && max.X >= mLocalMin.X - scOverlapTolerance &&
		   min.Y <= mLocalMax.Y + scOverlapTolerance && max.Y >= mLocalMin.Y - scOverlapTolerance &&
		   min.Z <= mLocalMax.Z + scOverlapTolerance && max.Z >= mLocalMin.Z - scOverlapTolerance;
}

void MeshDecalBuilder::AddBrush(const Brush& brush, const FMat4& brush_to_world)
{
	if (!brush.IsValid()) {
		return;
	}

	const FMat4 to_local = brush_to_world * mWorldToLocal;

	FVec3 min(FLT_MAX);
	FVec3 max(-FLT_MAX);

	for (const FVec3 vertex : brush.GetVertices()) {
		const FVec3 point = TransformPoint(to_local, vertex);

		min = FVec3::Min(min, point);
		max = FVec3::Max(max, point);
	}

	if (!OverlapsVolume(min, max)) {
		return;
	}

	BrushReceiver& receiver = mBrushes.emplace_back();
	receiver.pBrush = &brush;
	receiver.ToLocal = to_local;
	receiver.Min = min;
	receiver.Max = max;
	receiver.Planes.reserve(brush.Planes.Size);

	for (const BrushPlane& plane : brush.Planes) {
		const FVec3 normal = TransformDirection(to_local, plane.Normal).Normalize();
		const FVec3 point = TransformPoint(to_local, plane.Normal * plane.Distance);

		receiver.Planes.push_back(LocalPlane { .Normal = normal, .Distance = normal.Dot(point) });
	}
}

void MeshDecalBuilder::ClipBrushes()
{
	FVec3 points[scMaxClipPoints];
	FVec3 clipped[scMaxClipPoints];

	std::vector<Polygon> fragments;

	for (size_t receiver_index = 0; receiver_index < mBrushes.size(); receiver_index++) {
		const BrushReceiver& receiver = mBrushes[receiver_index];

		for (const Brush::Face& face : receiver.pBrush->Faces) {
			const FVec3 normal = receiver.Planes[face.PlaneIndex].Normal;

			if (normal.Y < mMinFacing) {
				continue;
			}

			const uint32 count = std::min<uint32>(static_cast<uint32>(face.Vertices.Size), scMaxClipPoints);

			for (uint32 i = 0; i < count; i++) {
				points[i] = TransformPoint(receiver.ToLocal, face.Vertices[i]);
			}

			const uint32 clipped_count = ClipToVolume(points, count, clipped);

			if (clipped_count < 3) {
				continue;
			}

			fragments.clear();
			fragments.emplace_back(clipped, clipped + clipped_count);

			FVec3 fragment_min(FLT_MAX);
			FVec3 fragment_max(-FLT_MAX);

			for (uint32 i = 0; i < clipped_count; i++) {
				fragment_min = FVec3::Min(fragment_min, clipped[i]);
				fragment_max = FVec3::Max(fragment_max, clipped[i]);
			}

			for (size_t occluder_index = 0; occluder_index < mBrushes.size() && !fragments.empty(); occluder_index++) {
				const BrushReceiver& occluder = mBrushes[occluder_index];

				if (occluder_index == receiver_index) {
					continue;
				}

				const bool overlaps = fragment_min.X <= occluder.Max.X + scCoplanarTolerance &&
									  fragment_max.X >= occluder.Min.X - scCoplanarTolerance &&
									  fragment_min.Y <= occluder.Max.Y + scCoplanarTolerance &&
									  fragment_max.Y >= occluder.Min.Y - scCoplanarTolerance &&
									  fragment_min.Z <= occluder.Max.Z + scCoplanarTolerance &&
									  fragment_max.Z >= occluder.Min.Z - scCoplanarTolerance;

				if (overlaps) {
					SubtractReceiver(fragments, occluder, occluder_index < receiver_index, normal);
				}
			}

			for (const Polygon& fragment : fragments) {
				EmitPolygon(fragment.data(), static_cast<uint32>(std::min<size_t>(fragment.size(), scMaxClipPoints)),
							normal);
			}
		}
	}

	mBrushes.clear();
}

void MeshDecalBuilder::SubtractReceiver(std::vector<Polygon>& fragments, const BrushReceiver& occluder,
										bool occluder_wins_ties, const FVec3 normal)
{
	std::vector<Polygon> kept;
	Polygon remaining;
	Polygon front;
	Polygon back;
	std::vector<Polygon> outside;

	for (const Polygon& fragment : fragments) {
		remaining = fragment;
		outside.clear();

		for (const LocalPlane& plane : occluder.Planes) {
			const float32 facing = plane.Normal.Dot(normal);

			float32 bias = 0.0f;

			if (std::fabs(facing) > scCoplanarDot && LiesOnPlane(remaining, plane.Normal, plane.Distance)) {
				bias = scCoplanarTolerance;

				if (facing > 0.0f && !occluder_wins_ties) {
					bias = -scCoplanarTolerance;
				}
			}

			SplitPolygon(remaining, plane.Normal, plane.Distance + bias, front, back);

			if (front.size() >= 3) {
				outside.push_back(front);
			}

			remaining.swap(back);

			if (remaining.size() < 3) {
				break;
			}
		}

		if (remaining.size() < 3) {
			kept.push_back(fragment);
			continue;
		}

		for (Polygon& piece : outside) {
			kept.push_back(std::move(piece));
		}
	}

	fragments.swap(kept);
}

void MeshDecalBuilder::AddTriangles(const FVec3* positions, const FVec3* normals, const uint32* indices,
									uint32 index_count)
{
	FVec3 points[3];

	for (uint32 i = 0; i + 2 < index_count; i += 3) {
		const uint32 a = indices[i];
		const uint32 b = indices[i + 1];
		const uint32 c = indices[i + 2];

		const FVec3 normal = TransformDirection(mWorldToLocal, normals[a] + normals[b] + normals[c]).Normalize();

		if (normal.Y < mMinFacing) {
			continue;
		}

		points[0] = TransformPoint(mWorldToLocal, positions[a]);
		points[1] = TransformPoint(mWorldToLocal, positions[b]);
		points[2] = TransformPoint(mWorldToLocal, positions[c]);

		const FVec3 min = FVec3::Min(points[0], FVec3::Min(points[1], points[2]));
		const FVec3 max = FVec3::Max(points[0], FVec3::Max(points[1], points[2]));

		if (!OverlapsVolume(min, max)) {
			continue;
		}

		FVec3 clipped[scMaxClipPoints];
		const uint32 clipped_count = ClipToVolume(points, 3, clipped);

		if (clipped_count >= 3) {
			EmitPolygon(clipped, clipped_count, normal);
		}
	}
}

uint32 MeshDecalBuilder::ClipToVolume(const FVec3* points, uint32 count, FVec3* out_points) const
{
	FVec3 buffer_a[scMaxClipPoints];
	FVec3 buffer_b[scMaxClipPoints];

	for (uint32 i = 0; i < count; i++) {
		buffer_a[i] = points[i];
	}

	FVec3* current = buffer_a;
	FVec3* next = buffer_b;

	for (const BrushPlane& plane : mVolume.Planes) {
		count = ClipToPlane(current, count, plane, next);

		if (count < 3) {
			return 0;
		}

		std::swap(current, next);
	}

	for (uint32 i = 0; i < count; i++) {
		out_points[i] = current[i];
	}

	return count;
}

void MeshDecalBuilder::EmitPolygon(const FVec3* points, uint32 count, const FVec3 normal)
{
	const FVec3 u_gradient = mUGradient - normal * normal.Dot(mUGradient);
	const FVec3 v_gradient = mVGradient - normal * normal.Dot(mVGradient);

	const float32 uu = u_gradient.Dot(u_gradient);
	const float32 uv = u_gradient.Dot(v_gradient);
	const float32 vv = v_gradient.Dot(v_gradient);
	const float32 determinant = uu * vv - uv * uv;

	if (determinant <= FLT_EPSILON) {
		return;
	}

	FVec3 tangent = (u_gradient * vv - v_gradient * uv) * (1.0f / determinant);
	tangent.NormalizeIP();

	const FVec3 v_direction = (v_gradient * uu - u_gradient * uv) * (1.0f / determinant);
	const float32 handedness = (normal.Cross(tangent).Dot(v_direction) > 0.0f) ? -1.0f : 1.0f;

	const FVec3 offset = normal * scSurfaceOffset;
	const uint32 base = mPositions.Size;

	for (uint32 i = 0; i < count; i++) {
		const FVec3 point = points[i];

		mPositions.Insert(point + offset);
		mNormals.Insert(normal);
		mTangents.Insert(tangent);
		mHandedness.Insert(handedness);
		mTexcoords.Insert(FVec2(point.Dot(mUGradient) - mLocalMin.Dot(mUGradient),
								point.Dot(mVGradient) - mLocalMax.Dot(mVGradient)));
	}

	for (uint32 i = 1; i + 1 < count; i++) {
		mIndices.Insert(base);
		mIndices.Insert(base + i);
		mIndices.Insert(base + i + 1);
	}
}

template <typename TElementType, typename TSource>
static void CopyInto(SizedArray<TElementType>& destination, const TSource& source)
{
	destination.InitCapacity(source.Size);

	for (uint32 i = 0; i < source.Size; i++) {
		destination.Insert(source[i]);
	}
}

void MeshDecalBuilder::Build(MeshGen::GeneratedMesh& out_mesh) const
{
	CopyInto(out_mesh.Positions, mPositions);
	CopyInto(out_mesh.Normals, mNormals);
	CopyInto(out_mesh.Tangents, mTangents);
	CopyInto(out_mesh.TangentHandedness, mHandedness);
	CopyInto(out_mesh.Texcoords, mTexcoords);
	CopyInto(out_mesh.Indices, mIndices);
}

void MeshDecalBuilder::BuildPlaceholder(const Brush& volume, MeshGen::GeneratedMesh& out_mesh)
{
	const FVec3 center = volume.GetCenter();

	out_mesh.Positions.InitCapacity(3);
	out_mesh.Normals.InitCapacity(3);
	out_mesh.Tangents.InitCapacity(3);
	out_mesh.TangentHandedness.InitCapacity(3);
	out_mesh.Texcoords.InitCapacity(3);
	out_mesh.Indices.InitCapacity(3);

	for (uint32 i = 0; i < 3; i++) {
		out_mesh.Positions.Insert(center);
		out_mesh.Normals.Insert(FVec3(0.0f, 1.0f, 0.0f));
		out_mesh.Tangents.Insert(FVec3(1.0f, 0.0f, 0.0f));
		out_mesh.TangentHandedness.Insert(1.0f);
		out_mesh.Texcoords.Insert(FVec2(0.0f, 0.0f));
		out_mesh.Indices.Insert(i);
	}
}

} // namespace fx
