#pragma once

#include "BaseOperation.hpp"

#include <vector>


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
	LightTransformOperation(LightSpot* light, const Vec3f& position_before, const Vec3f& position_after,
							const Vec3f& direction_before, const Vec3f& direction_after);

	void Execute(EditorSelection& selection) override;
	void Undo(EditorSelection& selection) override;

private:
	Vec3f mPositionBefore;
	Vec3f mPositionAfter;
	Vec3f mDirectionBefore;
	Vec3f mDirectionAfter;
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
