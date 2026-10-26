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

static Vec3f TransformPoint(const Mat4f& matrix, const Vec3f point)
{
	const Vec4f result = matrix * Vec4f(point.X, point.Y, point.Z, 1.0f);
	return Vec3f(result.X, result.Y, result.Z);
}

static Vec3f TransformDirection(const Mat4f& matrix, const Vec3f direction)
{
	const Vec4f result = matrix * Vec4f(direction.X, direction.Y, direction.Z, 0.0f);
	return Vec3f(result.X, result.Y, result.Z);
}

static uint32 ClipToPlane(const Vec3f* points, uint32 count, const BrushPlane& plane, Vec3f* out_points)
{
	uint32 out_count = 0;

	for (uint32 i = 0; i < count; i++) {
		const Vec3f current = points[i];
		const Vec3f next = points[(i + 1) % count];

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

static void SplitPolygon(const std::vector<Vec3f>& points, const Vec3f normal, float32 distance,
						 std::vector<Vec3f>& out_front, std::vector<Vec3f>& out_back)
{
	out_front.clear();
	out_back.clear();

	const size_t count = points.size();

	for (size_t i = 0; i < count; i++) {
		const Vec3f current = points[i];
		const Vec3f next = points[(i + 1) % count];

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
			const Vec3f crossing = current + (next - current) * t;

			out_front.push_back(crossing);
			out_back.push_back(crossing);
		}
	}
}

static bool LiesOnPlane(const std::vector<Vec3f>& points, const Vec3f normal, float32 distance)
{
	for (const Vec3f point : points) {
		if (std::fabs(normal.Dot(point) - distance) > scCoplanarTolerance) {
			return false;
		}
	}

	return true;
}

MeshDecalBuilder::MeshDecalBuilder(const Brush& volume, const Mat4f& volume_to_world)
	: mVolume(volume), mWorldToLocal(volume_to_world.AffineInverse())
{
	mLocalMin = volume.GetBoundsMin();
	mLocalMax = volume.GetBoundsMax();

	const Vec3f size = mLocalMax - mLocalMin;

	mUGradient = Vec3f(1.0f / std::max(size.X, scMinExtent), 0.0f, 0.0f);
	mVGradient = Vec3f(0.0f, 0.0f, -1.0f / std::max(size.Z, scMinExtent));

	mMinFacing = std::cos(MathUtil::DegreesToRadians(scMaxAngleDegrees));
}

bool MeshDecalBuilder::OverlapsVolume(const Vec3f min, const Vec3f max) const
{
	return min.X <= mLocalMax.X + scOverlapTolerance && max.X >= mLocalMin.X - scOverlapTolerance &&
		   min.Y <= mLocalMax.Y + scOverlapTolerance && max.Y >= mLocalMin.Y - scOverlapTolerance &&
		   min.Z <= mLocalMax.Z + scOverlapTolerance && max.Z >= mLocalMin.Z - scOverlapTolerance;
}

void MeshDecalBuilder::AddBrush(const Brush& brush, const Mat4f& brush_to_world)
{
	if (!brush.IsValid()) {
		return;
	}

	const Mat4f to_local = brush_to_world * mWorldToLocal;

	Vec3f min(FLT_MAX);
	Vec3f max(-FLT_MAX);

	for (const Vec3f vertex : brush.GetVertices()) {
		const Vec3f point = TransformPoint(to_local, vertex);

		min = Vec3f::Min(min, point);
		max = Vec3f::Max(max, point);
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
		const Vec3f normal = TransformDirection(to_local, plane.Normal).Normalize();
		const Vec3f point = TransformPoint(to_local, plane.Normal * plane.Distance);

		receiver.Planes.push_back(LocalPlane { .Normal = normal, .Distance = normal.Dot(point) });
	}
}

void MeshDecalBuilder::ClipBrushes()
{
	Vec3f points[scMaxClipPoints];
	Vec3f clipped[scMaxClipPoints];

	std::vector<Polygon> fragments;

	for (size_t receiver_index = 0; receiver_index < mBrushes.size(); receiver_index++) {
		const BrushReceiver& receiver = mBrushes[receiver_index];

		for (const Brush::Face& face : receiver.pBrush->Faces) {
			const Vec3f normal = receiver.Planes[face.PlaneIndex].Normal;

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

			Vec3f fragment_min(FLT_MAX);
			Vec3f fragment_max(-FLT_MAX);

			for (uint32 i = 0; i < clipped_count; i++) {
				fragment_min = Vec3f::Min(fragment_min, clipped[i]);
				fragment_max = Vec3f::Max(fragment_max, clipped[i]);
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
										bool occluder_wins_ties, const Vec3f normal)
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

void MeshDecalBuilder::AddTriangles(const Vec3f* positions, const Vec3f* normals, const uint32* indices,
								   uint32 index_count)
{
	Vec3f points[3];

	for (uint32 i = 0; i + 2 < index_count; i += 3) {
		const uint32 a = indices[i];
		const uint32 b = indices[i + 1];
		const uint32 c = indices[i + 2];

		const Vec3f normal = TransformDirection(mWorldToLocal, normals[a] + normals[b] + normals[c]).Normalize();

		if (normal.Y < mMinFacing) {
			continue;
		}

		points[0] = TransformPoint(mWorldToLocal, positions[a]);
		points[1] = TransformPoint(mWorldToLocal, positions[b]);
		points[2] = TransformPoint(mWorldToLocal, positions[c]);

		const Vec3f min = Vec3f::Min(points[0], Vec3f::Min(points[1], points[2]));
		const Vec3f max = Vec3f::Max(points[0], Vec3f::Max(points[1], points[2]));

		if (!OverlapsVolume(min, max)) {
			continue;
		}

		Vec3f clipped[scMaxClipPoints];
		const uint32 clipped_count = ClipToVolume(points, 3, clipped);

		if (clipped_count >= 3) {
			EmitPolygon(clipped, clipped_count, normal);
		}
	}
}

uint32 MeshDecalBuilder::ClipToVolume(const Vec3f* points, uint32 count, Vec3f* out_points) const
{
	Vec3f buffer_a[scMaxClipPoints];
	Vec3f buffer_b[scMaxClipPoints];

	for (uint32 i = 0; i < count; i++) {
		buffer_a[i] = points[i];
	}

	Vec3f* current = buffer_a;
	Vec3f* next = buffer_b;

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

void MeshDecalBuilder::EmitPolygon(const Vec3f* points, uint32 count, const Vec3f normal)
{
	const Vec3f u_gradient = mUGradient - normal * normal.Dot(mUGradient);
	const Vec3f v_gradient = mVGradient - normal * normal.Dot(mVGradient);

	const float32 uu = u_gradient.Dot(u_gradient);
	const float32 uv = u_gradient.Dot(v_gradient);
	const float32 vv = v_gradient.Dot(v_gradient);
	const float32 determinant = uu * vv - uv * uv;

	if (determinant <= FLT_EPSILON) {
		return;
	}

	Vec3f tangent = (u_gradient * vv - v_gradient * uv) * (1.0f / determinant);
	tangent.NormalizeIP();

	const Vec3f v_direction = (v_gradient * uu - u_gradient * uv) * (1.0f / determinant);
	const float32 handedness = (normal.Cross(tangent).Dot(v_direction) > 0.0f) ? -1.0f : 1.0f;

	const Vec3f offset = normal * scSurfaceOffset;
	const uint32 base = mPositions.Size;

	for (uint32 i = 0; i < count; i++) {
		const Vec3f point = points[i];

		mPositions.Insert(point + offset);
		mNormals.Insert(normal);
		mTangents.Insert(tangent);
		mHandedness.Insert(handedness);
		mTexcoords.Insert(Vec2f(point.Dot(mUGradient) - mLocalMin.Dot(mUGradient),
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
	const Vec3f center = volume.GetCenter();

	out_mesh.Positions.InitCapacity(3);
	out_mesh.Normals.InitCapacity(3);
	out_mesh.Tangents.InitCapacity(3);
	out_mesh.TangentHandedness.InitCapacity(3);
	out_mesh.Texcoords.InitCapacity(3);
	out_mesh.Indices.InitCapacity(3);

	for (uint32 i = 0; i < 3; i++) {
		out_mesh.Positions.Insert(center);
		out_mesh.Normals.Insert(Vec3f(0.0f, 1.0f, 0.0f));
		out_mesh.Tangents.Insert(Vec3f(1.0f, 0.0f, 0.0f));
		out_mesh.TangentHandedness.Insert(1.0f);
		out_mesh.Texcoords.Insert(Vec2f(0.0f, 0.0f));
		out_mesh.Indices.Insert(i);
	}
}

} // namespace fx
