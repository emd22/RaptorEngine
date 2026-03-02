#include "RxVertex.hpp"


/////////////////////////////////////
// Vertex List Functions
/////////////////////////////////////


void RxVertexList::CreateSlimFrom(const FxSizedArray<float32>& positions)
{
    FxAssert(positions.Size > 0);
    FxAssert((positions.Size % 3) == 0);

    VertexType = RxVertexType::eSlim;

    mLocalBuffer.Create(sizeof(RxVertex<RxVertexType::eSlim>), positions.Size / 3);

    for (int i = 0; i < mLocalBuffer.Capacity; i++) {
        RxVertex<RxVertexType::eSlim> vertex;
        memcpy(&vertex.Position, &positions.pData[i * 3], sizeof(float32) * 3);

        mLocalBuffer.Insert(vertex);
    }
}

void RxVertexList::CreateSlimFrom(const FxSizedArray<FxVec3f>& positions)
{
    FxAssert(positions.Size > 0);

    VertexType = RxVertexType::eSlim;

    mLocalBuffer.Create(sizeof(RxVertex<RxVertexType::eSlim>), positions.Size);

    for (int i = 0; i < mLocalBuffer.Capacity; i++) {
        RxVertex<RxVertexType::eSlim> vertex;
        memcpy(&vertex.Position, &positions.pData[i].mData, sizeof(float32) * 3);

        mLocalBuffer.Insert(vertex);
    }
}
