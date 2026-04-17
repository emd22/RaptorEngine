#pragma once

#include "Backend/Pipeline.hpp"
#include "Backend/Shader.hpp"

#include <Core/Hash.hpp>
#include <Core/Ref.hpp>
#include <Core/StackArray.hpp>

// namespace fx::renderer {

// class RenderPass;

// class State
// {
// public:
//     State();

//     void BufferOffset(eShaderType shader_type, uint32 buffer_offset);
//     void Pipeline(Pipeline* pipeline);
//     void RenderPass(RenderPass* rp);

//     void Apply(const CommandBuffer& cmd);

//     void Reset();

//     ~State();

// private:
//     Pipeline* mpPipeline;
//     RenderPass* mpRenderPass = nullptr;

//     StackArray<ShaderBindOptions, ShaderUtil::scNumShaderTypes> mBindOptions;
// };

// } // namespace fx::renderer
