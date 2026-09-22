#include "Blockout.hpp"

#include <Asset/AssetManager.hpp>
#include <Asset/ConfigFile.hpp>
#include <InGameEditor.hpp>
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
	BlockoutObjects.Init(256);

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


	{
		mBlueMaterialID = gMaterialManager->NewMaterial("ProtoBlue", renderer::ePipelineName::Geometry, false);
		Material* test_material = gMaterialManager->GetMaterial(mBlueMaterialID);

		AssetTicket diffuse = gAssetManager->LoadImage(eImageType::Flat, eImageFormat::RGBA8_UNorm,
													   "Data/Demo/Textures/aqua_check.png", eImageCreateFlags::None);

		test_material->Attach(Material::eResourceType::Diffuse, diffuse);
		test_material->Finalize();
	}


	{
		mProtoTileID = gMaterialManager->NewMaterial("ProtoTile", renderer::ePipelineName::GeometryNormalMaps, false);
		Material* test_material = gMaterialManager->GetMaterial(mProtoTileID);

		AssetTicket diffuse = gAssetManager->LoadImage(eImageType::Flat, eImageFormat::RGBA8_UNorm,
													   "Data/Demo/Textures/blue_tile/BlueTiles01_1K_BaseColor.png",
													   eImageCreateFlags::None);

		AssetTicket normal = gAssetManager->LoadImage(eImageType::Flat, eImageFormat::RGBA8_UNorm,
													  "Data/Demo/Textures/blue_tile/BlueTiles01_1K_Normal.png",
													  eImageCreateFlags::None);

		AssetTicket roughness = gAssetManager->LoadImage(eImageType::Flat, eImageFormat::RGBA8_UNorm,
														 "Data/Demo/Textures/blue_tile/BlueTiles01_1K_Roughness.png",
														 eImageCreateFlags::None);


		test_material->Attach(Material::eResourceType::Diffuse, diffuse);
		test_material->Attach(Material::eResourceType::Normal, normal);
		test_material->Finalize();
	}

	// Selection material
	{
		SelectionMaterialID = gMaterialManager->NewMaterial("ProtoSelect", renderer::ePipelineName::Geometry, false);
		Material* test_material = gMaterialManager->GetMaterial(SelectionMaterialID);

		AssetTicket diffuse = gAssetManager->LoadImage(eImageType::Flat, eImageFormat::RGBA8_UNorm,
													   "Data/Demo/Textures/aqua_check.png", eImageCreateFlags::None);

		test_material->SetAlpha(0.7f);
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

		pXFormObject = gObjectManager->NewObject("PROTO_XFORM", SelectionMaterialID,
												 eObjectTag::Blockout | eObjectTag::LockTransform);
		pXFormObject->SetUnlit(true);
		pXFormObject->pMesh = cube_mesh->AsDefaultMesh();


		AssetTicket ticket(static_cast<void*>(pXFormObject));
		ticket.MarkAndSignalLoaded();

		pWorld->Attach(ticket);

		pXFormObject->SetPosition(Vec3f(200.0f));
	}
}


constexpr float scMinBlockoutThickness = 0.1f;

