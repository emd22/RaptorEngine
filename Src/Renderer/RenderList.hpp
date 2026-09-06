/*
 * File:        RenderList.hpp
 * Author:      emd22
 * Created:     01/06/2026
 * Description: Provides a container for renderable objects
 */

#pragma once

#include <Core/Bitset.hpp>
#include <Core/DynArray.hpp>
#include <Core/SizedArray.hpp>
#include <Core/Types.hpp>
#include <Renderer/PipelineNames.hpp>


namespace fx {
class Object;
struct ObjectID;

namespace renderer {

struct RenderListSection
{
	DynArray<ObjectID> Objects;
	Bitset InUse;
};

class RenderList
{
public:
	static constexpr uint32 scNotFound = UINT32_MAX;

public:
	RenderList() = default;

	uint32 Add(ePipelineName pl_name, const ObjectID id);

	void Remove(ePipelineName pl_name, const ObjectID id);
	void RemoveAllOfObject(const ObjectID id);

	int32 CheckForObjectDuplicates(const ObjectID id) const;


	/**
	 * @brief Gets the object's index in the given pipeline (section) of the renderlist. Returns scNotFound if not
	 * found.
	 */
	uint32 GetObjectIndex(ePipelineName pl_name, const ObjectID id) const;

	RenderListSection& GetSection(ePipelineName pl_name);

private:
	SizedArray<RenderListSection> mSections;
};

} // namespace renderer
} // namespace fx
