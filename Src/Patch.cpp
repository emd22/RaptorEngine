#include "Patch.hpp"

#include <Core/Slice.hpp>
#include <algorithm>
#include <cmath>

namespace fx {

namespace {

constexpr double scDegenerateCross = 1e-12;
constexpr double scDegenerateTexture = 1e-12;
constexpr double scDegenerateLength = 1e-12;
constexpr double scNudges[] = { 1e-4, 1e-3, 1e-2, 5e-2 };
constexpr uint32 scArcLengthSamples = 16;

struct SurfaceSample
{
	double Color[4] = { 1.0, 1.0, 1.0, 1.0 };
	Vec3d Position = Vec3d(0.0);
	Vec3d DerivU = Vec3d(0.0);
	Vec3d DerivV = Vec3d(0.0);
	Vec2d Texcoord = Vec2d(0.0, 0.0);
	Vec2d TexcoordDerivU = Vec2d(0.0, 0.0);
	Vec2d TexcoordDerivV = Vec2d(0.0, 0.0);
};

Vec3d ToDouble(const Vec3f value) { return Vec3d(value.X, value.Y, value.Z); }

Vec2d ToDouble(const Vec2f value) { return Vec2d(value.X, value.Y); }

Vec3f ToFloat(const Vec3d& value)
{
	return Vec3f(static_cast<float32>(value.X), static_cast<float32>(value.Y), static_cast<float32>(value.Z));
}

PatchColor MixColors(const PatchColor& a, float32 weight_a, const PatchColor& b, float32 weight_b)
{
	return PatchColor {
		.R = a.R * weight_a + b.R * weight_b,
		.G = a.G * weight_a + b.G * weight_b,
		.B = a.B * weight_a + b.B * weight_b,
		.A = a.A * weight_a + b.A * weight_b,
	};
}

PatchColor MakeColor(const double values[4])
{
	return PatchColor {
		.R = static_cast<float32>(values[0]),
		.G = static_cast<float32>(values[1]),
		.B = static_cast<float32>(values[2]),
		.A = static_cast<float32>(values[3]),
	};
}

void AddColor(double sum[4], const PatchColor& color, double weight)
{
	sum[0] += static_cast<double>(color.R) * weight;
	sum[1] += static_cast<double>(color.G) * weight;
	sum[2] += static_cast<double>(color.B) * weight;
	sum[3] += static_cast<double>(color.A) * weight;
}

void GetBasis(double t, double out_basis[3], double out_derivative[3])
{
	const double s = 1.0 - t;

	out_basis[0] = s * s;
	out_basis[1] = 2.0 * t * s;
	out_basis[2] = t * t;

	out_derivative[0] = -2.0 * s;
	out_derivative[1] = 2.0 * (s - t);
	out_derivative[2] = 2.0 * t;
}

Vec3f MakeAxisPoint(uint32 axis, float32 along, float32 a, float32 b)
{
	float32 values[3];
	values[axis % 3] = along;
	values[(axis + 1) % 3] = a;
	values[(axis + 2) % 3] = b;

	return Vec3f(values[0], values[1], values[2]);
}

float32 GetAxis(const Vec3f value, uint32 axis)
{
	if (axis == 0) {
		return value.X;
	}

	if (axis == 1) {
		return value.Y;
	}

	return value.Z;
}

struct SectionPoint
{
	float32 A = 0.0f;
	float32 B = 0.0f;
	float32 Weight = 1.0f;
};

constexpr float32 scArc = Patch::scQuarterArcWeight;

constexpr SectionPoint scRing[] = {
	{ 1.0f, 0.5f },		   { 1.0f, 1.0f, scArc }, { 0.5f, 1.0f },		 { 0.0f, 1.0f, scArc }, { 0.0f, 0.5f },
	{ 0.0f, 0.0f, scArc }, { 0.5f, 0.0f },		  { 1.0f, 0.0f, scArc }, { 1.0f, 0.5f },
};

Vec3f GetBoxPoint(const Vec3f min, const Vec3f max, uint32 axis, float32 along, float32 a, float32 b)
{
	axis = axis % 3;

	const auto lerp = [&](uint32 index, float32 t)
	{ return GetAxis(min, index) + (GetAxis(max, index) - GetAxis(min, index)) * t; };

	return MakeAxisPoint(axis, lerp(axis, along), lerp((axis + 1) % 3, a), lerp((axis + 2) % 3, b));
}

Patch MakeExtrusion(const Vec3f min, const Vec3f max, uint32 axis, const Slice<const SectionPoint> section,
					bool collapse_top)
{
	axis = axis % 3;

	const uint32 axis_a = (axis + 1) % 3;
	const uint32 axis_b = (axis + 2) % 3;

	const float32 min_a = GetAxis(min, axis_a);
	const float32 max_a = GetAxis(max, axis_a);
	const float32 min_b = GetAxis(min, axis_b);
	const float32 max_b = GetAxis(max, axis_b);

	const float32 center_a = (min_a + max_a) * 0.5f;
	const float32 center_b = (min_b + max_b) * 0.5f;

	const float32 bottom = GetAxis(min, axis);
	const float32 top = GetAxis(max, axis);

	Patch patch = Patch::FromGrid(section.Size, 3);

	for (uint32 row = 0; row < 3; row++) {
		const float32 along = top + (bottom - top) * (static_cast<float32>(row) * 0.5f);
		float32 shrink = 1.0f;

		if (collapse_top) {
			shrink = static_cast<float32>(row) * 0.5f;
		}

		for (uint32 column = 0; column < section.Size; column++) {
			const float32 a = min_a + (max_a - min_a) * section[column].A;
			const float32 b = min_b + (max_b - min_b) * section[column].B;

			PatchPoint& point = patch.GetPoint(column, row);

			point.Position = MakeAxisPoint(axis, along, center_a + (a - center_a) * shrink,
										   center_b + (b - center_b) * shrink);
			point.Weight = section[column].Weight;
		}
	}

	return patch;
}

void OrientAwayFrom(Patch& patch, const Vec3d& inside)
{
	const PatchVertex vertex = patch.Evaluate(static_cast<double>(patch.GetSubPatchColumns()) * 0.5,
											  static_cast<double>(patch.GetSubPatchRows()) * 0.5);

	if (vertex.Normal.Dot(vertex.Position - inside) < 0.0) {
		patch.Invert();
	}
}

double GetCurveLength(const PatchPoint& a, const PatchPoint& b, const PatchPoint& c)
{
	const Vec3d position_a = ToDouble(a.Position);
	const Vec3d position_b = ToDouble(b.Position);
	const Vec3d position_c = ToDouble(c.Position);

	double length = 0.0;
	Vec3d previous = position_a;

	for (uint32 i = 1; i <= scArcLengthSamples; i++) {
		const double t = static_cast<double>(i) / static_cast<double>(scArcLengthSamples);
		const double s = 1.0 - t;

		const double weight_a = s * s * a.Weight;
		const double weight_b = 2.0 * t * s * b.Weight;
		const double weight_c = t * t * c.Weight;

		const Vec3d point = (position_a * weight_a + position_b * weight_b + position_c * weight_c) *
							(1.0 / (weight_a + weight_b + weight_c));

		length += (point - previous).Length();
		previous = point;
	}

	return length;
}

void SplitCurve(const PatchPoint& a, const PatchPoint& b, const PatchPoint& c, PatchPoint out[5])
{
	const auto mix = [](const PatchPoint& x, const PatchPoint& y)
	{
		PatchPoint point;
		point.Weight = (x.Weight + y.Weight) * 0.5f;

		const float32 scale = 0.5f / point.Weight;

		point.Position = (x.Position * x.Weight + y.Position * y.Weight) * scale;
		point.Texcoord = (x.Texcoord * x.Weight + y.Texcoord * y.Weight) * scale;
		point.Color = MixColors(x.Color, x.Weight * scale, y.Color, y.Weight * scale);
		return point;
	};

	const PatchPoint left = mix(a, b);
	const PatchPoint right = mix(b, c);

	out[0] = a;
	out[1] = left;
	out[2] = mix(left, right);
	out[3] = right;
	out[4] = c;
}

PatchPoint JoinCurves(const PatchPoint& a, const PatchPoint& left, const PatchPoint& right, const PatchPoint& c)
{
	PatchPoint point;
	point.Weight = (left.Weight * 2.0f - a.Weight + right.Weight * 2.0f - c.Weight) * 0.5f;

	if (point.Weight < Patch::scMinWeight) {
		point.Weight = 1.0f;
		point.Position = (left.Position * 2.0f - a.Position + right.Position * 2.0f - c.Position) * 0.5f;
		point.Texcoord = (left.Texcoord * 2.0f - a.Texcoord + right.Texcoord * 2.0f - c.Texcoord) * 0.5f;
		point.Color = MixColors(MixColors(left.Color, 1.0f, a.Color, -0.5f), 1.0f,
								MixColors(right.Color, 1.0f, c.Color, -0.5f), 1.0f);
		return point;
	}

	const float32 scale = 0.5f / point.Weight;

	point.Position = (left.Position * (left.Weight * 2.0f) - a.Position * a.Weight +
					  right.Position * (right.Weight * 2.0f) - c.Position * c.Weight) *
					 scale;
	point.Texcoord = (left.Texcoord * (left.Weight * 2.0f) - a.Texcoord * a.Weight +
					  right.Texcoord * (right.Weight * 2.0f) - c.Texcoord * c.Weight) *
					 scale;
	point.Color = MixColors(MixColors(left.Color, left.Weight * 2.0f * scale, a.Color, -a.Weight * scale), 1.0f,
							MixColors(right.Color, right.Weight * 2.0f * scale, c.Color, -c.Weight * scale), 1.0f);
	return point;
}

} // namespace

Patch Patch::FromGrid(uint32 width, uint32 height)
{
	Patch patch;

	if (!IsValidSize(width) || !IsValidSize(height)) {
		return patch;
	}

	patch.Width = width;
	patch.Height = height;
	patch.Points.InitSize(width * height);

	return patch;
}

Patch Patch::FromDetailMesh(uint32 width, uint32 height)
{
	Patch patch;

	if (!IsValidDetailMeshSize(width) || !IsValidDetailMeshSize(height)) {
		return patch;
	}

	patch.Width = width;
	patch.Height = height;
	patch.bIsDetailMesh = true;
	patch.SubdivisionsU = 1;
	patch.SubdivisionsV = 1;
	patch.Points.InitSize(width * height);
	patch.TurnedCells.InitCapacity((width - 1) * (height - 1));

	for (uint32 cell = 0; cell < (width - 1) * (height - 1); cell++) {
		patch.TurnedCells.Insert(static_cast<uint8>(0));
	}

	return patch;
}

Patch Patch::MakeFlat(const Vec3f min, const Vec3f max, uint32 axis)
{
	axis = axis % 3;

	const uint32 axis_a = (axis + 1) % 3;
	const uint32 axis_b = (axis + 2) % 3;

	const float32 height = GetAxis(min, axis);

	Patch patch = FromGrid(3, 3);

	for (uint32 row = 0; row < 3; row++) {
		const float32 b = GetAxis(max, axis_b) + (GetAxis(min, axis_b) - GetAxis(max, axis_b)) * row * 0.5f;

		for (uint32 column = 0; column < 3; column++) {
			const float32 a = GetAxis(min, axis_a) + (GetAxis(max, axis_a) - GetAxis(min, axis_a)) * column * 0.5f;

			patch.GetPoint(column, row).Position = MakeAxisPoint(axis, height, a, b);
		}
	}

	OrientAwayFrom(patch,
				   ToDouble(GetBoxPoint(min, max, axis, 0.0f, 0.5f, 0.5f) - MakeAxisPoint(axis, 1.0f, 0.0f, 0.0f)));
	patch.NaturalTexture();

	return patch;
}

Patch Patch::MakeCylinder(const Vec3f min, const Vec3f max, uint32 axis)
{
	Patch patch = MakeExtrusion(min, max, axis, MakeSlice(scRing), false);

	const Vec3d center = ToDouble(GetBoxPoint(min, max, axis, 0.5f, 0.5f, 0.5f));
	const Vec3d direction = ToDouble(MakeAxisPoint(axis % 3, 1.0f, 0.0f, 0.0f));
	const Vec3d sample = patch.Evaluate(0.5, 0.5).Position;

	OrientAwayFrom(patch, center + direction * direction.Dot(sample - center));
	patch.NaturalTexture();

	return patch;
}

Patch Patch::MakeCone(const Vec3f min, const Vec3f max, uint32 axis)
{
	Patch patch = MakeExtrusion(min, max, axis, MakeSlice(scRing), true);

	OrientAwayFrom(patch, ToDouble(GetBoxPoint(min, max, axis, 0.0f, 0.5f, 0.5f)));
	patch.NaturalTexture();

	return patch;
}

Patch Patch::MakeBevel(const Vec3f min, const Vec3f max, uint32 axis)
{
	static constexpr SectionPoint scSection[] = { { 0.0f, 1.0f }, { 1.0f, 1.0f }, { 1.0f, 0.0f } };

	Patch patch = MakeExtrusion(min, max, axis, MakeSlice(scSection), false);

	OrientAwayFrom(patch, ToDouble(GetBoxPoint(min, max, axis, 0.5f, 0.0f, 0.0f)));
	patch.NaturalTexture();

	return patch;
}

Patch Patch::MakeEndcap(const Vec3f min, const Vec3f max, uint32 axis)
{
	static constexpr SectionPoint scSection[] = {
		{ 0.0f, 0.0f }, { 0.0f, 1.0f, scArc }, { 0.5f, 1.0f }, { 1.0f, 1.0f, scArc }, { 1.0f, 0.0f },
	};

	Patch patch = MakeExtrusion(min, max, axis, MakeSlice(scSection), false);

	OrientAwayFrom(patch, ToDouble(GetBoxPoint(min, max, axis, 0.5f, 0.5f, 0.0f)));
	patch.NaturalTexture();

	return patch;
}

Patch Patch::MakeDetailMesh(const Vec3f min, const Vec3f max, uint32 axis, uint32 cells_u, uint32 cells_v)
{
	axis = axis % 3;
	cells_u = std::clamp(cells_u, scMinDetailMeshSize - 1, scMaxDetailMeshSize - 1);
	cells_v = std::clamp(cells_v, scMinDetailMeshSize - 1, scMaxDetailMeshSize - 1);

	const uint32 axis_a = (axis + 1) % 3;
	const uint32 axis_b = (axis + 2) % 3;

	const float32 height = GetAxis(min, axis);

	Patch patch = FromDetailMesh(cells_u + 1, cells_v + 1);

	for (uint32 row = 0; row <= cells_v; row++) {
		const float32 t = static_cast<float32>(row) / static_cast<float32>(cells_v);
		const float32 b = GetAxis(max, axis_b) + (GetAxis(min, axis_b) - GetAxis(max, axis_b)) * t;

		for (uint32 column = 0; column <= cells_u; column++) {
			const float32 s = static_cast<float32>(column) / static_cast<float32>(cells_u);
			const float32 a = GetAxis(min, axis_a) + (GetAxis(max, axis_a) - GetAxis(min, axis_a)) * s;

			patch.GetPoint(column, row).Position = MakeAxisPoint(axis, height, a, b);
		}
	}

	OrientAwayFrom(patch,
				   ToDouble(GetBoxPoint(min, max, axis, 0.0f, 0.5f, 0.5f) - MakeAxisPoint(axis, 1.0f, 0.0f, 0.0f)));
	std::fill(patch.TurnedCells.begin(), patch.TurnedCells.end(), static_cast<uint8>(0));
	patch.WorldTexture();

	return patch;
}

Patch Patch::MakePrimitive(ePatchPrimitive primitive, const Vec3f min, const Vec3f max, uint32 axis)
{
	switch (primitive) {
	case ePatchPrimitive::Flat:
		return MakeFlat(min, max, axis);
	case ePatchPrimitive::Cylinder:
		return MakeCylinder(min, max, axis);
	case ePatchPrimitive::Cone:
		return MakeCone(min, max, axis);
	case ePatchPrimitive::Bevel:
		return MakeBevel(min, max, axis);
	case ePatchPrimitive::Endcap:
		return MakeEndcap(min, max, axis);
	case ePatchPrimitive::DetailMesh:
		return MakeDetailMesh(min, max, axis);
	default:
		return {};
	}
}

Patch Patch::Clone() const
{
	Patch patch;
	patch.Width = Width;
	patch.Height = Height;
	patch.Points = SizedArray<PatchPoint>::Clone(Points);
	patch.bIsDetailMesh = bIsDetailMesh;
	patch.TurnedCells = SizedArray<uint8>::Clone(TurnedCells);
	patch.SubdivisionsU = SubdivisionsU;
	patch.SubdivisionsV = SubdivisionsV;
	patch.bSmooth = bSmooth;
	patch.SmoothingGroup = SmoothingGroup;
	patch.Closure = Closure;
	patch.Thickness = Thickness;

	return patch;
}

bool Patch::IsValid() const
{
	if (bIsDetailMesh) {
		return IsValidDetailMeshSize(Width) && IsValidDetailMeshSize(Height) && Points.Size == Width * Height &&
			   TurnedCells.Size == (Width - 1) * (Height - 1);
	}

	return IsValidSize(Width) && IsValidSize(Height) && Points.Size == Width * Height &&
		   SubdivisionsU >= scMinSubdivisions && SubdivisionsU <= scMaxSubdivisions &&
		   SubdivisionsV >= scMinSubdivisions && SubdivisionsV <= scMaxSubdivisions;
}

bool Patch::IsCellTurned(uint32 column, uint32 row) const
{
	const size_t index = static_cast<size_t>(row) * (Width - 1) + column;

	if (!bIsDetailMesh || column + 1 >= Width || row + 1 >= Height || index >= TurnedCells.Size) {
		return false;
	}

	return TurnedCells[index] != 0;
}

void Patch::TurnCell(uint32 column, uint32 row)
{
	const size_t index = static_cast<size_t>(row) * (Width - 1) + column;

	if (!bIsDetailMesh || column + 1 >= Width || row + 1 >= Height || index >= TurnedCells.Size) {
		return;
	}

	TurnedCells[index] = static_cast<uint8>(TurnedCells[index] == 0);
}

Vec3f Patch::GetBoundsMin() const
{
	if (Points.IsEmpty()) {
		return Vec3f::sZero;
	}

	Vec3f result = Points[0].Position;

	for (const PatchPoint& point : Points) {
		result = Vec3f::Min(result, point.Position);
	}

	return result;
}

Vec3f Patch::GetBoundsMax() const
{
	if (Points.IsEmpty()) {
		return Vec3f::sZero;
	}

	Vec3f result = Points[0].Position;

	for (const PatchPoint& point : Points) {
		result = Vec3f::Max(result, point.Position);
	}

	return result;
}

namespace {

SurfaceSample SampleSubPatch(const Patch& patch, uint32 sub_column, uint32 sub_row, double u, double v)
{
	double basis_u[3];
	double basis_v[3];
	double derivative_u[3];
	double derivative_v[3];

	GetBasis(u, basis_u, derivative_u);
	GetBasis(v, basis_v, derivative_v);

	SurfaceSample sample;
	sample.Color[0] = 0.0;
	sample.Color[1] = 0.0;
	sample.Color[2] = 0.0;
	sample.Color[3] = 0.0;

	double total = 0.0;
	double total_u = 0.0;
	double total_v = 0.0;

	for (uint32 row = 0; row < 3; row++) {
		for (uint32 column = 0; column < 3; column++) {
			const PatchPoint& point = patch.GetPoint(sub_column * 2 + column, sub_row * 2 + row);
			const Vec3d position = ToDouble(point.Position);
			const Vec2d texcoord = ToDouble(point.Texcoord);
			const double point_weight = static_cast<double>(point.Weight);

			const double weight = basis_u[column] * basis_v[row] * point_weight;
			const double weight_u = derivative_u[column] * basis_v[row] * point_weight;
			const double weight_v = basis_u[column] * derivative_v[row] * point_weight;

			sample.Position = sample.Position + position * weight;
			sample.DerivU = sample.DerivU + position * weight_u;
			sample.DerivV = sample.DerivV + position * weight_v;

			sample.Texcoord += texcoord * weight;
			sample.TexcoordDerivU += texcoord * weight_u;
			AddColor(sample.Color, point.Color, weight);
			sample.TexcoordDerivV += texcoord * weight_v;

			total += weight;
			total_u += weight_u;
			total_v += weight_v;
		}
	}

	const double inverse = 1.0 / total;

	sample.Position = sample.Position * inverse;
	sample.DerivU = (sample.DerivU - sample.Position * total_u) * inverse;
	sample.DerivV = (sample.DerivV - sample.Position * total_v) * inverse;

	sample.Texcoord = sample.Texcoord * inverse;

	for (double& channel : sample.Color) {
		channel *= inverse;
	}

	sample.TexcoordDerivU = (sample.TexcoordDerivU - sample.Texcoord * total_u) * inverse;
	sample.TexcoordDerivV = (sample.TexcoordDerivV - sample.Texcoord * total_v) * inverse;

	return sample;
}

Vec3d GetAnyPerpendicular(const Vec3d& normal)
{
	Vec3d axis = Vec3d(0.0, 1.0, 0.0);

	if (std::fabs(normal.X) < 0.9) {
		axis = Vec3d(1.0, 0.0, 0.0);
	}
	const Vec3d tangent = axis - normal * normal.Dot(axis);

	return tangent.Normalized();
}

void SolveTangent(const SurfaceSample& sample, const Vec3d& normal, Vec3d& out_tangent, double& out_handedness)
{
	const double s_u = sample.TexcoordDerivU.X;
	const double t_u = sample.TexcoordDerivU.Y;
	const double s_v = sample.TexcoordDerivV.X;
	const double t_v = sample.TexcoordDerivV.Y;

	const double determinant = s_u * t_v - s_v * t_u;

	out_handedness = 1.0;

	if (std::fabs(determinant) > scDegenerateTexture) {
		const Vec3d along_s = (sample.DerivU * t_v - sample.DerivV * t_u) * (1.0 / determinant);
		const Vec3d along_t = (sample.DerivV * s_u - sample.DerivU * s_v) * (1.0 / determinant);

		const Vec3d tangent = along_s - normal * normal.Dot(along_s);
		const double length = tangent.Length();

		if (length > scDegenerateLength) {
			out_tangent = tangent * (1.0 / length);

			if (normal.Cross(out_tangent).Dot(along_t) > 0.0) {
				out_handedness = -1.0;
			}

			return;
		}
	}

	const Vec3d tangent = sample.DerivU - normal * normal.Dot(sample.DerivU);
	const double length = tangent.Length();

	if (length > scDegenerateLength) {
		out_tangent = tangent * (1.0 / length);
	}
	else {
		out_tangent = GetAnyPerpendicular(normal);
	}
}

struct SubPatchSpan
{
	uint32 Index = 0;
	double Local = 0.0;
};

uint32 FindSubPatchSpans(double value, uint32 count, SubPatchSpan out_spans[2])
{
	const uint32 index = std::min(static_cast<uint32>(value), count - 1);

	out_spans[0] = SubPatchSpan { index, value - static_cast<double>(index) };

	if (out_spans[0].Local == 0.0 && index > 0) {
		out_spans[1] = SubPatchSpan { index - 1, 1.0 };
		return 2;
	}

	return 1;
}

bool GetFrame(const Patch& patch, const SubPatchSpan& column, const SubPatchSpan& row, Vec3d& out_normal,
			  Vec3d& out_tangent, double& out_handedness)
{
	SurfaceSample sample = SampleSubPatch(patch, column.Index, row.Index, column.Local, row.Local);
	Vec3d normal = sample.DerivU.Cross(sample.DerivV);

	for (const double nudge : scNudges) {
		if (normal.Length() > scDegenerateCross) {
			break;
		}

		const double nudged_u = column.Local + (0.5 - column.Local) * nudge * 2.0;
		const double nudged_v = row.Local + (0.5 - row.Local) * nudge * 2.0;

		sample = SampleSubPatch(patch, column.Index, row.Index, nudged_u, nudged_v);
		normal = sample.DerivU.Cross(sample.DerivV);
	}

	const double length = normal.Length();

	if (length <= scDegenerateCross) {
		return false;
	}

	out_normal = normal * (1.0 / length);

	SolveTangent(sample, out_normal, out_tangent, out_handedness);

	return true;
}

const PatchPoint& GetLinePoint(const Patch& patch, bool along_columns, uint32 index, uint32 line)
{
	if (along_columns) {
		return patch.GetPoint(index, line);
	}

	return patch.GetPoint(line, index);
}

void MeasureNaturalDistances(const Patch& patch, bool along_columns, SizedArray<double>& out_distances)
{
	uint32 count = patch.Height;
	uint32 lines = patch.Width;

	if (along_columns) {
		count = patch.Width;
		lines = patch.Height;
	}

	out_distances.InitCapacity(count);
	out_distances.Insert(0.0);

	if (patch.bIsDetailMesh) {
		for (uint32 index = 0; index + 1 < count; index++) {
			double length = 0.0;

			for (uint32 line = 0; line < lines; line++) {
				length += (ToDouble(GetLinePoint(patch, along_columns, index + 1, line).Position) -
						   ToDouble(GetLinePoint(patch, along_columns, index, line).Position))
							  .Length();
			}

			out_distances.Insert(out_distances[index] + length / static_cast<double>(lines));
		}

		return;
	}

	for (uint32 index = 0; index + 2 < count; index += 2) {
		double length = 0.0;

		for (uint32 line = 0; line < lines; line++) {
			length += GetCurveLength(GetLinePoint(patch, along_columns, index, line),
									 GetLinePoint(patch, along_columns, index + 1, line),
									 GetLinePoint(patch, along_columns, index + 2, line));
		}

		const double start = out_distances[index];
		const double end = start + length / static_cast<double>(lines);

		out_distances.Insert((start + end) * 0.5);
		out_distances.Insert(end);
	}
}

struct CellCorners
{
	uint32 Index00 = 0;
	uint32 Index10 = 0;
	uint32 Index01 = 0;
	uint32 Index11 = 0;
};

CellCorners GetCellCorners(const Patch& patch, uint32 column, uint32 row)
{
	CellCorners corners;
	corners.Index00 = row * patch.Width + column;
	corners.Index10 = corners.Index00 + 1;
	corners.Index01 = corners.Index00 + patch.Width;
	corners.Index11 = corners.Index01 + 1;

	return corners;
}

void GetCellTriangles(const Patch& patch, uint32 column, uint32 row, uint32 out_corners[6])
{
	const CellCorners cell = GetCellCorners(patch, column, row);

	if (patch.IsCellTurned(column, row)) {
		const uint32 corners[6] = {
			cell.Index00, cell.Index10, cell.Index01, cell.Index10, cell.Index11, cell.Index01
		};
		std::copy(std::begin(corners), std::end(corners), out_corners);
		return;
	}

	const uint32 corners[6] = { cell.Index00, cell.Index10, cell.Index11, cell.Index00, cell.Index11, cell.Index01 };
	std::copy(std::begin(corners), std::end(corners), out_corners);
}

bool SolveTriangleFrame(const PatchPoint& a, const PatchPoint& b, const PatchPoint& c, Vec3d& out_normal,
						Vec3d& out_tangent, double& out_handedness)
{
	SurfaceSample sample;
	sample.DerivU = ToDouble(b.Position) - ToDouble(a.Position);
	sample.DerivV = ToDouble(c.Position) - ToDouble(a.Position);
	sample.TexcoordDerivU = ToDouble(b.Texcoord) - ToDouble(a.Texcoord);
	sample.TexcoordDerivV = ToDouble(c.Texcoord) - ToDouble(a.Texcoord);

	const Vec3d normal = sample.DerivU.Cross(sample.DerivV);
	const double length = normal.Length();

	if (length <= scDegenerateCross) {
		return false;
	}

	out_normal = normal * (1.0 / length);

	SolveTangent(sample, out_normal, out_tangent, out_handedness);

	return true;
}

PatchVertex EvaluateDetailMesh(const Patch& patch, double u, double v)
{
	PatchVertex vertex;

	u = std::clamp(u, 0.0, static_cast<double>(patch.Width - 1));
	v = std::clamp(v, 0.0, static_cast<double>(patch.Height - 1));

	const uint32 column = std::min(static_cast<uint32>(u), patch.Width - 2);
	const uint32 row = std::min(static_cast<uint32>(v), patch.Height - 2);

	const double s = u - static_cast<double>(column);
	const double t = v - static_cast<double>(row);

	const CellCorners cell = GetCellCorners(patch, column, row);

	uint32 corners[3] = { cell.Index00, cell.Index10, cell.Index11 };
	double weights[3] = { 1.0 - s, s - t, t };

	if (patch.IsCellTurned(column, row)) {
		if (s + t <= 1.0) {
			corners[2] = cell.Index01;
			weights[0] = 1.0 - s - t;
			weights[1] = s;
			weights[2] = t;
		}
		else {
			corners[0] = cell.Index10;
			corners[1] = cell.Index11;
			corners[2] = cell.Index01;
			weights[0] = 1.0 - t;
			weights[1] = s + t - 1.0;
			weights[2] = 1.0 - s;
		}
	}
	else if (t > s) {
		corners[1] = cell.Index11;
		corners[2] = cell.Index01;
		weights[0] = 1.0 - t;
		weights[1] = s;
		weights[2] = t - s;
	}

	double color[4] = { 0.0, 0.0, 0.0, 0.0 };

	for (uint32 corner = 0; corner < 3; corner++) {
		const PatchPoint& point = patch.Points[corners[corner]];

		vertex.Position = vertex.Position + ToDouble(point.Position) * weights[corner];
		vertex.Texcoord += ToDouble(point.Texcoord) * weights[corner];
		AddColor(color, point.Color, weights[corner]);
	}

	vertex.Color = MakeColor(color);

	vertex.Parameter = Vec2d(u, v);

	if (!SolveTriangleFrame(patch.Points[corners[0]], patch.Points[corners[1]], patch.Points[corners[2]], vertex.Normal,
							vertex.Tangent, vertex.Handedness)) {
		vertex.Normal = Vec3d(0.0, 1.0, 0.0);
		vertex.Tangent = Vec3d(1.0, 0.0, 0.0);
	}

	return vertex;
}

bool AddGridTriangle(const SizedArray<PatchVertex>& grid, SizedArray<uint32>& triangles, uint32 a, uint32 b, uint32 c)
{
	const Vec3d& pa = grid[a].Position;
	const Vec3d cross = (grid[b].Position - pa).Cross(grid[c].Position - pa);

	if (cross.Length() <= scDegenerateCross) {
		return false;
	}

	triangles.Insert(a);
	triangles.Insert(c);
	triangles.Insert(b);

	return true;
}

void BuildCurveGrid(const Patch& patch, SizedArray<PatchVertex>& out_grid, SizedArray<uint32>& out_triangles,
					uint32& out_columns, uint32& out_rows)
{
	const uint32 columns = patch.GetSubPatchColumns() * patch.SubdivisionsU + 1;
	const uint32 rows = patch.GetSubPatchRows() * patch.SubdivisionsV + 1;

	out_grid.InitCapacity(columns * rows);

	for (uint32 row = 0; row < rows; row++) {
		const double v = static_cast<double>(row) / static_cast<double>(patch.SubdivisionsV);

		for (uint32 column = 0; column < columns; column++) {
			const double u = static_cast<double>(column) / static_cast<double>(patch.SubdivisionsU);

			out_grid.Insert(patch.Evaluate(u, v));
		}
	}

	out_triangles.InitCapacity((columns - 1) * (rows - 1) * 6);

	for (uint32 row = 0; row + 1 < rows; row++) {
		for (uint32 column = 0; column + 1 < columns; column++) {
			const uint32 i00 = row * columns + column;
			const uint32 i10 = i00 + 1;
			const uint32 i01 = i00 + columns;
			const uint32 i11 = i01 + 1;

			const double diagonal_a = (out_grid[i11].Position - out_grid[i00].Position).Length();
			const double diagonal_b = (out_grid[i01].Position - out_grid[i10].Position).Length();

			if (diagonal_a <= diagonal_b) {
				AddGridTriangle(out_grid, out_triangles, i00, i10, i11);
				AddGridTriangle(out_grid, out_triangles, i00, i11, i01);
			}
			else {
				AddGridTriangle(out_grid, out_triangles, i00, i10, i01);
				AddGridTriangle(out_grid, out_triangles, i10, i11, i01);
			}
		}
	}

	out_columns = columns;
	out_rows = rows;
}

void BuildDetailMeshGrid(const Patch& patch, SizedArray<PatchVertex>& out_grid, SizedArray<uint32>& out_triangles)
{
	out_grid.InitCapacity(patch.Points.Size);

	for (uint32 row = 0; row < patch.Height; row++) {
		for (uint32 column = 0; column < patch.Width; column++) {
			const PatchPoint& point = patch.GetPoint(column, row);

			PatchVertex vertex;
			vertex.Position = ToDouble(point.Position);
			vertex.Texcoord = ToDouble(point.Texcoord);
			vertex.Color = point.Color;
			vertex.Parameter = Vec2d(static_cast<double>(column), static_cast<double>(row));

			out_grid.Insert(vertex);
		}
	}

	SizedArray<Vec3d> bitangents(patch.Points.Size);

	for (size_t i = 0; i < patch.Points.Size; i++) {
		bitangents.Insert(Vec3d(0.0));
	}

	out_triangles.InitCapacity((patch.Width - 1) * (patch.Height - 1) * 6);

	for (uint32 row = 0; row + 1 < patch.Height; row++) {
		for (uint32 column = 0; column + 1 < patch.Width; column++) {
			uint32 corners[6];
			GetCellTriangles(patch, column, row, corners);

			for (uint32 triangle = 0; triangle < 2; triangle++) {
				const uint32 a = corners[triangle * 3];
				const uint32 b = corners[triangle * 3 + 1];
				const uint32 c = corners[triangle * 3 + 2];

				if (!AddGridTriangle(out_grid, out_triangles, a, b, c)) {
					continue;
				}

				const Vec3d edge_b = out_grid[b].Position - out_grid[a].Position;
				const Vec3d edge_c = out_grid[c].Position - out_grid[a].Position;
				const Vec2d texture_b = out_grid[b].Texcoord - out_grid[a].Texcoord;
				const Vec2d texture_c = out_grid[c].Texcoord - out_grid[a].Texcoord;

				const Vec3d normal = edge_b.Cross(edge_c);
				const double determinant = texture_b.X * texture_c.Y - texture_c.X * texture_b.Y;

				Vec3d along_s = edge_b;
				Vec3d along_t = Vec3d(0.0);

				if (std::fabs(determinant) > scDegenerateTexture) {
					along_s = (edge_b * texture_c.Y - edge_c * texture_b.Y) * (1.0 / determinant);
					along_t = (edge_c * texture_b.X - edge_b * texture_c.X) * (1.0 / determinant);
				}

				for (const uint32 corner : { a, b, c }) {
					out_grid[corner].Normal = out_grid[corner].Normal + normal;
					out_grid[corner].Tangent = out_grid[corner].Tangent + along_s;
					bitangents[corner] = bitangents[corner] + along_t;
				}
			}
		}
	}

	for (size_t i = 0; i < out_grid.Size; i++) {
		PatchVertex& vertex = out_grid[i];
		const double normal_length = vertex.Normal.Length();

		if (normal_length <= scDegenerateCross) {
			vertex.Normal = Vec3d(0.0, 1.0, 0.0);
		}
		else {
			vertex.Normal = vertex.Normal * (1.0 / normal_length);
		}

		const Vec3d tangent = vertex.Tangent - vertex.Normal * vertex.Normal.Dot(vertex.Tangent);
		const double tangent_length = tangent.Length();

		if (tangent_length > scDegenerateLength) {
			vertex.Tangent = tangent * (1.0 / tangent_length);
		}
		else {
			vertex.Tangent = GetAnyPerpendicular(vertex.Normal);
		}

		vertex.Handedness = 1.0;

		if (vertex.Normal.Cross(vertex.Tangent).Dot(bitangents[i]) > 0.0) {
			vertex.Handedness = -1.0;
		}
	}
}

Vec3d GetFacingNormal(const Patch& patch)
{
	if (!patch.bIsDetailMesh) {
		return patch
			.Evaluate(static_cast<double>(patch.GetSubPatchColumns()) * 0.5,
					  static_cast<double>(patch.GetSubPatchRows()) * 0.5)
			.Normal;
	}

	Vec3d sum = Vec3d(0.0);

	for (uint32 row = 0; row + 1 < patch.Height; row++) {
		for (uint32 column = 0; column + 1 < patch.Width; column++) {
			const CellCorners cell = GetCellCorners(patch, column, row);
			const Vec3d diagonal_a = ToDouble(patch.Points[cell.Index11].Position) -
									 ToDouble(patch.Points[cell.Index00].Position);
			const Vec3d diagonal_b = ToDouble(patch.Points[cell.Index01].Position) -
									 ToDouble(patch.Points[cell.Index10].Position);

			sum = sum + diagonal_a.Cross(diagonal_b);
		}
	}

	const double length = sum.Length();

	if (length <= scDegenerateCross) {
		return Vec3d(0.0, 1.0, 0.0);
	}

	return sum * (1.0 / length);
}

} // namespace

PatchVertex Patch::Evaluate(double u, double v) const
{
	PatchVertex vertex;

	if (!IsValid()) {
		return vertex;
	}

	if (bIsDetailMesh) {
		return EvaluateDetailMesh(*this, u, v);
	}

	const uint32 sub_columns = GetSubPatchColumns();
	const uint32 sub_rows = GetSubPatchRows();

	u = std::clamp(u, 0.0, static_cast<double>(sub_columns));
	v = std::clamp(v, 0.0, static_cast<double>(sub_rows));

	SubPatchSpan columns[2];
	SubPatchSpan rows[2];

	const uint32 column_count = FindSubPatchSpans(u, sub_columns, columns);
	const uint32 row_count = FindSubPatchSpans(v, sub_rows, rows);

	const SurfaceSample sample = SampleSubPatch(*this, columns[0].Index, rows[0].Index, columns[0].Local,
												rows[0].Local);

	vertex.Position = sample.Position;
	vertex.Texcoord = sample.Texcoord;
	vertex.Color = MakeColor(sample.Color);
	vertex.Parameter = Vec2d(u, v);

	Vec3d normal_sum = Vec3d(0.0);
	Vec3d tangent_sum = Vec3d(0.0);
	double handedness_sum = 0.0;

	for (uint32 row = 0; row < row_count; row++) {
		for (uint32 column = 0; column < column_count; column++) {
			Vec3d normal;
			Vec3d tangent;
			double handedness = 1.0;

			if (GetFrame(*this, columns[column], rows[row], normal, tangent, handedness)) {
				normal_sum = normal_sum + normal;
				tangent_sum = tangent_sum + tangent;
				handedness_sum += handedness;
			}
		}
	}

	const double normal_length = normal_sum.Length();

	if (normal_length <= scDegenerateLength) {
		vertex.Normal = Vec3d(0.0, 1.0, 0.0);
		vertex.Tangent = Vec3d(1.0, 0.0, 0.0);
		return vertex;
	}

	vertex.Normal = normal_sum * (1.0 / normal_length);

	const Vec3d tangent = tangent_sum - vertex.Normal * vertex.Normal.Dot(tangent_sum);
	const double tangent_length = tangent.Length();

	if (tangent_length > scDegenerateLength) {
		vertex.Tangent = tangent * (1.0 / tangent_length);
	}
	else {
		vertex.Tangent = GetAnyPerpendicular(vertex.Normal);
	}

	if (handedness_sum < 0.0) {
		vertex.Handedness = -1.0;
	}

	return vertex;
}

void Patch::Tessellate(PatchMesh& out_mesh) const
{
	out_mesh.Columns = 0;
	out_mesh.Rows = 0;
	out_mesh.Vertices.Clear();
	out_mesh.Indices.Clear();

	if (!IsValid()) {
		return;
	}

	SizedArray<PatchVertex> grid;
	SizedArray<uint32> triangles;

	uint32 columns = Width;
	uint32 rows = Height;

	if (bIsDetailMesh) {
		BuildDetailMeshGrid(*this, grid, triangles);
	}
	else {
		BuildCurveGrid(*this, grid, triangles, columns, rows);
	}

	out_mesh.Columns = columns;
	out_mesh.Rows = rows;

	if (bSmooth) {
		out_mesh.Vertices = std::move(grid);
		out_mesh.Indices = std::move(triangles);
		return;
	}

	out_mesh.Vertices.InitCapacity(triangles.Size);
	out_mesh.Indices.InitCapacity(triangles.Size);

	for (uint32 index = 0; index + 2 < triangles.Size; index += 3) {
		const Vec3d& a = grid[triangles[index]].Position;
		const Vec3d& b = grid[triangles[index + 1]].Position;
		const Vec3d& c = grid[triangles[index + 2]].Position;

		const Vec3d face_normal = (c - a).Cross(b - a).Normalized();

		for (uint32 corner = 0; corner < 3; corner++) {
			PatchVertex vertex = grid[triangles[index + corner]];

			const Vec3d tangent = vertex.Tangent - face_normal * face_normal.Dot(vertex.Tangent);
			const double length = tangent.Length();

			vertex.Normal = face_normal;

			if (length > scDegenerateLength) {
				vertex.Tangent = tangent * (1.0 / length);
			}
			else {
				vertex.Tangent = GetAnyPerpendicular(face_normal);
			}

			out_mesh.Indices.Insert(static_cast<uint32>(out_mesh.Vertices.Size));
			out_mesh.Vertices.Insert(vertex);
		}
	}
}

uint32 PatchColor::Pack() const
{
	const auto to_byte = [](float32 value)
	{ return static_cast<uint32>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f)); };

	return to_byte(R) | (to_byte(G) << 8) | (to_byte(B) << 16) | (to_byte(A) << 24);
}

