#pragma once

#include "Material.hpp"
#include "MaterialID.hpp"

#include <Core/Bitset.hpp>
#include <Core/FreeArray.hpp>
#include <Core/String.hpp>
#include <Core/Types.hpp>
#include <Renderer/Backend/Descriptors.hpp>

#define FX_MAX_BOUND_MATERIALS 64

namespace fx {

// Forward declarations
namespace renderer {
class Pipeline;
class CommandBuffer;
} // namespace renderer

class Material;

class MaterialManager
{
public:
	void Create();

	MaterialID NewMaterial(const String& name, renderer::ePipelineName pl_name, bool supports_skinning);
	Material* GetMaterial(const MaterialID& id);
	void DestroyMaterial(const MaterialID& id);

	bool BindWithPipeline(const renderer::CommandBuffer& cmd, const renderer::Pipeline& pipeline, const MaterialID& id);

	renderer::DescriptorPool& GetDescriptorPool() { return mDescriptorPool; }

	void Destroy();

	~MaterialManager();

private:
	Material* GetNewMaterial();

	/**
	 * @brief Syncs the properties of material `id` to be accessible in shader.
	 */
	void SyncMaterialToGpu(Material* material);

	void MakeNullMaterial();

public:
	/**
	 * @brief A GPU buffer containing the properties for each material. This mirrors the layout of `mMaterialList`, but
	 * holds only instances of `MaterialProperties`.
	 */
	renderer::RawGpuBuffer MaterialPropertiesBuffer {};

	/**
	 * @brief Descriptor set for material properties. Used in the light pass.
	 */
	renderer::DescriptorSet mMaterialPropertiesDS {};


private:
	FreeArray<Material> mMaterialList;

	renderer::DescriptorPool mDescriptorPool;

	// VkDescriptorSetLayout DsLayoutMaterialBasic;
	// VkDescriptorSetLayout DsLayoutMaterialPBR;
	// VkDescriptorSetLayout DsLayoutMaterialPBRSkinned;

	bool mbInitialized : 1 = false;

	std::mutex mInUse;
};

} // namespace fx
