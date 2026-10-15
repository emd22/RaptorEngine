#pragma once

#ifdef FX_IS_EDITOR

#include <Core/Types.hpp>
#include <Math/Vec3.hpp>

namespace fx::editor {

class SpawnEditor
{
public:
	static constexpr float32 scHalfWidth = 0.3f;

	void Move(const Vec3f offset);
	void Aim(const Vec3f direction);

	void ResetToDefault();

	void DrawMarker(bool selected);

	static float32 GetBodyHeight();
	static float32 GetEyeHeight();

private:
	void Push(const Vec3f position, const Vec3f direction, bool custom);
};

} // namespace fx::editor

#endif
