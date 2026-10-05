#pragma once

#include <raptor_ffi.h>
#include <vulkan/vulkan.h>

#include <Color.hpp>
#include <Core/SizedArray.hpp>
#include <Core/Types.hpp>

namespace fx::renderer {

union PipelineBlendFactor
{
	struct
	{
		VkBlendFactor Src : 8;
		VkBlendFactor Dst : 8;
	} Ops;

	uint16 Value;
};

union PipelineBlendOp
{
	struct
	{
		VkBlendOp Alpha : 8;
		VkBlendOp Color : 8;
	} Ops;

	uint16 Value;
};


struct BlendAttachment
{
	bool Enabled = false;
	eColorComponent Mask = eColorComponent::RGBA;
	PipelineBlendOp BlendOp = { .Ops = { .Alpha = VK_BLEND_OP_ADD, .Color = VK_BLEND_OP_ADD } };

	PipelineBlendFactor AlphaBlend = { .Ops = { .Src = VK_BLEND_FACTOR_ZERO, .Dst = VK_BLEND_FACTOR_ZERO } };
	PipelineBlendFactor ColorBlend = { .Ops = { .Src = VK_BLEND_FACTOR_ZERO, .Dst = VK_BLEND_FACTOR_ZERO } };

	uint16 TargetIndex = 0;
};


} // namespace fx::renderer
