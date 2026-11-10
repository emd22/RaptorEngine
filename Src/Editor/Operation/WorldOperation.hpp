#pragma once

#include "BaseOperation.hpp"

namespace fx {
namespace editor {


class SpawnTransformOperation final : public EditOperation
{
public:
	struct State
	{
		FVec3 Position = FVec3::sZero;
		FVec3 Direction = FVec3::sForward;
		bool bCustom = false;
	};

public:
	SpawnTransformOperation(const State& before, const State& after);

	void Execute(EditorSelection& selection) override;
	void Undo(EditorSelection& selection) override;

private:
	State mBefore;
	State mAfter;
};


} // namespace editor
} // namespace fx
