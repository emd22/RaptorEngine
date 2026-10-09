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
#include <Script/ObjectScripts.hpp>
#include <World.hpp>


namespace fx {

static constexpr MaterialLibraryID scDefaultMaterialID = MaterialLibraryID(0);
static constexpr MaterialLibraryID scEditableMaterialID = MaterialLibraryID(1);

Blockout::Blockout() {}

static void ReadScriptEntry(Object* object, const ConfigEntry& entry)
{
	const ConfigEntry* script = entry.GetMember(HashStr32("script"));

	if (script != nullptr) {
		gObjectScripts->Attach(object, String(script->Get<const char*>()));
	}
	else {
		gObjectScripts->Detach(object->ID);
	}
}

static void WriteScriptEntry(ConfigEntry& entry, const Object* object)
{
	const String& path = gObjectScripts->GetPath(object->ID);

	if (!path.IsEmpty()) {
		entry.AddMember(ConfigEntry::Literal("script", path.CStr()));
	}
}

static void ReadTriggerEntry(Object* object, const ConfigEntry& entry)
{
	gObjectScripts->SetRequiredEnterDirection(object->ID,
											  entry.GetMemberValue<Vec3f>(HashStr32("enter_dir"), Vec3f::sZero));
}

static void WriteTriggerEntry(ConfigEntry& entry, const Object* object)
{
	Vec3f direction;

	if (gObjectScripts->TryGetRequiredEnterDirection(object->ID, direction)) {
		entry.AddMember(ConfigEntry::Literal("enter_dir", direction));
	}
}

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
		brush.GenerateMesh(mesh->Positions, mesh->Normals, mesh->Tangents, mesh->TangentHandedness, mesh->Texcoords,
						   mesh->Indices);
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

bool Blockout::MoveFace(Object* object, const Vec3f face_normal, float32 distance)
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

static Vec3f GetOffsetToKeepInPlace(const Object* object, const Vec3f old_center, const Vec3f new_center)
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

	RebuildBrush(object, std::move(brush), GetMotionType(object));
}

