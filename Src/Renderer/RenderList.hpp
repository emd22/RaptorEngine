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
namespace renderer {

class PrimitiveMesh;
class Material;

struct RenderListSection
{
    DynArray<PrimitiveMesh*> Meshes;
    DynArray<Material*> Materials;
    Bitset InUse;
};

class RenderList
{
public:
    RenderList() = default;

    uint32 Insert(ePipelineName pl_name, PrimitiveMesh* mesh, Material* material);

    void Remove(ePipelineName pl_name, PrimitiveMesh* mesh);
    void RemoveAllOfMesh(PrimitiveMesh* mesh);

    const RenderListSection& GetSection(ePipelineName pl_name) { return mSections[static_cast<uint32>(pl_name)]; }

private:
    RenderListSection mSections[scNumPipelines];
};

} // namespace renderer
} // namespace fx
