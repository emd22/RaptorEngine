#include "RenderList.hpp"

namespace fx::renderer {

static constexpr uint32 scMaxRenderable = 1024;

uint32 RenderList::Insert(ePipelineName pl_name, Object* object)
{
    RenderListSection& section = mSections[static_cast<uint32>(pl_name)];
    if (!section.InUse.IsInited()) {
        section.InUse.InitZero(scMaxRenderable);
    }

    uint32 index = section.InUse.FindNextFreeBit();


    if (index >= section.Objects.Size) {
        section.Objects.Insert(object);
    }
    else {
        section.Objects[index] = object;
    }

    section.InUse.Set(index);

    return index;
}

void RenderList::Remove(ePipelineName pl_name, Object* mesh)
{
    RenderListSection& section = mSections[static_cast<uint32>(pl_name)];

    for (uint32 mesh_index = 0; mesh_index < section.Objects.Size; mesh_index++) {
        if (section.Objects[mesh_index] == mesh) {
            section.InUse.Unset(mesh_index);
        }
    }
}

void RenderList::RemoveAllOfObject(Object* object)
{
    for (uint32 si = 0; si < scNumPipelines; si++) {
        Remove(static_cast<ePipelineName>(si), object);
    }
}


} // namespace fx::renderer
