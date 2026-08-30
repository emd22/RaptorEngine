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

	mBlockoutMaterialID = gMaterialManager->NewMaterial("TestMat", renderer::ePipelineName::Geometry, false);
	Material* test_material = gMaterialManager->GetMaterial(mBlockoutMaterialID);

	AssetTicket diffuse = gAssetManager->LoadImage(eImageType::Flat, eImageFormat::RGBA8_UNorm,
												   "Data/Demo/Textures/gray_check.png", eImageCreateFlags::None);

	test_material->Attach(Material::eResourceType::Diffuse, diffuse);
	test_material->Finalize();
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

	return Vec3f(fx::simd::AbsDiff(vmax, vmin));
}

static Vec3f GetCubeSize(const CubeGenOptions& cgo)
{
	return (Vec3f(cgo.Left.Scale, cgo.Top.Scale, cgo.Front.Scale) +
			Vec3f(cgo.Right.Scale, cgo.Bottom.Scale, cgo.Back.Scale));
}

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

	Object* object = gObjectManager->NewObject(blockout_id.Str(), eObjectTag::Blockout);
	object->pMesh = cube_mesh->AsDefaultMesh();
	object->MoveBy(position);
	object->mMaterialID = mBlockoutMaterialID;
	object->SetShadowCaster(true);

	physics::Body* phys = gPhysics->NewBody(blockout_id);
	phys->CreatePrimitiveBody(physics::ePrimitiveType::Box, GetCubeSize(cgo), physics::eMotionType::Static,
							  physics::BodyProps {});
	phys->Teleport(position + GetCubeMidpointOffset(cgo), Quat::scIdentity);

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
