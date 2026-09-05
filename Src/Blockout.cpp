#include "Blockout.hpp"

#include <Asset/AssetManager.hpp>
#include <Asset/ConfigFile.hpp>
#include <Material/Material.hpp>
#include <Material/MaterialManager.hpp>
#include <Math/SIMDHelper.hpp>
#include <Physics/JoltPhysicsBackend.hpp>
#include <Physics/PhysicsManager.hpp>
#include <Renderer/PipelineNames.hpp>
#include <World.hpp>

namespace fx {

Blockout::Blockout() {}

void Blockout::Create(World* world)
{
	BlockoutObjects.Create(64);

	pWorld = world;

	// White material
	{
		mWhiteMaterialID = gMaterialManager->NewMaterial("ProtoWhite", renderer::ePipelineName::Geometry, false);
		Material* test_material = gMaterialManager->GetMaterial(mWhiteMaterialID);

		AssetTicket diffuse = gAssetManager->LoadImage(eImageType::Flat, eImageFormat::RGBA8_UNorm,
													   "Data/Demo/Textures/gray_check.png", eImageCreateFlags::None);

		test_material->Attach(Material::eResourceType::Diffuse, diffuse);

		test_material->Finalize();
	}

	// Orange material
	{
		mOrangeMaterialID = gMaterialManager->NewMaterial("ProtoOrange", renderer::ePipelineName::Geometry, false);
		Material* test_material = gMaterialManager->GetMaterial(mOrangeMaterialID);

		AssetTicket diffuse = gAssetManager->LoadImage(eImageType::Flat, eImageFormat::RGBA8_UNorm,
													   "Data/Demo/Textures/orange_check.png", eImageCreateFlags::None);

		test_material->Attach(Material::eResourceType::Diffuse, diffuse);

		test_material->Finalize();
	}

	// Blue material
	{
		SelectionMaterialID = gMaterialManager->NewMaterial("ProtoBlue", renderer::ePipelineName::Geometry, false);
		Material* test_material = gMaterialManager->GetMaterial(SelectionMaterialID);

		AssetTicket diffuse = gAssetManager->LoadImage(eImageType::Flat, eImageFormat::RGBA8_UNorm,
													   "Data/Demo/Textures/aqua_check.png", eImageCreateFlags::None);

		test_material->Attach(Material::eResourceType::Diffuse, diffuse);

		test_material->Finalize();
	}
}

void Blockout::RemoveBlockoutFromWorld(World* world)
{
	for (BlockoutBox& bbox : BlockoutObjects) {
		Object* object = gObjectManager->GetObject(bbox.ID);
		if (object == nullptr) {
			continue;
		}

		world->Detach(object->ID);

		if (!object->PhysicsID.IsNull()) {
			gPhysics->DestroyBody(object->PhysicsID);
		}

		gObjectManager->DestroyObject(object->ID);
	}

	BlockoutObjects.Clear();
}

/**
 * @brief Get the offset to get the center of an asymmetrical block.
 */
static Vec3f GetCubeMidpointOffset(const CubeGenOptions& cgo)
{
	// We want to offset the position of the block by the difference betwween the opposing side of the box.
	// If we take a single dimension, e.g. X dimension:
	//     |     :          |
	// left^  pos^     right^
	//
	// Then we can offset the midpoint between the difference between left and right.

	const FLOAT4 vmax = fx::simd::LoadFloat4(cgo.Right.Scale, cgo.Top.Scale, cgo.Front.Scale, 0.0f);
	const FLOAT4 vmin = fx::simd::LoadFloat4(cgo.Left.Scale, cgo.Bottom.Scale, cgo.Back.Scale, 0.0f);

	return Vec3f(fx::simd::Sub(vmax, vmin)) * 0.5f;
}

static Vec3f GetCubeSize(const CubeGenOptions& cgo)
{
	return (Vec3f(cgo.Left.Scale, cgo.Top.Scale, cgo.Front.Scale) +
			Vec3f(cgo.Right.Scale, cgo.Bottom.Scale, cgo.Back.Scale));
}

enum class eCProtoMat
{
	Gray = 0,
	Orange = 1,
};

void Blockout::CreateCubeVolume(ConfigEntry& entry)
{
	Vec3f position = entry.GetMemberValue<Vec3f>(HashStr32("pos"), Vec3f::sZero);


	String blockout_id = String::Fmt("{}{}", (entry.Name.Get().starts_with("PROTO_") ? "" : "PROTO_"),
									 entry.Name.Get());

	LogInfo("Adding blockout '{}'", blockout_id);

	const PagedArray<ConfigPrimitive>& scales = entry.GetMember(HashStr32("scale"))->GetArrayData();

	LogInfo("Creating block id {}", blockout_id);

	if (scales.Size() < 6) {
		return;
	}

	BlockoutBox bbox {
		.Scales = {
		   scales[0].Get<float32>(),
		   scales[1].Get<float32>(),
		   scales[2].Get<float32>(),
		   scales[3].Get<float32>(),
		scales[4].Get<float32>(),
			scales[5].Get<float32>(),
		},
	};


	CubeGenOptions cgo {
		.Left = { .Scale = bbox.Scales[0] },
		.Right = { .Scale = bbox.Scales[1] },
		.Top = { .Scale = bbox.Scales[2] },
		.Bottom = { .Scale = bbox.Scales[3] },
		.Front = { .Scale = bbox.Scales[4] },
		.Back = { .Scale = bbox.Scales[5] },

		.bAlignUVs = true,
	};

	Ref<MeshGen::GeneratedMesh> cube_mesh = MeshGen::MakeCube(cgo);

	eCProtoMat mat_index = static_cast<eCProtoMat>(entry.GetMemberValue<int>(HashStr32("mat"), 0));

	MaterialID mat_id = mWhiteMaterialID;

	switch (mat_index) {
	case eCProtoMat::Gray:
		break;
	case eCProtoMat::Orange:
		mat_id = mOrangeMaterialID;
		break;
	default:;
	}


	Object* object = gObjectManager->NewObject(blockout_id.Str(), eObjectTag::Blockout);
	object->pMesh = cube_mesh->AsDefaultMesh();
	object->MoveBy(position);
	object->mMaterialID = mat_id;
	object->SetShadowCaster(true);
	Vec3f midpoint = GetCubeMidpointOffset(cgo);

	bbox.ID = object->ID;

	bool is_locked = entry.GetMemberValue(HashStr32("lock"), 0) == 1;
	if (is_locked) {
		object->SetTag(eObjectTag::LockTransform);
	}
	else {
		object->mMaterialID = mOrangeMaterialID;
	}

	Quat rotation = Quat::scIdentity;

	{
		ConfigEntry* rot_entry = entry.GetMember(HashStr32("rot"));

		if (rot_entry != nullptr) {
			rotation = Quat::FromEulerAngles(rot_entry->GetValue<Vec3f>());
		}
	}

	{
		ConfigEntry* rotq_entry = entry.GetMember(HashStr32("rotquat"));

		if (rotq_entry != nullptr) {
			rotation = rotq_entry->GetValue<Quat>();
		}
	}

	object->SetRotation(rotation);

	object->SetRotationOrigin(-midpoint);

	bool is_dynamic = entry.GetMemberValue(HashStr32("dynamic"), 0) == 1;

	physics::Body* phys = gPhysics->NewBody(blockout_id);
	phys->CreatePrimitiveBody(physics::ePrimitiveType::Box, GetCubeSize(cgo),
							  is_dynamic ? physics::eMotionType::Dynamic : physics::eMotionType::Static,
							  physics::BodyProps {
								  .ConvexRadius = 0.05f,
								  .Density = 20,
							  });

	phys->SetMidpoint(midpoint);
	phys->Teleport(position, rotation);

	object->AttachCollider(phys);

	AssetTicket ticket(static_cast<void*>(object));
	ticket.MarkAndSignalLoaded();

	pWorld->Attach(ticket);

	BlockoutObjects.Insert(std::move(bbox));
}


void Blockout::Load(const String& path)
{
	ConfigFile info {};
	info.Load(path.CStr());

	if (info.HasErrors()) {
		return;
	}

	ConfigEntry* blocks_entry = info.GetEntry(HashStr32("all"));

	// Remove the current blockout from the world
	RemoveBlockoutFromWorld(pWorld);

	for (ConfigEntry& entry : blocks_entry->Members) {
		CreateCubeVolume(entry);
	}
}

void Blockout::Save(const String& path)
{
	ConfigFile info {};

	ConfigEntry* all_entry = info.AddEntry("all");

	for (const BlockoutBox& box : BlockoutObjects) {
		Object* object = gObjectManager->GetObject(box.ID);
		if (object == nullptr) {
			continue;
		}

		ConfigEntry blockout_entry = ConfigEntry::Struct(object->Name.Get());
		{
			blockout_entry.AddMember(ConfigEntry::Literal("pos", object->mPosition));

			ConfigEntry scales_array = ConfigEntry::Array("scale", ConfigPrimitive::ePrimitiveType::Float);
			scales_array.AppendValue(ConfigPrimitive::FromValue(box.Scales[0]));
			scales_array.AppendValue(ConfigPrimitive::FromValue(box.Scales[1]));
			scales_array.AppendValue(ConfigPrimitive::FromValue(box.Scales[2]));
			scales_array.AppendValue(ConfigPrimitive::FromValue(box.Scales[3]));
			scales_array.AppendValue(ConfigPrimitive::FromValue(box.Scales[4]));
			scales_array.AppendValue(ConfigPrimitive::FromValue(box.Scales[5]));
			blockout_entry.AddMember(std::move(scales_array));

			blockout_entry.AddMember(ConfigEntry::Literal("rotquat", object->mRotation));

			if (object->HasTags(eObjectTag::LockTransform)) {
				blockout_entry.AddMember(ConfigEntry::Literal("lock", 1));
			}
		}
		all_entry->AddMember(std::move(blockout_entry));
	}

	info.Write(path.CStr());
}


Blockout::~Blockout() {}


} // namespace fx
