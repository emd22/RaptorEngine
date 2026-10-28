#pragma once

#include "BaseOperation.hpp"

#include <Audio/SoundEmitters.hpp>

namespace fx {
namespace editor {


class SoundEmitterCreateOperation final : public EditOperation
{
public:
	explicit SoundEmitterCreateOperation(const SoundEmitter& emitter);

	void Execute(EditorSelection& selection) override;
	void Undo(EditorSelection& selection) override;

private:
	SoundEmitter mEmitter;
};


class SoundEmitterDeleteOperation final : public EditOperation
{
public:
	explicit SoundEmitterDeleteOperation(const SoundEmitter& emitter);

	void Execute(EditorSelection& selection) override;
	void Undo(EditorSelection& selection) override;

private:
	SoundEmitter mEmitter;
};


class SoundEmitterEditOperation final : public EditOperation
{
public:
	SoundEmitterEditOperation(const SoundEmitter& before, const SoundEmitter& after);

	void Execute(EditorSelection& selection) override;
	void Undo(EditorSelection& selection) override;

private:
	SoundEmitter mBefore;
	SoundEmitter mAfter;
};


} // namespace editor
} // namespace fx
