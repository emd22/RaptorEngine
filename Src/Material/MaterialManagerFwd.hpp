/*
 * File:        MaterialManagerFwd.hpp
 * Author:      emd22
 * Created:     17/05/2026 by emd22
 * Description: Forward declarations for MaterialManager
 */

#pragma once

namespace fx {

namespace renderer {
class DescriptorPool;
class DescriptorSet;
class RawGpuBuffer;
} // namespace renderer

namespace MaterialManagerFwd {
renderer::DescriptorPool& GetDescriptorPool();
renderer::DescriptorSet& GetDescriptorSet();
renderer::RawGpuBuffer& GetMaterialPropertiesBuffer();


} // namespace MaterialManagerFwd

} // namespace fx
