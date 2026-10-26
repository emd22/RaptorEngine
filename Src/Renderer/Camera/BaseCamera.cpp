#include "BaseCamera.hpp"

#include <cmath>

namespace fx {

void BaseCamera::RebuildViewMatrix()
{
	Mat4f basis = Mat4f::AsRotation(mRotation);

	const Vec3f right { basis.Rows[0] };
	const Vec3f up { basis.Rows[1] };
	const Vec3f forward { basis.Rows[2] };

	// Note, not identity row (0.0, 0.0, 0.0, 1.0) as we sub the 1.0 in at the end
	basis.Rows[3] = Vec4f(0.0f);

	// Transpose the matrix, copy the result to mViewMatrix. This is to prevent having a ton of separate load
	// operations here.
	basis.TransposeTo(mViewMatrix);

	// Set the translation
	mViewMatrix.Rows[3].Set(-mPosition.Dot(right), -mPosition.Dot(up), -mPosition.Dot(forward), 1.0f);

	mbTransformDirty = false;
}

static Quat QuatFromBasis(const Vec3f right, const Vec3f up, const Vec3f forward)
{
	const float32 trace = right.X + up.Y + forward.Z;

	if (trace > 0.0f) {
		const float32 s = 0.5f / std::sqrt(trace + 1.0f);
		return Quat((up.Z - forward.Y) * s, (forward.X - right.Z) * s, (right.Y - up.X) * s, 0.25f / s);
	}

	if (right.X > up.Y && right.X > forward.Z) {
		const float32 s = 2.0f * std::sqrt(1.0f + right.X - up.Y - forward.Z);
		return Quat(0.25f * s, (right.Y + up.X) / s, (right.Z + forward.X) / s, (up.Z - forward.Y) / s);
	}

	if (up.Y > forward.Z) {
		const float32 s = 2.0f * std::sqrt(1.0f + up.Y - right.X - forward.Z);
		return Quat((right.Y + up.X) / s, 0.25f * s, (up.Z + forward.Y) / s, (forward.X - right.Z) / s);
	}

	const float32 s = 2.0f * std::sqrt(1.0f + forward.Z - right.X - up.Y);
	return Quat((right.Z + forward.X) / s, (up.Z + forward.Y) / s, 0.25f * s, (right.Y - up.X) / s);
}

void BaseCamera::LookAt(const Vec3f target, const Vec3f up)
{
	const Vec3f forward = (target - mPosition).Normalize();

	Vec3f right = up.Cross(forward);

	if (right.Dot(right) < 1e-8f) {
		right = Vec3f::sForward.Cross(forward);
	}

	right.NormalizeIP();

	SetRotation(QuatFromBasis(right, forward.Cross(right), forward));
}


} // namespace fx