bool Patch::HasVertexColors() const
{
	return std::any_of(Points.begin(), Points.end(), [](const PatchPoint& point) { return !point.Color.IsWhite(); });
}

void Patch::GenerateMesh(SizedArray<Vec3f>& positions, SizedArray<Vec3f>& normals, SizedArray<Vec3f>& tangents,
						 SizedArray<float32>& tangent_handedness, SizedArray<Vec2f>& texcoords,
						 SizedArray<uint32>& indices, const Mat4f& to_world, SizedArray<uint32>* packed_colors) const
{
	Patch pieces[scMaxClosurePieces];
	const uint32 piece_count = BuildClosure(pieces, to_world);

	SizedArray<PatchMesh> meshes;
	meshes.InitCapacity(piece_count + 1);

	size_t vertex_count = 0;
	size_t index_count = 0;

	for (uint32 piece = 0; piece <= piece_count; piece++) {
		const Patch* shape = this;

		if (piece > 0) {
			shape = &pieces[piece - 1];
		}

		PatchMesh mesh;
		shape->Tessellate(mesh);

		vertex_count += mesh.Vertices.Size;
		index_count += mesh.Indices.Size;

		meshes.Insert(std::move(mesh));
	}

	if (index_count == 0) {
		return;
	}

	positions.InitCapacity(vertex_count);
	normals.InitCapacity(vertex_count);
	tangents.InitCapacity(vertex_count);
	tangent_handedness.InitCapacity(vertex_count);
	texcoords.InitCapacity(vertex_count);
	indices.InitCapacity(index_count);

	if (packed_colors != nullptr) {
		packed_colors->InitCapacity(vertex_count);
	}

	for (const PatchMesh& mesh : meshes) {
		const uint32 base = static_cast<uint32>(positions.Size);

		for (const PatchVertex& vertex : mesh.Vertices) {
			positions.Insert(ToFloat(vertex.Position));
			normals.Insert(ToFloat(vertex.Normal));
			tangents.Insert(ToFloat(vertex.Tangent));
			tangent_handedness.Insert(static_cast<float32>(vertex.Handedness));
			texcoords.Insert(Vec2f(static_cast<float32>(vertex.Texcoord.X), static_cast<float32>(vertex.Texcoord.Y)));

			if (packed_colors != nullptr) {
				packed_colors->Insert(vertex.Color.Pack());
			}
		}

		for (const uint32 index : mesh.Indices) {
			indices.Insert(base + index);
		}
	}
}

