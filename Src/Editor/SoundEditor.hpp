#pragma once

#ifdef FX_IS_EDITOR

#include <Core/String.hpp>
#include <Core/Types.hpp>
#include <Math/FVec3.hpp>

namespace fx {

struct SoundEmitter;

} // namespace fx

namespace fx::editor {

class SoundEditor
{
public:
	SoundEmitter* GetSelected();
	uint32 GetSelectedId() const { return mSelectedId; }

	void Select(uint32 id);
	void Deselect();

	void MoveSelected(const FVec3 offset);
	void EditSelected(const SoundEmitter& after);

	SoundEmitter* Create(const FVec3 position);
	void DeleteSelected();
	void DuplicateSelected();

	void DrawMarkers();

private:
	uint32 mSelectedId = 0;
	String mLastSound;
};

} // namespace fx::editor

#endif
