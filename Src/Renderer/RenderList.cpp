#include "RenderList.hpp"

namespace fx::renderer {

static constexpr uint32 scMaxRenderable = 1024;

uint32 RenderList::Insert(ePipelineName pl_name, PrimitiveMesh* mesh, Material* material)
{
    RenderListSection& section = mSections[static_cast<uint32>(pl_name)];
    if (!section.InUse.IsInited()) {
        section.InUse.InitZero(scMaxRenderable);
    }

    uint32 index = section.InUse.FindNextFreeBit();


    if (index > section.Meshes.Size) {
        section.Meshes.Insert(mesh);
        section.Materials.Insert(material);
    }
    else {
        section.Meshes[index] = mesh;
        section.Materials[index] = material;
    }

    section.InUse.Set(index);

    return index;
}

void RenderList::Remove(ePipelineName pl_name, PrimitiveMesh* mesh)
{
    RenderListSection& section = mSections[static_cast<uint32>(pl_name)];

    for (uint32 mesh_index = 0; mesh_index < section.Meshes.Size; mesh_index++) {
        if (section.Meshes[mesh_index] == mesh) {
            section.InUse.Unset(mesh_index);
        }
    }
}

void RenderList::RemoveAllOfMesh(PrimitiveMesh* mesh)
{
    for (uint32 si = 0; si < scNumPipelines; si++) {
        Remove(static_cast<ePipelineName>(si), mesh);
    }
}


} // namespace fx::renderer
