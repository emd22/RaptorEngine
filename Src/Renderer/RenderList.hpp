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
    RenderList() = default;

    uint32 Add(ePipelineName pl_name, const ObjectID& id);

    void Remove(ePipelineName pl_name, const ObjectID& id);
    void RemoveAllOfObject(const ObjectID& id);

    const RenderListSection& GetSection(ePipelineName pl_name) { return mSections[static_cast<uint32>(pl_name)]; }

private:
    SizedArray<RenderListSection> mSections;
};

} // namespace renderer
} // namespace fx
