#include "RenderList.hpp"

#include <Object/ObjectID.hpp>

namespace fx::renderer {

static constexpr uint32 scMaxRenderable = 1024;

uint32 RenderList::Add(ePipelineName pl_name, const ObjectID& id)
{
    RenderListSection& section = mSections[static_cast<uint32>(pl_name)];
    if (!section.InUse.IsInited()) {
        section.InUse.InitZero(scMaxRenderable);
    }

    uint32 index = section.InUse.FindNextFreeBit();

    if (index >= section.Objects.Size) {
        section.Objects.Insert(id);
    }
    else {
        section.Objects[index] = id;
    }

    section.InUse.Set(index);

    return index;
}

void RenderList::Remove(ePipelineName pl_name, const ObjectID& id)
{
    RenderListSection& section = mSections[static_cast<uint32>(pl_name)];

    for (uint32 object_index = 0; object_index < section.Objects.Size; object_index++) {
        if (section.Objects[object_index] == id) {
            section.InUse.Unset(object_index);
        }
    }
}

void RenderList::RemoveAllOfObject(const ObjectID& id)
{
    for (uint32 si = 0; si < scNumPipelines; si++) {
        Remove(static_cast<ePipelineName>(si), id);
    }
}


} // namespace fx::renderer
