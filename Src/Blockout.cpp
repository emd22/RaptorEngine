#include "Blockout.hpp"

#include <Asset/AssetManager.hpp>
#include <Asset/ConfigFile.hpp>
#include <Asset/MeshGen.hpp>
#include <Core/RefUtil.hpp>
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

	// Face resizing works on the bounds, which only describe box brushes
	const Brush* brush = GetBrush(object);
	if (brush != nullptr && !brush->IsBox()) {
		LogWarning("Cannot resize blockout '{}', face resizing only supports box brushes", object->Name.Get());
		return;
	}

	constexpr float threshold = 0.01f;

	const Vec3f old_midpoint = (object->Bounds.Min + object->Bounds.Max) * 0.5f;
	Vec3f local_shift = Vec3f::sZero;

	auto ScaleAxis = [&](float face, float mag, float& min, float& max, float& shift_axis)
	{
		if (face > threshold) {
			const float desired = max + mag;
			if (desired - min >= scMinBlockoutThickness) {
				max = desired;
			}
			else {
				max = min + scMinBlockoutThickness;
				shift_axis = desired - max;
			}
		}
		else if (face < -threshold) {
			const float desired = min - mag;
			if (max - desired >= scMinBlockoutThickness) {
				min = desired;
			}
			else {
				min = max - scMinBlockoutThickness;
				shift_axis = desired - min;
			}
		}
	};

	ScaleAxis(face_dir.X, magnitude.X, object->Bounds.Min.X, object->Bounds.Max.X, local_shift.X);
	ScaleAxis(face_dir.Y, magnitude.Y, object->Bounds.Min.Y, object->Bounds.Max.Y, local_shift.Y);
	ScaleAxis(face_dir.Z, magnitude.Z, object->Bounds.Min.Z, object->Bounds.Max.Z, local_shift.Z);

	const Vec3f midpoint_delta = (object->Bounds.Min + object->Bounds.Max) * 0.5f - old_midpoint;
	const Vec3f offset = (local_shift + midpoint_delta).Rotate(object->mRotation) - midpoint_delta;

	// Blockouts rotate about their midpoint, which is applied unrotated, so a midpoint change has to be
	// compensated for or the opposite face moves on rotated objects
	if (!offset.IsCloseTo(simd::LoadFloat4(0.0f))) {
		object->SetPosition(object->GetPosition() + offset);
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
			new_object_id = CreateBrushObject(entry);
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

	mBrushes.erase(object->ID.GetID());

	gObjectManager->DestroyObject(object->ID);
}

