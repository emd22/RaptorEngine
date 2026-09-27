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
#include <Renderer/PipelineKey.hpp>
#include <Renderer/PipelineNames.hpp>

#include <memory>
#include <vector>


namespace fx {
class Object;
struct ObjectID;

namespace renderer {

struct RenderListSection
{
	DynArray<ObjectID> Objects;
};

class RenderList
{
public:
	static constexpr uint32 scNotFound = UINT32_MAX;

public:
	RenderList() = default;

	void AddObject(PipelineHandle pipeline, const ObjectID id);
	/**
	 * @brief Invalidate object from renderlist. Use when an object is going to be destroyed or will be invalid before
	 * the next renderlist rebuild.
	 */
	void InvalidateObject(const ObjectID id);
	void ClearSection(PipelineHandle pipeline);

	int32 CheckForObjectDuplicates(const ObjectID id) const;

	uint32 GetItemCount() const;

	/**
	 * @brief The objects drawn with a pipeline. Sections are made when they are first asked for, and stay where they are.
	 */
	RenderListSection& GetSection(PipelineHandle pipeline);

private:
	/// Indexed by pipeline handle. Not every pipeline has a section, so some are null.
	std::vector<std::unique_ptr<RenderListSection>> mSections;
};

} // namespace renderer
} // namespace fx
