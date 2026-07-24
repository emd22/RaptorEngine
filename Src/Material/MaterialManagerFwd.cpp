#include "MaterialManagerFwd.hpp"

#include "MaterialID.hpp"
#include "MaterialManager.hpp"

#include <Renderer/Globals.hpp>

namespace fx {

namespace MaterialManagerFwd {
renderer::DescriptorPool& GetDescriptorPool() { return gMaterialManager->GetDescriptorPool(); }
// renderer::DescriptorSet& GetDescriptorSet() { return gMaterialManager->mMaterialPropertiesDS; }
renderer::RawGpuBuffer& GetMaterialPropertiesBuffer() { return gMaterialManager->MaterialPropertiesBuffer; }
void DestroyMaterial(const MaterialID& id) { gMaterialManager->DestroyMaterial(id); }
Material* GetMaterial(const MaterialID& id) { return gMaterialManager->GetMaterial(id); }

} // namespace MaterialManagerFwd

} // namespace fx
