#include "MaterialManagerFwd.hpp"

#include "MaterialManager.hpp"

#include <Renderer/Globals.hpp>

namespace fx {


namespace MaterialManagerFwd {
renderer::DescriptorPool& GetDescriptorPool() { return gMaterialManager->GetDescriptorPool(); }
renderer::DescriptorSet& GetDescriptorSet() { return gMaterialManager->mMaterialPropertiesDS; }
renderer::RawGpuBuffer& GetMaterialPropertiesBuffer() { return gMaterialManager->MaterialPropertiesBuffer; }

} // namespace MaterialManagerFwd

} // namespace fx
