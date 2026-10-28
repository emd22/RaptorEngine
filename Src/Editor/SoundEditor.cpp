#include "SoundEditor.hpp"

#ifdef FX_IS_EDITOR

#include "RaptorEditor.hpp"

#include <Audio/SoundEmitters.hpp>
#include <Editor/Operation/SoundOperation.hpp>
#include <Engine.hpp>
#include <Renderer/DebugDraw.hpp>
#include <Renderer/Globals.hpp>
#include <World.hpp>

namespace fx::editor {

static constexpr float32 scEditTolerance = 0.0005f;

SoundEmitter* SoundEditor::GetSelected()
{
	if (mSelectedId == 0) {
		return nullptr;
	}

	SoundEmitter* emitter = gWorld->SoundEmitters.Find(mSelectedId);

	if (emitter == nullptr) {
		mSelectedId = 0;
	}

	return emitter;
}

void SoundEditor::Select(uint32 id) { mSelectedId = id; }

void SoundEditor::Deselect() { mSelectedId = 0; }

void SoundEditor::MoveSelected(const Vec3f offset)
{
	const SoundEmitter* emitter = GetSelected();

	if (emitter == nullptr || offset.IsCloseTo(Vec3f::sZero, scEditTolerance)) {
		return;
	}

	SoundEmitter after = *emitter;
	after.Position = emitter->Position + offset;

	gEditor->EmplaceEditOperation<SoundEmitterEditOperation>(*emitter, after);
}

void SoundEditor::EditSelected(const SoundEmitter& after)
{
	const SoundEmitter* emitter = GetSelected();

	if (emitter == nullptr || *emitter == after) {
		return;
	}

	mLastSound = after.Sound;

	gEditor->EmplaceEditOperation<SoundEmitterEditOperation>(*emitter, after);
}

SoundEmitter* SoundEditor::Create(const Vec3f position)
{
	SoundEmitter emitter;
	emitter.Id = gWorld->SoundEmitters.NewId();
	emitter.Name = gWorld->SoundEmitters.NewName();
	emitter.Sound = mLastSound;
	emitter.Position = position;

	gEditor->EmplaceEditOperation<SoundEmitterCreateOperation>(emitter);

	Select(emitter.Id);

	return GetSelected();
}

void SoundEditor::DeleteSelected()
{
	const SoundEmitter* emitter = GetSelected();

	if (emitter == nullptr) {
		return;
	}

	gEditor->EmplaceEditOperation<SoundEmitterDeleteOperation>(*emitter);

	Deselect();
}

void SoundEditor::DuplicateSelected()
{
	const SoundEmitter* emitter = GetSelected();

	if (emitter == nullptr) {
		return;
	}

	SoundEmitter copy = *emitter;
	copy.Id = gWorld->SoundEmitters.NewId();
	copy.Name = gWorld->SoundEmitters.NewName();

	gEditor->EmplaceEditOperation<SoundEmitterCreateOperation>(copy);

	Select(copy.Id);
}

void SoundEditor::DrawMarkers()
{
	const EditorTheme::MarkerColors& colors = gEditor->GetTheme().Marker;

	for (const SoundEmitter& emitter : gWorld->SoundEmitters.GetEmitters()) {
		Color color = colors.Sound;

		if (emitter.Id == mSelectedId) {
			color = colors.SoundSelected;
		}

		renderer::gDebugDraw->SolidBox(emitter.Position, Vec3f(scMarkerHalfExtent), Quat::scIdentity, color);
	}
}

} // namespace fx::editor

#endif
