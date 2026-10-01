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
#include <World.hpp>


namespace fx {

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

		pXFormObject->pMesh = cube_mesh->AsDefaultMesh();


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
		pPreviewObject->pMesh = mesh->AsDefaultMesh();
		mPreviewPlanes = brush.Planes;

		AssetTicket ticket(static_cast<void*>(pPreviewObject));
		ticket.MarkAndSignalLoaded();

		pWorld->Attach(ticket);

		HidePreview();
	}
}


constexpr float scMinBlockoutThickness = 0.1f;

/// Degrees per full turn, used to keep face texture rotations in [0, 360)
constexpr float32 scDegreesPerTurn = 360.0f;

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

	const int32 plane_index = brush->FindPlane(face_normal);
	if (plane_index == Brush::scNoPlane) {
		LogWarning("Blockout '{}' has no face facing {}", object->Name.Get(), face_normal);
		return false;
	}

	Brush::PlaneList planes = brush->Planes;
	BrushPlane& plane = planes[plane_index];

	const float32 thickness = plane.Distance + brush->GetSupport(-plane.Normal);
	plane.Distance += std::max(distance, scMinBlockoutThickness - thickness);

	Brush moved = Brush::FromPlanes(planes);
	if (!moved.IsValid()) {
		LogWarning("Cannot move the face of blockout '{}' that far", object->Name.Get());
		return false;
	}

	ApplyBrushInPlace(object, std::move(moved));

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
	const Vec3f center_delta = new_center - old_center;
	return center_delta.Rotate(object->mRotation) - center_delta;
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

	const Vec4f local_a = to_local * Vec4f(point_a.X, point_a.Y, point_a.Z, 1.0f);
	const Vec4f local_b = to_local * Vec4f(point_b.X, point_b.Y, point_b.Z, 1.0f);
	const Vec4f local_normal = to_local * Vec4f(face_normal.X, face_normal.Y, face_normal.Z, 0.0f);

	const Vec3f a(local_a.X, local_a.Y, local_a.Z);
	const Vec3f b(local_b.X, local_b.Y, local_b.Z);

	// The cut runs along the line and straight down through the face it was drawn on
	const Vec3f cut_normal = (b - a).Cross(Vec3f(local_normal.X, local_normal.Y, local_normal.Z));

	if (cut_normal.IsCloseTo(simd::LoadFloat4(0.0f))) {
		return false;
	}

	const Vec3f normal = cut_normal.Normalize();

	// The cut faces line up with the world grid, like the faces of a box made with MakeWorldBox()
	if (!brush->Split(normal, normal.Dot(a), object->GetPosition(), out_kept, out_split)) {
		return false;
	}

	const Brush split = Brush::FromPlanes(out_split);

	// The split piece keeps the blockout's local space, so its textures carry on from the other piece
	out_split_position = object->GetPosition() + GetOffsetToKeepInPlace(object, brush->GetCenter(), split.GetCenter());

	return true;
}

Brush Blockout::MakeWorldBox(const Vec3f& min, const Vec3f& max, Vec3f& out_position) const
{
	out_position = (min + max) * 0.5f;

	const Vec3f half_size = (max - min) * 0.5f;

	Brush brush = Brush::FromBox(-half_size, half_size);
	brush.AlignTexturesToWorld(out_position);

	return brush;
}

