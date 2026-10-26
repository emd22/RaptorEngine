#include "SoundOperation.hpp"

#include <Engine.hpp>
#include <World.hpp>

namespace fx {
namespace editor {

SoundEmitterCreateOperation::SoundEmitterCreateOperation(const SoundEmitter& emitter)
	: EditOperation(1), mEmitter(emitter)
{
}

void SoundEmitterCreateOperation::Execute(EditorSelection& selection) { gWorld->SoundEmitters.Add(mEmitter); }

void SoundEmitterCreateOperation::Undo(EditorSelection& selection) { gWorld->SoundEmitters.Remove(mEmitter.Id); }


SoundEmitterDeleteOperation::SoundEmitterDeleteOperation(const SoundEmitter& emitter)
	: EditOperation(1), mEmitter(emitter)
{
}

void SoundEmitterDeleteOperation::Execute(EditorSelection& selection) { gWorld->SoundEmitters.Remove(mEmitter.Id); }

void SoundEmitterDeleteOperation::Undo(EditorSelection& selection) { gWorld->SoundEmitters.Add(mEmitter); }


SoundEmitterEditOperation::SoundEmitterEditOperation(const SoundEmitter& before, const SoundEmitter& after)
	: EditOperation(1), mBefore(before), mAfter(after)
{
}

void SoundEmitterEditOperation::Execute(EditorSelection& selection) { gWorld->SoundEmitters.Replace(mAfter); }

void SoundEmitterEditOperation::Undo(EditorSelection& selection) { gWorld->SoundEmitters.Replace(mBefore); }


} // namespace editor
} // namespace fx
