#pragma once

#include "BaseOperation.hpp"

#include <BrushEntry.hpp>
#include <vector>


namespace fx {


namespace editor {


/////////////////////////////////////
// Subviews
/////////////////////////////////////

struct SubviewBrushRef
{
	Object* pObject = nullptr;
	ObjectID ID = ObjectID::scNull;
};

/**
 * @brief Places an existing subview into the current prototype.
 */
class SubviewPlaceOperation final : public EditOperation
{
public:
	SubviewPlaceOperation(uint32 instance, const BrushPlacement& placement, int32 group_size = 1);

	void Execute(EditorSelection& selection) override;
	void Undo(EditorSelection& selection) override;

	bool ChangesObjects() const override { return true; }

private:
	uint32 mInstance;
	BrushPlacement mPlacement;
};

/**
 * @brief Removes a subview from the current prototype.
 */
class SubviewRemoveOperation final : public EditOperation
{
public:
	explicit SubviewRemoveOperation(uint32 instance, int32 group_size = 1);

	void Execute(EditorSelection& selection) override;
	void Undo(EditorSelection& selection) override;

	bool ChangesObjects() const override { return true; }

private:
	uint32 mInstance;
	BrushPlacement mPlacement;
};

/**
 * @brief Creates a new subview from a list of objects (e.g. from selection).
 */
class SubviewCreateOperation final : public EditOperation
{
public:
	SubviewCreateOperation(uint32 instance, Object* const* objects, uint32 count, const BrushPlacement& placement);

	void Execute(EditorSelection& selection) override;
	void Undo(EditorSelection& selection) override;

	bool ChangesObjects() const override { return true; }

private:
	uint32 mInstance;
	BrushPlacement mPlacement;
	std::vector<SubviewBrushRef> mBrushes;
	std::vector<uint32> mInstances;
};

/**
 * @brief Copies all objects in a subview into the current prototype to be modified locally without modifying all
 * copies.
 */
class SubviewDissolveOperation final : public EditOperation
{
public:
	explicit SubviewDissolveOperation(uint32 instance, int32 group_size = 1);

	void Execute(EditorSelection& selection) override;
	void Undo(EditorSelection& selection) override;

	bool ChangesObjects() const override { return true; }

private:
	uint32 mInstance;
	BrushPlacement mPlacement;
	std::vector<SubviewBrushRef> mBrushes;
	std::vector<uint32> mInstances;
};


} // namespace editor
} // namespace fx
