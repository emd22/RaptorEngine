#include "Blockout.hpp"

#include <Asset/AssetManager.hpp>
#include <Asset/ConfigFile.hpp>
#include <Asset/MeshGen.hpp>
#include <Core/RefUtil.hpp>
#include <Editor/RaptorEditor.hpp>
#include <Engine.hpp>
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

	{
		pPreviewObject = gObjectManager->NewObject("PROTO_PREVIEW", SelectionMaterialID,
												   eObjectTag::Blockout | eObjectTag::LockTransform);
		pPreviewObject->SetUnlit(true);
		pPreviewObject->SetProbeVisible(false);

		// Needs a mesh to be added to the world; ShowPreview() replaces it
		Brush brush = Brush::FromBox(Vec3f(-0.25f), Vec3f(0.25f));
		Ref<MeshGen::GeneratedMesh> mesh = MakeRef<MeshGen::GeneratedMesh>();
		SizedArray<MeshSection> sections;

		brush.GenerateMesh(mesh->Positions, mesh->Normals, mesh->Tangents, mesh->Texcoords, mesh->Indices, sections);
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

	if (!brush->Split(normal, normal.Dot(a), out_kept, out_split)) {
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
		SizedArray<MeshSection> sections;

		brush.GenerateMesh(mesh->Positions, mesh->Normals, mesh->Tangents, mesh->Texcoords, mesh->Indices, sections);

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
	pPreviewObject->SetPosition(Vec3f(200.0f));
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
	case eFaceTextureEdit::CycleMaterial: {
		// No material of its own (the object's material), then each prototype material in turn
		const int32 slot = texture.Material.IsNull() ? -1 : GetSlotForMaterial(texture.Material);
		const int32 next_slot = slot + 1;

		texture.Material = (next_slot < static_cast<int32>(eCProtoMat::Count))
							   ? GetMaterialForSlot(static_cast<eCProtoMat>(next_slot))
							   : MaterialID::scNull;
		break;
	}
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

bool Blockout::GetMaterialEdit(Object* object, const Vec3f& face_normal, const MaterialID& material, bool whole_brush,
							   Brush::PlaneList& out_planes)
{
	const Brush* brush = GetBrush(object);
	if (brush == nullptr) {
		return false;
	}

	const int32 plane_index = brush->FindPlane(face_normal);
	if (!whole_brush && plane_index == Brush::scNoPlane) {
		return false;
	}

	out_planes = brush->Planes;

	bool changed = false;

	for (uint32 i = 0; i < out_planes.Size; i++) {
		if (!whole_brush && static_cast<int32>(i) != plane_index) {
			continue;
		}

		MaterialID& face_material = out_planes[i].Texture.Material;

		changed |= !(face_material == material);
		face_material = material;
	}

	return changed;
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

int32 Blockout::GetSlotForMaterial(const MaterialID& material) const
{
	for (int32 slot = 0; slot < static_cast<int32>(eCProtoMat::Count); slot++) {
		if (GetMaterialForSlot(static_cast<eCProtoMat>(slot)) == material) {
			return slot;
		}
	}

	return -1;
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
	SizedArray<MeshSection> sections;

	brush.GenerateMesh(mesh->Positions, mesh->Normals, mesh->Tangents, mesh->Texcoords, mesh->Indices, sections);

	object->pMesh = mesh->AsDefaultMesh();
	object->MeshSections = std::move(sections);

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

/// Values stored per plane in a blockout's `uvs` array: offset U and V, scale U and V, then rotation
constexpr uint32 scValuesPerFaceTexture = 5;

/// Stored in a blockout's `facemats` array for faces that use the object's material
constexpr int32 scNoFaceMaterial = -1;

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

	ConfigEntry* materials_entry = entry.GetMember(HashStr32("facemats"));

	if (materials_entry != nullptr) {
		const PagedArray<ConfigPrimitive>& slots = materials_entry->GetArrayData();

		for (uint32 i = 0; i < plane_count && i < slots.Size(); i++) {
			const int32 slot = slots[i].Get<int32>();

			if (slot != scNoFaceMaterial) {
				planes[i].Texture.Material = GetMaterialForSlot(static_cast<eCProtoMat>(slot));
			}
		}
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

	bool has_face_materials = false;

	for (const BrushPlane& plane : brush->Planes) {
		has_face_materials |= !plane.Texture.Material.IsNull();
	}

	if (has_face_materials) {
		ConfigEntry materials_array = ConfigEntry::Array("facemats", ConfigPrimitive::ePrimitiveType::Int);

		for (const BrushPlane& plane : brush->Planes) {
			const int32 slot = plane.Texture.Material.IsNull() ? scNoFaceMaterial
															   : GetSlotForMaterial(plane.Texture.Material);
			materials_array.AppendValue(ConfigPrimitive::FromValue(slot));
		}

		entry.AddMember(std::move(materials_array));
	}

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

#ifdef FX_IS_EDITOR
	// Everything is getting reloaded, so the editor can't hold on to any objects
	gEditor->ForgetObjects();
#endif

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

			WriteBrushEntry(blockout_entry, object);

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
