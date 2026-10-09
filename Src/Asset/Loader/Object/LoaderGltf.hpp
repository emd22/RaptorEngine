#pragma once

#include "../ObjectLoaderBase.hpp"

#include <Asset/Animation.hpp>
#include <Core/Path.hpp>
#include <Material/Material.hpp>
#include <Object/Object.hpp>
#include <map>
#include <string>
#include <unordered_set>
#include <vector>

struct cgltf_data;
struct cgltf_material;
struct cgltf_mesh;
struct cgltf_texture_view;
struct cgltf_primitive;
struct cgltf_animation;
struct cgltf_skin;
struct cgltf_node;

namespace fx {

namespace loader {

struct MeshNodeContext
{
	cgltf_skin* pSkin = nullptr;
	bool bBakeTransform = false;
	float WorldMatrix[16] = {};
};

class LoaderGltf final : public ObjectLoaderBase
{
public:
	LoaderGltf() = default;

	eLoaderStatus Load(AssetTicket& ticket, const String& path) override;
	eLoaderStatus Load(AssetTicket& ticket, const uint8* data, uint32 size) override;

	void CreateGpuResource(AssetTicket& object_id) override;
	void UploadMeshToGpu(Object* object);

	void Publish(Object* root) override;

	void Destroy() override;

	~LoaderGltf() override { Destroy(); }

private:
	void MakeMaterialForPrimitive(Object* object, cgltf_primitive* primitive);

	/**
	 * @brief Builds the vertex list and indices of `primitive` into `mesh`. Joints and weights are only loaded when the
	 * node has a skin, as a mesh on a node without a skin is drawn unskinned even when it carries them.
	 * @returns false if the primitive can not be drawn
	 */
	bool BuildPrimitiveMesh(PrimitiveMesh& mesh, cgltf_primitive* primitive, const MeshNodeContext& context);

	std::vector<int32> BuildJointLookup(cgltf_skin* skin) const;

	void LoadSkeleton(Skeleton& skel, cgltf_skin* skin, const std::vector<int32>& joint_lookup);
	void LoadAnimation(Animation& out_anim, const cgltf_animation& anim, cgltf_skin* skin,
					   const std::vector<int32>& joint_lookup);
	void LoadAnimations(Skeleton& skel, cgltf_skin* skin, const std::vector<int32>& joint_lookup);

	bool BuildObjectsFromPrimitives(Object* container_object, cgltf_mesh* gltf_mesh, const MeshNodeContext& context,
									BBox& out_bounds);

	void AttachChild(Object* parent, ObjectID child_id);
	std::vector<ObjectID> GetChildren(Object* parent) const;

	void UploadObjectTree(Object* object);

	bool ShouldWarn(const std::string& key);

	bool ValidateData();

	/**
	 * @brief Process the GLTF data and build out the object tree.
	 */
	void ProcessData(AssetTicket& ticket);


public:
	bool bKeepInMemory : 1 = false;

private:
	cgltf_data* mpGltfData = nullptr;
	String mModelPath;

	Object* mpRootObject = nullptr;
	std::vector<ObjectID> mRootChildren;

	std::map<std::pair<const cgltf_material*, bool>, MaterialID> mMaterialCache;
	std::unordered_set<std::string> mWarned;
};

} // namespace loader

} // namespace fx
