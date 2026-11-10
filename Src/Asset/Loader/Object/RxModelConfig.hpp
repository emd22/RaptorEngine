#pragma once

#include <Core/DynArray.hpp>
#include <Core/Name.hpp>
#include <Core/PagedArray.hpp>
#include <Core/Slice.hpp>
#include <Core/String.hpp>
#include <Core/Types.hpp>
#include <Math/FVec3.hpp>
#include <Math/Quat.hpp>
#include <utility>

namespace fx {

namespace loader {

struct RxFileRef
{
	String Path;
	uint64 Hash = 0;
	bool bHasHash = false;
};

struct RxTransform
{
	FVec3 Position = FVec3::sZero;
	Quat Rotation = Quat::scIdentity;
	FVec3 Scale = FVec3::sOne;
	bool bIsIdentity = true;
};

enum class eRxBlendMode : uint8
{
	Opaque,
	Masked,
	Blend,
};

struct RxMaterialParam
{
	String Name;
	DynArray<float32> Values;
};

struct RxMaterialDesc
{
	const RxMaterialParam* FindParam(const StringView name) const;
	const String* FindTexture(const StringView slot) const;

	String MaterialName;
	String Shader;
	DynArray<std::pair<String, String>> Textures;
	DynArray<RxMaterialParam> Params;
	eRxBlendMode Blend = eRxBlendMode::Opaque;
	float32 AlphaCutoff = 0.5f;
	bool bDoubleSided = false;
};

struct RxSocketDesc
{
	String SocketName;
	Name JointName;
	RxTransform Transform;
};

struct RxClipDesc
{
	String ClipName;
	RxFileRef File;
	Name RootJoint;
	float32 Speed = 1.0f;
	bool bLoop = false;
	bool bRootMotion = false;
};

struct RxModelDesc
{
	const RxMaterialDesc* FindMaterial(const StringView name) const;

	String ModelName;
	RxFileRef Mesh;
	RxFileRef Skeleton;
	bool bHasSkeleton = false;
	RxTransform Root;
	DynArray<float32> LodScreenSizes;

	DynArray<RxMaterialDesc> Materials;
	DynArray<RxSocketDesc> Sockets;
	DynArray<RxClipDesc> Clips;
};

struct RxWeaponDesc
{
	RxFileRef Model;
	RxFileRef Viewhands;
	RxFileRef Skeleton;
	String AttachJoint;
	String RootJoint;
	uint32 ViewhandsJoints = 0;
	uint32 WeaponJoints = 0;

	DynArray<RxClipDesc> Clips;
};

bool LoadRxModelDesc(const String& path, RxModelDesc& out_desc);
bool LoadRxWeaponDesc(const String& path, RxWeaponDesc& out_desc);

} // namespace loader

} // namespace fx