void Patch::Invert()
{
	for (uint32 row = 0; row < Height; row++) {
		for (uint32 column = 0; column < Width / 2; column++) {
			std::swap(GetPoint(column, row), GetPoint(Width - 1 - column, row));
		}
	}

	if (!bIsDetailMesh || TurnedCells.Size != (Width - 1) * (Height - 1)) {
		return;
	}

	const uint32 cells = Width - 1;

	for (uint32 row = 0; row + 1 < Height; row++) {
		uint8* line = &TurnedCells[row * cells];

		std::reverse(line, line + cells);

		for (uint32 column = 0; column < cells; column++) {
			line[column] = static_cast<uint8>(line[column] == 0);
		}
	}
}

void Patch::Transpose()
{
	SizedArray<PatchPoint> points(Points.Size);

	for (uint32 column = 0; column < Width; column++) {
		for (uint32 row = 0; row < Height; row++) {
			points.Insert(GetPoint(column, row));
		}
	}

	if (bIsDetailMesh && TurnedCells.Size == (Width - 1) * (Height - 1)) {
		SizedArray<uint8> turned(TurnedCells.Size);

		for (uint32 column = 0; column + 1 < Width; column++) {
			for (uint32 row = 0; row + 1 < Height; row++) {
				turned.Insert(static_cast<uint8>(TurnedCells[row * (Width - 1) + column]));
			}
		}

		TurnedCells = std::move(turned);
	}

	std::swap(Width, Height);
	std::swap(SubdivisionsU, SubdivisionsV);

	Points = std::move(points);
}

