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

static void RemoveBlockoutFromWorld(World* world)
{
	SizedArray<Object*> objects = gObjectManager->CollectWithTags(eObjectTag::Blockout);

	for (Object* object : objects) {
		Assert(object != nullptr);

		world->Detach(object->ID);

		if (!object->PhysicsID.IsNull()) {
			gPhysics->DestroyBody(object->PhysicsID);
		}

		gObjectManager->DestroyObject(object->ID);
	}
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

	String blockout_id = String::Fmt("PROTO_{}", entry.Name.Get());

	LogInfo("Adding blockout '{}'", blockout_id);

	const PagedArray<ConfigPrimitive>& scales = entry.GetMember(HashStr32("scale"))->GetArrayData();

	LogInfo("Creating block id {}", blockout_id);

	if (scales.Size() < 6) {
		return;
	}

	CubeGenOptions cgo {
		.Left = { .Scale = scales[0].Get<float32>() },
		.Right = { .Scale = scales[1].Get<float32>() },
		.Top = { .Scale = scales[2].Get<float32>() },
		.Bottom = { .Scale = scales[3].Get<float32>() },
		.Front = { .Scale = scales[4].Get<float32>() },
		.Back = { .Scale = scales[5].Get<float32>() },

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

	bool is_locked = entry.GetMemberValue(HashStr32("lock"), 0) == 1;
	if (is_locked) {
		object->SetTag(eObjectTag::LockTransform);
	}
	else {
		object->mMaterialID = mOrangeMaterialID;
	}

	Quat rotation = Quat::FromEulerAngles(entry.GetMemberValue<Vec3f>(HashStr32("rot"), Vec3f::sZero));
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


Blockout::~Blockout() {}


} // namespace fx
