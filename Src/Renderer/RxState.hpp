#pragma once

#include "Backend/RxPipeline.hpp"
#include "Backend/RxShader.hpp"

#include <Core/Hash.hpp>
#include <Core/Ref.hpp>
#include <Core/StackArray.hpp>

namespace fx::renderer {

class RxRenderPass;

class RxState
{
public:
    RxState();

    void BufferOffset(RxShaderType shader_type, uint32 buffer_offset);
    void Pipeline(RxPipeline* pipeline);
    void RenderPass(RxRenderPass* rp);

    void Apply(const RxCommandBuffer& cmd);

    void Reset();

    ~RxState();

private:
    RxPipeline* mpPipeline;
    RxRenderPass* mpRenderPass = nullptr;

    StackArray<RxShaderBindOptions, RxShaderUtil::scNumShaderTypes> mBindOptions;
};

} // namespace fx::renderer
