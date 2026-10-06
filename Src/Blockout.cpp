#include "Blockout.hpp"

#include <Asset/AssetManager.hpp>
#include <Asset/ConfigFile.hpp>
#include <Asset/MeshGen.hpp>
#include <CVar.hpp>
#include <Core/RefUtil.hpp>
#include <Editor/RaptorEditor.hpp>
#include <Engine.hpp>
#include <Material/Material.hpp>
#include <Material/MaterialManager.hpp>
#include <Math/SIMDHelper.hpp>
#include <Physics/JoltPhysicsBackend.hpp>
#include <Physics/PhysicsManager.hpp>
#include <Renderer/LightManager.hpp>
#include <Renderer/PipelineNames.hpp>
#include <Util/RustInterop.hpp>
#include <World.hpp>

#include <deque>
#include <memory>
#include <unordered_set>
#include <vector>


namespace fx {

static_assert(sizeof(RxLevelSun) == 44);
static_assert(sizeof(RxLevelLight) == 120);
static_assert(sizeof(RxLevelCamera) == 36);
static_assert(sizeof(RxLevelPlane) == 40);
static_assert(sizeof(RxLevelBlock) == 120);
static_assert(sizeof(RxLevelWriteSun) == 40);
static_assert(sizeof(RxLevelWriteLight) == 80);
static_assert(sizeof(RxLevelWritePlane) == 36);
static_assert(sizeof(RxLevelWriteBlock) == 120);
static_assert(sizeof(RxLevelWrite) == 88);

namespace {

struct LevelDeleter
{
	void operator()(RxLevel* level) const { rx_level_free(level); }
};

using LevelPtr = std::unique_ptr<RxLevel, LevelDeleter>;

const RxLogSink scLevelLog = { .user = nullptr, .log = RustInterop::Log };

LevelPtr ReadLevel(const char* path)
{
	std::vector<uint8> bytes;

	if (!ReadConfigFileBytes(path, bytes)) {
		return nullptr;
	}

	const RxHost host = MakeConfigHost();
	const std::string constants_path = GetConfigConstantsPath();

	return LevelPtr(rx_level_parse(bytes.data(), bytes.size(), constants_path.c_str(), &host));
}

} // namespace

static constexpr int32 scDefaultMaterialID = 0;
static constexpr int32 scEditableMaterialID = 1;

Blockout::Blockout() {}

void Blockout::Create(World* world)
{
	BlockoutObjects.Init(256);

	pWorld = world;

	mMaterials.Load("RaptorData/Data/materials/list.prx", "RaptorData/Data/materials");

	// Selection material
	{
		SelectionMaterialID = gMaterialManager->NewMaterial("ProtoSelect", false);
		Material* test_material = gMaterialManager->GetMaterial(SelectionMaterialID);

		AssetTicket diffuse = gAssetManager->LoadImage(eImageType::Flat, eImageFormat::RGBA8_UNorm,
													   "RaptorData/Data/Demo/Textures/aqua_check.png",
													   eImageCreateFlags::None);

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
		// pXFormObject->SetUnlit(true);
		pXFormObject->SetProbeVisible(false);

		pXFormObject->SetMesh(cube_mesh->AsDefaultMesh());


		AssetTicket ticket(static_cast<void*>(pXFormObject));
		ticket.MarkAndSignalLoaded();

		pWorld->Attach(ticket);

		pXFormObject->SetPosition(Vec3f(200.0f));
	}

	{
		pPreviewObject = gObjectManager->NewObject("PROTO_PREVIEW", SelectionMaterialID,
												   eObjectTag::Blockout | eObjectTag::LockTransform);
		// pPreviewObject->SetUnlit(true);
		pPreviewObject->SetProbeVisible(false);

		// Needs a mesh to be added to the world; ShowPreview() replaces it
		Brush brush = Brush::FromBox(Vec3f(-0.25f), Vec3f(0.25f));
		Ref<MeshGen::GeneratedMesh> mesh = MakeRef<MeshGen::GeneratedMesh>();
		brush.GenerateMesh(mesh->Positions, mesh->Normals, mesh->Tangents, mesh->Texcoords, mesh->Indices);
		pPreviewObject->SetMesh(mesh->AsDefaultMesh());
		mPreviewPlanes = brush.Planes;

		AssetTicket ticket(static_cast<void*>(pPreviewObject));
		ticket.MarkAndSignalLoaded();

		pWorld->Attach(ticket);

		HidePreview();
	}
}


constexpr float scMinBlockoutThickness = 0.1f;

static physics::eMotionType GetMotionType(const Object* object)
{
	physics::Body* body = gPhysics->GetBody(object->PhysicsID);
	return (body != nullptr) ? body->GetMotionType() : physics::eMotionType::Static;
}

bool Blockout::MoveFace(Object* object, const Vec3f& face_normal, float32 distance)
{
	Brush* brush = GetBrush(object);
	if (brush == nullptr) {
		return false;
	}

	const BrushPlaneBuffer planes(brush->Planes);
	BrushPlaneBuffer moved;

	moved.Count = rx_blockout_move_face(planes.Planes, planes.Count, &face_normal.mData[0], distance,
										scMinBlockoutThickness, moved.Planes, Brush::scMaxPlanes);

	if (moved.Count == 0) {
		LogWarning("Cannot move the face of blockout '{}' facing {} that far", object->Name.Get(), face_normal);
		return false;
	}

	ApplyBrushInPlace(object, Brush::FromPlanes(moved.ToList()));

	return true;
}

bool Blockout::SetBrushPlanes(Object* object, const Brush::PlaneList& planes)
{
	if (object == nullptr) {
		return false;
	}

	Brush brush = Brush::FromPlanes(planes);
	if (!brush.IsValid()) {
		LogError("Cannot set the planes of blockout '{}', they do not make a valid brush", object->Name.Get());
		return false;
	}

	ApplyBrushInPlace(object, std::move(brush));

	return true;
}

static Vec3f GetOffsetToKeepInPlace(const Object* object, const Vec3f& old_center, const Vec3f& new_center)
{
	Vec3f offset;
	rx_blockout_keep_in_place(&object->mRotation.mData[0], &old_center.mData[0], &new_center.mData[0],
							  &offset.mData[0]);
	return offset;
}

void Blockout::ApplyBrushInPlace(Object* object, Brush&& brush)
{
	const Brush* current = GetBrush(object);

	if (current != nullptr) {
		const Vec3f offset = GetOffsetToKeepInPlace(object, current->GetCenter(), brush.GetCenter());

		if (!offset.IsCloseTo(simd::LoadFloat4(0.0f))) {
			object->SetPosition(object->GetPosition() + offset);
		}
	}

	ApplyBrush(object, std::move(brush), GetMotionType(object));
}

bool Blockout::GetClipPieces(Object* object, const Vec3f& point_a, const Vec3f& point_b, const Vec3f& face_normal,
							 Brush::PlaneList& out_kept, Brush::PlaneList& out_split, Vec3f& out_split_position)
{
	const Brush* brush = GetBrush(object);
	if (brush == nullptr) {
		return false;
	}

	const Mat4f to_local = object->GetWorldMatrix().Inverse();
	const BrushPlaneBuffer planes(brush->Planes);

	BrushPlaneBuffer kept;
	BrushPlaneBuffer split;
	size_t counts[2] = {};

	if (!rx_blockout_clip(planes.Planes, planes.Count, &to_local.Rows[0].mData[0], &object->mRotation.mData[0],
						  &object->GetPosition().mData[0], &point_a.mData[0], &point_b.mData[0],
						  &face_normal.mData[0], kept.Planes, split.Planes, Brush::scMaxPlanes, counts,
						  &out_split_position.mData[0])) {
		return false;
	}

	kept.Count = counts[0];
	split.Count = counts[1];

	out_kept = kept.ToList();
	out_split = split.ToList();

	return true;
}

Brush Blockout::MakeWorldBox(const Vec3f& min, const Vec3f& max, Vec3f& out_position) const
{
	BrushPlaneBuffer planes;
	planes.Count = rx_blockout_world_box(&min.mData[0], &max.mData[0], planes.Planes, Brush::scMaxPlanes,
										 &out_position.mData[0]);

	return Brush::FromPlanes(planes.ToList());
}

void Blockout::ShowPreview(const Vec3f& position, const Quat& rotation, const Brush& brush)
{
	if (!brush.IsValid()) {
		HidePreview();
		return;
	}

	const BrushPlaneBuffer planes(brush.Planes);
	const BrushPlaneBuffer previous(mPreviewPlanes);

	const bool is_same_brush = rx_blockout_same_planes(planes.Planes, planes.Count, previous.Planes, previous.Count);

	// Only rebuild the mesh when the brush changes, as dragging mostly moves it between the same few snapped sizes
	if (!is_same_brush) {
		Ref<MeshGen::GeneratedMesh> mesh = MakeRef<MeshGen::GeneratedMesh>();
		brush.GenerateMesh(mesh->Positions, mesh->Normals, mesh->Tangents, mesh->Texcoords, mesh->Indices);

		pPreviewObject->SetMesh(mesh->AsDefaultMesh());
		pPreviewObject->Bounds.Min = brush.GetBoundsMin();
		pPreviewObject->Bounds.Max = brush.GetBoundsMax();
		pPreviewObject->SetRotationOrigin(-brush.GetCenter());

		mPreviewPlanes = brush.Planes;
	}

	pPreviewObject->SetRotation(rotation);
	pPreviewObject->SetPosition(position);
}

void Blockout::HidePreview()
{
	// Out of the way, the same place the transform marker hides
	pPreviewObject->SetPosition(Vec3f(-200.0f));
}

bool Blockout::GetFaceTextureEdit(Object* object, const Vec3f& face_normal, eFaceTextureEdit edit, const Vec2f& amount,
								  Brush::PlaneList& out_planes)
{
	Brush* brush = GetBrush(object);
	if (brush == nullptr) {
		return false;
	}

	const BrushPlaneBuffer planes(brush->Planes);
	BrushPlaneBuffer edited;

	const float32 amount_values[2] = { amount.X, amount.Y };

	edited.Count = rx_blockout_edit_face_texture(planes.Planes, planes.Count, &face_normal.mData[0],
												 static_cast<uint32>(edit), amount_values, edited.Planes,
												 Brush::scMaxPlanes);

	if (edited.Count == 0) {
		return false;
	}

	out_planes = edited.ToList();

	return true;
}

Object* Blockout::RaycastBlockout(const Vec3f& origin, const Vec3f& direction, Vec3f& out_face_normal)
{
	// Sorted nearest first
	SizedArray<physics::BodyHandle> hits = gPhysics->pBackend->RaycastObjects(origin, direction);

	for (const physics::BodyHandle& body_id : hits) {
		physics::Body* body = gPhysics->FindBody(body_id);
		if (body == nullptr) {
			continue;
		}

		Object* object = gObjectManager->GetObject(body->GetObjectID());

		if (object != nullptr && RaycastFace(object, origin, direction, out_face_normal)) {
			return object;
		}
	}

	return nullptr;
}

bool Blockout::RaycastFace(Object* object, const Vec3f& origin, const Vec3f& direction, Vec3f& out_face_normal,
						   Vec3f* out_point)
{
	const Brush* brush = GetBrush(object);
	if (brush == nullptr) {
		return false;
	}

	// Bring the ray into the brush's local space
	const Mat4f to_local = object->GetWorldMatrix().Inverse();
	const Vec4f local_origin = to_local * Vec4f(origin.X, origin.Y, origin.Z, 1.0f);
	const Vec4f local_direction = to_local * Vec4f(direction.X, direction.Y, direction.Z, 0.0f);

	float32 distance;
	uint32 plane_index;

	if (!brush->Raycast(Vec3f(local_origin.X, local_origin.Y, local_origin.Z),
						Vec3f(local_direction.X, local_direction.Y, local_direction.Z), distance, plane_index)) {
		return false;
	}

	out_face_normal = brush->Planes[plane_index].Normal;

	// The transform to local space is affine, so the distance along the ray is the same in world space
	if (out_point != nullptr) {
		*out_point = origin + direction * distance;
	}

	return true;
}

void Blockout::ReloadSingleObject(Object* object)
{
	if (object == nullptr || !object->HasTags(eObjectTag::Blockout)) {
		return;
	}

	LevelPtr level = ReadLevel(gWorld->BlockoutPath.CStr());

	if (level == nullptr || rx_level_has_errors(level.get())) {
		return;
	}

	ObjectID old_object_id = object->ID;
	Hash32 object_name_hash = object->Name.GetHash();

	ObjectID new_object_id;

	for (uint32 i = 0; i < rx_level_block_count(level.get()); i++) {
		const RxLevelBlock* block = rx_level_block(level.get(), i);

		if (HashStr32(std::string(block->name, block->name_length).c_str()) == object_name_hash) {
			RemoveSingleObjectFromWorld(object);
			new_object_id = CreateBrushObject(*block);
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

	mBrushes.Remove(object->ID.GetID());

	gObjectManager->DestroyObject(object->ID);
}

void Blockout::RemoveBlockoutFromWorld(World* world)
{
	for (ObjectID box_id : BlockoutObjects) {
		RemoveSingleObjectFromWorld(gObjectManager->GetObject(box_id));
	}

	BlockoutObjects.Clear();
	mBrushes.Clear();
}

MaterialID Blockout::GetMaterialForID(int32 id) const
{
	const MaterialID material = mMaterials.GetMaterial(id);

	return material.IsNull() ? GetDefaultMaterial() : material;
}

int32 Blockout::GetIDForMaterial(const MaterialID& material) const { return mMaterials.FindID(material); }

MaterialID Blockout::GetDefaultMaterial() const { return mMaterials.GetMaterial(scDefaultMaterialID); }

Brush* Blockout::GetBrush(const Object* object)
{
	if (object == nullptr) {
		return nullptr;
	}

	return mBrushes.Find(object->ID.GetID());
}

void Blockout::ApplyBrush(Object* object, Brush&& brush, physics::eMotionType motion_type)
{
	Assert(brush.IsValid());

	Ref<MeshGen::GeneratedMesh> mesh = MakeRef<MeshGen::GeneratedMesh>();
	brush.GenerateMesh(mesh->Positions, mesh->Normals, mesh->Tangents, mesh->Texcoords, mesh->Indices);

	object->SetMesh(mesh->AsDefaultMesh());

	object->Bounds.Min = brush.GetBoundsMin();
	object->Bounds.Max = brush.GetBoundsMax();

	// Blockouts rotate about the centre of their bounds
	const Vec3f midpoint = brush.GetCenter();
	object->SetRotationOrigin(-midpoint);

	gWorldGrid->UpdateObject(object);

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

	mBrushes.Insert(object->ID.GetID(), std::move(brush));
}

Brush Blockout::MakeBrush(const RxLevelBlock& block) const
{
	BrushPlaneBuffer planes;
	planes.Count = rx_blockout_make_planes(&block, planes.Planes, Brush::scMaxPlanes);

	if (planes.Count == 0) {
		return {};
	}

	return Brush::FromPlanes(planes.ToList());
}

ObjectID Blockout::CreateBrushObject(const RxLevelBlock& block)
{
	const std::string block_name(block.name, block.name_length);

	Vec3f position(block.position);

	String blockout_id = String::Fmt("{}", block_name);

	LogInfo("Adding blockout '{}'", blockout_id);

	Brush brush = MakeBrush(block);

	if (!brush.IsValid()) {
		LogError("Blockout '{}' does not describe a valid convex brush, skipping", blockout_id);
		return ObjectID::scNull;
	}

	MaterialID material_id = GetDefaultMaterial();

	eObjectTag object_tags = eObjectTag::Blockout;

	if (block.locked) {
		SetFlag(object_tags, eObjectTag::LockTransform);
	}
	else {
		material_id = mMaterials.GetMaterial(scEditableMaterialID);
	}

	if (block.has_material) {
		if (mMaterials.GetMaterial(block.material).IsNull()) {
			LogWarning(LC_ASSET, "Blockout '{}' uses the unknown material {}", block_name, block.material);
		}

		material_id = GetMaterialForID(block.material);
	}

	Object* object = gObjectManager->NewObject(blockout_id.Str(), material_id, object_tags);
	object->MoveBy(position);
	object->SetShadowCaster(true);

	Quat rotation = Quat::scIdentity;

	if (block.rotation_kind == RX_LEVEL_ROTATION_EULER) {
		rotation = Quat::FromEulerAngles(Vec3f(block.rotation));
	}
	else if (block.rotation_kind == RX_LEVEL_ROTATION_QUAT) {
		rotation = Quat(block.rotation);
	}

	object->SetRotation(rotation);

	if (block.probe_volume) {
		object->SetProbeVolume(true);
	}

	if (block.reflection_probe) {
		object->SetReflectionProbe(true);
	}

	if (block.bleeds) {
		object->SetTag(eObjectTag::Bleeds);
	}

	ApplyBrush(object, std::move(brush),
			   block.dynamic ? physics::eMotionType::Dynamic : physics::eMotionType::Static);

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

	const Brush* existing = GetBrush(object);

	Brush brush = (existing != nullptr) ? Brush::FromPlanes(existing->Planes)
										: Brush::FromBox(object->Bounds.Min, object->Bounds.Max);

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

	mBrushes.Remove(object->ID.GetID());

	gWorld->Detach(object->ID);
	gObjectManager->DestroyObject(object->ID);
}

Object* Blockout::NewObject(const Vec3f& position)
{
	std::string blockout_name = String::Fmt("{}", BlockoutObjects.Size).Str();
	LogInfo("Creating new blockout object '{}'", blockout_name);

	Object* object = gObjectManager->NewObject(blockout_name, GetDefaultMaterial(), eObjectTag::Blockout);

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

	if (object->IsReflectionProbe()) {
		dupe->SetReflectionProbe(true);
	}

	if (object->Bleeds()) {
		dupe->SetTag(eObjectTag::Bleeds);
	}

	ApplyBrush(dupe, std::move(brush), motion_type);

	AssetTicket ticket(static_cast<void*>(dupe));
	ticket.MarkAndSignalLoaded();

	gWorld->Attach(ticket);

	BlockoutObjects.NewItem(nullptr, dupe->ID);

	return dupe;
}

bool Blockout::IsDynamic(const Object* object) const
{
	return object != nullptr && GetMotionType(object) == physics::eMotionType::Dynamic;
}

Object* Blockout::RestoreObject(const Vec3f& position, const Brush::PlaneList& planes, MaterialID material,
								const Quat& rotation, const Name& name, bool is_dynamic)
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

	Object* object = gObjectManager->NewObject(
		blockout_name, material.IsNull() ? mMaterials.GetMaterial(scEditableMaterialID) : material,
		eObjectTag::Blockout);

	object->MoveBy(position);
	object->SetShadowCaster(true);
	object->SetRotation(rotation);

	ApplyBrush(object, std::move(brush), is_dynamic ? physics::eMotionType::Dynamic : physics::eMotionType::Static);

	AssetTicket ticket(static_cast<void*>(object));
	ticket.MarkAndSignalLoaded();

	pWorld->Attach(ticket);

	BlockoutObjects.NewItem(nullptr, object->ID);

	return object;
}

static void ApplyLightColor(LightBase& light, const int32* color, bool has_color, float32 intensity,
							bool has_intensity)
{
	if (has_color) {
		light.Color = Color::FromRGBA(color[0], color[1], color[2], 255);
	}
	else {
		light.Color = Color::FromRGBA(light.Color.R, light.Color.G, light.Color.B, 255);
	}

	if (has_intensity) {
		light.Intensity = intensity;
	}
}

void Blockout::AddOrUpdateLightFromEntry(const RxLevelLight& light_entry)
{
	const std::string light_name(light_entry.name, light_entry.name_length);

	eLightType light_type;

	switch (light_entry.kind) {
	case RX_LEVEL_LIGHT_POINT:
		light_type = eLightType::Point;
		break;
	case RX_LEVEL_LIGHT_SPOT:
		light_type = eLightType::Spot;
		break;
	default:
		LogError(LC_ASSET, "Light '{}' has unknown type {}", light_name, light_entry.kind);
		return;
	}

	// Lights are matched by name so a hot reload updates them in place
	Ref<LightBase> light = gLightManager->FindLight(HashStr32(light_name.c_str()));

	if (light.IsValid() && light->Type != light_type) {
		LogWarning(LC_ASSET, "Light '{}' changed type, restart to apply", light_name);
		return;
	}

	if (!light.IsValid()) {
		if (light_type == eLightType::Spot) {
			light = gLightManager->NewLight<LightSpot>(light_name);
		}
		else {
			light = gLightManager->NewLight<LightPoint>(light_name);
		}
	}

	if (light_entry.has_position) {
		light->SetPosition(Vec3f(light_entry.position));
	}

	ApplyLightColor(*light, light_entry.color, light_entry.has_color, light_entry.intensity,
					light_entry.has_intensity);
	light->SetRadius(light_entry.radius);

	if (light_type == eLightType::Spot) {
		Ref<LightSpot> spot(light);

		if (light_entry.has_direction) {
			spot->SetDirection(Vec3f(light_entry.direction));
		}
		else {
			spot->SetRotation(light_entry.has_rotation ? Quat(light_entry.rotation) : spot->mRotation);
		}

		spot->SetConeAngles(MathUtil::DegreesToRadians(light_entry.inner_degrees),
							MathUtil::DegreesToRadians(light_entry.outer_degrees));

		spot->bCastShadows = (light_entry.shadows != 0);
	}
}

void Blockout::LoadLights(const RxLevel* level)
{
	// Load sun
	Ref<LightDirectional> sun = gLightManager->GetDirectionalLight();
	if (!sun.IsValid()) {
		sun = gLightManager->NewLight<LightDirectional>("Sun");
	}

	const RxLevelSun* sun_entry = rx_level_sun(level);

	const bool sun_enabled = (sun_entry->present != 0) && (sun_entry->enabled != 0);

	sun->bEnabled = sun_enabled;
	gCVars->Set("b_sun_enabled", sun_enabled);

	if (sun_entry->present) {
		sun->SetPosition(Vec3f(sun_entry->position));

		ApplyLightColor(*sun, sun_entry->color, sun_entry->has_color, sun_entry->intensity,
						sun_entry->has_intensity);
	}

	// Load point and spot lights

	std::unordered_set<Hash32> names_in_file;

	for (uint32 i = 0; i < rx_level_light_count(level); i++) {
		const RxLevelLight* light_entry = rx_level_light(level, i);

		AddOrUpdateLightFromEntry(*light_entry);

		names_in_file.insert(HashStr32(std::string(light_entry->name, light_entry->name_length).c_str()));
	}

	std::vector<LightID> stale_lights;

	for (const Ref<LightBase>& light : gLightManager->GetCache()) {
		if (!light.IsValid() || (light->Type != eLightType::Point && light->Type != eLightType::Spot)) {
			continue;
		}

		if (!names_in_file.contains(light->Name.GetHash())) {
			stale_lights.push_back(light->ID);
		}
	}

	for (LightID& id : stale_lights) {
		gLightManager->DestroyLight(id);
	}
}

void Blockout::LoadCamera(const RxLevel* level)
{
	const RxLevelCamera* camera = rx_level_camera(level);

	if (!camera->present) {
		return;
	}

	gCVars->Set("r_aperture", camera->has_aperture ? camera->aperture : gCVars->Get("r_aperture", 16.0f));
	gCVars->Set("r_shutter", camera->has_shutter ? camera->shutter : gCVars->Get("r_shutter", 0.01f));
	gCVars->Set("r_iso", camera->has_iso ? camera->iso : gCVars->Get("r_iso", 100.0f));
	gCVars->Set("r_exposure_ev",
				camera->has_exposure_ev ? camera->exposure_ev : gCVars->Get("r_exposure_ev", 0.0f));
}

bool Blockout::Load(const String& path)
{
	LevelPtr level = ReadLevel(path.CStr());

	if (level != nullptr && rx_level_has_errors(level.get())) {
		return false;
	}

	if (level == nullptr || !rx_level_has_blocks(level.get())) {
		LogError("Blockout '{}' could not be loaded or has no 'all' entry", path);
		return false;
	}

#ifdef FX_IS_EDITOR
	// Everything is getting reloaded, so the editor can't hold on to any objects
	gEditor->ForgetObjects();
#endif

	LoadLights(level.get());
	LoadCamera(level.get());

	// Remove the current blockout from the world
	RemoveBlockoutFromWorld(pWorld);

	for (uint32 i = 0; i < rx_level_block_count(level.get()); i++) {
		CreateBrushObject(*rx_level_block(level.get(), i));
	}

	return true;
}

static void StoreLastBlockoutPath(const String& path, const char* config_path)
{
	ConfigFile config {};
	config.Load(config_path);

	if (config.HasErrors() || config.GetEntries().Size() == 0) {
		LogWarning(LC_ASSET, "Could not update the blockout entry in {}", config_path);
		return;
	}

	ConfigEntry* entry = config.GetEntry(HashStr32("blockout"));

	if (entry != nullptr) {
		entry->Set(std::string(path.CStr()));
	}
	else {
		config.AddEntry(ConfigEntry::Literal("blockout", path.CStr()));
	}

	config.Write(config_path);
}

static void CopyColor(int32 out_color[4], const Color& color)
{
	out_color[0] = static_cast<int32>(color.R);
	out_color[1] = static_cast<int32>(color.G);
	out_color[2] = static_cast<int32>(color.B);
	out_color[3] = static_cast<int32>(color.A);
}

static void CopyVec3(float32 out[3], const Vec3f& value)
{
	out[0] = value.X;
	out[1] = value.Y;
	out[2] = value.Z;
}

bool Blockout::WriteLevelFile(const String& path)
{
	RxLevelWrite write {};

	Ref<LightDirectional> sun = gLightManager->GetDirectionalLight();

	if (sun.IsValid()) {
		write.sun.present = 1;
		write.sun.enabled = sun->bEnabled ? 1 : 0;
		CopyVec3(write.sun.position, sun->GetPosition());
		CopyColor(write.sun.color, sun->Color);
		write.sun.intensity = sun->Intensity;
	}

	std::vector<RxLevelWriteLight> lights;

	for (const Ref<LightBase>& light : gLightManager->GetCache()) {
		if (light->Type != eLightType::Point && light->Type != eLightType::Spot) {
			continue;
		}

		RxLevelWriteLight out {};

		out.name = light->Name.Get().c_str();
		out.name_length = light->Name.Get().size();
		out.spot = (light->Type == eLightType::Spot) ? 1 : 0;
		CopyVec3(out.position, light->GetPosition());
		CopyColor(out.color, light->Color);
		out.intensity = light->Intensity;
		out.radius = light->GetRadius();

		if (light->Type == eLightType::Spot) {
			Ref<LightSpot> spot(light);

			CopyVec3(out.direction, spot->GetDirection());
			out.inner_degrees = MathUtil::RadiansToDegrees(spot->GetInnerAngle());
			out.outer_degrees = MathUtil::RadiansToDegrees(spot->GetOuterAngle());
			out.shadows = spot->bCastShadows ? 1 : 0;
		}

		lights.push_back(out);
	}

	write.lights = lights.data();
	write.light_count = static_cast<uint32>(lights.size());

	write.camera[0] = gCVars->Get("r_aperture", 16.0f);
	write.camera[1] = gCVars->Get("r_shutter", 0.01f);
	write.camera[2] = gCVars->Get("r_iso", 100.0f);
	write.camera[3] = gCVars->Get("r_exposure_ev", 0.0f);

	std::vector<RxLevelWriteBlock> blocks;
	std::deque<std::vector<RxLevelWritePlane>> plane_lists;

	for (const ObjectID box_id : BlockoutObjects) {
		Object* object = gObjectManager->GetObject(box_id);
		if (object == nullptr) {
			continue;
		}

		const Brush* brush = GetBrush(object);

		RxLevelWriteBlock out {};

		out.name = object->Name.Get().c_str();
		out.name_length = object->Name.Get().size();
		CopyVec3(out.position, object->mPosition);

		const uint32 shape = (brush != nullptr) ? rx_blockout_save_shape(BrushPlaneBuffer(brush->Planes).Planes,
																			brush->Planes.Size)
												: RX_BLOCKOUT_SHAPE_BOX;

		if ((shape & RX_BLOCKOUT_SHAPE_BOX) != 0) {
			out.brush_kind = RX_LEVEL_BRUSH_BOX;
			out.box_extents[0] = -object->Bounds.Min.X;
			out.box_extents[1] = object->Bounds.Max.X;
			out.box_extents[2] = object->Bounds.Max.Y;
			out.box_extents[3] = -object->Bounds.Min.Y;
			out.box_extents[4] = object->Bounds.Max.Z;
			out.box_extents[5] = -object->Bounds.Min.Z;
		}
		else {
			out.brush_kind = RX_LEVEL_BRUSH_PLANES;

			std::vector<RxLevelWritePlane>& planes = plane_lists.emplace_back();

			for (const BrushPlane& plane : brush->Planes) {
				RxLevelWritePlane out_plane {};

				CopyVec3(out_plane.normal, plane.Normal);
				out_plane.distance = plane.Distance;
				out_plane.offset[0] = plane.Texture.Offset.X;
				out_plane.offset[1] = plane.Texture.Offset.Y;
				out_plane.scale[0] = plane.Texture.Scale.X;
				out_plane.scale[1] = plane.Texture.Scale.Y;
				out_plane.rotation = plane.Texture.Rotation;

				planes.push_back(out_plane);
			}

			out.planes = planes.data();
			out.plane_count = static_cast<uint32>(planes.size());
			out.textures = (shape & RX_BLOCKOUT_SHAPE_TEXTURES) != 0 ? 1 : 0;
		}

		out.rotation[0] = object->mRotation.X;
		out.rotation[1] = object->mRotation.Y;
		out.rotation[2] = object->mRotation.Z;
		out.rotation[3] = object->mRotation.W;

		out.locked = object->HasTags(eObjectTag::LockTransform) ? 1 : 0;
		out.probe_volume = object->IsProbeVolume() ? 1 : 0;
		out.reflection_probe = object->IsReflectionProbe() ? 1 : 0;
		out.bleeds = object->Bleeds() ? 1 : 0;
		out.dynamic = IsDynamic(object) ? 1 : 0;

#ifdef FX_IS_EDITOR
		const MaterialID object_material = gEditor->GetSelection().GetStoredMaterial(object);
#else
		const MaterialID object_material = object->GetMaterialID();
#endif

		const int32 material_id = GetIDForMaterial(object_material);

		if (material_id >= 0) {
			out.has_material = 1;
			out.material = material_id;
		}

		blocks.push_back(out);
	}

	write.blocks = blocks.data();
	write.block_count = static_cast<uint32>(blocks.size());

	if (!rx_level_save(path.CStr(), &write, &scLevelLog)) {
		LogError(LC_ASSET, "Could not write the blockout '{}'", path);
		return false;
	}

	return true;
}

void Blockout::Save(const String& path)
{
	WriteLevelFile(path);

	StoreLastBlockoutPath(path, "Config/Main.conf");
}


Blockout::~Blockout() {}


} // namespace fx