void Blockout::RemoveBlockoutFromWorld(World* world)
{
	for (ObjectID box_id : BlockoutObjects) {
		RemoveSingleObjectFromWorld(gObjectManager->GetObject(box_id));
	}

	BlockoutObjects.Clear();
	mBrushes.clear();
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

Brush* Blockout::GetBrush(const Object* object)
{
	if (object == nullptr) {
		return nullptr;
	}

	auto it = mBrushes.find(object->ID.GetID());
	return (it != mBrushes.end()) ? &it->second : nullptr;
}

void Blockout::ApplyBrush(Object* object, Brush&& brush, physics::eMotionType motion_type)
{
	Assert(brush.IsValid());

	Ref<MeshGen::GeneratedMesh> mesh = MakeRef<MeshGen::GeneratedMesh>();
	brush.GenerateMesh(mesh->Positions, mesh->Normals, mesh->Tangents, mesh->Texcoords, mesh->Indices);
	object->pMesh = mesh->AsDefaultMesh();

	object->Bounds.Min = brush.GetBoundsMin();
	object->Bounds.Max = brush.GetBoundsMax();

	// Blockouts rotate about the centre of their bounds
	const Vec3f midpoint = brush.GetCenter();
	object->SetRotationOrigin(-midpoint);

	gPhysics->DestroyBody(object->PhysicsID);

	SizedArray<Vec3f> hull_points;
	hull_points.InitCapacity(brush.GetVertices().Size);

	// The collider is centred on the midpoint so that it rotates the same way as the object
	for (const Vec3f& vertex : brush.GetVertices()) {
		hull_points.Insert(vertex - midpoint);
	}

	physics::Body* phys = gPhysics->NewBody(object->Name.Get());
	phys->CreateConvexHullBody(hull_points, motion_type,
							   physics::BodyProps {
								   .ConvexRadius = 0.05f,
								   .Density = 20,
							   });

	phys->SetMidpoint(midpoint);
	phys->Teleport(object->GetPosition(), object->mRotation);

	object->AttachCollider(phys);

	// A probe volume's collider has to be taken back out of the world
	if (object->IsProbeVolume()) {
		object->SetProbeVolume(true);
	}

	mBrushes[object->ID.GetID()] = std::move(brush);
}

/**
 * @brief Reads a blockout's brush from either a box (`scale`) or a list of planes (`planes`).
 */
static Brush ReadBrushEntry(ConfigEntry& entry)
{
	ConfigEntry* scale_entry = entry.GetMember(HashStr32("scale"));

	if (scale_entry != nullptr) {
		const PagedArray<ConfigPrimitive>& scales = scale_entry->GetArrayData();

		if (scales.Size() < 6) {
			return {};
		}

		// Left, right, top, bottom, front, back extents
		const Vec3f min = -Vec3f(scales[0].Get<float32>(), scales[3].Get<float32>(), scales[5].Get<float32>());
		const Vec3f max = Vec3f(scales[1].Get<float32>(), scales[2].Get<float32>(), scales[4].Get<float32>());

		return Brush::FromBox(min, max);
	}

	ConfigEntry* planes_entry = entry.GetMember(HashStr32("planes"));

	if (planes_entry != nullptr) {
		const PagedArray<ConfigPrimitive>& values = planes_entry->GetArrayData();

		// Four values per plane: the outward normal, then the distance from the origin
		const uint32 plane_count = static_cast<uint32>(values.Size() / 4);

		if (plane_count > Brush::scMaxPlanes) {
			LogError("Blockout '{}' has {} planes, the maximum is {}", entry.Name.Get(), plane_count,
					 Brush::scMaxPlanes);
			return {};
		}

		Brush::PlaneList planes;

		for (uint32 i = 0; i < plane_count; i++) {
			planes.Insert(BrushPlane {
				.Normal = Vec3f(values[i * 4].Get<float32>(), values[i * 4 + 1].Get<float32>(),
								values[i * 4 + 2].Get<float32>()),
				.Distance = values[i * 4 + 3].Get<float32>(),
			});
		}

		return Brush::FromPlanes(planes);
	}

	return {};
}

ObjectID Blockout::CreateBrushObject(ConfigEntry& entry)
{
	Vec3f position = entry.GetMemberValue<Vec3f>(HashStr32("pos"), Vec3f::sZero);

	String blockout_id = String::Fmt("{}", entry.Name.Get());

	LogInfo("Adding blockout '{}'", blockout_id);

	Brush brush = ReadBrushEntry(entry);

	if (!brush.IsValid()) {
		LogError("Blockout '{}' does not describe a valid convex brush, skipping", blockout_id);
		return ObjectID::scNull;
	}

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
	object->MoveBy(position);
	object->SetShadowCaster(true);

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

	if (entry.GetMemberValue(HashStr32("probevolume"), 0) == 1) {
		object->SetProbeVolume(true);
	}

	bool is_dynamic = entry.GetMemberValue(HashStr32("dynamic"), 0) == 1;

	ApplyBrush(object, std::move(brush), is_dynamic ? physics::eMotionType::Dynamic : physics::eMotionType::Static);

	AssetTicket ticket(static_cast<void*>(object));
	ticket.MarkAndSignalLoaded();

	pWorld->Attach(ticket);

	BlockoutObjects.NewItem(nullptr, object->ID);

	return object->ID;
}


void Blockout::RebuildObject(Object* object)
{
	if (object == nullptr) {
		return;
	}

	physics::Body* body = gPhysics->GetBody(object->PhysicsID);
	if (body == nullptr) {
		return;
	}

	Brush* existing = GetBrush(object);

	// A box is edited through the object's bounds, anything else keeps its planes
	Brush brush = (existing == nullptr || existing->IsBox()) ? Brush::FromBox(object->Bounds.Min, object->Bounds.Max)
															 : Brush::FromPlanes(existing->Planes);

	if (!brush.IsValid()) {
		LogError("Could not rebuild blockout '{}', its brush is invalid", object->Name.Get());
		return;
	}

	ApplyBrush(object, std::move(brush), body->GetMotionType());
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

	mBrushes.erase(object->ID.GetID());

	gWorld->Detach(object->ID);
	gObjectManager->DestroyObject(object->ID);
}

Object* Blockout::NewObject(const Vec3f& position)
{
	std::string blockout_name = String::Fmt("{}", BlockoutObjects.Size).Str();
	LogInfo("Creating new blockout object '{}'", blockout_name);

	Object* object = gObjectManager->NewObject(blockout_name, mWhiteMaterialID, eObjectTag::Blockout);

	constexpr float32 scale = 0.25f;

	object->MoveBy(position);
	object->SetShadowCaster(true);

	ApplyBrush(object, Brush::FromBox(Vec3f(-scale), Vec3f(scale)), physics::eMotionType::Static);

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

	const Brush* source_brush = GetBrush(object);
	Brush brush = (source_brush != nullptr) ? Brush::FromPlanes(source_brush->Planes)
											: Brush::FromBox(object->Bounds.Min, object->Bounds.Max);

	physics::Body* existing_body = gPhysics->GetBody(object->PhysicsID);
	physics::eMotionType motion_type = (existing_body != nullptr) ? existing_body->GetMotionType()
																  : physics::eMotionType::Static;

	dupe->SetPosition(object->GetPosition());
	dupe->SetRotation(object->mRotation);
	dupe->SetShadowCaster(true);

	if (object->IsProbeVolume()) {
		dupe->SetProbeVolume(true);
	}

	ApplyBrush(dupe, std::move(brush), motion_type);

	AssetTicket ticket(static_cast<void*>(dupe));
	ticket.MarkAndSignalLoaded();

	gWorld->Attach(ticket);

	BlockoutObjects.NewItem(nullptr, dupe->ID);

	return dupe;
}

Object* Blockout::RestoreObject(const Vec3f& position, const Brush::PlaneList& planes, MaterialID material,
								const Quat& rotation, const Name& name)
{
	Brush brush = Brush::FromPlanes(planes);

	if (!brush.IsValid()) {
		LogError("Cannot restore blockout '{}', its brush is invalid", name.Get());
		return nullptr;
	}

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

	object->MoveBy(position);
	object->SetShadowCaster(true);
	object->SetRotation(rotation);

	ApplyBrush(object, std::move(brush), physics::eMotionType::Static);

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

	if (blocks_entry == nullptr) {
		LogError("Blockout '{}' could not be loaded or has no 'all' entry", path);
		return;
	}

	// Remove the current blockout from the world
	RemoveBlockoutFromWorld(pWorld);

	for (ConfigEntry& entry : blocks_entry->Members) {
		CreateBrushObject(entry);
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

			const Brush* brush = GetBrush(object);

			if (brush == nullptr || brush->IsBox()) {
				// Boxes are stored as their extents in each direction
				ConfigEntry scales_array = ConfigEntry::Array("scale", ConfigPrimitive::ePrimitiveType::Float);

				scales_array.AppendValue(ConfigPrimitive::FromValue(-object->Bounds.Min.X));
				scales_array.AppendValue(ConfigPrimitive::FromValue(object->Bounds.Max.X));
				scales_array.AppendValue(ConfigPrimitive::FromValue(object->Bounds.Max.Y));
				scales_array.AppendValue(ConfigPrimitive::FromValue(-object->Bounds.Min.Y));
				scales_array.AppendValue(ConfigPrimitive::FromValue(object->Bounds.Max.Z));
				scales_array.AppendValue(ConfigPrimitive::FromValue(-object->Bounds.Min.Z));

				blockout_entry.AddMember(std::move(scales_array));
			}
			else {
				ConfigEntry planes_array = ConfigEntry::Array("planes", ConfigPrimitive::ePrimitiveType::Float);

				for (const BrushPlane& plane : brush->Planes) {
					planes_array.AppendValue(ConfigPrimitive::FromValue(plane.Normal.X));
					planes_array.AppendValue(ConfigPrimitive::FromValue(plane.Normal.Y));
					planes_array.AppendValue(ConfigPrimitive::FromValue(plane.Normal.Z));
					planes_array.AppendValue(ConfigPrimitive::FromValue(plane.Distance));
				}

				blockout_entry.AddMember(std::move(planes_array));
			}

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