namespace {

PatchPoint GetMidpoint(const PatchPoint& a, const PatchPoint& b)
{
	PatchPoint point;
	point.Position = (a.Position + b.Position) * 0.5f;
	point.Texcoord = (a.Texcoord + b.Texcoord) * 0.5f;
	point.Color = MixColors(a.Color, 0.5f, b.Color, 0.5f);

	return point;
}

} // namespace

bool Patch::ResampleDetailMesh(uint32 cells_u, uint32 cells_v)
{
	if (!bIsDetailMesh || !IsValid()) {
		return false;
	}

	cells_u = std::clamp(cells_u, scMinDetailMeshSize - 1, scMaxDetailMeshSize - 1);
	cells_v = std::clamp(cells_v, scMinDetailMeshSize - 1, scMaxDetailMeshSize - 1);

	if (cells_u + 1 == Width && cells_v + 1 == Height) {
		return true;
	}

	Patch resampled = FromDetailMesh(cells_u + 1, cells_v + 1);

	const double scale_u = static_cast<double>(Width - 1) / static_cast<double>(cells_u);
	const double scale_v = static_cast<double>(Height - 1) / static_cast<double>(cells_v);

	for (uint32 row = 0; row <= cells_v; row++) {
		for (uint32 column = 0; column <= cells_u; column++) {
			const PatchVertex vertex = Evaluate(static_cast<double>(column) * scale_u,
												static_cast<double>(row) * scale_v);
			PatchPoint& point = resampled.GetPoint(column, row);

			point.Position = ToFloat(vertex.Position);
			point.Texcoord = Vec2f(static_cast<float32>(vertex.Texcoord.X), static_cast<float32>(vertex.Texcoord.Y));
			point.Color = vertex.Color;
		}
	}

	Width = resampled.Width;
	Height = resampled.Height;
	Points = std::move(resampled.Points);
	TurnedCells = std::move(resampled.TurnedCells);

	return true;
}

