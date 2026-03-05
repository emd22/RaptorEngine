#pragma once

#include "Backend/RxPipeline.hpp"
#include "Backend/RxShader.hpp"
#include "RxVertex.hpp"

#include <Core/FxHash.hpp>
#include <Core/FxRef.hpp>
#include <unordered_map>

class RxRenderPass;

class RxRenderState
{
    struct State
    {
        FxStackArray<RxShaderProgram*, RxShaderUtil::scNumShaderTypes> Shaders;
        RxVertexType VertexType = RxVertexType::eSlim;
    };

public:
    RxRenderState();

    void SetRenderPass(const RxRenderPass& render_pass);
    void SetShader(const RxShaderType type, const FxRef<RxShaderProgram>& program);
    void SetVertexType(const RxVertexType vertex_type);
    void SetPushConstants(const FxSlice<RxPushConstants>& push_constants);

    void Finalize();

    ~RxRenderState();

private:
    State mState;

    FxStackArray<RxPushConstants, RxShaderUtil::scNumShaderTypes> mPushConstants;
    std::unordered_map<FxHash64, FxRef<RxPipeline>> mPipelines;
};
