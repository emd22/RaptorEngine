#pragma once

#ifdef FX_IS_EDITOR

#include <Core/Types.hpp>
#include <Math/FVec3.hpp>

namespace fx::editor {

class SpawnEditor
{
public:
	static constexpr float32 scHalfWidth = 0.3f;

	void Move(const FVec3 offset);
	void Aim(const FVec3 direction);

	void ResetToDefault();

	void DrawMarker(bool selected);

	static float32 GetBodyHeight();
	static float32 GetEyeHeight();

private:
	void Push(const FVec3 position, const FVec3 direction, bool custom);
};

} // namespace fx::editor

#endif