bool Patch::InsertColumns(uint32 sub_column)
{
	if (bIsDetailMesh) {
		if (!IsValid() || sub_column >= GetSubPatchColumns() || Width + 1 > scMaxDetailMeshSize) {
			return false;
		}

		const uint32 width = Width + 1;
		SizedArray<PatchPoint> points(width * Height);
		SizedArray<uint8> turned((width - 1) * (Height - 1));

		for (uint32 row = 0; row < Height; row++) {
			for (uint32 column = 0; column < Width; column++) {
				points.Insert(GetPoint(column, row));

				if (column == sub_column) {
					points.Insert(GetMidpoint(GetPoint(column, row), GetPoint(column + 1, row)));
				}
			}
		}

		for (uint32 row = 0; row + 1 < Height; row++) {
			for (uint32 column = 0; column + 1 < Width; column++) {
				const uint8 flag = TurnedCells[row * (Width - 1) + column];

				turned.Insert(static_cast<uint8>(flag));

				if (column == sub_column) {
					turned.Insert(static_cast<uint8>(flag));
				}
			}
		}

		Width = width;
		Points = std::move(points);
		TurnedCells = std::move(turned);

		return true;
	}

	if (!IsValid() || sub_column >= GetSubPatchColumns() || Width + 2 > scMaxSize) {
		return false;
	}

	const uint32 width = Width + 2;
	SizedArray<PatchPoint> points(width * Height);

	for (uint32 row = 0; row < Height; row++) {
		for (uint32 column = 0; column < sub_column * 2; column++) {
			points.Insert(GetPoint(column, row));
		}

		PatchPoint split[5];
		SplitCurve(GetPoint(sub_column * 2, row), GetPoint(sub_column * 2 + 1, row), GetPoint(sub_column * 2 + 2, row),
				   split);

		for (const PatchPoint& point : split) {
			points.Insert(point);
		}

		for (uint32 column = sub_column * 2 + 3; column < Width; column++) {
			points.Insert(GetPoint(column, row));
		}
	}

	Width = width;
	Points = std::move(points);

	return true;
}

