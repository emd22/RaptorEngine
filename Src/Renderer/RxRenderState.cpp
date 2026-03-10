#include "RxRenderState.hpp"

#include <Core/FxSlice.hpp>
#include <Renderer/Backend/RxRenderPass.hpp>
#include <Renderer/Backend/RxShader.hpp>
#include <Renderer/RxPipelineBuilder.hpp>

RxRenderState::RxRenderState() { mState.Shaders.MarkFull(); }


void RxRenderState::SetRenderPass(const RxRenderPass& render_pass) {}

void RxRenderState::SetShader(const RxShaderType type, const FxRef<RxShaderProgram>& program)
{
    mState.Shaders[static_cast<uint32>(type)] = program.mpPtr;
}

void RxRenderState::SetVertexType(const RxVertexType vertex_type) { mState.VertexType = vertex_type; }

void RxRenderState::Finalize()
{
    FxHash64 state_hash = FxHashData64(FxSlice<State>(&mState, sizeof(State)));

    // Find a matching pipeline
    auto it = mPipelines.find(state_hash);

    FxRef<RxPipeline> pipeline = it->second;

    if (it == mPipelines.end()) {
        // Pipeline has not been created
        pipeline = FxMakeRef<RxPipeline>();

        RxPipelineBuilder builder;


        // builder.SetLayout();
    }
}


RxRenderState::~RxRenderState() {}
