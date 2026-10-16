#include "Patch.hpp"

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
};

constexpr SectionPoint scRing[] = {
	{ 1.0f, 0.5f }, { 1.0f, 1.0f }, { 0.5f, 1.0f }, { 0.0f, 1.0f }, { 0.0f, 0.5f },
	{ 0.0f, 0.0f }, { 0.5f, 0.0f }, { 1.0f, 0.0f }, { 1.0f, 0.5f },
};

Vec3f GetBoxPoint(const Vec3f min, const Vec3f max, uint32 axis, float32 along, float32 a, float32 b)
{
	axis = axis % 3;

	const auto lerp = [&](uint32 index, float32 t)
	{ return GetAxis(min, index) + (GetAxis(max, index) - GetAxis(min, index)) * t; };

	return MakeAxisPoint(axis, lerp(axis, along), lerp((axis + 1) % 3, a), lerp((axis + 2) % 3, b));
}

Patch MakeExtrusion(const Vec3f min, const Vec3f max, uint32 axis, const SectionPoint* section, uint32 section_size,
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

	Patch patch = Patch::FromGrid(section_size, 3);

	for (uint32 row = 0; row < 3; row++) {
		const float32 along = top + (bottom - top) * (static_cast<float32>(row) * 0.5f);
		float32 shrink = 1.0f;

		if (collapse_top) {
			shrink = static_cast<float32>(row) * 0.5f;
		}

		for (uint32 column = 0; column < section_size; column++) {
			const float32 a = min_a + (max_a - min_a) * section[column].A;
			const float32 b = min_b + (max_b - min_b) * section[column].B;

			patch.GetPoint(column, row).Position = MakeAxisPoint(axis, along, center_a + (a - center_a) * shrink,
																 center_b + (b - center_b) * shrink);
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

double GetCurveLength(const Vec3d& a, const Vec3d& b, const Vec3d& c)
{
	double length = 0.0;
	Vec3d previous = a;

	for (uint32 i = 1; i <= scArcLengthSamples; i++) {
		const double t = static_cast<double>(i) / static_cast<double>(scArcLengthSamples);
		const double s = 1.0 - t;
		const Vec3d point = a * (s * s) + b * (2.0 * t * s) + c * (t * t);

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
		point.Position = (x.Position + y.Position) * 0.5f;
		point.Texcoord = (x.Texcoord + y.Texcoord) * 0.5f;
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
	point.Position = (left.Position * 2.0f - a.Position + right.Position * 2.0f - c.Position) * 0.5f;
	point.Texcoord = (left.Texcoord * 2.0f - a.Texcoord + right.Texcoord * 2.0f - c.Texcoord) * 0.5f;
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
	Patch patch = MakeExtrusion(min, max, axis, scRing, 9, false);

	const Vec3d center = ToDouble(GetBoxPoint(min, max, axis, 0.5f, 0.5f, 0.5f));
	const Vec3d direction = ToDouble(MakeAxisPoint(axis % 3, 1.0f, 0.0f, 0.0f));
	const Vec3d sample = patch.Evaluate(0.5, 0.5).Position;

	OrientAwayFrom(patch, center + direction * direction.Dot(sample - center));
	patch.NaturalTexture();

	return patch;
}

Patch Patch::MakeCone(const Vec3f min, const Vec3f max, uint32 axis)
{
	Patch patch = MakeExtrusion(min, max, axis, scRing, 9, true);

	OrientAwayFrom(patch, ToDouble(GetBoxPoint(min, max, axis, 0.0f, 0.5f, 0.5f)));
	patch.NaturalTexture();

	return patch;
}

Patch Patch::MakeBevel(const Vec3f min, const Vec3f max, uint32 axis)
{
	static constexpr SectionPoint scSection[] = { { 0.0f, 1.0f }, { 1.0f, 1.0f }, { 1.0f, 0.0f } };

	Patch patch = MakeExtrusion(min, max, axis, scSection, 3, false);

	OrientAwayFrom(patch, ToDouble(GetBoxPoint(min, max, axis, 0.5f, 0.0f, 0.0f)));
	patch.NaturalTexture();

	return patch;
}

Patch Patch::MakeEndcap(const Vec3f min, const Vec3f max, uint32 axis)
{
	static constexpr SectionPoint scSection[] = {
		{ 0.0f, 0.0f }, { 0.0f, 1.0f }, { 0.5f, 1.0f }, { 1.0f, 1.0f }, { 1.0f, 0.0f },
	};

	Patch patch = MakeExtrusion(min, max, axis, scSection, 5, false);

	OrientAwayFrom(patch, ToDouble(GetBoxPoint(min, max, axis, 0.5f, 0.5f, 0.0f)));
	patch.NaturalTexture();

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
	return IsValidSize(Width) && IsValidSize(Height) && Points.Size == Width * Height &&
		   SubdivisionsU >= scMinSubdivisions && SubdivisionsU <= scMaxSubdivisions &&
		   SubdivisionsV >= scMinSubdivisions && SubdivisionsV <= scMaxSubdivisions;
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

	for (uint32 row = 0; row < 3; row++) {
		for (uint32 column = 0; column < 3; column++) {
			const PatchPoint& point = patch.GetPoint(sub_column * 2 + column, sub_row * 2 + row);
			const Vec3d position = ToDouble(point.Position);
			const Vec2d texcoord = ToDouble(point.Texcoord);

			const double weight = basis_u[column] * basis_v[row];
			const double weight_u = derivative_u[column] * basis_v[row];
			const double weight_v = basis_u[column] * derivative_v[row];

			sample.Position = sample.Position + position * weight;
			sample.DerivU = sample.DerivU + position * weight_u;
			sample.DerivV = sample.DerivV + position * weight_v;

			sample.Texcoord += texcoord * weight;
			sample.TexcoordDerivU += texcoord * weight_u;
			sample.TexcoordDerivV += texcoord * weight_v;
		}
	}

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

Vec3d GetLinePoint(const Patch& patch, bool along_columns, uint32 index, uint32 line)
{
	if (along_columns) {
		return ToDouble(patch.GetPoint(index, line).Position);
	}

	return ToDouble(patch.GetPoint(line, index).Position);
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

} // namespace

PatchVertex Patch::Evaluate(double u, double v) const
{
	PatchVertex vertex;

	if (!IsValid()) {
		return vertex;
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

	const uint32 columns = GetSubPatchColumns() * SubdivisionsU + 1;
	const uint32 rows = GetSubPatchRows() * SubdivisionsV + 1;

	SizedArray<PatchVertex> grid(columns * rows);

	for (uint32 row = 0; row < rows; row++) {
		const double v = static_cast<double>(row) / static_cast<double>(SubdivisionsV);

		for (uint32 column = 0; column < columns; column++) {
			const double u = static_cast<double>(column) / static_cast<double>(SubdivisionsU);

			grid.Insert(Evaluate(u, v));
		}
	}

	SizedArray<uint32> triangles((columns - 1) * (rows - 1) * 6);

	const auto add_triangle = [&grid, &triangles](uint32 a, uint32 b, uint32 c)
	{
		const Vec3d& pa = grid[a].Position;
		const Vec3d cross = (grid[b].Position - pa).Cross(grid[c].Position - pa);

		if (cross.Length() <= scDegenerateCross) {
			return;
		}

		triangles.Insert(a);
		triangles.Insert(c);
		triangles.Insert(b);
	};

	for (uint32 row = 0; row + 1 < rows; row++) {
		for (uint32 column = 0; column + 1 < columns; column++) {
			const uint32 i00 = row * columns + column;
			const uint32 i10 = i00 + 1;
			const uint32 i01 = i00 + columns;
			const uint32 i11 = i01 + 1;

			const double diagonal_a = (grid[i11].Position - grid[i00].Position).Length();
			const double diagonal_b = (grid[i01].Position - grid[i10].Position).Length();

			if (diagonal_a <= diagonal_b) {
				add_triangle(i00, i10, i11);
				add_triangle(i00, i11, i01);
			}
			else {
				add_triangle(i00, i10, i01);
				add_triangle(i10, i11, i01);
			}
		}
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

void Patch::GenerateMesh(SizedArray<Vec3f>& positions, SizedArray<Vec3f>& normals, SizedArray<Vec3f>& tangents,
						 SizedArray<float32>& tangent_handedness, SizedArray<Vec2f>& texcoords,
						 SizedArray<uint32>& indices) const
{
	Patch pieces[scMaxClosurePieces];
	const uint32 piece_count = BuildClosure(pieces);

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

	for (const PatchMesh& mesh : meshes) {
		const uint32 base = static_cast<uint32>(positions.Size);

		for (const PatchVertex& vertex : mesh.Vertices) {
			positions.Insert(ToFloat(vertex.Position));
			normals.Insert(ToFloat(vertex.Normal));
			tangents.Insert(ToFloat(vertex.Tangent));
			tangent_handedness.Insert(static_cast<float32>(vertex.Handedness));
			texcoords.Insert(Vec2f(static_cast<float32>(vertex.Texcoord.X), static_cast<float32>(vertex.Texcoord.Y)));
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
}

void Patch::Transpose()
{
	SizedArray<PatchPoint> points(Points.Size);

	for (uint32 column = 0; column < Width; column++) {
		for (uint32 row = 0; row < Height; row++) {
			points.Insert(GetPoint(column, row));
		}
	}

	std::swap(Width, Height);
	std::swap(SubdivisionsU, SubdivisionsV);

	Points = std::move(points);
}

bool Patch::InsertColumns(uint32 sub_column)
{
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
		if ((a[i].Position - b[i].Position).Length() > scEdgeMatchDistance) {
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
					uint32 subdivisions, const Vec3d& outward)
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

	const Vec3f ring[3] = { start.Position, corner.Position, end.Position };

	for (uint32 column = 0; column < cap.Width; column++) {
		cap.GetPoint(column, 0).Position = ring[column];
		cap.GetPoint(column, 1).Position = (ring[column] + fill) * 0.5f;
		cap.GetPoint(column, 2).Position = fill;
	}

	cap.SubdivisionsU = subdivisions;
	cap.SubdivisionsV = 1;
	cap.bSmooth = false;

	OrientOutward(cap, outward);
	cap.WorldTexture();

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
	}

	bridge.SubdivisionsU = subdivisions;
	bridge.SubdivisionsV = 1;

	return bridge;
}

} // namespace

void Patch::WorldTexture(float32 scale)
{
	if (!IsValid() || scale == 0.0f) {
		return;
	}

	const PatchVertex middle = Evaluate(static_cast<double>(GetSubPatchColumns()) * 0.5,
										static_cast<double>(GetSubPatchRows()) * 0.5);

	const double x = std::fabs(middle.Normal.X);
	const double y = std::fabs(middle.Normal.Y);
	const double z = std::fabs(middle.Normal.Z);

	for (PatchPoint& point : Points) {
		const Vec3f position = point.Position;

		if (y >= x && y >= z) {
			point.Texcoord = Vec2f(position.X / scale, position.Z / scale);
		}
		else if (x >= z) {
			point.Texcoord = Vec2f(position.Z / scale, -position.Y / scale);
		}
		else {
			point.Texcoord = Vec2f(position.X / scale, -position.Y / scale);
		}
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

	for (uint32 row = 0; row < Height; row++) {
		for (uint32 column = 0; column < Width; column++) {
			const Vec3d normal = Evaluate(static_cast<double>(column) * 0.5, static_cast<double>(row) * 0.5).Normal;

			offset.GetPoint(column, row).Position = GetPoint(column, row).Position - ToFloat(normal) * distance;
		}
	}

	offset.Invert();

	return offset;
}

Patch Patch::MakeCap(uint32 edge) const
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
	}

	cap.SubdivisionsU = GetEdgeSubdivisions(*this, edge);
	cap.SubdivisionsV = 1;
	cap.bSmooth = false;

	OrientOutward(cap, GetEdgeOutward(edge));
	cap.WorldTexture();

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

uint32 Patch::Cap(Patch out_caps[scMaxCapPieces]) const
{
	uint32 count = 0;

	SizedArray<PatchPoint> points;

	for (uint32 edge = 0; edge < scEdgeCount; edge++) {
		if (IsEdgeClosed(edge)) {
			continue;
		}

		if (IsEdgeRing(edge)) {
			Patch cap = MakeCap(edge);

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

			const double along = static_cast<double>(span / 2) + 0.5;

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

			const Vec3f middle = (start.Position + corner.Position * 2.0f + end.Position) * 0.25f;
			const Vec3d normal = Evaluate(u, v).Normal;

			Vec3f fill = corner.Position;

			if (normal.Dot(ToDouble(corner.Position - middle)) > 0.0) {
				fill = start.Position + end.Position - corner.Position;
			}

			Patch cap = MakeCornerCap(start, corner, end, fill, subdivisions, outward);

			if (cap.IsValid()) {
				out_caps[count++] = std::move(cap);
			}
		}
	}

	return count;
}

uint32 Patch::BuildClosure(Patch out_pieces[scMaxClosurePieces]) const
{
	uint32 count = 0;

	if (Closure == ePatchClosure::Cap) {
		count = Cap(out_pieces);
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