bool Patch::InsertRows(uint32 sub_row)
{
	Transpose();
	const bool inserted = InsertColumns(sub_row);
	Transpose();

	return inserted;
}

bool Patch::RemoveColumns(uint32 sub_column)
{
	if (!IsValid() || sub_column + 1 >= GetSubPatchColumns()) {
		return false;
	}

	if (bIsDetailMesh) {
		const uint32 width = Width - 1;
		const uint32 removed = sub_column + 1;

		SizedArray<PatchPoint> points(width * Height);
		SizedArray<uint8> turned((width - 1) * (Height - 1));

		for (uint32 row = 0; row < Height; row++) {
			for (uint32 column = 0; column < Width; column++) {
				if (column != removed) {
					points.Insert(GetPoint(column, row));
				}
			}
		}

		for (uint32 row = 0; row + 1 < Height; row++) {
			for (uint32 column = 0; column + 1 < Width; column++) {
				if (column != removed) {
					turned.Insert(static_cast<uint8>(TurnedCells[row * (Width - 1) + column]));
				}
			}
		}

		Width = width;
		Points = std::move(points);
		TurnedCells = std::move(turned);

		return true;
	}

	const uint32 width = Width - 2;
	SizedArray<PatchPoint> points(width * Height);

	const uint32 first = sub_column * 2;

	for (uint32 row = 0; row < Height; row++) {
		for (uint32 column = 0; column < first + 1; column++) {
			points.Insert(GetPoint(column, row));
		}

		points.Insert(JoinCurves(GetPoint(first, row), GetPoint(first + 1, row), GetPoint(first + 3, row),
								 GetPoint(first + 4, row)));

		for (uint32 column = first + 4; column < Width; column++) {
			points.Insert(GetPoint(column, row));
		}
	}

	Width = width;
	Points = std::move(points);

	return true;
}

