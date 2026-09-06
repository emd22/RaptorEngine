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

		test_material->SetAlpha(0.5);
		test_material->Attach(Material::eResourceType::Diffuse, diffuse);

		test_material->Finalize();
	}

	{
		const float scale = 0.25f;
		CubeGenOptions cgo {
			.Left = { .Scale = scale },
			.Right = { .Scale = scale },
			.Top = { .Scale = scale },
			.Bottom = { .Scale = scale },
			.Front = { .Scale = scale },
			.Back = { .Scale = scale },

			.bAlignUVs = true,
		};

		Ref<MeshGen::GeneratedMesh> cube_mesh = MeshGen::MakeCube(cgo);

		MaterialID mat_id = mWhiteMaterialID;

		pXFormObject = gObjectManager->NewObject("PROTO_XFORM", mat_id, eObjectTag::Blockout);
		pXFormObject->pMesh = cube_mesh->AsDefaultMesh();


		AssetTicket ticket(static_cast<void*>(pXFormObject));
		ticket.MarkAndSignalLoaded();

		pWorld->Attach(ticket);
	}
}


void Blockout::ScaleInDirection(Object* object, const Vec3f& face_dir, const Vec3f& magnitude)
{
	if (object == nullptr) {
		return;
	}

	const float32 threshold = 0.01f;

	if (face_dir.X > threshold) {
		object->Bounds.Max.X += magnitude.X;
	}
	else if (face_dir.X < threshold) {
		object->Bounds.Min.X -= magnitude.X;
	}

	if (face_dir.Y > threshold) {
		object->Bounds.Max.Y += magnitude.Y;
	}
	else if (face_dir.Y < threshold) {
		object->Bounds.Min.Y -= magnitude.Y;
	}

	if (face_dir.Z > threshold) {
		object->Bounds.Max.Z += magnitude.Z;
	}
	else if (face_dir.Z < threshold) {
		object->Bounds.Min.Z -= magnitude.Z;
	}
}

void Blockout::ReloadSingleObject(Object* object)
{
	if (object == nullptr || !object->HasTags(eObjectTag::Blockout)) {
		return;
	}

	ConfigFile info {};
	info.Load(gWorld->BlockoutPath.CStr());

	if (info.HasErrors()) {
		return;
	}

	ConfigEntry* blocks_entry = info.GetEntry(HashStr32("all"));

	ObjectID old_object_id = object->ID;
	Hash32 object_name_hash = object->Name.GetHash();

	ObjectID new_object_id;


	for (ConfigEntry& entry : blocks_entry->Members) {
		if (entry.Name.GetHash() == object_name_hash) {
			RemoveSingleObjectFromWorld(object);
			new_object_id = CreateCubeVolume(entry);
			break;
		}
	}

	// Update the old object id in the BlockoutObjects buffer.
	if (new_object_id != old_object_id) {
		for (int i = 0; i < BlockoutObjects.Size(); i++) {
			if (BlockoutObjects[i] == old_object_id) {
				BlockoutObjects[i] = new_object_id;
				break;
			}
		}
	}
}

void Blockout::RemoveSingleObjectFromWorld(Object* object)
{
	if (object == nullptr) {
		return;
	}

	gWorld->Detach(object->ID);

	if (!object->PhysicsID.IsNull()) {
		gPhysics->DestroyBody(object->PhysicsID);
	}

	gObjectManager->DestroyObject(object->ID);
}

void Blockout::RemoveBlockoutFromWorld(World* world)
{
	for (ObjectID box_id : BlockoutObjects) {
		RemoveSingleObjectFromWorld(gObjectManager->GetObject(box_id));
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


ObjectID Blockout::CreateCubeVolume(ConfigEntry& entry)
{
	Vec3f position = entry.GetMemberValue<Vec3f>(HashStr32("pos"), Vec3f::sZero);


	String blockout_id = String::Fmt("{}", entry.Name.Get());

	LogInfo("Adding blockout '{}'", blockout_id);

	const PagedArray<ConfigPrimitive>& scales = entry.GetMember(HashStr32("scale"))->GetArrayData();

	LogInfo("Creating block id {}", blockout_id);

	if (scales.Size() < 6) {
		return ObjectID::scNull;
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

	auto clamp_scale = [](float v) { return std::max(v, 0.01f); };
	cgo.Left.Scale = clamp_scale(cgo.Left.Scale);
	cgo.Right.Scale = clamp_scale(cgo.Right.Scale);
	cgo.Top.Scale = clamp_scale(cgo.Top.Scale);
	cgo.Bottom.Scale = clamp_scale(cgo.Bottom.Scale);
	cgo.Front.Scale = clamp_scale(cgo.Front.Scale);
	cgo.Back.Scale = clamp_scale(cgo.Back.Scale);

	Ref<MeshGen::GeneratedMesh> cube_mesh = MeshGen::MakeCube(cgo);

	eCProtoMat mat_index = static_cast<eCProtoMat>(entry.GetMemberValue<int>(HashStr32("mat"), 0));

	MaterialID material_id = mWhiteMaterialID;

	switch (mat_index) {
	case eCProtoMat::Gray:
		break;
	case eCProtoMat::Orange:
		material_id = mOrangeMaterialID;
		break;
	default:;
	}


	eObjectTag object_tags = eObjectTag::Blockout;

	bool is_locked = entry.GetMemberValue(HashStr32("lock"), 0) == 1;
	if (is_locked) {
		SetFlag(object_tags, eObjectTag::LockTransform);
	}
	else {
		material_id = mOrangeMaterialID;
	}

	Object* object = gObjectManager->NewObject(blockout_id.Str(), material_id, object_tags);
	object->pMesh = cube_mesh->AsDefaultMesh();
	object->MoveBy(position);
	object->SetShadowCaster(true);
	object->Bounds.Min = -Vec3f(cgo.Left.Scale, cgo.Bottom.Scale, cgo.Back.Scale);
	object->Bounds.Max = Vec3f(cgo.Right.Scale, cgo.Top.Scale, cgo.Front.Scale);
	Vec3f midpoint = GetCubeMidpointOffset(cgo);


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

	BlockoutObjects.Insert(object->ID);

	return object->ID;
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

	for (const ObjectID box_id : BlockoutObjects) {
		Object* object = gObjectManager->GetObject(box_id);
		if (object == nullptr) {
			continue;
		}

		ConfigEntry blockout_entry = ConfigEntry::Struct(object->Name.Get());
		{
			blockout_entry.AddMember(ConfigEntry::Literal("pos", object->mPosition));

			ConfigEntry scales_array = ConfigEntry::Array("scale", ConfigPrimitive::ePrimitiveType::Float);
			scales_array.AppendValue(ConfigPrimitive::FromValue(-object->Bounds.Min.X));
			scales_array.AppendValue(ConfigPrimitive::FromValue(object->Bounds.Max.X));
			scales_array.AppendValue(ConfigPrimitive::FromValue(object->Bounds.Max.Y));
			scales_array.AppendValue(ConfigPrimitive::FromValue(-object->Bounds.Min.Y));
			scales_array.AppendValue(ConfigPrimitive::FromValue(object->Bounds.Max.Z));
			scales_array.AppendValue(ConfigPrimitive::FromValue(-object->Bounds.Min.Z));
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
