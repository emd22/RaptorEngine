/*
 * File:        RenderList.hpp
 * Author:      emd22
 * Created:     01/06/2026
 * Description: Provides a container for renderable objects
 */

#pragma once

#include <Core/Bitset.hpp>
#include <Core/DynArray.hpp>
#include <Core/Types.hpp>
#include <Renderer/PipelineNames.hpp>

namespace fx {
class Object;

namespace renderer {

struct RenderListSection
{
    DynArray<Object*> Objects;
    Bitset InUse;
};

class RenderList
{
public:
    RenderList() = default;

    uint32 Insert(ePipelineName pl_name, Object* object);

    void Remove(ePipelineName pl_name, Object* mesh);
    void RemoveAllOfObject(Object* mesh);

    const RenderListSection& GetSection(ePipelineName pl_name) { return mSections[static_cast<uint32>(pl_name)]; }

private:
    std::array<RenderListSection, scNumPipelines> mSections;
};

} // namespace renderer
} // namespace fx