bool Patch::RemoveRows(uint32 sub_row)
{
	Transpose();
	const bool removed = RemoveColumns(sub_row);
	Transpose();

	return removed;
}

void Patch::NaturalTexture(float32 scale)
{
	if (!IsValid() || scale == 0.0f) {
		return;
	}

	SizedArray<double> distances_u;
	SizedArray<double> distances_v;

	MeasureNaturalDistances(*this, true, distances_u);
	MeasureNaturalDistances(*this, false, distances_v);

	for (uint32 row = 0; row < Height; row++) {
		for (uint32 column = 0; column < Width; column++) {
			GetPoint(column, row).Texcoord = Vec2f(static_cast<float32>(distances_u[column] / scale),
												   static_cast<float32>(distances_v[row] / scale));
		}
	}
}

void Patch::FitTexture(float32 repeats_u, float32 repeats_v)
{
	if (!IsValid()) {
		return;
	}

	for (uint32 row = 0; row < Height; row++) {
		for (uint32 column = 0; column < Width; column++) {
			GetPoint(column, row).Texcoord = Vec2f(
				repeats_u * static_cast<float32>(column) / static_cast<float32>(Width - 1),
				repeats_v * static_cast<float32>(row) / static_cast<float32>(Height - 1));
		}
	}
}


namespace {

constexpr float32 scEdgeMatchDistance = 1e-4f;
constexpr float32 scCornerCapMinBulge = 1e-3f;
constexpr double scEdgeInset = 1e-2;
constexpr double scMinOffsetCos = 0.1;

uint32 GetEdgeSubdivisions(const Patch& patch, uint32 edge)
{
	if (edge == 0 || edge == 2) {
		return patch.SubdivisionsU;
	}

	return patch.SubdivisionsV;
}

bool AreSamePoints(const SizedArray<PatchPoint>& a, const SizedArray<PatchPoint>& b)
{
	if (a.Size != b.Size) {
		return false;
	}

	for (size_t i = 0; i < a.Size; i++) {
		if ((a[i].Position - b[i].Position).Length() > scEdgeMatchDistance ||
			std::fabs(a[i].Weight - b[i].Weight) > scEdgeMatchDistance) {
			return false;
		}
	}

	return true;
}

void OrientOutward(Patch& patch, const Vec3d& outward)
{
	const PatchVertex middle = patch.Evaluate(static_cast<double>(patch.GetSubPatchColumns()) * 0.5,
											  static_cast<double>(patch.GetSubPatchRows()) * 0.5);

	if (middle.Normal.Dot(outward) < 0.0) {
		patch.Invert();
	}
}

Patch MakeCornerCap(const PatchPoint& start, const PatchPoint& corner, const PatchPoint& end, const Vec3f fill,
					uint32 subdivisions, const Vec3d& outward, const Mat4f& to_world)
{
	const Vec3f chord = end.Position - start.Position;
	const Vec3f to_corner = corner.Position - start.Position;
	const float32 chord_length = chord.Length();

	if (chord_length <= scEdgeMatchDistance) {
		return {};
	}

	const float32 bulge = to_corner.Cross(chord).Length() / chord_length;

	if (bulge <= scCornerCapMinBulge) {
		return {};
	}

	Patch cap = Patch::FromGrid(3, 3);

	if (cap.Points.IsEmpty()) {
		return cap;
	}

	const PatchPoint* ring[3] = { &start, &corner, &end };

	for (uint32 column = 0; column < cap.Width; column++) {
		const Vec3f position = ring[column]->Position;
		const float32 weight = ring[column]->Weight;

		cap.GetPoint(column, 0).Position = position;
		cap.GetPoint(column, 1).Position = (position + fill) * 0.5f;
		cap.GetPoint(column, 2).Position = fill;

		for (uint32 row = 0; row < cap.Height; row++) {
			cap.GetPoint(column, row).Weight = weight;
			cap.GetPoint(column, row).Color = ring[column]->Color;
		}
	}

	cap.SubdivisionsU = subdivisions;
	cap.SubdivisionsV = 1;
	cap.bSmooth = false;

	OrientOutward(cap, outward);
	cap.WorldTexture(to_world);

	return cap;
}

Patch MakeBridge(const SizedArray<PatchPoint>& from, const SizedArray<PatchPoint>& to, uint32 subdivisions)
{
	Patch bridge = Patch::FromGrid(static_cast<uint32>(from.Size), 3);

	if (bridge.Points.IsEmpty()) {
		return bridge;
	}

	for (uint32 column = 0; column < bridge.Width; column++) {
		bridge.GetPoint(column, 0).Position = from[column].Position;
		bridge.GetPoint(column, 1).Position = (from[column].Position + to[column].Position) * 0.5f;
		bridge.GetPoint(column, 2).Position = to[column].Position;

		bridge.GetPoint(column, 0).Weight = from[column].Weight;
		bridge.GetPoint(column, 1).Weight = (from[column].Weight + to[column].Weight) * 0.5f;
		bridge.GetPoint(column, 2).Weight = to[column].Weight;

		bridge.GetPoint(column, 0).Color = from[column].Color;
		bridge.GetPoint(column, 1).Color = MixColors(from[column].Color, 0.5f, to[column].Color, 0.5f);
		bridge.GetPoint(column, 2).Color = to[column].Color;
	}

	bridge.SubdivisionsU = subdivisions;
	bridge.SubdivisionsV = 1;

	return bridge;
}

} // namespace

void Patch::WorldTexture(const Mat4f& to_world, float32 scale, uint32 axis)
{
	if (!IsValid() || scale == 0.0f) {
		return;
	}

	const Vec3d facing_normal = GetFacingNormal(*this);
	const Vec4f turned = to_world * Vec4f(static_cast<float32>(facing_normal.X), static_cast<float32>(facing_normal.Y),
										  static_cast<float32>(facing_normal.Z), 0.0f);
	const float32 normal[3] = { turned.X, turned.Y, turned.Z };

	uint32 dominant = axis;

	if (axis >= scAutoTextureAxis) {
		dominant = 1;

		if (std::fabs(normal[0]) > std::fabs(normal[1]) && std::fabs(normal[0]) >= std::fabs(normal[2])) {
			dominant = 0;
		}
		else if (std::fabs(normal[2]) > std::fabs(normal[1]) && std::fabs(normal[2]) > std::fabs(normal[0])) {
			dominant = 2;
		}
	}

	Vec3f u_axis(1.0f, 0.0f, 0.0f);
	Vec3f v_axis(0.0f, -1.0f, 0.0f);

	if (dominant == 0) {
		u_axis = Vec3f(0.0f, 0.0f, 1.0f);
	}
	else if (dominant == 1) {
		v_axis = Vec3f(0.0f, 0.0f, 1.0f);
	}

	float32 facing = 1.0f;

	if (normal[dominant] < 0.0f) {
		facing = -1.0f;
	}

	if (GetAxis(u_axis.Cross(v_axis), dominant) * facing < 0.0f) {
		u_axis = u_axis * -1.0f;
	}

	for (PatchPoint& point : Points) {
		const Vec4f moved = to_world * Vec4f(point.Position.X, point.Position.Y, point.Position.Z, 1.0f);
		const Vec3f world_position(moved.X, moved.Y, moved.Z);

		point.Texcoord = Vec2f(world_position.Dot(u_axis) / scale, world_position.Dot(v_axis) / scale);
	}
}

void Patch::GetEdgePoints(uint32 edge, SizedArray<PatchPoint>& out_points) const
{
	out_points.Clear();

	if (!IsValid() || edge >= scEdgeCount) {
		return;
	}

	uint32 count = Height;

	if (edge == 0 || edge == 2) {
		count = Width;
	}

	out_points.InitCapacity(count);

	for (uint32 i = 0; i < count; i++) {
		if (edge == 0) {
			out_points.Insert(GetPoint(i, 0));
		}
		else if (edge == 1) {
			out_points.Insert(GetPoint(Width - 1, i));
		}
		else if (edge == 2) {
			out_points.Insert(GetPoint(i, Height - 1));
		}
		else {
			out_points.Insert(GetPoint(0, i));
		}
	}
}

