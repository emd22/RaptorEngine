#pragma once

#include <ThirdParty/Jolt/Jolt.h>
// Jolt headers
#include <ThirdParty/Jolt/Geometry/IndexedTriangle.h>
#include <ThirdParty/Jolt/Math/Float3.h>
#include <ThirdParty/Jolt/Physics/Collision/Shape/MeshShape.h>
#include <ThirdParty/Jolt/Physics/Collision/Shape/Shape.h>

#include <Core/MemberRef.hpp>
#include <Core/SizedArray.hpp>
#include <Renderer/PrimitiveMesh.hpp>

namespace fx {

class PhMesh
{
public:
    PhMesh(const PrimitiveMesh& mesh);

    JPH::MeshShapeSettings GetShapeSettings() const;

public:
    JPH::VertexList VertexList;
    JPH::IndexedTriangleList TriangleList;
};

} // namespace fx