void Blockout::ScaleInDirection(Object* object, const Vec3f& face_dir, const Vec3f& magnitude)
{
	if (!object) {
		return;
	}

	constexpr float threshold = 0.01f;

	auto scaleAxis = [&](float face, float mag, float& min, float& max, const Vec3f& axis)
	{
		if (face > threshold) {
			const float desired = max + mag;
			if (desired - min >= scMinBlockoutThickness) {
				max = desired;
			}
			else {
				max = min + scMinBlockoutThickness;
				const float shift = desired - max;
				object->SetPosition(object->GetPosition() + Vec3f(axis.X * shift, axis.Y * shift, axis.Z * shift));
			}
		}
		else if (face < -threshold) {
			const float desired = min - mag;
			if (max - desired >= scMinBlockoutThickness) {
				min = desired;
			}
			else {
				min = max - scMinBlockoutThickness;
				const float shift = desired - min;
				object->SetPosition(object->GetPosition() + Vec3f(axis.X * shift, axis.Y * shift, axis.Z * shift));
			}
		}
	};

	scaleAxis(face_dir.X, magnitude.X, object->Bounds.Min.X, object->Bounds.Max.X, Vec3f(1.0f, 0.0f, 0.0f));
	scaleAxis(face_dir.Y, magnitude.Y, object->Bounds.Min.Y, object->Bounds.Max.Y, Vec3f(0.0f, 1.0f, 0.0f));
	scaleAxis(face_dir.Z, magnitude.Z, object->Bounds.Min.Z, object->Bounds.Max.Z, Vec3f(0.0f, 0.0f, 1.0f));
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
		for (ObjectID& id : BlockoutObjects) {
			if (id == old_object_id) {
				id = new_object_id;
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

MaterialID Blockout::GetMaterialForSlot(eCProtoMat slot) const
{
	switch (slot) {
	case eCProtoMat::Gray:
		return mWhiteMaterialID;
	case eCProtoMat::Orange:
		return mOrangeMaterialID;
	case eCProtoMat::Blue:
		return mBlueMaterialID;
	case eCProtoMat::Tile:
		return mProtoTileID;
	default:
		return mWhiteMaterialID;
	}
}

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

	Ref<MeshGen::GeneratedMesh> cube_mesh = MeshGen::MakeCube(cgo);

	MaterialID material_id = mWhiteMaterialID;

	eObjectTag object_tags = eObjectTag::Blockout;

	bool is_locked = entry.GetMemberValue(HashStr32("lock"), 0) == 1;
	if (is_locked) {
		SetFlag(object_tags, eObjectTag::LockTransform);
	}
	else {
		material_id = mOrangeMaterialID;
	}

	ConfigEntry* mat_entry = entry.GetMember(HashStr32("mat"));

	if (mat_entry != nullptr) {
		eCProtoMat mat_index = static_cast<eCProtoMat>(mat_entry->Get<int32>());
		material_id = GetMaterialForSlot(mat_index);
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

	if (entry.GetMemberValue(HashStr32("probevolume"), 0) == 1) {
		object->SetProbeVolume(true);
	}

	AssetTicket ticket(static_cast<void*>(object));
	ticket.MarkAndSignalLoaded();

	pWorld->Attach(ticket);

	BlockoutObjects.NewItem(nullptr, object->ID);

	return object->ID;
}


void Blockout::RebuildObject(Object* object)
{
	CubeGenOptions cgo {
		.Left = { .Scale = -object->Bounds.Min.X },
		.Right = { .Scale = object->Bounds.Max.X },
		.Top = { .Scale = object->Bounds.Max.Y },
		.Bottom = { .Scale = -object->Bounds.Min.Y },
		.Front = { .Scale = object->Bounds.Max.Z },
		.Back = { .Scale = -object->Bounds.Min.Z },

		.bAlignUVs = true,
	};

	Ref<MeshGen::GeneratedMesh> cube_mesh = MeshGen::MakeCube(cgo);
	object->pMesh = cube_mesh->AsDefaultMesh();

	physics::Body* body = gPhysics->GetBody(object->PhysicsID);
	if (body == nullptr) {
		return;
	}

	Vec3f midpoint = GetCubeMidpointOffset(cgo);
	Vec3f position = object->GetPosition();
	Quat rotation = body->GetRotation();

	object->SetRotationOrigin(-midpoint);

	physics::eMotionType motion_type = body->GetMotionType();


	gPhysics->DestroyBody(object->PhysicsID);

	physics::Body* phys = gPhysics->NewBody(object->Name.Get());
	phys->CreatePrimitiveBody(physics::ePrimitiveType::Box, GetCubeSize(cgo), motion_type,
							  physics::BodyProps {
								  .ConvexRadius = 0.05f,
								  .Density = 20,
							  });

	phys->SetMidpoint(midpoint);
	phys->Teleport(position, rotation);

	object->AttachCollider(phys);

	if (object->IsProbeVolume()) {
		object->SetProbeVolume(true);
	}
}

void Blockout::DestroyObject(Object* object)
{
	LogInfo(LC_SCRIPT, "Destroying object {}", object ? object->ID : ObjectID::scNull);
	if (object == nullptr) {
		return;
	}

	gPhysics->DestroyBody(object->PhysicsID);

	// Find the index for the blockout
	uint32 index = 0;
	while (true) {
		index = BlockoutObjects.SlotsInUse.FindNextSetBit(index);
		if (index == Bitset::scNoFreeBits) {
			break;
		}

		ObjectID* object_id = BlockoutObjects.GetItem(index);
		if (!object_id) {
			break;
		}

		if ((*object_id) == object->ID) {
			BlockoutObjects.FreeItem(index);
			break;
		}

		++index;
	}

	gWorld->Detach(object->ID);
	gObjectManager->DestroyObject(object->ID);
}

Object* Blockout::NewObject(const Vec3f& position)
{
	std::string blockout_name = String::Fmt("{}", BlockoutObjects.Size).Str();
	LogInfo("Creating new blockout object '{}'", blockout_name);

	Object* object = gObjectManager->NewObject(blockout_name, mWhiteMaterialID, eObjectTag::Blockout);

	float32 scale = 0.25f;

	CubeGenOptions cgo = CubeGenOptions::Uniform(scale);

	Ref<MeshGen::GeneratedMesh> cube_mesh = MeshGen::MakeCube(cgo);

	object->pMesh = cube_mesh->AsDefaultMesh();
	object->MoveBy(position);
	object->SetShadowCaster(true);
	object->Bounds.Min = -Vec3f(scale);
	object->Bounds.Max = Vec3f(scale);

	physics::Body* phys = gPhysics->NewBody(blockout_name);
	phys->CreatePrimitiveBody(physics::ePrimitiveType::Box, GetCubeSize(cgo), physics::eMotionType::Static,
							  physics::BodyProps {
								  .ConvexRadius = 0.05f,
								  .Density = 20,
							  });

	phys->Teleport(position, Quat::scIdentity);

	object->AttachCollider(phys);

	AssetTicket ticket(static_cast<void*>(object));
	ticket.MarkAndSignalLoaded();

	pWorld->Attach(ticket);

	BlockoutObjects.NewItem(nullptr, object->ID);

	return object;
}

Object* Blockout::DupeObject(Object* object)
{
	if (!object) {
		return nullptr;
	}

	std::string blockout_name = String::Fmt("{}", BlockoutObjects.Size).Str();
	LogInfo("Creating new blockout object '{}'", blockout_name);

	Object* dupe = gObjectManager->NewObject(blockout_name, object->GetMaterialID(), eObjectTag::Blockout);

	dupe->Bounds.Min = object->Bounds.Min;
	dupe->Bounds.Max = object->Bounds.Max;

	CubeGenOptions cgo {
		.Left = { .Scale = -object->Bounds.Min.X },
		.Right = { .Scale = object->Bounds.Max.X },
		.Top = { .Scale = object->Bounds.Max.Y },
		.Bottom = { .Scale = -object->Bounds.Min.Y },
		.Front = { .Scale = object->Bounds.Max.Z },
		.Back = { .Scale = -object->Bounds.Min.Z },

		.bAlignUVs = true,
	};

	Ref<MeshGen::GeneratedMesh> cube_mesh = MeshGen::MakeCube(cgo);
	dupe->pMesh = cube_mesh->AsDefaultMesh();

	physics::Body* existing_body = gPhysics->GetBody(object->PhysicsID);
	if (existing_body != nullptr) {
		Vec3f midpoint = GetCubeMidpointOffset(cgo);
		Vec3f position = object->GetPosition();
		Quat rotation = existing_body->GetRotation();

		dupe->SetRotationOrigin(-midpoint);

		physics::eMotionType motion_type = existing_body->GetMotionType();

		physics::Body* phys = gPhysics->NewBody(dupe->Name.Get());
		phys->CreatePrimitiveBody(physics::ePrimitiveType::Box, GetCubeSize(cgo), motion_type,
								  physics::BodyProps {
									  .ConvexRadius = 0.05f,
									  .Density = 20,
								  });

		phys->SetMidpoint(midpoint);
		phys->Teleport(position, rotation);

		dupe->AttachCollider(phys);
	}

	if (object->IsProbeVolume()) {
		dupe->SetProbeVolume(true);
	}

	AssetTicket ticket(static_cast<void*>(dupe));
	ticket.MarkAndSignalLoaded();

	gWorld->Attach(ticket);

	BlockoutObjects.NewItem(nullptr, dupe->ID);

	return dupe;
}

Object* Blockout::RestoreObject(const Vec3f& position, const Vec3f& bounds_min, const Vec3f& bounds_max,
								MaterialID material, const Quat& rotation, const Name& name)
{
	std::string base_name = name.Get().empty() ? String::Fmt("{}", BlockoutObjects.Size).Str() : name.Get();

	// The original name may have been reused since the delete; keep names unique.
	std::string blockout_name = base_name;
	if (gObjectManager->FindObject(HashStr32(blockout_name.c_str())) != nullptr) {
		int suffix = 0;
		do {
			blockout_name = String::Fmt("{}_undo{}", base_name, suffix++).Str();
		} while (gObjectManager->FindObject(HashStr32(blockout_name.c_str())) != nullptr);
	}

	LogInfo("Restoring blockout object '{}'", blockout_name);

	Object* object = gObjectManager->NewObject(blockout_name, material.IsNull() ? mOrangeMaterialID : material,
											   eObjectTag::Blockout);

	object->Bounds.Min = bounds_min;
	object->Bounds.Max = bounds_max;

	CubeGenOptions cgo {
		.Left = { .Scale = -bounds_min.X },
		.Right = { .Scale = bounds_max.X },
		.Top = { .Scale = bounds_max.Y },
		.Bottom = { .Scale = -bounds_min.Y },
		.Front = { .Scale = bounds_max.Z },
		.Back = { .Scale = -bounds_min.Z },

		.bAlignUVs = true,
	};

	Ref<MeshGen::GeneratedMesh> cube_mesh = MeshGen::MakeCube(cgo);
	object->pMesh = cube_mesh->AsDefaultMesh();
	object->MoveBy(position);
	object->SetShadowCaster(true);
	object->SetRotation(rotation);

	Vec3f midpoint = GetCubeMidpointOffset(cgo);
	object->SetRotationOrigin(-midpoint);

	physics::Body* phys = gPhysics->NewBody(blockout_name);
	phys->CreatePrimitiveBody(physics::ePrimitiveType::Box, GetCubeSize(cgo), physics::eMotionType::Static,
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

	BlockoutObjects.NewItem(nullptr, object->ID);

	return object;
}


void Blockout::Load(const String& path)
{
	ConfigFile info {};
	info.Load(path.CStr());

	if (info.HasErrors()) {
		return;
	}

	if (gPrototypeEditor != nullptr) {
		// Since everything is getting reloaded, we need to clear the editor undo stack.
		gPrototypeEditor->ResetUndoStack();
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

			if (object->IsProbeVolume()) {
				blockout_entry.AddMember(ConfigEntry::Literal("probevolume", 1));
			}

			// Save material type
			if (object->GetMaterialID() == mBlueMaterialID) {
				blockout_entry.AddMember(ConfigEntry::DotReference("mat", "$cprotomat.blue"));
			}
			else if (object->GetMaterialID() == mWhiteMaterialID) {
				blockout_entry.AddMember(ConfigEntry::DotReference("mat", "$cprotomat.grey"));
			}
			else if (object->GetMaterialID() == mOrangeMaterialID) {
				blockout_entry.AddMember(ConfigEntry::DotReference("mat", "$cprotomat.orange"));
			}
			else if (object->GetMaterialID() == mProtoTileID) {
				blockout_entry.AddMember(ConfigEntry::DotReference("mat", "$cprotomat.tile"));
			}
		}
		all_entry->AddMember(std::move(blockout_entry));
	}

	info.Write(path.CStr());
}


Blockout::~Blockout() {}


} // namespace fx