void Blockout::ShowPreview(const Vec3f& position, const Quat& rotation, const Brush& brush)
{
	if (!brush.IsValid()) {
		HidePreview();
		return;
	}

	bool is_same_brush = (brush.Planes.Size == mPreviewPlanes.Size);

	for (uint32 i = 0; is_same_brush && i < brush.Planes.Size; i++) {
		is_same_brush = brush.Planes[i].Normal.IsCloseTo(mPreviewPlanes[i].Normal, 0.0f) &&
						brush.Planes[i].Distance == mPreviewPlanes[i].Distance;
	}

	// Only rebuild the mesh when the brush changes, as dragging mostly moves it between the same few snapped sizes
	if (!is_same_brush) {
		Ref<MeshGen::GeneratedMesh> mesh = MakeRef<MeshGen::GeneratedMesh>();
		brush.GenerateMesh(mesh->Positions, mesh->Normals, mesh->Tangents, mesh->Texcoords, mesh->Indices);

		pPreviewObject->pMesh = mesh->AsDefaultMesh();
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

	const int32 plane_index = brush->FindPlane(face_normal);
	if (plane_index == Brush::scNoPlane) {
		return false;
	}

	out_planes = brush->Planes;
	BrushFaceTexture& texture = out_planes[plane_index].Texture;

	switch (edit) {
	case eFaceTextureEdit::Shift:
		texture.Offset = texture.Offset + amount;
		break;
	case eFaceTextureEdit::Scale:
		texture.Scale = texture.Scale * amount;
		break;
	case eFaceTextureEdit::Rotate:
		texture.Rotation = std::fmod(texture.Rotation + amount.X + scDegreesPerTurn, scDegreesPerTurn);
		break;
	case eFaceTextureEdit::Reset: {
		// The default layout depends on the brush's bounds, so let the brush work it out
		Brush reset = Brush::FromPlanes(out_planes);
		reset.ResetFaceTexture(static_cast<uint32>(plane_index));
		out_planes = reset.Planes;
		break;
	}
	}

	return true;
}

Object* Blockout::RaycastBlockout(const Vec3f& origin, const Vec3f& direction, Vec3f& out_face_normal)
{
	// Sorted nearest first
	SizedArray<JPH::BodyID> hits = gPhysics->pBackend->RaycastObjects(origin, direction);

	for (const JPH::BodyID& body_id : hits) {
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

	mBrushes[object->ID.GetID()] = std::move(brush);
}

/// Values stored per plane in a blockout's `uvs` array: offset U and V, scale U and V, then rotation
constexpr uint32 scValuesPerFaceTexture = 5;

Brush Blockout::ReadBrushEntry(ConfigEntry& entry) const
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

	if (planes_entry == nullptr) {
		return {};
	}

	const PagedArray<ConfigPrimitive>& values = planes_entry->GetArrayData();

	// Four values per plane: the outward normal, then the distance from the origin
	const uint32 plane_count = static_cast<uint32>(values.Size() / 4);

	if (plane_count > Brush::scMaxPlanes) {
		LogError("Blockout '{}' has {} planes, the maximum is {}", entry.Name.Get(), plane_count, Brush::scMaxPlanes);
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

	ConfigEntry* uvs_entry = entry.GetMember(HashStr32("uvs"));

	if (uvs_entry == nullptr) {
		// Without a layout of their own, the faces get the default one, which depends on the finished brush
		Brush brush = Brush::FromPlanes(planes);

		for (uint32 i = 0; i < brush.Planes.Size; i++) {
			brush.ResetFaceTexture(i);
		}

		return brush;
	}

	const PagedArray<ConfigPrimitive>& uvs = uvs_entry->GetArrayData();

	for (uint32 i = 0; i < plane_count && (i + 1) * scValuesPerFaceTexture <= uvs.Size(); i++) {
		const uint32 base = i * scValuesPerFaceTexture;
		BrushFaceTexture& texture = planes[i].Texture;

		texture.Offset = Vec2f(uvs[base].Get<float32>(), uvs[base + 1].Get<float32>());
		texture.Scale = Vec2f(uvs[base + 2].Get<float32>(), uvs[base + 3].Get<float32>());
		texture.Rotation = uvs[base + 4].Get<float32>();
	}

	return Brush::FromPlanes(planes);
}

void Blockout::WriteBrushEntry(ConfigEntry& entry, const Object* object)
{
	const Brush* brush = GetBrush(object);

	// Plain boxes are stored as their extents in each direction
	if (brush == nullptr || (brush->IsBox() && brush->HasDefaultTextures())) {
		ConfigEntry scales_array = ConfigEntry::Array("scale", ConfigPrimitive::ePrimitiveType::Float);

		scales_array.AppendValue(ConfigPrimitive::FromValue(-object->Bounds.Min.X));
		scales_array.AppendValue(ConfigPrimitive::FromValue(object->Bounds.Max.X));
		scales_array.AppendValue(ConfigPrimitive::FromValue(object->Bounds.Max.Y));
		scales_array.AppendValue(ConfigPrimitive::FromValue(-object->Bounds.Min.Y));
		scales_array.AppendValue(ConfigPrimitive::FromValue(object->Bounds.Max.Z));
		scales_array.AppendValue(ConfigPrimitive::FromValue(-object->Bounds.Min.Z));

		entry.AddMember(std::move(scales_array));
		return;
	}

	ConfigEntry planes_array = ConfigEntry::Array("planes", ConfigPrimitive::ePrimitiveType::Float);

	for (const BrushPlane& plane : brush->Planes) {
		planes_array.AppendValue(ConfigPrimitive::FromValue(plane.Normal.X));
		planes_array.AppendValue(ConfigPrimitive::FromValue(plane.Normal.Y));
		planes_array.AppendValue(ConfigPrimitive::FromValue(plane.Normal.Z));
		planes_array.AppendValue(ConfigPrimitive::FromValue(plane.Distance));
	}

	entry.AddMember(std::move(planes_array));

	if (!brush->HasDefaultTextures()) {
		ConfigEntry uvs_array = ConfigEntry::Array("uvs", ConfigPrimitive::ePrimitiveType::Float);

		for (const BrushPlane& plane : brush->Planes) {
			const BrushFaceTexture& texture = plane.Texture;

			uvs_array.AppendValue(ConfigPrimitive::FromValue(texture.Offset.X));
			uvs_array.AppendValue(ConfigPrimitive::FromValue(texture.Offset.Y));
			uvs_array.AppendValue(ConfigPrimitive::FromValue(texture.Scale.X));
			uvs_array.AppendValue(ConfigPrimitive::FromValue(texture.Scale.Y));
			uvs_array.AppendValue(ConfigPrimitive::FromValue(texture.Rotation));
		}

		entry.AddMember(std::move(uvs_array));
	}
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

	MaterialID material_id = GetDefaultMaterial();

	eObjectTag object_tags = eObjectTag::Blockout;

	bool is_locked = entry.GetMemberValue(HashStr32("lock"), 0) == 1;
	if (is_locked) {
		SetFlag(object_tags, eObjectTag::LockTransform);
	}
	else {
		material_id = mMaterials.GetMaterial(scEditableMaterialID);
	}

	ConfigEntry* mat_entry = entry.GetMember(HashStr32("mat"));

	if (mat_entry != nullptr) {
		const int32 mat_id = mat_entry->Get<int32>();

		if (mMaterials.GetMaterial(mat_id).IsNull()) {
			LogWarning(LC_ASSET, "Blockout '{}' uses the unknown material {}", entry.Name.Get(), mat_id);
		}

		material_id = GetMaterialForID(mat_id);
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

	mBrushes.erase(object->ID.GetID());

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

	Object* object = gObjectManager->NewObject(
		blockout_name, material.IsNull() ? mMaterials.GetMaterial(scEditableMaterialID) : material,
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

/// Values of the CLight constants in Config/Internal/Constants.conf
enum class ePrxLightType : int64
{
	Point = 0,
	Spot = 1,
};

static void WriteColorEntry(ConfigEntry& parent, const char* name, const Color& color)
{
	ConfigEntry color_array = ConfigEntry::Array(name, ConfigPrimitive::ePrimitiveType::Int);

	color_array.AppendValue(ConfigPrimitive::FromValue(static_cast<int32>(color.R)));
	color_array.AppendValue(ConfigPrimitive::FromValue(static_cast<int32>(color.G)));
	color_array.AppendValue(ConfigPrimitive::FromValue(static_cast<int32>(color.B)));
	color_array.AppendValue(ConfigPrimitive::FromValue(static_cast<int32>(color.A)));

	parent.AddMember(std::move(color_array));
}

static void ReadLightColor(const ConfigEntry& entry, LightBase& light)
{
	const Color color = entry.GetMemberValue(HashStr32("color"), light.Color);

	light.Color = Color::FromRGBA(color.R, color.G, color.B, 255);
	light.Intensity = entry.GetMemberValue<float32>(HashStr32("intensity"), light.Intensity);
}

void Blockout::AddOrUpdateLightFromEntry(const ConfigEntry& light_entry)
{
	const int64 type_value = light_entry.GetMemberValue<int64>(HashStr32("type"),
															   static_cast<int64>(ePrxLightType::Point));

	eLightType light_type;

	switch (static_cast<ePrxLightType>(type_value)) {
	case ePrxLightType::Point:
		light_type = eLightType::Point;
		break;
	case ePrxLightType::Spot:
		light_type = eLightType::Spot;
		break;
	default:
		LogError(LC_ASSET, "Light '{}' has unknown type {}", light_entry.Name.Get(), type_value);
		return;
	}

	// Lights are matched by name so a hot reload updates them in place
	Ref<LightBase> light = gLightManager->FindLight(light_entry.Name.GetHash());

	if (light.IsValid() && light->Type != light_type) {
		LogWarning(LC_ASSET, "Light '{}' changed type, restart to apply", light_entry.Name.Get());
		return;
	}

	if (!light.IsValid()) {
		if (light_type == eLightType::Spot) {
			light = gLightManager->NewLight<LightSpot>(light_entry.Name.Get());
		}
		else {
			light = gLightManager->NewLight<LightPoint>(light_entry.Name.Get());
		}
	}

	light->SetPosition(light_entry.GetMemberValue(HashStr32("pos"), light->GetPosition()));
	ReadLightColor(light_entry, *light);
	light->SetRadius(light_entry.GetMemberValue<float32>(HashStr32("radius"), 5.0f));

	if (light_type == eLightType::Spot) {
		Ref<LightSpot> spot(light);

		ConfigEntry* direction = light_entry.GetMember(HashStr32("dir"));
		if (direction != nullptr) {
			spot->SetDirection(direction->GetValue<Vec3f>());
		}
		else {
			spot->SetRotation(light_entry.GetMemberValue(HashStr32("rot"), spot->mRotation));
		}

		// Cone half-angles, in degrees
		const float32 inner_angle = light_entry.GetMemberValue<float32>(HashStr32("inner"), 20.0f);
		const float32 outer_angle = light_entry.GetMemberValue<float32>(HashStr32("outer"), 30.0f);

		spot->SetConeAngles(MathUtil::DegreesToRadians(inner_angle), MathUtil::DegreesToRadians(outer_angle));

		// Spot lights bake a shadow map into the shadow atlas unless `shadows = $False`
		spot->bCastShadows = (light_entry.GetMemberValue<int64>(HashStr32("shadows"), 1) != 0);
	}
}

void Blockout::LoadLights(ConfigFile& info)
{
	// Load sun
	Ref<LightDirectional> sun = gLightManager->GetDirectionalLight();
	if (!sun.IsValid()) {
		sun = gLightManager->NewLight<LightDirectional>("Sun");
	}

	ConfigEntry* sun_entry = info.GetEntry(HashStr32("sun"));

	const bool sun_enabled = (sun_entry != nullptr) && (sun_entry->GetMemberValue<int64>(HashStr32("enabled"), 1) != 0);

	sun->bEnabled = sun_enabled;
	gCVars->Set("b_sun_enabled", sun_enabled);

	if (sun_entry) {
		sun->SetPosition(sun_entry->GetMemberValue<Vec3f>(HashStr32("pos"), Vec3f::sZero));

		ReadLightColor(*sun_entry, *sun);
	}

	// Load point and spot lights

	ConfigEntry* light_list = info.GetEntry(HashStr32("lights"));
	if (light_list) {
		for (const ConfigEntry& light_entry : light_list->Members) {
			AddOrUpdateLightFromEntry(light_entry);
		}
	}

	std::vector<LightID> stale_lights;

	for (const Ref<LightBase>& light : gLightManager->GetCache()) {
		if (!light.IsValid() || (light->Type != eLightType::Point && light->Type != eLightType::Spot)) {
			continue;
		}

		bool in_file = false;

		if (light_list) {
			for (const ConfigEntry& light_entry : light_list->Members) {
				in_file |= (light_entry.Name.GetHash() == light->Name.GetHash());
			}
		}

		if (!in_file) {
			stale_lights.push_back(light->ID);
		}
	}

	for (LightID& id : stale_lights) {
		gLightManager->DestroyLight(id);
	}
}

void Blockout::SaveLights(ConfigFile& info)
{
	Ref<LightDirectional> sun = gLightManager->GetDirectionalLight();

	if (sun.IsValid()) {
		ConfigEntry sun_entry = ConfigEntry::Struct("sun");

		sun_entry.AddMember(ConfigEntry::Literal("enabled", static_cast<int64>(sun->bEnabled ? 1 : 0)));
		sun_entry.AddMember(ConfigEntry::Literal("pos", sun->GetPosition()));

		WriteColorEntry(sun_entry, "color", sun->Color);
		sun_entry.AddMember(ConfigEntry::Literal("intensity", sun->Intensity));

		info.AddEntry(std::move(sun_entry));
	}

	// Built as a Struct up front: AddEntry("lights") only becomes a Struct once AddMember runs, so with zero
	// point/spot lights it would otherwise serialize as a bare, unparsable `lights = ` line.
	ConfigEntry lights_container = ConfigEntry::Struct("lights");

	for (const Ref<LightBase>& light : gLightManager->GetCache()) {
		if (light->Type != eLightType::Point && light->Type != eLightType::Spot) {
			continue;
		}

		ConfigEntry light_entry = ConfigEntry::Struct(light->Name.Get());

		light_entry.AddMember(
			ConfigEntry::DotReference("type", light->Type == eLightType::Spot ? "$CLight.Spot" : "$CLight.Point"));

		light_entry.AddMember(ConfigEntry::Literal("pos", light->GetPosition()));
		WriteColorEntry(light_entry, "color", light->Color);
		light_entry.AddMember(ConfigEntry::Literal("intensity", light->Intensity));
		light_entry.AddMember(ConfigEntry::Literal("radius", light->GetRadius()));

		if (light->Type == eLightType::Spot) {
			Ref<LightSpot> spot(light);

			const Vec3f direction = spot->GetDirection();

			ConfigEntry direction_array = ConfigEntry::Array("dir", ConfigPrimitive::ePrimitiveType::Float);
			direction_array.AppendValue(ConfigPrimitive::FromValue(direction.X));
			direction_array.AppendValue(ConfigPrimitive::FromValue(direction.Y));
			direction_array.AppendValue(ConfigPrimitive::FromValue(direction.Z));
			light_entry.AddMember(std::move(direction_array));

			light_entry.AddMember(ConfigEntry::Literal("inner", MathUtil::RadiansToDegrees(spot->GetInnerAngle())));
			light_entry.AddMember(ConfigEntry::Literal("outer", MathUtil::RadiansToDegrees(spot->GetOuterAngle())));
			light_entry.AddMember(ConfigEntry::Literal("shadows", static_cast<int64>(spot->bCastShadows ? 1 : 0)));
		}

		lights_container.AddMember(std::move(light_entry));
	}

	info.AddEntry(std::move(lights_container));
}

void Blockout::LoadCamera(ConfigFile& info)
{
	const ConfigEntry* camera = info.GetEntry(HashStr32("camera"));

	if (camera == nullptr) {
		return;
	}

	gCVars->Set("r_aperture", camera->GetMemberValue<float32>(HashStr32("aperture"), gCVars->Get("r_aperture", 16.0f)));
	gCVars->Set("r_shutter", camera->GetMemberValue<float32>(HashStr32("shutter"), gCVars->Get("r_shutter", 0.01f)));
	gCVars->Set("r_iso", camera->GetMemberValue<float32>(HashStr32("iso"), gCVars->Get("r_iso", 100.0f)));
	gCVars->Set("r_exposure_ev",
				camera->GetMemberValue<float32>(HashStr32("exposure_ev"), gCVars->Get("r_exposure_ev", 0.0f)));
}

void Blockout::SaveCamera(ConfigFile& info)
{
	ConfigEntry camera = ConfigEntry::Struct("camera");

	camera.AddMember(ConfigEntry::Literal("aperture", gCVars->Get("r_aperture", 16.0f)));
	camera.AddMember(ConfigEntry::Literal("shutter", gCVars->Get("r_shutter", 0.01f)));
	camera.AddMember(ConfigEntry::Literal("iso", gCVars->Get("r_iso", 100.0f)));
	camera.AddMember(ConfigEntry::Literal("exposure_ev", gCVars->Get("r_exposure_ev", 0.0f)));

	info.AddEntry(std::move(camera));
}

bool Blockout::Load(const String& path)
{
	ConfigFile info {};
	info.Load(path.CStr());

	if (info.HasErrors()) {
		return false;
	}

	ConfigEntry* blocks_entry = info.GetEntry(HashStr32("all"));

	if (blocks_entry == nullptr) {
		LogError("Blockout '{}' could not be loaded or has no 'all' entry", path);
		return false;
	}

#ifdef FX_IS_EDITOR
	// Everything is getting reloaded, so the editor can't hold on to any objects
	gEditor->ForgetObjects();
#endif

	LoadLights(info);
	LoadCamera(info);

	// Remove the current blockout from the world
	RemoveBlockoutFromWorld(pWorld);

	for (ConfigEntry& entry : blocks_entry->Members) {
		CreateBrushObject(entry);
	}

	return true;
}

void Blockout::Save(const String& path)
{
	ConfigFile info {};

	SaveLights(info);
	SaveCamera(info);

	ConfigEntry* all_entry = info.AddEntry("all");

	for (const ObjectID box_id : BlockoutObjects) {
		Object* object = gObjectManager->GetObject(box_id);
		if (object == nullptr) {
			continue;
		}

		ConfigEntry blockout_entry = ConfigEntry::Struct(object->Name.Get());
		{
			blockout_entry.AddMember(ConfigEntry::Literal("pos", object->mPosition));

			WriteBrushEntry(blockout_entry, object);

			blockout_entry.AddMember(ConfigEntry::Literal("rotquat", object->mRotation));

			if (object->HasTags(eObjectTag::LockTransform)) {
				blockout_entry.AddMember(ConfigEntry::Literal("lock", 1));
			}

			if (object->IsProbeVolume()) {
				blockout_entry.AddMember(ConfigEntry::Literal("probevolume", 1));
			}

#ifdef FX_IS_EDITOR
			const MaterialID object_material = gEditor->GetSelection().GetStoredMaterial(object);
#else
			const MaterialID object_material = object->GetMaterialID();
#endif

			const int32 material_id = GetIDForMaterial(object_material);

			if (material_id >= 0) {
				blockout_entry.AddMember(ConfigEntry::Literal("mat", static_cast<int64>(material_id)));
			}
		}
		all_entry->AddMember(std::move(blockout_entry));
	}

	info.Write(path.CStr());
}


Blockout::~Blockout() {}


} // namespace fx
