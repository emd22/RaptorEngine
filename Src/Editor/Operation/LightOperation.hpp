#pragma once

#include "BaseOperation.hpp"


namespace fx {


namespace editor {


/////////////////////////////////////
// Light operations
/////////////////////////////////////

/**
 * @brief Transform a spotlight in the scene. This can be movement or direction.
 */
class LightTransformOperation final : public LightEditOperation
{
public:
	LightTransformOperation(LightSpot* light, const FVec3& position_before, const FVec3& position_after,
							const FVec3& direction_before, const FVec3& direction_after);

	void Execute(EditorSelection& selection) override;
	void Undo(EditorSelection& selection) override;

private:
	FVec3 mPositionBefore;
	FVec3 mPositionAfter;
	FVec3 mDirectionBefore;
	FVec3 mDirectionAfter;
};

/**
 * @brief Creates a new light in the current prototype.
 */
class LightCreateOperation final : public LightEditOperation
{
public:
	explicit LightCreateOperation(const LightSnapshot& snapshot);

	void Execute(EditorSelection& selection) override;
	void Undo(EditorSelection& selection) override;

	LightSpot* GetCreated() const { return ResolveLight(); }

private:
	LightSnapshot mSnapshot;
};


/**
 * @brief Deletes an existing light from the prototype.
 */
class LightDeleteOperation final : public LightEditOperation
{
public:
	explicit LightDeleteOperation(LightSpot* light);

	void Execute(EditorSelection& selection) override;
	void Undo(EditorSelection& selection) override;

private:
	LightSnapshot mSnapshot;
};


} // namespace editor
} // namespace fx
