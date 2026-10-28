#pragma once

#ifdef FX_IS_EDITOR

#include <Core/String.hpp>
#include <Core/Types.hpp>
#include <Math/Vec3.hpp>

namespace fx {

struct SoundEmitter;

} // namespace fx

namespace fx::editor {

class SoundEditor
{
public:
	static constexpr float32 scMarkerHalfExtent = 0.15f;

	SoundEmitter* GetSelected();
	uint32 GetSelectedId() const { return mSelectedId; }

	void Select(uint32 id);
	void Deselect();

	void MoveSelected(const Vec3f offset);
	void EditSelected(const SoundEmitter& after);

	SoundEmitter* Create(const Vec3f position);
	void DeleteSelected();
	void DuplicateSelected();

	void DrawMarkers();

private:
	uint32 mSelectedId = 0;
	String mLastSound;
};

} // namespace fx::editor

#endif