bool Patch::IsEdgeClosed(uint32 edge) const
{
	SizedArray<PatchPoint> points;
	SizedArray<PatchPoint> opposite;

	GetEdgePoints(edge, points);
	GetEdgePoints((edge + 2) % scEdgeCount, opposite);

	return points.IsNotEmpty() && AreSamePoints(points, opposite);
}

bool Patch::IsEdgeRing(uint32 edge) const
{
	SizedArray<PatchPoint> points;
	GetEdgePoints(edge, points);

	if (points.Size < 3) {
		return false;
	}

	const Vec3f first = points[0].Position;

	if ((points[points.Size - 1].Position - first).Length() > scEdgeMatchDistance) {
		return false;
	}

	for (const PatchPoint& point : points) {
		if ((point.Position - first).Length() > scEdgeMatchDistance) {
			return true;
		}
	}

	return false;
}

Vec3d Patch::GetEdgeOutward(uint32 edge) const
{
	if (!IsValid() || edge >= scEdgeCount) {
		return Vec3d(0.0);
	}

	const double columns = static_cast<double>(GetSubPatchColumns());
	const double rows = static_cast<double>(GetSubPatchRows());

	double u = columns * 0.5;
	double v = rows * 0.5;
	double inner_u = u;
	double inner_v = v;

	if (edge == 0) {
		v = 0.0;
		inner_v = scEdgeInset;
	}
	else if (edge == 1) {
		u = columns;
		inner_u = columns - scEdgeInset;
	}
	else if (edge == 2) {
		v = rows;
		inner_v = rows - scEdgeInset;
	}
	else {
		u = 0.0;
		inner_u = scEdgeInset;
	}

	const Vec3d outward = Evaluate(u, v).Position - Evaluate(inner_u, inner_v).Position;
	const double length = outward.Length();

	if (length <= 0.0) {
		return Vec3d(0.0);
	}

	return outward * (1.0 / length);
}

Patch Patch::MakeOffset(float32 distance) const
{
	Patch offset = Clone();

	if (!IsValid()) {
		return offset;
	}

	SizedArray<Vec3d> normals(Points.Size);

	for (uint32 row = 0; row < Height; row++) {
		for (uint32 column = 0; column < Width; column++) {
			normals.Insert(Evaluate(static_cast<double>(column) * 0.5, static_cast<double>(row) * 0.5).Normal);
		}
	}

	const auto get_normal = [&](uint32 column, uint32 row) -> const Vec3d& { return normals[row * Width + column]; };

	for (uint32 row = 0; row < Height; row++) {
		const uint32 row_low = row - (row & 1);
		const uint32 row_high = row + (row & 1);

		for (uint32 column = 0; column < Width; column++) {
			const uint32 column_low = column - (column & 1);
			const uint32 column_high = column + (column & 1);

			const Vec3d average = (get_normal(column_low, row_low) + get_normal(column_high, row_low) +
								   get_normal(column_low, row_high) + get_normal(column_high, row_high)) *
								  0.25;
			const double length_squared = average.Dot(average);

			Vec3d shift = get_normal(column, row) * static_cast<double>(distance);

			if (length_squared > scMinOffsetCos * scMinOffsetCos) {
				shift = average * (static_cast<double>(distance) / length_squared);
			}

			offset.GetPoint(column, row).Position = GetPoint(column, row).Position - ToFloat(shift);
		}
	}

	offset.Invert();

	return offset;
}

Patch Patch::MakeCap(uint32 edge, const Mat4f& to_world) const
{
	SizedArray<PatchPoint> ring;
	GetEdgePoints(edge, ring);

	Patch cap = FromGrid(static_cast<uint32>(ring.Size), 3);

	if (cap.Points.IsEmpty()) {
		return cap;
	}

	Vec3f center = Vec3f::sZero;

	for (size_t i = 0; i + 1 < ring.Size; i++) {
		center = center + ring[i].Position;
	}

	center = center * (1.0f / static_cast<float32>(ring.Size - 1));

	for (uint32 column = 0; column < cap.Width; column++) {
		cap.GetPoint(column, 0).Position = ring[column].Position;
		cap.GetPoint(column, 1).Position = (ring[column].Position + center) * 0.5f;
		cap.GetPoint(column, 2).Position = center;

		for (uint32 row = 0; row < cap.Height; row++) {
			cap.GetPoint(column, row).Weight = ring[column].Weight;
			cap.GetPoint(column, row).Color = ring[column].Color;
		}
	}

	cap.SubdivisionsU = GetEdgeSubdivisions(*this, edge);
	cap.SubdivisionsV = 1;
	cap.bSmooth = false;

	OrientOutward(cap, GetEdgeOutward(edge));
	cap.WorldTexture(to_world);

	return cap;
}

uint32 Patch::Thicken(float32 distance, Patch out_pieces[scMaxThickenPieces]) const
{
	if (!IsValid() || distance == 0.0f) {
		return 0;
	}

	Patch offset = MakeOffset(distance);

	Patch unflipped = offset.Clone();
	unflipped.Invert();

	uint32 count = 0;

	SizedArray<PatchPoint> from;
	SizedArray<PatchPoint> to;

	for (uint32 edge = 0; edge < scEdgeCount; edge++) {
		if (IsEdgeClosed(edge)) {
			continue;
		}

		GetEdgePoints(edge, from);
		unflipped.GetEdgePoints(edge, to);

		Patch bridge = MakeBridge(from, to, GetEdgeSubdivisions(*this, edge));

		if (!bridge.IsValid()) {
			continue;
		}

		OrientOutward(bridge, GetEdgeOutward(edge));
		bridge.NaturalTexture();

		out_pieces[1 + count++] = std::move(bridge);
	}

	out_pieces[0] = std::move(offset);

	return count + 1;
}

uint32 Patch::Cap(Patch out_caps[scMaxCapPieces], const Mat4f& to_world) const
{
	uint32 count = 0;

	SizedArray<PatchPoint> points;

	for (uint32 edge = 0; edge < scEdgeCount; edge++) {
		if (IsEdgeClosed(edge)) {
			continue;
		}

		if (IsEdgeRing(edge)) {
			Patch cap = MakeCap(edge, to_world);

			if (cap.IsValid()) {
				out_caps[count++] = std::move(cap);
			}

			continue;
		}

		GetEdgePoints(edge, points);

		const Vec3d outward = GetEdgeOutward(edge);
		const uint32 subdivisions = GetEdgeSubdivisions(*this, edge);

		const double columns = static_cast<double>(GetSubPatchColumns());
		const double rows = static_cast<double>(GetSubPatchRows());

		for (size_t span = 0; span + 2 < points.Size; span += 2) {
			const PatchPoint& start = points[span];
			const PatchPoint& corner = points[span + 1];
			const PatchPoint& end = points[span + 2];

			const double along = static_cast<double>(span / 2ULL) + 0.5;

			double u = along;
			double v = scEdgeInset;

			if (edge == 1) {
				u = columns - scEdgeInset;
				v = along;
			}
			else if (edge == 2) {
				v = rows - scEdgeInset;
			}
			else if (edge == 3) {
				u = scEdgeInset;
				v = along;
			}

			const float32 corner_weight = corner.Weight * 2.0f;
			const Vec3f middle = (start.Position * start.Weight + corner.Position * corner_weight +
								  end.Position * end.Weight) *
								 (1.0f / (start.Weight + corner_weight + end.Weight));
			const Vec3d normal = Evaluate(u, v).Normal;

			Vec3f fill = corner.Position;

			if (normal.Dot(ToDouble(corner.Position - middle)) > 0.0) {
				fill = start.Position + end.Position - corner.Position;
			}

			Patch cap = MakeCornerCap(start, corner, end, fill, subdivisions, outward, to_world);

			if (cap.IsValid()) {
				out_caps[count++] = std::move(cap);
			}
		}
	}

	return count;
}

uint32 Patch::BuildClosure(Patch out_pieces[scMaxClosurePieces], const Mat4f& to_world) const
{
	uint32 count = 0;

	if (bIsDetailMesh) {
		return count;
	}

	if (Closure == ePatchClosure::Cap) {
		count = Cap(out_pieces, to_world);
	}
	else if (Closure == ePatchClosure::Thicken) {
		count = Thicken(Thickness, out_pieces);
	}

	for (uint32 piece = 0; piece < count; piece++) {
		out_pieces[piece].Closure = ePatchClosure::None;
	}

	return count;
}

} // namespace fx
