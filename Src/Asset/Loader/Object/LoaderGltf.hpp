#pragma once

#include "../ObjectLoaderBase.hpp"

#include <raptor_ffi.h>

#include <Asset/Animation.hpp>
#include <Core/Path.hpp>
#include <Material/Material.hpp>
#include <Object/Object.hpp>
#include <vector>

namespace fx {

namespace loader {

struct AxGltfMaterialToLoad
{
	TSRef<Object> pObject { nullptr };
	int PrimitiveIndex = 0;
	int MeshIndex = 0;
};


class LoaderGltf final : public ObjectLoaderBase
{
public:
	LoaderGltf() = default;

	eLoaderStatus Load(AssetTicket& ticket, const String& path) override;
	eLoaderStatus Load(AssetTicket& ticket, const uint8* data, uint32 size) override;

	void CreateGpuResource(AssetTicket& object_id) override;
	void UploadMeshToGpu(Object* object);

	void Destroy() override;

	~LoaderGltf() override = default;

private:
	void MakeMaterialForPrimitive(Object* object, const RxGltfPrimitive& primitive);

	/**
	 * @brief Unpacks the vertex attributes of `primitive` into `mesh`. Joints and weights are only loaded when
	 * `is_skinned` is set, as a mesh on a node without a skin is drawn unskinned even when it carries them.
	 */
	void UnpackMeshAttributes(Ref<PrimitiveMesh>& mesh, const RxGltfPrimitive& primitive, bool is_skinned);

	void BuildObjectsFromPrimitives(Object* container_object, uint32 mesh_index, bool is_skinned);

	/**
	 * @brief Process the GLTF data and build out the object tree.
	 */
	void ProcessData(AssetTicket& ticket);


public:
	std::vector<AxGltfMaterialToLoad> MaterialsToLoad;
	bool bKeepInMemory : 1 = false;
	SizedArray<uint32> IndexBuffer;

private:
	RxGltf* mpGltf = nullptr;
	String mModelPath;

	SizedArray<Mat4f> mBones;
};

} // namespace loader

} // namespace fx
