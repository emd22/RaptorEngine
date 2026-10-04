#include "DsLayoutBuilder.hpp"

#include <Renderer/Globals.hpp>
#include <Renderer/GraphicsBackend.hpp>

namespace fx::renderer {

DsLayoutBuilder& DsLayoutBuilder::AddBinding(int binding, VkDescriptorType type, eShaderType stage, int count)
{
	mLayoutBindings.push_back(RxDsLayoutEntry {
		.binding = static_cast<uint32>(binding),
		.descriptor_type = type,
		.stages = ShaderUtil::ToUnderlyingType(stage),
		.count = static_cast<uint32>(count),
	});

	return *this;
}


VkDescriptorSetLayout DsLayoutBuilder::Build()
{
	uint64 handle = 0;

	const VkResult status = static_cast<VkResult>(rx_gpu_ds_layout_create(
		gGraphics->GetDevice()->GetRustDevice(), mLayoutBindings.data(), mLayoutBindings.size(), &handle));

	if (status != VK_SUCCESS) {
		LogError("Error building descriptor set layout with builder! (status={})", Util::ResultToStr(status));
		return nullptr;
	}

	mpDsLayout = RxFromRaw<VkDescriptorSetLayout>(handle);

	return mpDsLayout;
}

} // namespace fx::renderer