bool Blockout::GetClipPieces(Object* object, const Vec3f point_a, const Vec3f point_b, const Vec3f face_normal,
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

bool Blockout::GetSubtractPieces(Object* target, Object* cutter, std::vector<Brush::PlaneList>& out_pieces,
								 std::vector<Vec3f>& out_positions)
{
	const Brush* brush = GetBrush(target);
	const Brush* cutter_brush = GetBrush(cutter);

	if (brush == nullptr || cutter_brush == nullptr || target == cutter) {
		return false;
	}

	const Mat4f to_world = cutter->GetWorldMatrix();
	const Mat4f to_local = target->GetWorldMatrix().Inverse();

	Brush::PlaneList cutter_planes;

	for (const BrushPlane& plane : cutter_brush->Planes) {
		const Vec3f point = plane.Normal * plane.Distance;

		const Vec4f world_point = to_world * Vec4f(point.X, point.Y, point.Z, 1.0f);
		const Vec4f world_normal = to_world * Vec4f(plane.Normal.X, plane.Normal.Y, plane.Normal.Z, 0.0f);

		const Vec4f local_point = to_local * world_point;
		const Vec4f local_normal = to_local * world_normal;

		const Vec3f normal(local_normal.X, local_normal.Y, local_normal.Z);
		const float32 length = normal.Length();

		if (!(length > 0.0f)) {
			continue;
		}

		BrushPlane cut;
		cut.Normal = normal * (1.0f / length);
		cut.Distance = cut.Normal.Dot(Vec3f(local_point.X, local_point.Y, local_point.Z));

		cutter_planes.Insert(cut);
	}

	if (!brush->Subtract(cutter_planes, target->GetPosition(), out_pieces)) {
		return false;
	}

	out_positions.clear();

	for (const Brush::PlaneList& piece : out_pieces) {
		const Brush shape = Brush::FromPlanes(piece);

		out_positions.push_back(target->GetPosition() +
								GetOffsetToKeepInPlace(target, brush->GetCenter(), shape.GetCenter()));
	}

	return true;
}

Brush Blockout::MakeWorldBox(const Vec3f min, const Vec3f max, Vec3f& out_position) const
{
	out_position = (min + max) * 0.5f;

	const Vec3f half_size = (max - min) * 0.5f;

	Brush brush = Brush::FromBox(-half_size, half_size);
	brush.AlignTexturesToWorld(out_position);

	return brush;
}

void Blockout::ShowPreview(const Vec3f position, const Quat rotation, const Brush& brush)
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
		brush.GenerateMesh(mesh->Positions, mesh->Normals, mesh->Tangents, mesh->TangentHandedness, mesh->Texcoords,
						   mesh->Indices);

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

bool Blockout::GetFaceTextureEdit(Object* object, const Vec3f face_normal, eFaceTextureEdit edit, const Vec2f& amount,
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

Object* Blockout::RaycastBlockout(const Vec3f origin, const Vec3f direction, Vec3f& out_face_normal)
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

bool Blockout::RaycastFace(Object* object, const Vec3f origin, const Vec3f direction, Vec3f& out_face_normal,
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
			new_object_id = CreateBrush(entry);
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

MaterialID Blockout::GetMaterialForID(MaterialLibraryID id) const
{
	const MaterialID material = mMaterials.GetMaterial(id);

	if (material.IsNull()) {
		return GetDefaultMaterial();
	}

	return material;
}

MaterialLibraryID Blockout::GetIDForMaterial(const MaterialID& material) const { return mMaterials.FindID(material); }

MaterialID Blockout::GetDefaultMaterial() const { return mMaterials.GetMaterial(scDefaultMaterialID); }

Brush* Blockout::GetBrush(const Object* object)
{
	if (object == nullptr) {
		return nullptr;
	}

	return mBrushes.Find(object->ID.GetID());
}

void Blockout::RebuildBrush(Object* object, Brush&& brush, physics::eMotionType motion_type)
{
	Assert(brush.IsValid());

	Ref<MeshGen::GeneratedMesh> mesh = MakeRef<MeshGen::GeneratedMesh>();
	brush.GenerateMesh(mesh->Positions, mesh->Normals, mesh->Tangents, mesh->TangentHandedness, mesh->Texcoords,
					   mesh->Indices);

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
	for (const Vec3f vertex : brush.GetVertices()) {
		hull_points.Insert(vertex - midpoint);
	}

	physics::Body* phys = gPhysics->NewBody(object->Name.Get());
	phys->CreateConvexHullBody(hull_points, motion_type,
							   physics::BodyProps {
								   .ConvexRadius = 0.05f,
								   .Density = 100,
							   });

	phys->SetMidpoint(midpoint);
	phys->Teleport(object->GetPosition(), object->mRotation);

	object->AttachCollider(phys);

	// A probe volume's collider has to be taken back out of the world
	if (object->IsProbeVolume()) {
		object->SetProbeVolume(true);
	}

	if (object->IsTrigger()) {
		object->SetTrigger(true);
	}

	mBrushes.Insert(object->ID.GetID(), std::move(brush));
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

ObjectID Blockout::CreateBrush(const String& name, const Vec3f position, const Quat rotation,
							   const MaterialID material_id, const BBox bounds)
{
	static constexpr eObjectTag scObjectTags = eObjectTag::Blockout;

	Object* object = gObjectManager->NewObject(name, material_id, scObjectTags);

	// The object could not be made (likely ran out of memory), so return a null id
	if (object == nullptr) {
		return ObjectID::scNull;
	}

	object->SetPosition(position);
	object->SetRotation(rotation);

	Brush brush = Brush::FromBox(bounds.Min, bounds.Max);
	RebuildBrush(object, std::move(brush), physics::eMotionType::Static);

	AssetTicket ticket(static_cast<void*>(object));
	ticket.MarkAndSignalLoaded();

	pWorld->Attach(ticket);

	BlockoutObjects.NewItem(nullptr, object->ID);

	return object->ID;
}

ObjectID Blockout::CreateBrush(ConfigEntry& entry)
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

	if (mat_entry != nullptr && mat_entry->Type == ConfigEntry::ePrimitiveType::String) {
		MaterialLibraryID mat_id = MaterialLibraryID::scNull;

		const char* mat_name = mat_entry->Get<const char*>();
		mat_id = mMaterials.FindIDByName(mat_name);

		if (mat_id.IsValid()) {
			material_id = GetMaterialForID(mat_id);
		}
		else {
			LogWarning(LC_ASSET, "Blockout '{}' uses the unknown material '{}'", entry.Name.Get(), mat_name);
		}
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

	if (entry.GetMemberValue(HashStr32("reflectionprobe"), 0) == 1) {
		object->SetReflectionProbe(true);
	}

	if (entry.GetMemberValue(HashStr32("bleeds"), 0) == 1) {
		object->SetTag(eObjectTag::Bleeds);
	}

	if (entry.GetMemberValue(HashStr32("spawn"), 0) == 1) {
		object->SetTag(eObjectTag::Spawn);
	}

	if (entry.GetMemberValue(HashStr32("trigger"), 0) == 1) {
		object->SetTag(eObjectTag::Trigger);
	}

	ReadScriptEntry(object, entry);
	ReadTriggerEntry(object, entry);

	physics::eMotionType is_dynamic = entry.GetMemberValue(HashStr32("dynamic"), 0) ? physics::eMotionType::Dynamic
																					: physics::eMotionType::Static;

	RebuildBrush(object, std::move(brush), is_dynamic);

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

	RebuildBrush(object, std::move(brush), body->GetMotionType());
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

Object* Blockout::NewObject(const Vec3f position)
{
	std::string blockout_name = String::Fmt("{}", BlockoutObjects.Size).Str();
	LogInfo("Creating new blockout object '{}'", blockout_name);

	Object* object = gObjectManager->NewObject(blockout_name, GetDefaultMaterial(), eObjectTag::Blockout);

	constexpr float32 scale = 0.25f;

	object->MoveBy(position);
	object->SetShadowCaster(true);

	RebuildBrush(object, Brush::FromBox(Vec3f(-scale), Vec3f(scale)), physics::eMotionType::Static);

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

	if (object->IsSpawn()) {
		dupe->SetTag(eObjectTag::Spawn);
	}

	if (object->IsTrigger()) {
		dupe->SetTag(eObjectTag::Trigger);
	}

	gObjectScripts->Attach(dupe, gObjectScripts->GetPath(object->ID));

	Vec3f enter_direction;

	if (gObjectScripts->TryGetRequiredEnterDirection(object->ID, enter_direction)) {
		gObjectScripts->SetRequiredEnterDirection(dupe->ID, enter_direction);
	}

	RebuildBrush(dupe, std::move(brush), motion_type);

	AssetTicket ticket(static_cast<void*>(dupe));
	ticket.MarkAndSignalLoaded();

	gWorld->Attach(ticket);

	BlockoutObjects.NewItem(nullptr, dupe->ID);

	return dupe;
}

bool Blockout::HasBrush(const Object* object) { return GetBrush(object) != nullptr; }

bool Blockout::ContainsPoint(Object* object, const Vec3f point)
{
	const Brush* brush = GetBrush(object);

	if (brush == nullptr) {
		return object->ContainsPoint(point);
	}

	const Vec4f local = object->GetWorldMatrix().Inverse() * Vec4f(point.X, point.Y, point.Z, 1.0f);

	return brush->ContainsPoint(Vec3f(local.X, local.Y, local.Z));
}

bool Blockout::SetDynamic(Object* object, bool dynamic)
{
	const Brush* existing = GetBrush(object);

	if (existing == nullptr) {
		return false;
	}

	Brush brush = Brush::FromPlanes(existing->Planes);

	if (!brush.IsValid()) {
		LogError("Could not rebuild blockout '{}', its brush is invalid", object->Name.Get());
		return false;
	}

	if (IsDynamic(object) != dynamic) {
		RebuildBrush(object, std::move(brush), dynamic ? physics::eMotionType::Dynamic : physics::eMotionType::Static);
	}

	object->SetPhysicsEnabled(dynamic);

	return true;
}

bool Blockout::IsDynamic(const Object* object) const
{
	return object != nullptr && GetMotionType(object) == physics::eMotionType::Dynamic;
}

Object* Blockout::RestoreObject(const Vec3f position, const Brush::PlaneList& planes, MaterialID material,
								const Quat rotation, const Name& name, bool is_dynamic)
{
	Brush brush = Brush::FromPlanes(planes);

	if (!brush.IsValid()) {
		LogError("Cannot restore blockout '{}', its brush is invalid", name.Get());
		return nullptr;
	}

	String base_name = name.Get().IsEmpty() ? String::Fmt("{}", BlockoutObjects.Size).Str() : name.Get();

	// Make sure the names are unique
	String blockout_name = base_name;

	if (gObjectManager->FindObject(HashStr32(blockout_name.CStr())) != nullptr) {
		int suffix = 0;
		do {
			blockout_name = String::Fmt("{}_undo{}", base_name, suffix++).Str();
		} while (gObjectManager->FindObject(HashStr32(blockout_name.CStr())) != nullptr);
	}

	LogInfo("Restoring blockout object '{}'", blockout_name);

	Object* object = gObjectManager->NewObject(
		blockout_name, material.IsNull() ? mMaterials.GetMaterial(scEditableMaterialID) : material,
		eObjectTag::Blockout);

	object->MoveBy(position);
	object->SetShadowCaster(true);
	object->SetRotation(rotation);

	RebuildBrush(object, std::move(brush), is_dynamic ? physics::eMotionType::Dynamic : physics::eMotionType::Static);

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
	gLightManager->Clear();

	// Load sun
	Ref<LightDirectional> sun = gLightManager->GetDirectionalLight();
	if (!sun.IsValid()) {
		sun = gLightManager->NewLight<LightDirectional>("sun");
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

	// 	std::vector<LightID> stale_lights;
	//
	// 	for (const Ref<LightBase>& light : gLightManager->GetCache()) {
	// 		if (!light.IsValid() || (light->Type != eLightType::Point && light->Type != eLightType::Spot)) {
	// 			continue;
	// 		}
	//
	// 		bool in_file = false;
	//
	// 		if (light_list) {
	// 			for (const ConfigEntry& light_entry : light_list->Members) {
	// 				in_file |= (light_entry.Name.GetHash() == light->Name.GetHash());
	// 			}
	// 		}
	//
	// 		if (!in_file) {
	// 			stale_lights.push_back(light->ID);
	// 		}
	// 	}
	//
	// 	for (LightID& id : stale_lights) {
	// 		gLightManager->RemoveLight(id);
	// 	}
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
			ConfigEntry::DotReference("type", light->Type == eLightType::Spot ? "$clight.spot" : "$clight.point"));

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

void Blockout::LoadPlayerSpawn(ConfigFile& info)
{
	const ConfigEntry* spawn = info.GetEntry(HashStr32("player_spawn"));

	gWorld->PlayerSpawn.Reset();

	if (spawn == nullptr) {
		return;
	}

	gWorld->PlayerSpawn.Position = spawn->GetMemberValue<Vec3f>(HashStr32("pos"),
																World::PlayerSpawnPoint::DefaultPosition());
	gWorld->PlayerSpawn.Direction = spawn->GetMemberValue<Vec3f>(HashStr32("dir"), Vec3f::sForward).Normalize();
	gWorld->PlayerSpawn.bCustom = true;
}

void Blockout::SavePlayerSpawn(ConfigFile& info)
{
	if (!gWorld->PlayerSpawn.bCustom) {
		return;
	}

	ConfigEntry spawn = ConfigEntry::Struct("player_spawn");

	spawn.AddMember(ConfigEntry::Literal("pos", gWorld->PlayerSpawn.Position));
	spawn.AddMember(ConfigEntry::Literal("dir", gWorld->PlayerSpawn.Direction));

	info.AddEntry(std::move(spawn));
}

static void CollectModelNodes(Object& object, std::vector<ObjectID>& out_nodes)
{
	out_nodes.push_back(object.ID);

	for (ObjectID attached_id : object.AttachedNodes) {
		Object* attached = gObjectManager->GetObject(attached_id);

		if (attached != nullptr) {
			CollectModelNodes(*attached, out_nodes);
		}
	}
}

bool Blockout::RemoveModelFromWorld(ObjectID id)
{
	Object* object = gObjectManager->GetObject(id);

	if (object == nullptr) {
		return true;
	}

	// The asset thread is still writing to it
	if (!object->bIsAddedToWorld.load()) {
		return false;
	}

	std::vector<ObjectID> nodes;
	CollectModelNodes(*object, nodes);

	object->Destroy();

	// The object manager is locked while it destroys an object, so no object can be left to look up its nodes there
	for (ObjectID node_id : nodes) {
		gWorld->Detach(node_id);
		gObjectManager->GetObject(node_id)->AttachedNodes.Clear();
	}

	for (ObjectID& node_id : nodes) {
		gObjectManager->DestroyObject(node_id);
	}

	return true;
}

bool Blockout::IsModel(const Object* object) const
{
	if (object == nullptr) {
		return false;
	}

	return std::any_of(mModels.begin(), mModels.end(),
					   [&](const Model& model) { return model.ID.GetID() == object->ID.GetID(); });
}

void Blockout::RemovePendingModels()
{
	std::erase_if(mModelsPendingRemoval, [this](ObjectID id) { return RemoveModelFromWorld(id); });
}

void Blockout::ApplyModelProperties(Object& object, Model& model, const ConfigEntry& entry)
{
	const ConfigEntry* shadows = entry.GetMember(HashStr32("shadows"));
	if (shadows != nullptr) {
		object.SetShadowCaster(shadows->Get<int64>() != 0);
	}

	Quat rotation = entry.GetMemberValue(HashStr32("rot"), object.mRotation);
	rotation = entry.GetMemberValue(HashStr32("rotquat"), rotation);

	object.SetPosition(entry.GetMemberValue(HashStr32("pos"), object.mPosition));
	object.SetRotation(rotation);
	object.SetScale(entry.GetMemberValue(HashStr32("scale"), object.mScale));
	object.MarkTransformOutOfDate();

	const ConfigEntry* layer = entry.GetMember(HashStr32("layer"));
	if (layer != nullptr && layer->Get<int64>() == static_cast<int64>(eObjectLayer::PlayerLayer)) {
		object.SetObjectLayer(eObjectLayer::PlayerLayer);
	}

	object.SetUnlit(entry.GetMemberValue(HashStr32("unlit"), 0) != 0);

	const bool cullable = (entry.GetMemberValue(HashStr32("nocull"), 0) == 0);
	if (object.IsCullable() != cullable) {
		object.SetCullable(cullable);
	}

	const bool probe_visible = (entry.GetMemberValue(HashStr32("noprobe"), 0) == 0);
	if (!HasFlag(object.GetFlags(), eObjectFlags::NotProbeVisible) != probe_visible) {
		object.SetProbeVisible(probe_visible);
	}

	const auto apply_tag = [&](const char* name, eObjectTag tag)
	{
		if (entry.GetMemberValue(HashStr32(name), 0) != 0) {
			object.SetTag(tag);
		}
		else {
			object.ClearTag(tag);
		}
	};

	apply_tag("lock", eObjectTag::LockTransform);
	apply_tag("bleeds", eObjectTag::Bleeds);
	apply_tag("spawn", eObjectTag::Spawn);

	ReadScriptEntry(&object, entry);

	const ConfigEntry* collider = entry.GetMember(HashStr32("collider"));
	model.ColliderName = (collider != nullptr) ? collider->Get<const char*>() : "";
}

void Blockout::LinkModelColliders()
{
	for (const Model& model : mModels) {
		Object* object = gObjectManager->GetObject(model.ID);

		if (object == nullptr || model.ColliderName.IsEmpty() || !object->PhysicsID.IsNull()) {
			continue;
		}

		physics::Body* body = gPhysics->FindBody(HashStr32(model.ColliderName.CStr()));

		if (body != nullptr) {
			object->AttachCollider(body);
			object->SetPhysicsEnabled(true);
		}
	}
}

void Blockout::LoadModels(const ConfigEntry* list, const std::string& mesh_root)
{
	RemovePendingModels();

	std::vector<Model> previous = std::move(mModels);
	mModels.clear();

	if (list != nullptr) {
		for (const ConfigEntry& entry : list->Members) {
			const ConfigEntry* mesh = entry.GetMember(HashStr32("mesh"));

			if (mesh == nullptr) {
				LogWarning(LC_ASSET, "Model '{}' has no mesh", entry.Name.Get());
				continue;
			}

			Model model { .MeshPath = mesh_root + mesh->Get<const char*>() };

			const auto existing = std::find_if(previous.begin(), previous.end(),
											   [&](const Model& other)
											   {
												   const Object* object = gObjectManager->GetObject(other.ID);

												   return object != nullptr &&
														  object->Name.GetHash() == entry.Name.GetHash() &&
														  other.MeshPath == model.MeshPath;
											   });

			if (existing != previous.end()) {
				model.ID = existing->ID;
				previous.erase(existing);

				ApplyModelProperties(*gObjectManager->GetObject(model.ID), model, entry);
			}
			else {
				AssetTicket ticket = gAssetManager->LoadObject(entry.Name.Get(), model.MeshPath);
				Object* object = static_cast<Object*>(ticket.Get());

				model.ID = object->ID;

				ApplyModelProperties(*object, model, entry);

				gWorld->Attach(ticket);
			}

			mModels.push_back(std::move(model));
		}
	}

	for (const Model& model : previous) {
		if (!RemoveModelFromWorld(model.ID)) {
			mModelsPendingRemoval.push_back(model.ID);
		}
	}

	LinkModelColliders();
}

void Blockout::LoadLegacyModels()
{
	const std::string scene_path = gAssetManager->GetScenePath().CStr();

	if (scene_path.empty()) {
		return;
	}

	ConfigFile info {};
	info.Load(scene_path + "/info.prx");

	LoadModels(info.HasErrors() ? nullptr : info.GetEntry(HashStr32("objects")), scene_path + "/Models");
}

void Blockout::SaveModels(ConfigFile& info)
{
	RemovePendingModels();

	ConfigEntry models_container = ConfigEntry::Struct("objects");

	for (const Model& model : mModels) {
		const Object* object = gObjectManager->GetObject(model.ID);

		if (object == nullptr) {
			continue;
		}

		ConfigEntry model_entry = ConfigEntry::Struct(object->Name.Get());

		model_entry.AddMember(ConfigEntry::Literal("mesh", model.MeshPath.CStr()));
		model_entry.AddMember(ConfigEntry::Literal("pos", object->mPosition));
		model_entry.AddMember(ConfigEntry::Literal("rotquat", object->mRotation));
		model_entry.AddMember(ConfigEntry::Literal("scale", object->mScale));
		model_entry.AddMember(ConfigEntry::Literal("shadows", static_cast<int64>(object->IsShadowCaster() ? 1 : 0)));

		if (object->GetObjectLayer() == eObjectLayer::PlayerLayer) {
			model_entry.AddMember(ConfigEntry::Literal("layer", static_cast<int64>(eObjectLayer::PlayerLayer)));
		}

		if (object->IsUnlit()) {
			model_entry.AddMember(ConfigEntry::Literal("unlit", 1));
		}

		if (!object->IsCullable()) {
			model_entry.AddMember(ConfigEntry::Literal("nocull", 1));
		}

		if (HasFlag(object->GetFlags(), eObjectFlags::NotProbeVisible)) {
			model_entry.AddMember(ConfigEntry::Literal("noprobe", 1));
		}

		if (object->HasTags(eObjectTag::LockTransform)) {
			model_entry.AddMember(ConfigEntry::Literal("lock", 1));
		}

		if (object->Bleeds()) {
			model_entry.AddMember(ConfigEntry::Literal("bleeds", 1));
		}

		if (object->IsSpawn()) {
			model_entry.AddMember(ConfigEntry::Literal("spawn", 1));
		}

		WriteScriptEntry(model_entry, object);

		if (!model.ColliderName.IsEmpty()) {
			model_entry.AddMember(ConfigEntry::Literal("collider", model.ColliderName.CStr()));
		}

		models_container.AddMember(std::move(model_entry));
	}

	info.AddEntry(std::move(models_container));

	mbHasModelsEntry = true;
}

void Blockout::ResetToTemplate()
{
	// No world to clear, ignore
	if (pWorld == nullptr) {
		return;
	}

	RemoveBlockoutFromWorld(pWorld);

	MaterialLibraryID lib_base_material = gWorld->pBlockout->mMaterials.FindIDByName("white_tile");
	MaterialID base_material = gWorld->pBlockout->mMaterials.GetMaterial(lib_base_material);

	CreateBrush("base", Vec3f(0.0f, -2.0f, 0.0f), Quat::scIdentity, base_material,
				BBox(Vec3f(-10.0f, -0.1f, -10.0f), Vec3f(10.0f, 0.0f, 10.0f)));

	gLightManager->Clear();

	// Create default light (sun)

	LightDirectional* sun = gLightManager->GetDirectionalLight();

	// Create a new sun
	if (sun == nullptr) {
		sun = gLightManager->NewLight<LightDirectional>("sun");
		sun->bEnabled = true;
		sun->SetPosition(Vec3f(1.0f, 5.0f, -1.0f));

		gCVars->Set("b_sun_enabled", 1);
	}

	// Invalidate all shadows since we have replaced the lights
	renderer::gShadowAtlas->Invalidate();

	gWorld->Player.TeleportTo(Vec3f(0.0, 2.0, 0.0));
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
	LoadPlayerSpawn(info);

	// Remove the current blockout from the world
	RemoveBlockoutFromWorld(pWorld);

	for (ConfigEntry& entry : blocks_entry->Members) {
		CreateBrush(entry);
	}

	const ConfigEntry* models_entry = info.GetEntry(HashStr32("objects"));

	mbHasModelsEntry = (models_entry != nullptr);

	if (mbHasModelsEntry) {
		LoadModels(models_entry, "");
	}
	else {
		LoadLegacyModels();
	}

	renderer::gShadowAtlas->Invalidate();

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

void Blockout::Save(const String& path)
{
	ConfigFile info {};

	SaveLights(info);
	SaveCamera(info);
	SavePlayerSpawn(info);
	SaveModels(info);

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

			if (object->IsReflectionProbe()) {
				blockout_entry.AddMember(ConfigEntry::Literal("reflectionprobe", 1));
			}

			if (object->Bleeds()) {
				blockout_entry.AddMember(ConfigEntry::Literal("bleeds", 1));
			}

			if (object->IsSpawn()) {
				blockout_entry.AddMember(ConfigEntry::Literal("spawn", 1));
			}

			if (object->IsTrigger()) {
				blockout_entry.AddMember(ConfigEntry::Literal("trigger", 1));
				WriteTriggerEntry(blockout_entry, object);
			}

			WriteScriptEntry(blockout_entry, object);

			if (IsDynamic(object)) {
				blockout_entry.AddMember(ConfigEntry::Literal("dynamic", 1));
			}

#ifdef FX_IS_EDITOR
			const MaterialID object_material = gEditor->GetSelection().GetStoredMaterial(object);
#else
			const MaterialID object_material = object->GetMaterialID();
#endif

			const MaterialLibraryID material_id = GetIDForMaterial(object_material);

			if (material_id.IsValid()) {
				blockout_entry.AddMember(ConfigEntry::Literal("mat", mMaterials.GetName(material_id).CStr()));
			}
		}
		all_entry->AddMember(std::move(blockout_entry));
	}

	info.Write(path.CStr());

	StoreLastBlockoutPath(path, "Config/Main.conf");
}


Blockout::~Blockout() {}


} // namespace fx
