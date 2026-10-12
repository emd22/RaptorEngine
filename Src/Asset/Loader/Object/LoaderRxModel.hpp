#pragma once

#include "../ObjectLoaderBase.hpp"
#include "RxModelConfig.hpp"

#include <Asset/Animation.hpp>
#include <Core/Ref.hpp>
#include <Core/String.hpp>
#include <Material/Material.hpp>
#include <Object/Object.hpp>
#include <map>
#include <unordered_set>
#include <vector>

namespace fx {

namespace loader {

class RxBinaryFile;

class LoaderRxModel final : public ObjectLoaderBase
{
public:
	LoaderRxModel() = default;

	eLoaderStatus Load(AssetTicket& ticket, const String& path) override;
	eLoaderStatus Load(AssetTicket& ticket, const uint8* data, uint32 size) override;

	void CreateGpuResource(AssetTicket& ticket) override;
	void UploadMeshToGpu(Object* object);

	void Publish(Object* root) override;

	void Destroy() override;

	~LoaderRxModel() override { Destroy(); }

private:
	String ResolvePath(const String& relative_path) const;

	bool CheckFileHash(const RxBinaryFile& file, const RxFileRef& file_ref) const;

	bool LoadModel(const String& path);
	bool LoadWeapon(const String& path);
	bool LoadWeaponPart(LoaderRxModel& part, const String& path, uint32 joint_offset, uint32 joint_count,
						int32 static_joint);

	void SetModelPath(const String& path);

	bool ReadSkeleton(const RxFileRef& file_ref, Ref<Skeleton>& out_skeleton, uint64& out_signature) const;
	bool LoadSkeleton();
	bool LinkPartSkeleton();
	bool ValidateTextures() const;
	bool BuildObjects(const RxBinaryFile& mesh_file);

	bool LoadClip(Animation& out_anim, const RxClipDesc& clip);
	void LoadClips(const DynArray<RxClipDesc>& clips);

	bool LoadMaterialTexture(const RxMaterialDesc& desc, const char* slot, MaterialComponent& component,
							 bool* out_has_alpha = nullptr);
	MaterialID MakeMaterial(const String& name, bool is_skinned);

	int32 FindJoint(const char* name) const;

	void AttachChild(Object* parent, ObjectID child_id);
	void UploadObjectTree(Object* object);

	bool ShouldWarn(const String& key);

public:
	bool bKeepInMemory : 1 = false;

private:
	String mModelPath;
	String mDirectory;

	RxModelDesc mDesc;

	Object* mpRootObject = nullptr;
	std::vector<ObjectID> mRootChildren;

	Ref<Skeleton> mpSkeleton { nullptr };
	uint64 mSkeletonSignature = 0;

	uint32 mJointOffset = 0;
	uint32 mJointCount = 0;
	int32 mStaticJoint = -1;
	bool mbIsPart = false;

	std::map<std::pair<String, bool>, MaterialID> mMaterialCache;
	std::unordered_set<String> mWarned;
};

} // namespace loader

} // namespace fx
