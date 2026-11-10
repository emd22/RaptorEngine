/*
 * File:        Triangle.hpp
 * Author:      emd22
 * Created:     09/11/2026
 * Description: Triangle struct and helpers.
 */

#pragma once

#include "DVec3.hpp"
#include "FVec3.hpp"
#include "SIMDHelper.hpp"

namespace fx {


template <typename TVectorType>
struct Triangle
{
public:
	constexpr Triangle() = default;
	constexpr Triangle(const TVectorType v0, const TVectorType v1, const TVectorType v2)
	{
		Points[0] = v0;
		Points[1] = v1;
		Points[2] = v2;
	}

	constexpr const Triangle WithOffset(const TVectorType offset) const
	{
		return Triangle(Points[0] - offset, Points[1] - offset, Points[2] - offset);
	}

	constexpr const TVectorType GetEdge(uint32 point_index) const
	{
		return (Points[(point_index + 1) % 3] - Points[point_index]);
	}


	const TVectorType GetNormal() const { return GetEdge(0).Cross(GetEdge(1)); }


	bool OverlapsBox(const TVectorType box_center, double box_half_size) const
	{
		const Triangle tri = this->WithOffset(box_center);

		const TVectorType edges[3] = { GetEdge(0), GetEdge(1), GetEdge(2) };

		const TVectorType axes[3] = { TVectorType { 1.0, 0.0, 0.0 }, TVectorType { 0.0, 1.0, 0.0 },
									  TVectorType { 0.0, 0.0, 1.0 } };

		for (const DVec3 edge : edges) {
			for (const DVec3 box_axis : axes) {
				const DOUBLE4 axis = box_axis.Cross(edge).mIntrin;

				const double p0 = tri.Points[0].Dot(axis);
				const double p1 = tri.Points[1].Dot(axis);
				const double p2 = tri.Points[2].Dot(axis);

				const double radius = box_half_size * simd::HAdd(simd::Abs(simd::RemoveW(axis)));

				if (std::min({ p0, p1, p2 }) > radius || std::max({ p0, p1, p2 }) < -radius) {
					return false;
				}
			}
		}


		{
			const double low = std::min({ tri.Points[0].X, tri.Points[1].X, tri.Points[2].X });
			const double high = std::max({ tri.Points[0].X, tri.Points[1].X, tri.Points[2].X });

			if (low > box_half_size || high < -box_half_size) {
				return false;
			}
		}

		{
			const double low = std::min({ tri.Points[0].Y, tri.Points[1].Y, tri.Points[2].Y });
			const double high = std::max({ tri.Points[0].Y, tri.Points[1].Y, tri.Points[2].Y });

			if (low > box_half_size || high < -box_half_size) {
				return false;
			}
		}

		{
			const double low = std::min({ tri.Points[0].Z, tri.Points[1].Z, tri.Points[2].Z });
			const double high = std::max({ tri.Points[0].Z, tri.Points[1].Z, tri.Points[2].Z });

			if (low > box_half_size || high < -box_half_size) {
				return false;
			}
		}

		const TVectorType normal = edges[0].Cross(edges[1]);
		const double distance = normal.Dot(tri.Points[0]);

		const double radius = box_half_size * simd::HAdd(simd::Abs(simd::RemoveW(normal.mIntrin)));
		// const double radius = box_half_size * (std::abs(normal.X) + std::abs(normal.Y) + std::abs(normal.Z));

		return std::abs(distance) <= radius;
	}

	~Triangle() = default;

public:
	TVectorType Points[3] {};
};

using FTriangle = Triangle<FVec3>;
using DTriangle = Triangle<DVec3>;

} // namespace fx
