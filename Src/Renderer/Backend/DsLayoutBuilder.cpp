#include "DsLayoutBuilder.hpp"

#include <Renderer/Globals.hpp>
#include <Renderer/RenderBackend.hpp>

namespace fx::renderer {

DsLayoutBuilder& DsLayoutBuilder::AddBinding(int binding, VkDescriptorType type, eShaderType stage, int count)
{
    const VkSampler* pcImmutableSamplers = nullptr;

    mLayoutBindings.emplace_back(binding, type, count, ShaderUtil::ToUnderlyingType(stage), pcImmutableSamplers);

    return *this;
}


VkDescriptorSetLayout DsLayoutBuilder::Build()
{
    VkDescriptorSetLayoutCreateInfo create_info {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = static_cast<uint32>(mLayoutBindings.size()),
        .pBindings = mLayoutBindings.data(),
    };

    VkResult status = vkCreateDescriptorSetLayout(gRenderer->GetDevice()->Device, &create_info, nullptr, &mpDsLayout);

    if (status != VK_SUCCESS) {
        LogError("Error building descriptor set layout with builder! (status={})", Util::ResultToStr(status));
        return nullptr;
    }

    return mpDsLayout;
}

} // namespace fx::renderer
