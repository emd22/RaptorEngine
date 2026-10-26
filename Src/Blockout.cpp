#include "Blockout.hpp"

#include <Asset/AssetManager.hpp>
#include <Asset/ConfigFile.hpp>
#include <Asset/MeshGen.hpp>
#include <BrushEntry.hpp>
#include <LightEntry.hpp>
#include <CVar.hpp>
#include <Core/FilesystemIO.hpp>
#include <Core/RefUtil.hpp>
#include <Decal/MeshDecal.hpp>
#include <Editor/RaptorEditor.hpp>
#include <Engine.hpp>
#include <Map/CompiledMap.hpp>
#include <Material/Material.hpp>
#include <Material/MaterialManager.hpp>
#include <Math/SIMDHelper.hpp>
#include <Physics/JoltPhysicsBackend.hpp>
#include <Physics/PhysicsManager.hpp>
#include <Renderer/GraphicsBackend.hpp>
#include <Renderer/LightManager.hpp>
#include <Renderer/LightProbe.hpp>
#include <Renderer/Limits.hpp>
#include <Renderer/PipelineNames.hpp>
#include <Script/ObjectScripts.hpp>
#include <World.hpp>
#include <algorithm>
#include <cctype>
#include <cfloat>
#include <filesystem>


RS_OBJECT_BEGIN(fx::World::PlayerSpawnPoint)
	RS_FIELD_NAMED(Position, "pos")
	RS_FIELD_NAMED(Direction, "dir")
RS_OBJECT_END()


namespace fx {

static constexpr const char* scMaterialRoot = "RaptorData/Data/materials";
static constexpr const char* scDefaultMaterialName = "proto_white";
static constexpr const char* scEditableMaterialName = "proto_grey";

static constexpr int32 scCompiledLockedMaterialIndex = 0;
static constexpr int32 scCompiledEditableMaterialIndex = 1;

static constexpr float32 scBakedSunMinCosine = 0.99999f;
static constexpr float32 scDecalBuriedTolerance = 1e-4f;

Blockout::Blockout() {}

static void DestroyObjectTree(Object& object);

static bool DoesFileExist(const String& path)
{
	return std::filesystem::exists(FilesystemIO::ResolvePath(path).CStr());
}

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

	mMaterials.Load(scMaterialRoot);

	mDefaultMaterialID = mMaterials.FindIDByName(scDefaultMaterialName);
	mEditableMaterialID = mMaterials.FindIDByName(scEditableMaterialName);

	if (!mDefaultMaterialID.IsValid()) {
		LogError(LC_ASSET, "The default material '{}' is missing", scDefaultMaterialName);
		mDefaultMaterialID = MaterialLibraryID(0);
	}

	if (!mEditableMaterialID.IsValid()) {
		LogError(LC_ASSET, "The editable material '{}' is missing", scEditableMaterialName);
		mEditableMaterialID = mDefaultMaterialID;
	}

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


constexpr float scMinBlockoutThickness = Blockout::scMinThickness;

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

	const Mat4f to_local = object->GetWorldMatrix().AffineInverse();

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
	const Mat4f to_local = target->GetWorldMatrix().AffineInverse();

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
	if (GetPatch(object) != nullptr) {
		return RaycastPatch(object, origin, direction, out_face_normal, out_point);
	}

	const Brush* brush = GetBrush(object);
	if (brush == nullptr) {
		return false;
	}

	// Bring the ray into the brush's local space
	const Mat4f to_local = object->GetWorldMatrix().AffineInverse();
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
	info.Load(CompiledMap::GetPrototypePath(gWorld->PrototypePath).CStr());

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

	DestroyFaceNodes(object);

	gWorld->Detach(object->ID);

	if (!object->PhysicsID.IsNull()) {
		gPhysics->DestroyBody(object->PhysicsID);
	}

	mBrushes.Remove(object->ID.GetID());
	mPatches.Remove(object->ID.GetID());
	mPatchMeshFrames.Remove(object->ID.GetID());
	mDecalHashes.Remove(object->ID.GetID());

	gObjectManager->DestroyObject(object->ID);
}

void Blockout::RemoveBlockoutFromWorld(World* world)
{
	for (ObjectID box_id : BlockoutObjects) {
		RemoveSingleObjectFromWorld(gObjectManager->GetObject(box_id));
	}

	BlockoutObjects.Clear();
	mBrushes.Clear();
	mFaceNodeMaterials.Clear();
	mPatches.Clear();
	mPatchMeshFrames.Clear();
	mDecalHashes.Clear();
	mCompiledDecalReceivers.clear();

	for (ObjectID object_id : mCompiledObjects) {
		gWorld->Detach(object_id);
		gObjectManager->DestroyObject(object_id);
	}

	for (physics::BodyID& body_id : mCompiledBodies) {
		gPhysics->DestroyBody(body_id);
	}

	mCompiledObjects.clear();
	mCompiledBodies.clear();
	mCompiledProbeLayout.Clear();
	mCompiledProbeIrradiance.Free();
	mCompiledProbeMoments.Free();
	mbIsCompiledMap = false;

	gWorldGrid->ClearAreaGraph();

	for (const SubviewInstance& instance : mSubviewInstances) {
		Object* root = gObjectManager->GetObject(instance.RootID);

		if (root != nullptr) {
			DestroyObjectTree(*root);
		}
	}

	mSubviews.Clear();
	mSubviewInstances.Clear();
	mNextSubviewInstance = 1;
	mActiveSubview = scNoSubview;
	mOuterSubviews.clear();
	mLoadingSubviews.clear();

	if (mbHasBakedSun) {
		gProbeManager->ClearSunVisibility();
	}

	renderer::gGraphics->ClearFogShadow();

	mbHasBakedSun = false;
	mbHasBakedIndirect = false;
	mbBakedWithSun = false;
}

bool Blockout::IsBakedSunCurrent(const Vec3f sun_position) const
{
	if (!mbHasBakedSun || sun_position.Length() < 1e-6f) {
		return false;
	}

	return sun_position.Normalize().Dot(mBakedSunDirection) > scBakedSunMinCosine;
}

bool Blockout::IsBakedIndirectCurrent(bool sun_enabled, const Vec3f sun_position) const
{
	if (!mbHasBakedIndirect) {
		return false;
	}

	if (!mbBakedWithSun) {
		return !sun_enabled;
	}

	if (!sun_enabled || sun_position.Length() < 1e-6f) {
		return false;
	}

	return sun_position.Normalize().Dot(mBakedSunDirection) > scBakedSunMinCosine;
}

bool Blockout::LoadCompiledMap(const String& prototype_path, const ConfigFile& info, CompiledMap& out_map) const
{
	const String resolved_path = FilesystemIO::ResolvePath(prototype_path);
	const String compiled_path = CompiledMap::GetPathForPrototype(resolved_path);

	if (!out_map.Load(compiled_path)) {
		LogWarning(LC_ASSET, "There is no compiled map at '{}', loading the brushes of the prototype instead",
				   compiled_path);
		return false;
	}

	if (out_map.SourceHash != HashPrototype(resolved_path, info)) {
		LogWarning(LC_ASSET,
				   "The compiled map '{}' is out of date with its prototype, loading the brushes of the prototype "
				   "instead. Run mapc on the prototype to update it.",
				   compiled_path);
		return false;
	}

	return true;
}

void Blockout::CreateCompiledShadows(const CompiledMap& map)
{
	for (size_t surface_index = 0; surface_index < map.ShadowSurfaces.Size; surface_index++) {
		const CompiledMapShadowSurface& surface = map.ShadowSurfaces[surface_index];

		Object* object = gObjectManager->NewObject(String::Fmt("WORLD_SHADOW_{}", surface_index), GetDefaultMaterial(),
												   eObjectTag::LockTransform);

		if (object == nullptr) {
			continue;
		}

		const uint32 point_count = surface.GetPointCount();

		Ref<MeshGen::GeneratedMesh> mesh = MakeRef<MeshGen::GeneratedMesh>();

		mesh->Positions.InitCapacity(point_count);
		mesh->Normals.InitCapacity(point_count);
		mesh->Tangents.InitCapacity(point_count);
		mesh->TangentHandedness.InitCapacity(point_count);
		mesh->Texcoords.InitCapacity(point_count);
		mesh->Indices.InitCapacity(surface.Indices.Size);

		for (uint32 point = 0; point < point_count; point++) {
			mesh->Positions.Insert(
				Vec3f(surface.Points[point * 3], surface.Points[point * 3 + 1], surface.Points[point * 3 + 2]));
			mesh->Normals.Insert(Vec3f::sUp);
			mesh->Tangents.Insert(Vec3f::sRight);
			mesh->TangentHandedness.Insert(1.0f);
			mesh->Texcoords.Insert(Vec2f(0.0f, 0.0f));
		}

		for (const uint32 index : surface.Indices) {
			mesh->Indices.Insert(index);
		}

		object->pMesh = mesh->AsDefaultMesh();
		object->Bounds = surface.Bounds;

		object->SetShadowCaster(true);
		object->SetShadowOnly(true);
		object->SetLightmapped(mbHasBakedSun);

		gWorldGrid->UpdateObject(object);

		AssetTicket ticket(static_cast<void*>(object));
		ticket.MarkAndSignalLoaded();

		pWorld->Attach(ticket);

		mCompiledObjects.push_back(object->ID);
	}
}

void Blockout::CreateCompiledWorld(const CompiledMap& map)
{
	const CompiledMapLightmap& lightmap = map.Lightmap;
	const bool has_indirect = lightmap.HasIndirect();

	const uint16* irradiance = nullptr;
	const uint8* directions = nullptr;

	if (has_indirect) {
		irradiance = lightmap.Irradiance.pData;
		directions = lightmap.Directions.pData;
	}

	bool has_lightmap = false;

	if (map.HasLightmap()) {
		has_lightmap = renderer::gGraphics->UploadLightmap(Vec2u(lightmap.Width, lightmap.Height),
														   lightmap.SunVisibility.pData, irradiance, directions);

		if (!has_lightmap) {
			LogWarning(LC_ASSET, "The compiled map's {}x{} lightmap is larger than {}x{}, its lighting stays dynamic",
					   lightmap.Width, lightmap.Height, Limits::LightmapSize, Limits::LightmapSize);
		}
	}

	mbHasBakedSun = has_lightmap && map.HasBakedSun();
	mbHasBakedIndirect = has_lightmap && has_indirect;
	mbBakedWithSun = lightmap.HasSun();

	if (mbHasBakedSun && map.HasFogShadow()) {
		renderer::gGraphics->UploadFogShadow(map.FogShadow);
	}
	else {
		renderer::gGraphics->ClearFogShadow();
	}

	mCompiledDecalReceivers.reserve(map.Surfaces.Size);

	for (size_t surface_index = 0; surface_index < map.Surfaces.Size; surface_index++) {
		const CompiledMapSurface& surface = map.Surfaces[surface_index];
		const CompiledMapMaterial& material = map.Materials[surface.MaterialIndex];

		MaterialLibraryID library_id = GetCompiledFallbackMaterial(material.LibraryIndex);

		if (!material.Name.IsEmpty()) {
			library_id = mMaterials.FindIDByName(material.Name.CStr());

			if (!library_id.IsValid()) {
				LogWarning(LC_ASSET, "The compiled map uses the unknown material '{}'", material.Name);
				library_id = GetCompiledFallbackMaterial(material.LibraryIndex);
			}
		}

		const MaterialID material_id = library_id.IsValid() ? GetMaterialForID(library_id) : GetDefaultMaterial();

		Object* object = gObjectManager->NewObject(String::Fmt("WORLD_{}", surface_index), material_id,
												   eObjectTag::LockTransform);

		if (object == nullptr) {
			continue;
		}

		Ref<MeshGen::GeneratedMesh> mesh = MakeRef<MeshGen::GeneratedMesh>();

		mesh->Positions.InitCapacity(surface.Vertices.Size);
		mesh->Normals.InitCapacity(surface.Vertices.Size);
		mesh->Tangents.InitCapacity(surface.Vertices.Size);
		mesh->TangentHandedness.InitCapacity(surface.Vertices.Size);
		mesh->Texcoords.InitCapacity(surface.Vertices.Size);
		mesh->Indices.InitCapacity(surface.Indices.Size);

		for (const CompiledMapVertex& vertex : surface.Vertices) {
			mesh->Positions.Insert(vertex.Position);
			mesh->Normals.Insert(vertex.Normal);
			mesh->Tangents.Insert(Vec3f(vertex.Tangent));
			mesh->TangentHandedness.Insert(vertex.Tangent.W);
			mesh->Texcoords.Insert(vertex.Texcoord);
		}

		for (const uint32 index : surface.Indices) {
			mesh->Indices.Insert(index);
		}

		const Material* surface_material = gMaterialManager->GetMaterial(material_id);
		const bool is_transparent = surface_material != nullptr && surface_material->IsTransparent();

		object->SetShadowCaster(is_transparent);
		object->pMesh = mesh->AsDefaultMesh();

		if (surface.HasVertexColors()) {
			renderer::gGraphics->SubmitImmediateUploadCmd([&](renderer::CommandBuffer& cmd)
														  { object->pMesh->UploadVertexColors(cmd, surface.Colors); });

			object->SetVertexColored(true);
		}

		if (has_lightmap && surface.HasLightmapTexcoords()) {
			renderer::gGraphics->SubmitImmediateUploadCmd(
				[&](renderer::CommandBuffer& cmd)
				{ object->pMesh->UploadLightmapUVs(cmd, surface.LightmapTexcoords); });

			object->SetLightmapped(true);
		}

		object->Bounds = surface.Bounds;

		if (map.HasAreas()) {
			object->SetFixedArea(surface.Area);
		}

		if (!is_transparent) {
			CompiledDecalReceiver& receiver = mCompiledDecalReceivers.emplace_back();
			receiver.Bounds = surface.Bounds;
			receiver.Positions.InitCapacity(surface.Vertices.Size);
			receiver.Normals.InitCapacity(surface.Vertices.Size);
			receiver.Indices.InitCapacity(surface.Indices.Size);

			for (const CompiledMapVertex& vertex : surface.Vertices) {
				receiver.Positions.Insert(vertex.Position);
				receiver.Normals.Insert(vertex.Normal);
			}

			for (const uint32 index : surface.Indices) {
				receiver.Indices.Insert(index);
			}
		}

		gWorldGrid->UpdateObject(object);

		AssetTicket ticket(static_cast<void*>(object));
		ticket.MarkAndSignalLoaded();

		pWorld->Attach(ticket);

		mCompiledObjects.push_back(object->ID);
	}

	CreateCompiledShadows(map);

	gWorldGrid->SetAreaGraph(map);

	for (size_t hull_index = 0; hull_index < map.Hulls.Size; hull_index++) {
		const CompiledMapHull& hull = map.Hulls[hull_index];
		const uint32 point_count = hull.GetPointCount();

		if (point_count < 4) {
			continue;
		}

		Vec3f min = Vec3f(hull.Points[0], hull.Points[1], hull.Points[2]);
		Vec3f max = min;

		for (uint32 i = 1; i < point_count; i++) {
			const Vec3f point(hull.Points[i * 3], hull.Points[i * 3 + 1], hull.Points[i * 3 + 2]);

			min = Vec3f::Min(min, point);
			max = Vec3f::Max(max, point);
		}

		const Vec3f midpoint = (min + max) * 0.5f;

		SizedArray<Vec3f> hull_points;
		hull_points.InitCapacity(point_count);

		for (uint32 i = 0; i < point_count; i++) {
			hull_points.Insert(Vec3f(hull.Points[i * 3], hull.Points[i * 3 + 1], hull.Points[i * 3 + 2]) - midpoint);
		}

		physics::Body* body = gPhysics->NewBody(String::Fmt("WORLD_HULL_{}", hull_index));
		body->CreateConvexHullBody(hull_points, physics::eMotionType::Static,
								   physics::BodyProps {
									   .ConvexRadius = 0.05f,
									   .Density = 100,
								   });

		body->SetMidpoint(midpoint);
		body->Teleport(Vec3f::sZero, Quat::scIdentity);

		mCompiledBodies.push_back(body->GetID());
	}

	for (size_t mesh_index = 0; mesh_index < map.CollisionMeshes.Size; mesh_index++) {
		const CompiledMapCollisionMesh& mesh = map.CollisionMeshes[mesh_index];
		const uint32 point_count = mesh.GetPointCount();

		if (point_count < 3 || mesh.Indices.Size < 3) {
			continue;
		}

		Vec3f min = Vec3f(mesh.Points[0], mesh.Points[1], mesh.Points[2]);
		Vec3f max = min;

		for (uint32 i = 1; i < point_count; i++) {
			const Vec3f point(mesh.Points[i * 3], mesh.Points[i * 3 + 1], mesh.Points[i * 3 + 2]);

			min = Vec3f::Min(min, point);
			max = Vec3f::Max(max, point);
		}

		const Vec3f midpoint = (min + max) * 0.5f;

		SizedArray<Vec3f> positions;
		positions.InitCapacity(point_count);

		for (uint32 i = 0; i < point_count; i++) {
			positions.Insert(Vec3f(mesh.Points[i * 3], mesh.Points[i * 3 + 1], mesh.Points[i * 3 + 2]) - midpoint);
		}

		physics::Body* body = gPhysics->NewBody(String::Fmt("WORLD_MESH_{}", mesh_index));
		body->CreateTriangleMeshBody(positions, mesh.Indices, physics::BodyProps {});

		body->SetMidpoint(midpoint);
		body->Teleport(Vec3f::sZero, Quat::scIdentity);

		mCompiledBodies.push_back(body->GetID());
	}

	mCompiledProbeLayout.CopyFrom(map.Probes);
	mbIsCompiledMap = true;

	if (map.HasBakedProbes()) {
		mCompiledProbeIrradiance.InitAsCopyOf(map.ProbeIrradiance);
		mCompiledProbeMoments.InitAsCopyOf(map.ProbeMoments);
	}
	else {
		mCompiledProbeIrradiance.Free();
		mCompiledProbeMoments.Free();
	}

	mBakedSunDirection = Vec3f(lightmap.SunDirection[0], lightmap.SunDirection[1], lightmap.SunDirection[2]);

	if (mbHasBakedIndirect) {
		LogInfo("Loaded baked indirect light; {}x{} lightmap", lightmap.Width, lightmap.Height);
	}

	if (mbHasBakedSun) {
		gProbeManager->SetSunVisibility(Slice<const Vec3f>(map.Probes.Positions.pData, map.Probes.Positions.Size),
										Slice<const uint8>(map.ProbeSunVisibility.pData, map.ProbeSunVisibility.Size));

		LogInfo("Loaded baked sun shadows; {}x{} lightmap, {} probes", lightmap.Width, lightmap.Height,
				map.ProbeSunVisibility.Size);
	}

	LogInfo("Loaded the compiled map: {} surfaces, {} hulls and {} triangle meshes replace {} brushes and patches, {} "
			"light probes placed",
			map.Surfaces.Size, map.Hulls.Size, map.CollisionMeshes.Size, map.BrushNames.Size,
			map.Probes.GetProbeCount());
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

MaterialLibraryID Blockout::GetCompiledFallbackMaterial(int32 library_index) const
{
	if (library_index == scCompiledEditableMaterialIndex) {
		return mEditableMaterialID;
	}

	if (library_index == scCompiledLockedMaterialIndex) {
		return mDefaultMaterialID;
	}

	return MaterialLibraryID::scNull;
}

MaterialID Blockout::GetDefaultMaterial() const { return mMaterials.GetMaterial(mDefaultMaterialID); }

MaterialID Blockout::GetSpecialMaterial(eSpecialMaterial special) const
{
	return mMaterials.GetSpecialMaterial(special);
}

bool Blockout::IsCompiledObject(const Object* object) const
{
	return object != nullptr &&
		   std::find(mCompiledObjects.begin(), mCompiledObjects.end(), object->ID) != mCompiledObjects.end();
}

Brush* Blockout::GetBrush(const Object* object)
{
	if (object == nullptr) {
		return nullptr;
	}

	return mBrushes.Find(object->ID.GetID());
}

Patch* Blockout::GetPatch(const Object* object)
{
	if (object == nullptr) {
		return nullptr;
	}

	return mPatches.Find(object->ID.GetID());
}

bool Blockout::GetLocalCenter(const Object* object, Vec3f& out_center)
{
	const Brush* brush = GetBrush(object);

	if (brush != nullptr) {
		out_center = brush->GetCenter();
		return true;
	}

	const Patch* patch = GetPatch(object);

	if (patch != nullptr) {
		out_center = patch->GetCenter();
		return true;
	}

	return false;
}

static void HashBytes(uint64& hash, const void* data, size_t size)
{
	const uint8* bytes = static_cast<const uint8*>(data);

	for (size_t i = 0; i < size; i++) {
		hash ^= bytes[i];
		hash *= 0x100000001B3ULL;
	}
}

static void HashFloat(uint64& hash, float32 value) { HashBytes(hash, &value, sizeof(value)); }

static void HashMatrix(uint64& hash, const Mat4f& matrix) { HashBytes(hash, matrix.RawData, sizeof(matrix.RawData)); }

static void HashPlanes(uint64& hash, const Brush& brush)
{
	for (const BrushPlane& plane : brush.Planes) {
		HashFloat(hash, plane.Normal.X);
		HashFloat(hash, plane.Normal.Y);
		HashFloat(hash, plane.Normal.Z);
		HashFloat(hash, plane.Distance);
	}
}

static Vec3f TransformPoint(const Mat4f& matrix, const Vec3f point)
{
	const Vec4f moved = matrix * Vec4f(point.X, point.Y, point.Z, 1.0f);

	return Vec3f(moved.X, moved.Y, moved.Z);
}

static Vec3f TransformDirection(const Mat4f& matrix, const Vec3f direction)
{
	const Vec4f turned = matrix * Vec4f(direction.X, direction.Y, direction.Z, 0.0f);

	return Vec3f(turned.X, turned.Y, turned.Z);
}

static void HashPatch(uint64& hash, const Patch& patch)
{
	for (const PatchPoint& point : patch.Points) {
		HashFloat(hash, point.Position.X);
		HashFloat(hash, point.Position.Y);
		HashFloat(hash, point.Position.Z);
		HashFloat(hash, point.Weight);
	}

	const uint32 settings[] = {
		patch.Width,
		patch.Height,
		patch.SubdivisionsU,
		patch.SubdivisionsV,
		static_cast<uint32>(patch.bSmooth),
		static_cast<uint32>(patch.Closure),
		static_cast<uint32>(patch.bIsDetailMesh),
	};

	HashBytes(hash, settings, sizeof(settings));
	HashFloat(hash, patch.Thickness);

	if (patch.TurnedCells.IsNotEmpty()) {
		HashBytes(hash, patch.TurnedCells.pData, patch.TurnedCells.Size);
	}
}

struct PatchWorldMesh
{
	SizedArray<Vec3f> Positions;
	SizedArray<Vec3f> Normals;
	SizedArray<uint32> Indices;
};

static void AttachPatchMesh(Object* object, MeshGen::GeneratedMesh& mesh, const SizedArray<uint32>& colors)
{
	object->pMesh = mesh.AsDefaultMesh();

	const bool has_colors = colors.IsNotEmpty() && colors.Size == mesh.Positions.Size;

	if (has_colors) {
		renderer::gGraphics->SubmitImmediateUploadCmd([&](renderer::CommandBuffer& cmd)
													  { object->pMesh->UploadVertexColors(cmd, colors); });
	}

	object->SetVertexColored(has_colors);
}

static void MakePatchWorldMesh(const Patch& patch, const Mat4f& world, PatchWorldMesh& out_mesh)
{
	SizedArray<Vec3f> tangents;
	SizedArray<float32> handedness;
	SizedArray<Vec2f> texcoords;

	patch.GenerateMesh(out_mesh.Positions, out_mesh.Normals, tangents, handedness, texcoords, out_mesh.Indices);

	for (Vec3f& position : out_mesh.Positions) {
		position = TransformPoint(world, position);
	}

	for (Vec3f& normal : out_mesh.Normals) {
		normal = TransformDirection(world, normal);
	}
}

static bool BoundsOverlap(const BBox& a, const BBox& b)
{
	return a.Min.X <= b.Max.X && a.Max.X >= b.Min.X && a.Min.Y <= b.Max.Y && a.Max.Y >= b.Min.Y && a.Min.Z <= b.Max.Z &&
		   a.Max.Z >= b.Min.Z;
}

static BBox GetBrushWorldBounds(const Brush& brush, const Mat4f& world)
{
	return OBBox::FromLocalBounds(BBox(brush.GetBoundsMin(), brush.GetBoundsMax()), world).GetWorldAABB();
}

static bool HasOpaqueMaterial(Object* object)
{
	const Material* material = gMaterialManager->GetMaterial(object->GetMaterialID());

	return material == nullptr || !material->IsTransparent();
}

bool Blockout::IsDecalReceiver(Object* object)
{
	return object != nullptr && object != pXFormObject && object != pPreviewObject && !object->IsDecal() &&
		   !object->IsVolume() && !object->IsHidden() && !IsDynamic(object) && HasOpaqueMaterial(object);
}

void Blockout::GatherDecalReceivers(const BBox& bounds, std::vector<DecalReceiver>& out_receivers)
{
	out_receivers.clear();

	for (const ObjectID id : BlockoutObjects) {
		Object* object = gObjectManager->GetObject(id);
		const Brush* brush = GetBrush(object);

		if (brush == nullptr || !brush->IsValid() || !IsDecalReceiver(object)) {
			continue;
		}

		const Mat4f world = object->GetWorldMatrix();

		if (BoundsOverlap(GetBrushWorldBounds(*brush, world), bounds)) {
			out_receivers.push_back(DecalReceiver { .pBrush = brush, .World = world, .ID = object->ID });
		}
	}

	for (const ObjectID id : BlockoutObjects) {
		Object* object = gObjectManager->GetObject(id);
		const Patch* patch = GetPatch(object);

		if (patch == nullptr || !patch->IsValid() || !IsDecalReceiver(object)) {
			continue;
		}

		const Mat4f world = object->GetWorldMatrix();

		if (BoundsOverlap(OBBox::FromLocalBounds(object->Bounds, world).GetWorldAABB(), bounds)) {
			out_receivers.push_back(DecalReceiver { .pPatch = patch, .World = world, .ID = object->ID });
		}
	}

	std::vector<SubviewSolid> solids;
	GetSubviewSolids(solids);

	for (const SubviewSolid& solid : solids) {
		if (solid.bTransparent || solid.pOwner->IsHidden() || !solid.pBrush->IsValid()) {
			continue;
		}

		if (BoundsOverlap(GetBrushWorldBounds(*solid.pBrush, solid.World), bounds)) {
			out_receivers.push_back(
				DecalReceiver { .pBrush = solid.pBrush, .World = solid.World, .ID = solid.pOwner->ID });
		}
	}
}

uint64 Blockout::HashDecal(Object* decal, const Brush& volume, const BBox& bounds,
						   const std::vector<DecalReceiver>& receivers) const
{
	uint64 hash = FX_HASH64_FNV1A_INIT;

	HashMatrix(hash, decal->GetWorldMatrix());
	HashPlanes(hash, volume);

	for (const DecalReceiver& receiver : receivers) {
		const uint32 id = receiver.ID.GetID();

		HashBytes(hash, &id, sizeof(id));
		HashMatrix(hash, receiver.World);

		if (receiver.pBrush != nullptr) {
			HashPlanes(hash, *receiver.pBrush);
		}

		if (receiver.pPatch != nullptr) {
			HashPatch(hash, *receiver.pPatch);
		}
	}

	const uint64 compiled_count = mCompiledDecalReceivers.size();
	HashBytes(hash, &compiled_count, sizeof(compiled_count));

	return hash;
}

void Blockout::RebuildDecalMesh(Object* decal, const Brush& volume)
{
	const Mat4f world = decal->GetWorldMatrix();
	const BBox bounds = GetBrushWorldBounds(volume, world);

	MeshDecalBuilder builder(volume, world);

	std::vector<DecalReceiver> receivers;
	GatherDecalReceivers(bounds, receivers);

	for (const DecalReceiver& receiver : receivers) {
		if (receiver.pBrush != nullptr) {
			builder.AddBrush(*receiver.pBrush, receiver.World);
		}
	}

	builder.ClipBrushes();

	for (const DecalReceiver& receiver : receivers) {
		if (receiver.pPatch == nullptr) {
			continue;
		}

		PatchWorldMesh mesh;
		MakePatchWorldMesh(*receiver.pPatch, receiver.World, mesh);

		builder.AddTriangles(mesh.Positions.pData, mesh.Normals.pData, mesh.Indices.pData,
							 static_cast<uint32>(mesh.Indices.Size));
	}

	for (const CompiledDecalReceiver& receiver : mCompiledDecalReceivers) {
		if (BoundsOverlap(receiver.Bounds, bounds)) {
			builder.AddTriangles(receiver.Positions.pData, receiver.Normals.pData, receiver.Indices.pData,
								 static_cast<uint32>(receiver.Indices.Size));
		}
	}

	Ref<MeshGen::GeneratedMesh> mesh = MakeRef<MeshGen::GeneratedMesh>();

	if (builder.IsEmpty()) {
		LogWarning(LC_ASSET, "Decal '{}' does not cover any surface that can receive it", decal->Name.Get());
		MeshDecalBuilder::BuildPlaceholder(volume, *mesh);
	}
	else {
		builder.Build(*mesh);
	}

	decal->pMesh = mesh->AsDefaultMesh();

	mDecalHashes.Insert(decal->ID.GetID(), HashDecal(decal, volume, bounds, receivers));
}

void Blockout::RefreshDecals(bool force)
{
	std::vector<DecalReceiver> receivers;

	for (const ObjectID id : BlockoutObjects) {
		Object* decal = gObjectManager->GetObject(id);

		if (decal == nullptr || !decal->IsDecal()) {
			continue;
		}

		const Brush* volume = GetBrush(decal);

		if (volume == nullptr || !volume->IsValid()) {
			continue;
		}

		if (!force) {
			const BBox bounds = GetBrushWorldBounds(*volume, decal->GetWorldMatrix());
			GatherDecalReceivers(bounds, receivers);

			const uint64* previous = mDecalHashes.Find(decal->ID.GetID());

			if (previous != nullptr && *previous == HashDecal(decal, *volume, bounds, receivers)) {
				continue;
			}
		}

		RebuildDecalMesh(decal, *volume);
	}
}

static bool RayEntersBrush(const Brush& brush, const Mat4f& world, const Vec3f origin, const Vec3f direction,
						   float32& out_distance)
{
	const Mat4f to_local = world.AffineInverse();
	const Vec3f local_origin = TransformPoint(to_local, origin);
	const Vec3f local_direction = TransformDirection(to_local, direction);

	float32 enter = -FLT_MAX;
	float32 exit = FLT_MAX;

	for (const BrushPlane& plane : brush.Planes) {
		const float32 side = plane.Normal.Dot(local_origin) - plane.Distance;
		const float32 approach = plane.Normal.Dot(local_direction);

		if (std::fabs(approach) < 1e-8f) {
			if (side > 0.0f) {
				return false;
			}

			continue;
		}

		const float32 distance = -side / approach;

		if (approach < 0.0f) {
			enter = std::max(enter, distance);
		}
		else {
			exit = std::min(exit, distance);
		}
	}

	if (enter < 0.0f || enter > exit) {
		return false;
	}

	out_distance = enter;
	return true;
}

static bool RayHitsTriangle(const Vec3f origin, const Vec3f direction, const Vec3f a, const Vec3f b, const Vec3f c,
							float32& out_distance)
{
	const Vec3f edge_ab = b - a;
	const Vec3f edge_ac = c - a;
	const Vec3f p = direction.Cross(edge_ac);
	const float32 determinant = edge_ab.Dot(p);

	if (std::fabs(determinant) < 1e-12f) {
		return false;
	}

	const float32 inverse = 1.0f / determinant;
	const Vec3f to_origin = origin - a;
	const float32 u = to_origin.Dot(p) * inverse;

	if (u < 0.0f || u > 1.0f) {
		return false;
	}

	const Vec3f q = to_origin.Cross(edge_ab);
	const float32 v = direction.Dot(q) * inverse;

	if (v < 0.0f || u + v > 1.0f) {
		return false;
	}

	const float32 distance = edge_ac.Dot(q) * inverse;

	if (distance < 0.0f) {
		return false;
	}

	out_distance = distance;
	return true;
}

void Blockout::RaycastDecalSurfaces(const Vec3f origin, const Vec3f direction, float32 max_distance,
									std::vector<float32>& out_distances)
{
	out_distances.clear();

	const Vec3f end = origin + direction * max_distance;
	const BBox bounds(Vec3f::Min(origin, end), Vec3f::Max(origin, end));

	std::vector<DecalReceiver> receivers;
	GatherDecalReceivers(bounds, receivers);

	std::vector<float32> candidates;

	for (const DecalReceiver& receiver : receivers) {
		float32 distance = 0.0f;

		if (receiver.pBrush != nullptr &&
			RayEntersBrush(*receiver.pBrush, receiver.World, origin, direction, distance) && distance <= max_distance) {
			candidates.push_back(distance);
		}

		if (receiver.pPatch == nullptr) {
			continue;
		}

		PatchWorldMesh mesh;
		MakePatchWorldMesh(*receiver.pPatch, receiver.World, mesh);

		for (size_t i = 0; i + 2 < mesh.Indices.Size; i += 3) {
			const uint32 a = mesh.Indices[i];
			const uint32 b = mesh.Indices[i + 1];
			const uint32 c = mesh.Indices[i + 2];

			const Vec3f normal = mesh.Normals[a] + mesh.Normals[b] + mesh.Normals[c];

			if (normal.Dot(direction) >= 0.0f) {
				continue;
			}

			if (RayHitsTriangle(origin, direction, mesh.Positions[a], mesh.Positions[b], mesh.Positions[c], distance) &&
				distance <= max_distance) {
				candidates.push_back(distance);
			}
		}
	}

	for (const CompiledDecalReceiver& receiver : mCompiledDecalReceivers) {
		if (!BoundsOverlap(receiver.Bounds, bounds)) {
			continue;
		}

		for (size_t i = 0; i + 2 < receiver.Indices.Size; i += 3) {
			const uint32 a = receiver.Indices[i];
			const uint32 b = receiver.Indices[i + 1];
			const uint32 c = receiver.Indices[i + 2];

			const Vec3f normal = receiver.Normals[a] + receiver.Normals[b] + receiver.Normals[c];

			if (normal.Dot(direction) >= 0.0f) {
				continue;
			}

			float32 distance = 0.0f;

			if (RayHitsTriangle(origin, direction, receiver.Positions[a], receiver.Positions[b], receiver.Positions[c],
								distance) &&
				distance <= max_distance) {
				candidates.push_back(distance);
			}
		}
	}

	for (const float32 distance : candidates) {
		const Vec3f above = origin + direction * (distance - MeshDecalBuilder::scSurfaceOffset);

		bool is_buried = false;

		for (const DecalReceiver& receiver : receivers) {
			if (receiver.pBrush == nullptr) {
				continue;
			}

			const Vec3f local = TransformPoint(receiver.World.AffineInverse(), above);

			if (receiver.pBrush->ContainsPoint(local, -scDecalBuriedTolerance)) {
				is_buried = true;
				break;
			}
		}

		if (!is_buried) {
			out_distances.push_back(distance);
		}
	}
}

MaterialID Blockout::GetFaceMaterial(const String& name) const
{
	const MaterialLibraryID id = mMaterials.FindIDByName(name.CStr());

	if (!id.IsValid()) {
		return GetDefaultMaterial();
	}

	return GetMaterialForID(id);
}

bool Blockout::IsFaceNode(const Object* object) const { return mFaceNodeMaterials.Contains(object->ID.GetID()); }

void Blockout::DestroyFaceNodes(Object* object)
{
	if (object->AttachedNodes.IsEmpty()) {
		return;
	}

	for (ObjectID node_id : object->AttachedNodes) {
		mFaceNodeMaterials.Remove(node_id.GetID());
		gWorld->Detach(node_id);
		gObjectManager->DestroyObject(node_id);
	}

	object->AttachedNodes.Clear();
}

static Ref<PrimitiveMesh> MakeFaceMesh(const Brush& brush, const String* material, BBox& out_bounds)
{
	MeshGen::GeneratedMesh mesh;
	brush.GenerateMesh(mesh.Positions, mesh.Normals, mesh.Tangents, mesh.TangentHandedness, mesh.Texcoords,
					   mesh.Indices, material);

	if (mesh.Positions.Size == 0) {
		return nullptr;
	}

	out_bounds = BBox(mesh.Positions[0], mesh.Positions[0]);

	for (const Vec3f position : mesh.Positions) {
		out_bounds = BBox(Vec3f::Min(out_bounds.Min, position), Vec3f::Max(out_bounds.Max, position));
	}

	return mesh.AsDefaultMesh();
}

void Blockout::RebuildFaceMeshes(Object* object, const Brush& brush)
{
	StackArray<String, Brush::scMaxPlanes> materials;
	brush.GetFaceMaterials(materials);

	BBox bounds;

	if (materials.Size == 0) {
		DestroyFaceNodes(object);
		object->pMesh = MakeFaceMesh(brush, nullptr, bounds);
		return;
	}

	const String brush_faces {};
	object->pMesh = MakeFaceMesh(brush, &brush_faces, bounds);

	StackArray<ObjectID, Brush::scMaxPlanes> nodes;
	nodes.Size = materials.Size;

	for (ObjectID& node_id : nodes) {
		node_id = ObjectID::scNull;
	}

	StackArray<ObjectID, Brush::scMaxPlanes> unused_nodes;

	for (ObjectID node_id : object->AttachedNodes) {
		const String* name = mFaceNodeMaterials.Find(node_id.GetID());
		bool is_used = false;

		for (uint32 i = 0; name != nullptr && i < materials.Size; i++) {
			if (nodes[i].IsNull() && materials[i] == *name) {
				nodes[i] = node_id;
				is_used = true;
				break;
			}
		}

		if (!is_used && unused_nodes.Size < unused_nodes.Capacity) {
			unused_nodes.Insert(node_id);
		}
	}

	for (ObjectID node_id : unused_nodes) {
		mFaceNodeMaterials.Remove(node_id.GetID());
		gWorld->Detach(node_id);
		gObjectManager->DestroyObject(node_id);
	}

	if (!object->AttachedNodes.IsInited()) {
		object->AttachedNodes.Create(8);
	}

	object->AttachedNodes.Clear();

	for (uint32 i = 0; i < materials.Size; i++) {
		Object* node = nodes[i].IsNull() ? nullptr : gObjectManager->GetObject(nodes[i]);
		const MaterialID material = GetFaceMaterial(materials[i]);

		if (node == nullptr) {
			node = gObjectManager->NewObject(String::Fmt("{}_face{}", object->Name.Get(), i), material,
											 eObjectTag::None);

			if (node == nullptr) {
				continue;
			}

			node->SetRotation(object->mRotation);
			object->AttachObject(node->ID);

			mFaceNodeMaterials.Insert(node->ID.GetID(), materials[i]);
		}
		else {
			node->SetMaterial(material);
			object->AttachedNodes.Insert(node->ID);
		}

		node->pMesh = MakeFaceMesh(brush, &materials[i], node->Bounds);
		node->SetRotationOrigin(object->RotationOrigin);
		node->SetShadowCaster(object->IsShadowCaster());
		node->SetCullable(object->IsCullable());
		node->SetHidden(object->IsHidden());
		node->SubviewInstance = object->SubviewInstance;
	}
}

void Blockout::RebuildBrush(Object* object, Brush&& brush, physics::eMotionType motion_type)
{
	Assert(brush.IsValid());

	if (object->IsDecal()) {
		object->Bounds.Min = brush.GetBoundsMin();
		object->Bounds.Max = brush.GetBoundsMax();
		object->SetRotationOrigin(-brush.GetCenter());

		gPhysics->DestroyBody(object->PhysicsID);
		object->PhysicsID = physics::BodyID::scNull;

		object->SetDecal(true);

		DestroyFaceNodes(object);
		RebuildDecalMesh(object, brush);

		gWorldGrid->UpdateObject(object);

		mBrushes.Insert(object->ID.GetID(), std::move(brush));
		return;
	}

	object->Bounds.Min = brush.GetBoundsMin();
	object->Bounds.Max = brush.GetBoundsMax();

	// Blockouts rotate about the centre of their bounds
	const Vec3f midpoint = brush.GetCenter();
	object->SetRotationOrigin(-midpoint);

	RebuildFaceMeshes(object, brush);

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

	if (IsBrushMarkerMaterial(object->GetSpecialMaterial())) {
		object->ApplyVolumeState();
	}

	mBrushes.Insert(object->ID.GetID(), std::move(brush));
}

void Blockout::RebuildPatch(Object* object, Patch&& patch)
{
	Assert(patch.IsValid());

	const Vec3f center = patch.GetCenter();
	object->SetRotationOrigin(-center);

	const Mat4f world = object->GetWorldMatrix();

	Ref<MeshGen::GeneratedMesh> mesh = MakeRef<MeshGen::GeneratedMesh>();
	SizedArray<uint32> colors;
	SizedArray<uint32>* color_output = nullptr;

	if (patch.HasVertexColors()) {
		color_output = &colors;
	}

	patch.GenerateMesh(mesh->Positions, mesh->Normals, mesh->Tangents, mesh->TangentHandedness, mesh->Texcoords,
					   mesh->Indices, world, color_output);

	if (mesh->Indices.IsEmpty()) {
		LogWarning(LC_ASSET, "Patch '{}' has no area, it is not drawn", object->Name.Get());

		mesh->Positions = { center, center, center };
		mesh->Normals = { Vec3f::sUp, Vec3f::sUp, Vec3f::sUp };
		mesh->Tangents = { Vec3f(1.0f, 0.0f, 0.0f), Vec3f(1.0f, 0.0f, 0.0f), Vec3f(1.0f, 0.0f, 0.0f) };
		mesh->TangentHandedness = { 1.0f, 1.0f, 1.0f };
		mesh->Texcoords = { Vec2f::scZero, Vec2f::scZero, Vec2f::scZero };
		mesh->Indices = { 0, 1, 2 };
	}

	Vec3f min = mesh->Positions[0];
	Vec3f max = mesh->Positions[0];

	for (const Vec3f position : mesh->Positions) {
		min = Vec3f::Min(min, position);
		max = Vec3f::Max(max, position);
	}

	AttachPatchMesh(object, *mesh, colors);

	object->Bounds.Min = min;
	object->Bounds.Max = max;
	object->SetTag(eObjectTag::Patch);

	gWorldGrid->UpdateObject(object);

	gPhysics->DestroyBody(object->PhysicsID);
	object->PhysicsID = physics::BodyID::scNull;

	SizedArray<Vec3f> collider_points;
	collider_points.InitCapacity(mesh->Positions.Size);

	for (const Vec3f position : mesh->Positions) {
		collider_points.Insert(position - center);
	}

	physics::Body* phys = gPhysics->NewBody(object->Name.Get());
	phys->CreateTriangleMeshBody(collider_points, mesh->Indices, physics::BodyProps {});

	phys->SetMidpoint(center);
	phys->Teleport(object->GetPosition(), object->mRotation);

	object->AttachCollider(phys);

	uint64 frame = FX_HASH64_FNV1A_INIT;
	HashMatrix(frame, world);
	mPatchMeshFrames.Insert(object->ID.GetID(), frame);

	mPatches.Insert(object->ID.GetID(), std::move(patch));
}

void Blockout::RefreshCappedPatches()
{
	for (const ObjectID id : BlockoutObjects) {
		Object* object = gObjectManager->GetObject(id);
		const Patch* patch = GetPatch(object);

		if (patch == nullptr || patch->Closure != ePatchClosure::Cap) {
			continue;
		}

		const Mat4f world = object->GetWorldMatrix();

		uint64 frame = FX_HASH64_FNV1A_INIT;
		HashMatrix(frame, world);

		const uint64* previous = mPatchMeshFrames.Find(id.GetID());

		if (previous != nullptr && *previous == frame) {
			continue;
		}

		Ref<MeshGen::GeneratedMesh> mesh = MakeRef<MeshGen::GeneratedMesh>();
		SizedArray<uint32> colors;
		SizedArray<uint32>* color_output = nullptr;

		if (patch->HasVertexColors()) {
			color_output = &colors;
		}

		patch->GenerateMesh(mesh->Positions, mesh->Normals, mesh->Tangents, mesh->TangentHandedness, mesh->Texcoords,
							mesh->Indices, world, color_output);

		if (mesh->Indices.IsNotEmpty()) {
			AttachPatchMesh(object, *mesh, colors);
		}

		mPatchMeshFrames.Insert(id.GetID(), frame);
	}
}

bool Blockout::SetPatch(Object* object, Patch&& patch)
{
	const Patch* current = GetPatch(object);

	if (current == nullptr || !patch.IsValid()) {
		return false;
	}

	const Vec3f offset = GetOffsetToKeepInPlace(object, current->GetCenter(), patch.GetCenter());

	if (!offset.IsCloseTo(simd::LoadFloat4(0.0f))) {
		object->SetPosition(object->GetPosition() + offset);
	}

	RebuildPatch(object, std::move(patch));

	return true;
}

bool Blockout::RaycastPatch(Object* object, const Vec3f origin, const Vec3f direction, Vec3f& out_face_normal,
							Vec3f* out_point)
{
	const Patch* patch = GetPatch(object);

	if (patch == nullptr) {
		return false;
	}

	const Mat4f to_local = object->GetWorldMatrix().AffineInverse();
	const Vec4f local_origin4 = to_local * Vec4f(origin.X, origin.Y, origin.Z, 1.0f);
	const Vec4f local_direction4 = to_local * Vec4f(direction.X, direction.Y, direction.Z, 0.0f);

	const Vec3d local_origin(local_origin4.X, local_origin4.Y, local_origin4.Z);
	const Vec3d local_direction(local_direction4.X, local_direction4.Y, local_direction4.Z);

	SizedArray<Vec3f> positions;
	SizedArray<Vec3f> normals;
	SizedArray<Vec3f> tangents;
	SizedArray<float32> handedness;
	SizedArray<Vec2f> texcoords;
	SizedArray<uint32> indices;

	patch->GenerateMesh(positions, normals, tangents, handedness, texcoords, indices);

	const auto to_double = [](const Vec3f value) { return Vec3d(value.X, value.Y, value.Z); };

	double nearest = DBL_MAX;

	for (size_t index = 0; index + 2 < indices.Size; index += 3) {
		const Vec3d a = to_double(positions[indices[index]]);
		const Vec3d b = to_double(positions[indices[index + 1]]);
		const Vec3d c = to_double(positions[indices[index + 2]]);

		const Vec3d edge_a = b - a;
		const Vec3d edge_b = c - a;
		const Vec3d p = local_direction.Cross(edge_b);
		const double determinant = edge_a.Dot(p);

		if (std::fabs(determinant) < 1e-14) {
			continue;
		}

		const double inverse = 1.0 / determinant;
		const Vec3d to_origin = local_origin - a;
		const double u = to_origin.Dot(p) * inverse;

		if (u < 0.0 || u > 1.0) {
			continue;
		}

		const Vec3d q = to_origin.Cross(edge_a);
		const double v = local_direction.Dot(q) * inverse;

		if (v < 0.0 || u + v > 1.0) {
			continue;
		}

		const double distance = edge_b.Dot(q) * inverse;

		if (distance < 0.0 || distance >= nearest) {
			continue;
		}

		Vec3d normal = edge_b.Cross(edge_a).Normalized();

		if (normal.Dot(local_direction) > 0.0) {
			normal = normal * -1.0;
		}

		nearest = distance;
		out_face_normal = normal.ToVec3f();
	}

	if (nearest == DBL_MAX) {
		return false;
	}

	if (out_point != nullptr) {
		*out_point = origin + direction * static_cast<float32>(nearest);
	}

	return true;
}

Brush Blockout::ReadBrushEntry(ConfigEntry& entry) const { return fx::ReadBrushEntry(entry); }

void Blockout::WriteBrushEntry(ConfigEntry& entry, const Object* object)
{
	const Patch* patch = GetPatch(object);

	if (patch != nullptr) {
		WritePatchEntry(entry, *patch);
		return;
	}

	const Brush* brush = GetBrush(object);

	// Plain boxes are stored as their extents in each direction
	if (brush == nullptr || (brush->IsBox() && brush->HasDefaultTextures() && !brush->HasFaceMaterials())) {
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

	WriteFaceMaterials(entry, *brush);
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
	object->SubviewInstance = mActiveSubview;

	Brush brush = Brush::FromBox(bounds.Min, bounds.Max);
	RebuildBrush(object, std::move(brush), physics::eMotionType::Static);

	AssetTicket ticket(static_cast<void*>(object));
	ticket.MarkAndSignalLoaded();

	pWorld->Attach(ticket);

	BlockoutObjects.NewItem(nullptr, object->ID);

	return object->ID;
}

MaterialID Blockout::ReadBrushMaterial(ConfigEntry& entry)
{
	MaterialID material_id = GetDefaultMaterial();

	if (entry.GetMemberValue(HashStr32("lock"), 0) != 1) {
		material_id = mMaterials.GetMaterial(mEditableMaterialID);
	}

	const char* mat_name = ReadBrushMaterialName(entry);

	if (mat_name[0] == '\0') {
		return material_id;
	}

	const MaterialLibraryID mat_id = mMaterials.FindIDByName(mat_name);

	if (mat_id.IsValid()) {
		material_id = GetMaterialForID(mat_id);
	}
	else {
		LogWarning(LC_ASSET, "Blockout '{}' uses the unknown material '{}'", entry.Name.Get(), mat_name);
	}

	return material_id;
}

ObjectID Blockout::CreateBrush(ConfigEntry& entry)
{
	const BrushPlacement placement = ReadBrushPlacement(entry);

	String blockout_id = String::Fmt("{}", entry.Name.Get());

	LogInfo("Adding blockout '{}'", blockout_id);

	const bool is_patch = IsPatchEntry(entry);

	Brush brush;
	Patch patch;

	if (is_patch) {
		patch = ReadPatchEntry(entry);

		if (!patch.IsValid()) {
			LogError("Blockout '{}' does not describe a valid patch, skipping", blockout_id);
			return ObjectID::scNull;
		}
	}
	else {
		brush = ReadBrushEntry(entry);

		if (!brush.IsValid()) {
			LogError("Blockout '{}' does not describe a valid convex brush, skipping", blockout_id);
			return ObjectID::scNull;
		}
	}

	const MaterialID material_id = ReadBrushMaterial(entry);

	eObjectTag object_tags = eObjectTag::Blockout;

	if (entry.GetMemberValue(HashStr32("lock"), 0) == 1) {
		SetFlag(object_tags, eObjectTag::LockTransform);
	}

	Object* object = gObjectManager->NewObject(blockout_id.Str(), material_id, object_tags);
	object->MoveBy(placement.Position);
	object->SetShadowCaster(entry.GetMemberValue(HashStr32("shadows"), 1) != 0);
	object->SetCullable(entry.GetMemberValue(HashStr32("nocull"), 0) == 0);

	object->SetRotation(placement.Rotation);

	if (entry.GetMemberValue(HashStr32("probevolume"), 0) == 1) {
		object->SetProbeVolume(true);
	}

	if (entry.GetMemberValue(HashStr32("reflectionprobe"), 0) == 1) {
		object->SetReflectionProbe(true);
	}


	if (entry.GetMemberValue(HashStr32("spawn"), 0) == 1) {
		object->SetTag(eObjectTag::Spawn);
	}

	if (entry.GetMemberValue(HashStr32("trigger"), 0) == 1) {
		object->SetTag(eObjectTag::Trigger);
	}

	if (IsDecalEntry(entry)) {
		object->SetTag(eObjectTag::Decal);
	}

	ReadScriptEntry(object, entry);
	ReadTriggerEntry(object, entry);

	physics::eMotionType is_dynamic = entry.GetMemberValue(HashStr32("dynamic"), 0) ? physics::eMotionType::Dynamic
																					: physics::eMotionType::Static;

	if (is_patch) {
		RebuildPatch(object, std::move(patch));
	}
	else {
		RebuildBrush(object, std::move(brush), is_dynamic);
	}

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

	const Patch* existing_patch = GetPatch(object);

	if (existing_patch != nullptr) {
		RebuildPatch(object, existing_patch->Clone());
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

	DestroyFaceNodes(object);

	mBrushes.Remove(object->ID.GetID());
	mPatches.Remove(object->ID.GetID());
	mPatchMeshFrames.Remove(object->ID.GetID());

	gWorld->Detach(object->ID);
	gObjectManager->DestroyObject(object->ID);
}

std::string Blockout::MakeUniqueBrushName() const
{
	uint32 index = BlockoutObjects.Size;
	String name = String::Fmt("{}", index);

	while (gObjectManager->FindObject(HashStr32(name.CStr())) != nullptr) {
		name = String::Fmt("{}", ++index);
	}

	return name.Str();
}

Object* Blockout::NewObject(const Vec3f position, MaterialID material)
{
	std::string blockout_name = MakeUniqueBrushName();
	LogInfo("Creating new blockout object '{}'", blockout_name);

	Object* object = gObjectManager->NewObject(blockout_name, material.IsNull() ? GetDefaultMaterial() : material,
											   eObjectTag::Blockout);

	constexpr float32 scale = 0.25f;

	object->MoveBy(position);
	object->SetShadowCaster(true);
	object->SubviewInstance = mActiveSubview;

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

	std::string blockout_name = MakeUniqueBrushName();
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
	dupe->SetShadowCaster(object->IsShadowCaster());
	dupe->SetCullable(object->IsCullable());

	if (IsEditingSubview()) {
		dupe->SubviewInstance = mActiveSubview;
	}
	else {
		dupe->SubviewInstance = object->SubviewInstance;
		dupe->SubviewMember = object->SubviewMember;
	}

	if (object->IsProbeVolume()) {
		dupe->SetProbeVolume(true);
	}

	if (object->IsReflectionProbe()) {
		dupe->SetReflectionProbe(true);
	}


	if (object->IsSpawn()) {
		dupe->SetTag(eObjectTag::Spawn);
	}

	if (object->IsTrigger()) {
		dupe->SetTag(eObjectTag::Trigger);
	}

	if (object->IsDecal()) {
		dupe->SetTag(eObjectTag::Decal);
	}

	gObjectScripts->Attach(dupe, gObjectScripts->GetPath(object->ID));

	Vec3f enter_direction;

	if (gObjectScripts->TryGetRequiredEnterDirection(object->ID, enter_direction)) {
		gObjectScripts->SetRequiredEnterDirection(dupe->ID, enter_direction);
	}

	const Patch* source_patch = GetPatch(object);

	if (source_patch != nullptr) {
		RebuildPatch(dupe, source_patch->Clone());
	}
	else {
		RebuildBrush(dupe, std::move(brush), motion_type);
	}

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

	const Vec4f local = object->GetWorldMatrix().AffineInverse() * Vec4f(point.X, point.Y, point.Z, 1.0f);

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

static String MakeRestoredName(const Name& name, uint32 object_count)
{
	const String base_name = name.Get().IsEmpty() ? String::Fmt("{}", object_count).Str() : name.Get();

	String restored_name = base_name;

	if (gObjectManager->FindObject(HashStr32(restored_name.CStr())) != nullptr) {
		int suffix = 0;
		do {
			restored_name = String::Fmt("{}_undo{}", base_name, suffix++).Str();
		} while (gObjectManager->FindObject(HashStr32(restored_name.CStr())) != nullptr);
	}

	return restored_name;
}

Object* Blockout::RestorePatch(const Vec3f position, const Patch& patch, MaterialID material, const Quat rotation,
							   const Name& name)
{
	if (!patch.IsValid()) {
		LogError("Cannot restore patch '{}', it is invalid", name.Get());
		return nullptr;
	}

	const String patch_name = MakeRestoredName(name, BlockoutObjects.Size);

	LogInfo("Restoring patch '{}'", patch_name);

	Object* object = gObjectManager->NewObject(
		patch_name, material.IsNull() ? mMaterials.GetMaterial(mEditableMaterialID) : material, eObjectTag::Blockout);

	object->MoveBy(position);
	object->SetShadowCaster(true);
	object->SetRotation(rotation);
	object->SubviewInstance = mActiveSubview;

	RebuildPatch(object, patch.Clone());

	AssetTicket ticket(static_cast<void*>(object));
	ticket.MarkAndSignalLoaded();

	pWorld->Attach(ticket);

	BlockoutObjects.NewItem(nullptr, object->ID);

	return object;
}

Object* Blockout::RestoreObject(const Vec3f position, const Brush::PlaneList& planes, MaterialID material,
								const Quat rotation, const Name& name, bool is_dynamic, bool is_decal)
{
	Brush brush = Brush::FromPlanes(planes);

	if (!brush.IsValid()) {
		LogError("Cannot restore blockout '{}', its brush is invalid", name.Get());
		return nullptr;
	}

	const String blockout_name = MakeRestoredName(name, BlockoutObjects.Size);

	LogInfo("Restoring blockout object '{}'", blockout_name);

	Object* object = gObjectManager->NewObject(
		blockout_name, material.IsNull() ? mMaterials.GetMaterial(mEditableMaterialID) : material,
		eObjectTag::Blockout);

	object->MoveBy(position);
	object->SetShadowCaster(true);
	object->SetRotation(rotation);
	object->SubviewInstance = mActiveSubview;

	if (is_decal) {
		object->SetTag(eObjectTag::Decal);
	}

	RebuildBrush(object, std::move(brush), is_dynamic ? physics::eMotionType::Dynamic : physics::eMotionType::Static);

	AssetTicket ticket(static_cast<void*>(object));
	ticket.MarkAndSignalLoaded();

	pWorld->Attach(ticket);

	BlockoutObjects.NewItem(nullptr, object->ID);

	return object;
}

void Blockout::AddOrUpdateLightFromEntry(const ConfigEntry& light_entry)
{
	const LightEntry data = ReadLightEntry(light_entry);

	eLightType light_type;

	switch (data.Type) {
	case ePrxLightType::Point:
		light_type = eLightType::Point;
		break;
	case ePrxLightType::Spot:
		light_type = eLightType::Spot;
		break;
	default:
		LogError(LC_ASSET, "Light '{}' has unknown type {}", light_entry.Name.Get(), static_cast<int64>(data.Type));
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

	light->SetPosition(data.Position);
	light->Color = data.Color;
	light->Intensity = data.Intensity;
	light->SetRadius(data.Radius);

	if (light_type == eLightType::Spot) {
		Ref<LightSpot> spot(light);

		if (!data.Spot.Direction.IsZero()) {
			spot->SetDirection(data.Spot.Direction);
		}
		else {
			spot->SetRotation(light_entry.GetMemberValue(HashStr32("rot"), spot->mRotation));
		}

		spot->SetConeAngles(MathUtil::DegreesToRadians(data.Spot.InnerAngle),
							MathUtil::DegreesToRadians(data.Spot.OuterAngle));

		spot->bCastShadows = data.Spot.bCastShadows;
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

	SunLightEntry sun_data;

	if (sun_entry != nullptr) {
		sun_data = ReadSunLightEntry(*sun_entry);
	}

	const bool sun_enabled = (sun_entry != nullptr) && sun_data.bEnabled;

	sun->bEnabled = sun_enabled;
	gCVars->Set("b_sun_enabled", sun_enabled);

	if (sun_entry) {
		sun->SetPosition(sun_data.Position);
		sun->Color = sun_data.Color;
		sun->Intensity = sun_data.Intensity;
	}

	// Load point and spot lights

	ConfigEntry* light_list = info.GetEntry(HashStr32("lights"));
	if (light_list) {
		for (const ConfigEntry& light_entry : light_list->Members) {
			AddOrUpdateLightFromEntry(light_entry);
		}
	}

	if (renderer::gShadowAtlas != nullptr) {
		renderer::gShadowAtlas->Invalidate();
	}
}

void Blockout::SaveLights(ConfigFile& info)
{
	Ref<LightDirectional> sun = gLightManager->GetDirectionalLight();

	if (sun.IsValid()) {
		const SunLightEntry sun_data {
			.bEnabled = sun->bEnabled,
			.Position = sun->GetPosition(),
			.Color = sun->Color,
			.Intensity = sun->Intensity,
		};

		ConfigEntry sun_entry = ConfigEntry::Struct("sun");
		WriteSunLightEntry(sun_entry, sun_data);

		info.AddEntry(std::move(sun_entry));
	}

	// Built as a Struct up front: AddEntry("lights") only becomes a Struct once AddMember runs, so with zero
	// point/spot lights it would otherwise serialize as a bare, unparsable `lights = ` line.
	ConfigEntry lights_container = ConfigEntry::Struct("lights");

	for (const Ref<LightBase>& light : gLightManager->GetCache()) {
		if (light->Type != eLightType::Point && light->Type != eLightType::Spot) {
			continue;
		}

		LightEntry data {
			.Type = ePrxLightType::Point,
			.Position = light->GetPosition(),
			.Color = light->Color,
			.Intensity = light->Intensity,
			.Radius = light->GetRadius(),
		};

		if (light->Type == eLightType::Spot) {
			Ref<LightSpot> spot(light);

			data.Type = ePrxLightType::Spot;
			data.Spot = SpotLightEntry {
				.Direction = spot->GetDirection(),
				.InnerAngle = MathUtil::RadiansToDegrees(spot->GetInnerAngle()),
				.OuterAngle = MathUtil::RadiansToDegrees(spot->GetOuterAngle()),
				.bCastShadows = spot->bCastShadows,
			};
		}

		ConfigEntry light_entry = ConfigEntry::Struct(light->Name.Get());
		WriteLightEntry(light_entry, data);

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

	Serializable<World::PlayerSpawnPoint>::LoadFromConfig(*spawn, gWorld->PlayerSpawn);

	gWorld->PlayerSpawn.Direction = gWorld->PlayerSpawn.Direction.Normalize();
	gWorld->PlayerSpawn.bCustom = true;
}

void Blockout::SavePlayerSpawn(ConfigFile& info)
{
	if (!gWorld->PlayerSpawn.bCustom) {
		return;
	}

	ConfigEntry spawn = ConfigEntry::Struct("player_spawn");
	Serializable<World::PlayerSpawnPoint>::SaveToConfig(gWorld->PlayerSpawn, spawn);

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

static void DestroyObjectTree(Object& object)
{
	std::vector<ObjectID> nodes;
	CollectModelNodes(object, nodes);

	object.Destroy();

	// The object manager is locked while it destroys an object, so no object can be left to look up its nodes there
	for (ObjectID node_id : nodes) {
		gWorld->Detach(node_id);
		gObjectManager->GetObject(node_id)->AttachedNodes.Clear();
	}

	for (ObjectID& node_id : nodes) {
		gObjectManager->DestroyObject(node_id);
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

	DestroyObjectTree(*object);

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

void Blockout::LoadFog(ConfigFile& info)
{
	renderer::FogSettings fog;

	const ConfigEntry* fog_entry = info.GetEntry(HashStr32("fog"));

	if (fog_entry != nullptr) {
		fog.bEnabled = true;
		Serializable<renderer::FogSettings>::LoadFromConfig(*fog_entry, fog);

		fog.Albedo = Color::FromRGBA(fog.Albedo.R, fog.Albedo.G, fog.Albedo.B, 255);
	}

	gWorld->Fog = fog;
}

void Blockout::SaveFog(ConfigFile& info)
{
	const renderer::FogSettings& fog = gWorld->Fog;

	if (fog == renderer::FogSettings {}) {
		return;
	}

	ConfigEntry fog_entry = ConfigEntry::Struct("fog");
	Serializable<renderer::FogSettings>::SaveToConfig(fog, fog_entry);

	info.AddEntry(std::move(fog_entry));
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

ConfigEntry Blockout::MakeBrushEntry(Object* object, const String& name, const BrushPlacement& placement)
{
	ConfigEntry blockout_entry = ConfigEntry::Struct(name);
	{
		WriteBrushPlacement(blockout_entry, placement);
		WriteBrushEntry(blockout_entry, object);

		if (object->HasTags(eObjectTag::LockTransform)) {
			blockout_entry.AddMember(ConfigEntry::Literal("lock", 1));
		}

		if (object->IsProbeVolume()) {
			blockout_entry.AddMember(ConfigEntry::Literal("probevolume", 1));
		}

		if (object->IsReflectionProbe()) {
			blockout_entry.AddMember(ConfigEntry::Literal("reflectionprobe", 1));
		}


		if (object->IsSpawn()) {
			blockout_entry.AddMember(ConfigEntry::Literal("spawn", 1));
		}

		if (!object->IsShadowCaster() && !object->IsVolume() && !object->IsDecal()) {
			blockout_entry.AddMember(ConfigEntry::Literal("shadows", static_cast<int64>(0)));
		}

		if (!object->IsCullable()) {
			blockout_entry.AddMember(ConfigEntry::Literal("nocull", 1));
		}

		if (object->IsTrigger()) {
			blockout_entry.AddMember(ConfigEntry::Literal("trigger", 1));
			WriteTriggerEntry(blockout_entry, object);
		}

		if (object->IsDecal()) {
			blockout_entry.AddMember(ConfigEntry::Literal(scDecalName, 1));
		}

		WriteScriptEntry(blockout_entry, object);

		if (IsDynamic(object)) {
			blockout_entry.AddMember(ConfigEntry::Literal("dynamic", 1));
		}

		const MaterialLibraryID material_id = GetIDForMaterial(object->GetMaterialID());

		if (material_id.IsValid()) {
			blockout_entry.AddMember(ConfigEntry::Literal("mat", mMaterials.GetName(material_id).CStr()));
		}
	}

	return blockout_entry;
}

/////////////////////////////////////
// Subviews
/////////////////////////////////////

static Mat4f MakeBrushMatrix(const BrushPlacement& placement, const Vec3f center)
{
	return Mat4f::AsTranslation(-center) * Mat4f::AsRotation(placement.Rotation) * Mat4f::AsTranslation(center) *
		   Mat4f::AsTranslation(placement.Position);
}

static ConfigEntry MakeSubviewInstanceEntry(const String& name, const String& subview_name,
											const BrushPlacement& placement)
{
	ConfigEntry entry = ConfigEntry::Struct(name);
	entry.AddMember(ConfigEntry::Literal(scSubviewMemberName, subview_name.CStr()));
	WriteBrushPlacement(entry, placement);

	return entry;
}

static bool PlacesSubview(const ConfigEntry& instances, const String& subview_name)
{
	if (!instances.Members.IsInited()) {
		return false;
	}

	for (const ConfigEntry& instance : instances.Members) {
		const char* name = ReadInstanceSubviewName(instance);

		if (name != nullptr && subview_name == String(name)) {
			return true;
		}
	}

	return false;
}

template <typename TElement>
static void CopyToSizedArray(const std::vector<TElement>& source, SizedArray<TElement>& out)
{
	out.InitCapacity(source.size());

	for (const TElement& element : source) {
		out.Insert(element);
	}
}

Blockout::Subview* Blockout::FindSubview(const String& name)
{
	for (Subview& subview : mSubviews) {
		if (subview.Name == name) {
			return &subview;
		}
	}

	return nullptr;
}

const Blockout::Subview* Blockout::FindSubview(const String& name) const
{
	for (const Subview& subview : mSubviews) {
		if (subview.Name == name) {
			return &subview;
		}
	}

	return nullptr;
}

Blockout::Subview* Blockout::LoadSubview(const ConfigFile* info, const String& name)
{
	// If the subview is already loaded, return the currently loaded version
	for (Subview& subview : mSubviews) {
		if (subview.Name == name || (!subview.LegacyName.IsEmpty() && subview.LegacyName == name)) {
			return &subview;
		}
	}

	if (std::find(mLoadingSubviews.begin(), mLoadingSubviews.end(), name) != mLoadingSubviews.end()) {
		return nullptr;
	}

	const ConfigFile no_info {};
	const ConfigFile& source = (info != nullptr) ? *info : no_info;

	ConfigEntry brushes;
	ConfigEntry instances;

	if (!ReadSubviewFile(source, mPrototypePath, name, brushes, instances)) {
		return nullptr;
	}

	const ConfigEntry* inline_subviews = source.GetEntry(HashStr32(scSubviewsEntryName));
	const bool is_inline = inline_subviews != nullptr && inline_subviews->GetMember(HashStr32(name.CStr())) != nullptr;

	Subview subview;
	subview.Name = name;
	subview.bHasFile = true;

	if (is_inline) {
		subview.Name = MakeSubviewName(name);
		subview.LegacyName = name;
		subview.bHasFile = false;
		subview.bDirty = true;
	}

	subview.Brushes = brushes.Clone(subview.Name);
	subview.Instances = ConfigEntry::Struct(scSubviewInstancesEntryName);

	mLoadingSubviews.push_back(name);

	if (instances.Members.IsInited()) {
		for (const ConfigEntry& instance : instances.Members) {
			const String child_name = ReadInstanceSubviewName(instance);

			if (std::find(mLoadingSubviews.begin(), mLoadingSubviews.end(), child_name) != mLoadingSubviews.end()) {
				LogWarning(LC_ASSET, "The subview '{}' places '{}', which already holds it, so it is left out", name,
						   child_name);
				continue;
			}

			if (LoadSubview(nullptr, child_name) == nullptr) {
				LogWarning(LC_ASSET, "Subview instance '{}' in '{}' uses the subview '{}', which can't be read",
						   instance.Name.Get(), name, child_name);
			}

			subview.Instances.AddMember(instance.Clone(instance.Name.Get()));
		}
	}

	mLoadingSubviews.pop_back();

	BuildSubview(subview);

	mSubviews.Insert(std::move(subview));

	return mSubviews.GetLast();
}

Blockout::SubviewInstance* Blockout::FindSubviewInstance(uint32 instance)
{
	for (SubviewInstance& other : mSubviewInstances) {
		if (other.ID == instance) {
			return &other;
		}
	}

	return nullptr;
}

const Blockout::SubviewInstance* Blockout::FindSubviewInstance(uint32 instance) const
{
	for (const SubviewInstance& other : mSubviewInstances) {
		if (other.ID == instance) {
			return &other;
		}
	}

	return nullptr;
}

uint32 Blockout::AddSubviewInstance(const String& subview_name, const String& wanted_name)
{
	SubviewInstance instance;
	instance.ID = mNextSubviewInstance++;
	instance.SubviewName = subview_name;
	instance.Parent = mActiveSubview;

	const auto is_taken = [this](const String& name)
	{
		return std::any_of(mSubviewInstances.begin(), mSubviewInstances.end(),
						   [&](const SubviewInstance& other) { return other.Name == name; });
	};

	instance.Name = wanted_name;

	if (instance.Name.IsEmpty() || is_taken(instance.Name)) {
		const String display_name = GetSubviewDisplayName(subview_name);

		uint32 suffix = instance.ID;

		do {
			instance.Name = String::Fmt("{}_{}", display_name, suffix++);
		} while (is_taken(instance.Name));
	}

	mSubviewInstances.Insert(instance);

	return instance.ID;
}

uint32 Blockout::AddNestedSubviewInstance(const ConfigEntry& entry, const BrushPlacement& parent)
{
	const char* subview_name = ReadInstanceSubviewName(entry);

	if (subview_name == nullptr) {
		return scNoSubview;
	}

	const uint32 instance = AddSubviewInstance(subview_name, entry.Name.Get());
	const BrushPlacement placement = PlaceInSubview(ReadBrushPlacement(entry), Vec3f::sZero, parent);

	if (FindSubview(subview_name) == nullptr) {
		SubviewInstance* found = FindSubviewInstance(instance);
		found->Placement = placement;
		found->bMissing = true;

		return instance;
	}

	PlaceSubviewInstance(instance, placement);

	return instance;
}

bool Blockout::IsEnteredSubview(uint32 instance) const
{
	if (instance == scNoSubview) {
		return false;
	}

	if (instance == mActiveSubview) {
		return true;
	}

	return std::any_of(mOuterSubviews.begin(), mOuterSubviews.end(),
					   [&](const EnteredSubview& entered) { return entered.Instance == instance; });
}

bool Blockout::IsPlacedInActiveSubview(uint32 instance) const
{
	const SubviewInstance* found = FindSubviewInstance(instance);

	return found != nullptr && found->Parent == mActiveSubview;
}

bool Blockout::SubviewContains(const String& outer, const String& inner) const
{
	std::vector<String> pending { outer };
	std::vector<String> seen;

	while (!pending.empty()) {
		const String name = pending.back();
		pending.pop_back();

		if (std::find(seen.begin(), seen.end(), name) != seen.end()) {
			continue;
		}

		seen.push_back(name);

		const Subview* subview = FindSubview(name);

		if (subview == nullptr || !subview->Instances.Members.IsInited()) {
			continue;
		}

		for (const ConfigEntry& instance : subview->Instances.Members) {
			const char* child_name = ReadInstanceSubviewName(instance);

			if (child_name == nullptr) {
				continue;
			}

			if (inner == String(child_name)) {
				return true;
			}

			pending.push_back(child_name);
		}
	}

	return false;
}

bool Blockout::CanPlaceSubview(const String& subview_name) const
{
	std::vector<uint32> entered;

	if (mActiveSubview != scNoSubview) {
		entered.push_back(mActiveSubview);
	}

	for (const EnteredSubview& outer : mOuterSubviews) {
		entered.push_back(outer.Instance);
	}

	for (const uint32 instance : entered) {
		const SubviewInstance* found = FindSubviewInstance(instance);

		if (found == nullptr) {
			continue;
		}

		if (found->SubviewName == subview_name || SubviewContains(subview_name, found->SubviewName)) {
			return false;
		}
	}

	return true;
}

String Blockout::GetSubviewName(uint32 instance) const
{
	const SubviewInstance* found = FindSubviewInstance(instance);

	if (found == nullptr) {
		return "";
	}

	return found->SubviewName;
}

String Blockout::GetSubviewNames() const
{
	std::vector<std::string> names;

	for (const Subview& subview : mSubviews) {
		if (!subview.Members.empty()) {
			names.push_back(subview.Name.CStr());
		}
	}

	if (!mPrototypePath.IsEmpty()) {
		const std::filesystem::path folder = std::filesystem::path(
			FilesystemIO::ResolvePath(
				(std::filesystem::path(mPrototypePath.CStr()).parent_path() / scSubviewDirectoryName).generic_string())
				.CStr());

		std::error_code error;

		for (const std::filesystem::directory_entry& file : std::filesystem::directory_iterator(folder, error)) {
			if (file.path().extension() != CompiledMap::scPrototypeExtension) {
				continue;
			}

			const std::string name = (std::filesystem::path(scSubviewDirectoryName) / file.path().filename())
										 .generic_string();

			if (std::find(names.begin(), names.end(), name) == names.end()) {
				names.push_back(name);
			}
		}
	}

	std::sort(names.begin(), names.end());

	std::string joined;

	for (const std::string& name : names) {
		if (!joined.empty()) {
			joined += '\n';
		}

		joined += name;
	}

	return String(joined);
}

String Blockout::MakeSubviewName(const String& wanted) const
{
	std::string base;

	for (const char* c = wanted.CStr(); *c != '\0'; c++) {
		if (std::isalnum(static_cast<unsigned char>(*c)) || *c == '_') {
			base += *c;
		}
		else if (*c == ' ' || *c == '-') {
			base += '_';
		}
	}

	if (base.empty()) {
		base = "subview";
	}

	const auto make_name = [](const std::string& stem)
	{
		return String((std::filesystem::path(scSubviewDirectoryName) / (stem + CompiledMap::scPrototypeExtension))
						  .generic_string());
	};

	const auto is_taken = [this](const String& name)
	{
		if (std::any_of(mSubviews.begin(), mSubviews.end(),
						[&](const Subview& subview) { return subview.Name == name; })) {
			return true;
		}

		return !mPrototypePath.IsEmpty() && DoesFileExist(GetSubviewFilePath(mPrototypePath, name));
	};

	String name = make_name(base);

	for (uint32 suffix = 2; is_taken(name); suffix++) {
		name = make_name(base + "_" + std::to_string(suffix));
	}

	return name;
}

void Blockout::BuildSubview(Subview& subview)
{
	struct GroupBuilder
	{
		MaterialID Material = MaterialID::scNull;

		std::vector<Vec3f> Positions;
		std::vector<Vec3f> Normals;
		std::vector<Vec3f> Tangents;
		std::vector<float32> TangentHandedness;
		std::vector<Vec2f> Texcoords;
		std::vector<uint32> Indices;

		Vec3f Min = Vec3f(FLT_MAX);
		Vec3f Max = Vec3f(-FLT_MAX);
	};

	subview.Members.clear();
	subview.Groups.clear();

	subview.Flat = subview.Brushes.Clone(subview.Name);

	if (subview.Instances.Members.IsInited()) {
		for (const ConfigEntry& instance : subview.Instances.Members) {
			const char* child_name = ReadInstanceSubviewName(instance);

			if (child_name == nullptr) {
				continue;
			}

			Subview* child = FindSubview(child_name);

			if (child == nullptr || child == &subview) {
				continue;
			}

			AppendPlacedSubviewBrushes(subview.Flat, child->Flat, instance.Name.Get(), ReadBrushPlacement(instance));
		}
	}

	std::vector<GroupBuilder> builders;
	std::vector<SizedArray<Vec3f>> hulls;

	Vec3f min(FLT_MAX);
	Vec3f max(-FLT_MAX);


	if (subview.Flat.Members.IsInited()) {
		for (ConfigEntry& entry : subview.Flat.Members) {
			SubviewMember& member = subview.Members.emplace_back();
			member.LocalBrush = ReadBrushEntry(entry);

			if (!member.LocalBrush.IsValid()) {
				continue;
			}

			const Brush& brush = member.LocalBrush;

			member.ToSubview = MakeBrushMatrix(ReadBrushPlacement(entry), brush.GetCenter());
			member.bMerged = !IsEntityBrushEntry(entry) &&
							 !IsBrushMarkerMaterial(FindSpecialMaterial(ReadBrushMaterialName(entry)));

			if (!member.bMerged) {
				continue;
			}

			member.Material = ReadBrushMaterial(entry);

			StackArray<String, Brush::scMaxPlanes> face_materials;
			brush.GetFaceMaterials(face_materials);

			const String brush_faces {};

			const uint32 group_count = face_materials.Size + 1;

			for (uint32 group = 0; group < group_count; group++) {
				const String* faces = nullptr;
				MaterialID material = member.Material;

				if (group > 0) {
					faces = &face_materials[group - 1];
					material = GetFaceMaterial(*faces);
				}
				else if (face_materials.Size > 0) {
					faces = &brush_faces;
				}

				MeshGen::GeneratedMesh mesh;
				brush.GenerateMesh(mesh.Positions, mesh.Normals, mesh.Tangents, mesh.TangentHandedness, mesh.Texcoords,
								   mesh.Indices, faces);

				if (mesh.Positions.Size == 0) {
					continue;
				}

				auto builder = std::find_if(builders.begin(), builders.end(),
											[&](const GroupBuilder& other) { return other.Material == material; });

				if (builder == builders.end()) {
					builders.emplace_back().Material = material;
					builder = builders.end() - 1;
				}

				const uint32 first_vertex = static_cast<uint32>(builder->Positions.size());

				for (size_t i = 0; i < mesh.Positions.Size; i++) {
					const Vec3f position = TransformPoint(member.ToSubview, mesh.Positions[i]);

					builder->Positions.push_back(position);
					builder->Normals.push_back(TransformDirection(member.ToSubview, mesh.Normals[i]));
					builder->Tangents.push_back(TransformDirection(member.ToSubview, mesh.Tangents[i]));
					builder->Texcoords.push_back(mesh.Texcoords[i]);

					if (i < mesh.TangentHandedness.Size) {
						builder->TangentHandedness.push_back(mesh.TangentHandedness[i]);
					}
					else {
						builder->TangentHandedness.push_back(1.0f);
					}

					builder->Min = Vec3f::Min(builder->Min, position);
					builder->Max = Vec3f::Max(builder->Max, position);

					min = Vec3f::Min(min, position);
					max = Vec3f::Max(max, position);
				}

				for (const uint32 index : mesh.Indices) {
					builder->Indices.push_back(first_vertex + index);
				}
			}

			SizedArray<Vec3f>& hull = hulls.emplace_back();
			hull.InitCapacity(brush.GetVertices().Size);

			for (const Vec3f vertex : brush.GetVertices()) {
				hull.Insert(TransformPoint(member.ToSubview, vertex));
			}
		}
	}

	for (const GroupBuilder& builder : builders) {
		Ref<MeshGen::GeneratedMesh> mesh = MakeRef<MeshGen::GeneratedMesh>();

		CopyToSizedArray(builder.Positions, mesh->Positions);
		CopyToSizedArray(builder.Normals, mesh->Normals);
		CopyToSizedArray(builder.Tangents, mesh->Tangents);
		CopyToSizedArray(builder.TangentHandedness, mesh->TangentHandedness);
		CopyToSizedArray(builder.Texcoords, mesh->Texcoords);
		CopyToSizedArray(builder.Indices, mesh->Indices);

		SubviewGroup& group = subview.Groups.emplace_back();
		group.Material = builder.Material;
		group.pMesh = mesh->AsDefaultMesh();
		group.Bounds = BBox(builder.Min, builder.Max);
	}

	SizedArray<SizedArray<Vec3f>> subview_hulls;
	subview_hulls.InitCapacity(hulls.size());

	for (SizedArray<Vec3f>& hull : hulls) {
		subview_hulls.Insert(std::move(hull));
	}

	subview.Hulls = std::move(subview_hulls);

	if (builders.empty()) {
		subview.Bounds = BBox(Vec3f::sZero, Vec3f::sZero);
	}
	else {
		subview.Bounds = BBox(min, max);
	}
}

bool Blockout::IsSubviewObject(const Object* object) const
{
	if (object == nullptr || object->SubviewInstance == scNoSubview) {
		return false;
	}

	const SubviewInstance* found = FindSubviewInstance(object->SubviewInstance);

	return found != nullptr && found->RootID == object->ID;
}

Object* Blockout::GetSubviewObject(uint32 instance) const
{
	const SubviewInstance* found = FindSubviewInstance(instance);

	if (found == nullptr) {
		return nullptr;
	}

	return gObjectManager->GetObject(found->RootID);
}

uint32 Blockout::GetSubviewBrushCount(uint32 instance) const
{
	const SubviewInstance* found = FindSubviewInstance(instance);

	if (found == nullptr) {
		return 0;
	}

	for (const Subview& subview : mSubviews) {
		if (subview.Name == found->SubviewName) {
			return static_cast<uint32>(subview.Members.size());
		}
	}

	return 0;
}

void Blockout::GetSubviewMembers(uint32 instance, std::vector<Object*>& out_members) const
{
	out_members.clear();

	if (instance == scNoSubview) {
		return;
	}

	for (const ObjectID id : BlockoutObjects) {
		Object* object = gObjectManager->GetObject(id);

		if (object != nullptr && object->SubviewInstance == instance) {
			out_members.push_back(object);
		}
	}
}

void Blockout::GetSubviewSolids(std::vector<SubviewSolid>& out_solids)
{
	out_solids.clear();

	for (const SubviewInstance& instance : mSubviewInstances) {
		Object* root = gObjectManager->GetObject(instance.RootID);
		const Subview* subview = FindSubview(instance.SubviewName);

		if (root == nullptr || subview == nullptr) {
			continue;
		}

		const Mat4f world = root->GetWorldMatrix();

		for (const SubviewMember& member : subview->Members) {
			if (!member.bMerged) {
				continue;
			}

			const Material* material = gMaterialManager->GetMaterial(member.Material);

			out_solids.push_back(SubviewSolid {
				.pBrush = &member.LocalBrush,
				.World = member.ToSubview * world,
				.pOwner = root,
				.bTransparent = (material != nullptr && material->IsTransparent()),
			});
		}
	}
}

bool Blockout::GetSubviewPlacement(uint32 instance, BrushPlacement& out_placement)
{
	if (instance == scNoSubview) {
		return false;
	}

	if (instance == mActiveSubview) {
		out_placement = mActivePlacement;
		return true;
	}

	for (const EnteredSubview& outer : mOuterSubviews) {
		if (outer.Instance == instance) {
			out_placement = outer.Placement;
			return true;
		}
	}

	const SubviewInstance* found = FindSubviewInstance(instance);

	if (found == nullptr || !found->bPlaced) {
		return false;
	}

	const Object* root = gObjectManager->GetObject(found->RootID);

	if (root != nullptr) {
		out_placement = BrushPlacement { root->GetPosition(), root->mRotation };
	}
	else {
		out_placement = found->Placement;
	}

	return true;
}

bool Blockout::GetSubviewBounds(uint32 instance, BBox& out_bounds)
{
	Object* root = GetSubviewObject(instance);

	if (root != nullptr) {
		out_bounds = root->GetWorldOBB().GetWorldAABB();
		return true;
	}

	std::vector<Object*> members;
	GetSubviewMembers(instance, members);

	for (const SubviewInstance& child : mSubviewInstances) {
		if (child.Parent != instance) {
			continue;
		}

		Object* child_root = gObjectManager->GetObject(child.RootID);

		if (child_root != nullptr) {
			members.push_back(child_root);
		}
	}

	return GetBrushBounds(members.data(), static_cast<uint32>(members.size()), out_bounds);
}

bool Blockout::GetBrushBounds(Object* const* objects, uint32 count, BBox& out_bounds)
{
	Vec3f min(FLT_MAX);
	Vec3f max(-FLT_MAX);
	bool has_bounds = false;

	for (uint32 i = 0; i < count; i++) {
		Object* member = objects[i];

		if (IsSubviewObject(member)) {
			const BBox world_bounds = member->GetWorldOBB().GetWorldAABB();

			min = Vec3f::Min(min, world_bounds.Min);
			max = Vec3f::Max(max, world_bounds.Max);
			has_bounds = true;
			continue;
		}

		const Brush* brush = GetBrush(member);
		const Patch* patch = GetPatch(member);

		if (brush == nullptr && patch == nullptr) {
			continue;
		}

		const Mat4f world = member->GetWorldMatrix();

		if (patch != nullptr) {
			const OBBox box = OBBox::FromLocalBounds(member->Bounds, world);
			const BBox world_bounds = box.GetWorldAABB();

			min = Vec3f::Min(min, world_bounds.Min);
			max = Vec3f::Max(max, world_bounds.Max);
			has_bounds = true;
			continue;
		}

		for (const Vec3f vertex : brush->GetVertices()) {
			const Vec4f point = world * Vec4f(vertex.X, vertex.Y, vertex.Z, 1.0f);

			min = Vec3f(std::min(min.X, point.X), std::min(min.Y, point.Y), std::min(min.Z, point.Z));
			max = Vec3f(std::max(max.X, point.X), std::max(max.Y, point.Y), std::max(max.Z, point.Z));
			has_bounds = true;
		}
	}

	if (has_bounds) {
		out_bounds = BBox(min, max);
	}

	return has_bounds;
}

uint32 Blockout::CreateSubview(const String& name, Object* const* objects, uint32 count, const Vec3f origin)
{
	const String subview_name = MakeSubviewName(name);

	BrushPlacement placement;
	placement.Position = origin;

	ConfigEntry brushes = ConfigEntry::Struct(subview_name);
	ConfigEntry instances = ConfigEntry::Struct(scSubviewInstancesEntryName);

	for (uint32 i = 0; i < count; i++) {
		Object* object = objects[i];

		if (IsSubviewObject(object)) {
			const SubviewInstance* child = FindSubviewInstance(object->SubviewInstance);
			BrushPlacement world;

			if (child == nullptr || !GetSubviewPlacement(child->ID, world)) {
				continue;
			}

			instances.AddMember(MakeSubviewInstanceEntry(child->Name, child->SubviewName,
														 RemoveFromSubview(world, Vec3f::sZero, placement)));
			continue;
		}

		Vec3f center;

		if (!GetLocalCenter(object, center)) {
			LogError(LC_ASSET, "'{}' is not a brush, so it can't be part of a subview", object->Name.Get());
			return scNoSubview;
		}

		const BrushPlacement local = RemoveFromSubview(BrushPlacement { object->GetPosition(), object->mRotation },
													   center, placement);

		brushes.AddMember(MakeBrushEntry(object, String::Fmt("{}", i), local));
	}

	Subview subview;
	subview.Name = subview_name;
	subview.Brushes = std::move(brushes);
	subview.Instances = std::move(instances);
	subview.bDirty = true;

	BuildSubview(subview);

	mSubviews.Insert(std::move(subview));

	return NewSubviewInstance(subview_name);
}

uint32 Blockout::NewSubviewInstance(const String& subview_name)
{
	const Subview* subview = LoadSubview(nullptr, subview_name);

	if (subview == nullptr) {
		return scNoSubview;
	}

	if (!CanPlaceSubview(subview->Name)) {
		LogWarning(LC_ASSET, "The subview '{}' can't be placed here, it holds the subview being edited", subview->Name);
		return scNoSubview;
	}

	return AddSubviewInstance(subview->Name, "");
}

uint32 Blockout::CreateSubviewBrushes(const SubviewInstance& instance, const BrushPlacement& placement,
									  bool entities_only, bool own_only, const CompiledMap* compiled_map)
{
	Subview* subview = FindSubview(instance.SubviewName);

	if (subview == nullptr) {
		return 0;
	}

	ConfigEntry* brushes = &subview->Flat;

	if (own_only) {
		brushes = &subview->Brushes;
	}

	if (!brushes->Members.IsInited()) {
		return 0;
	}

	uint32 created_count = 0;
	uint32 member_index = 0;

	for (ConfigEntry& brush_entry : brushes->Members) {
		const uint32 index = member_index++;
		const String name = MakeSubviewBrushName(instance.Name, index);

		if (entities_only && index < subview->Members.size() && subview->Members[index].bMerged) {
			continue;
		}

		if (compiled_map != nullptr && compiled_map->HasBrush(name)) {
			continue;
		}

		ConfigEntry placed = PlaceSubviewBrushEntry(brush_entry, name, placement);

		if (!placed.Members.IsInited() || placed.Members.Size() == 0) {
			LogError(LC_ASSET, "Brush {} of the subview '{}' is not a valid convex brush, skipping", index,
					 subview->Name);
			continue;
		}

		Object* object = gObjectManager->GetObject(CreateBrush(placed));

		if (object == nullptr) {
			continue;
		}

		object->SubviewInstance = instance.ID;
		object->SubviewMember = index;

		created_count++;
	}

	return created_count;
}

Object* Blockout::CreateSubviewObject(SubviewInstance& instance, Subview& subview, const BrushPlacement& placement)
{
	Object* root = gObjectManager->NewObject(instance.Name, GetDefaultMaterial(), eObjectTag::None);

	if (root == nullptr) {
		return nullptr;
	}

	root->SubviewInstance = instance.ID;
	root->Bounds = subview.Bounds;
	root->SetShadowCaster(true);

	uint32 group_index = 0;

	for (const SubviewGroup& group : subview.Groups) {
		Object* node = gObjectManager->NewObject(String::Fmt("{}_g{}", instance.Name, group_index++), group.Material,
												 eObjectTag::None);

		if (node == nullptr) {
			continue;
		}

		node->pMesh = group.pMesh;
		node->Bounds = group.Bounds;
		node->SubviewInstance = instance.ID;
		node->SetShadowCaster(true);

		root->AttachObject(node->ID);
	}

	if (subview.Hulls.Size > 0) {
		physics::Body* body = gPhysics->NewBody(instance.Name);
		body->CreateCompoundHullBody(subview.Hulls, physics::eMotionType::Static,
									 physics::BodyProps {
										 .ConvexRadius = 0.05f,
										 .Density = 100,
									 });

		root->AttachCollider(body);
	}

	root->SetPosition(placement.Position);
	root->SetRotation(placement.Rotation);

	AssetTicket ticket(static_cast<void*>(root));
	ticket.MarkAndSignalLoaded();

	pWorld->Attach(ticket);

	instance.RootID = root->ID;

	return root;
}

Object* Blockout::PlaceSubviewInstance(uint32 instance, const BrushPlacement& placement)
{
	SubviewInstance* found = FindSubviewInstance(instance);

	if (found == nullptr) {
		return nullptr;
	}

	if (found->bPlaced) {
		RemoveSubviewInstance(instance);
	}

	Subview* subview = FindSubview(found->SubviewName);

	if (subview == nullptr || subview->Members.empty()) {
		return nullptr;
	}

	found->Placement = placement;
	found->bPlaced = true;

	CreateSubviewBrushes(*found, placement, true, false, nullptr);

	Object* root = CreateSubviewObject(*found, *subview, placement);

	renderer::gShadowAtlas->Invalidate();

	return root;
}

void Blockout::RemoveSubviewInstance(uint32 instance)
{
	SubviewInstance* found = FindSubviewInstance(instance);

	if (found == nullptr) {
		return;
	}

	Object* root = gObjectManager->GetObject(found->RootID);

	if (root != nullptr) {
		found->Placement = BrushPlacement { root->GetPosition(), root->mRotation };

		DestroyObjectTree(*root);
	}

	found->RootID = ObjectID::scNull;
	found->bPlaced = false;
	found->bMissing = false;

	std::vector<Object*> members;
	GetSubviewMembers(instance, members);

	for (Object* member : members) {
		DestroyObject(member);
	}

	renderer::gShadowAtlas->Invalidate();
}

uint32 Blockout::DissolveSubviewInstance(uint32 instance, std::vector<ObjectID>& out_brushes,
										 std::vector<uint32>& out_instances)
{
	out_brushes.clear();
	out_instances.clear();

	BrushPlacement placement;

	if (IsEnteredSubview(instance) || !GetSubviewPlacement(instance, placement)) {
		return 0;
	}

	const SubviewInstance* found = FindSubviewInstance(instance);
	const String instance_name = found->Name;
	const String subview_name = found->SubviewName;

	RemoveSubviewInstance(instance);

	Subview* subview = FindSubview(subview_name);

	if (subview == nullptr) {
		return 0;
	}

	if (subview->Brushes.Members.IsInited()) {
		uint32 member_index = 0;

		for (ConfigEntry& brush_entry : subview->Brushes.Members) {
			String name = MakeSubviewBrushName(instance_name, member_index++);

			if (gObjectManager->FindObject(HashStr32(name.CStr())) != nullptr) {
				name = String(MakeUniqueBrushName());
			}

			ConfigEntry placed = PlaceSubviewBrushEntry(brush_entry, name, placement);

			if (!placed.Members.IsInited() || placed.Members.Size() == 0) {
				continue;
			}

			const ObjectID brush_id = CreateBrush(placed);

			if (!brush_id.IsInvalid()) {
				out_brushes.push_back(brush_id);
			}
		}
	}

	if (subview->Instances.Members.IsInited()) {
		for (const ConfigEntry& entry : subview->Instances.Members) {
			const uint32 child = AddNestedSubviewInstance(entry, placement);

			if (child != scNoSubview) {
				out_instances.push_back(child);
			}
		}
	}

	return static_cast<uint32>(out_brushes.size() + out_instances.size());
}

void Blockout::PlaceSubviewEntities(const SubviewInstance& instance)
{
	Subview* subview = FindSubview(instance.SubviewName);

	if (subview == nullptr || !subview->Flat.Members.IsInited()) {
		return;
	}

	std::vector<Object*> members;
	GetSubviewMembers(instance.ID, members);

	for (Object* member : members) {
		Vec3f center;

		if (!GetLocalCenter(member, center) || member->SubviewMember >= subview->Flat.Members.Size()) {
			continue;
		}

		const BrushPlacement local = ReadBrushPlacement(subview->Flat.Members[member->SubviewMember]);
		const BrushPlacement world = PlaceInSubview(local, center, instance.Placement);

		member->SetPosition(world.Position);
		member->SetRotation(world.Rotation);
	}
}

void Blockout::SyncSubviewInstances()
{
	for (SubviewInstance& instance : mSubviewInstances) {
		const Object* root = gObjectManager->GetObject(instance.RootID);

		if (root == nullptr) {
			continue;
		}

		if (root->GetPosition().IsCloseTo(instance.Placement.Position) &&
			root->mRotation.IsCloseTo(instance.Placement.Rotation)) {
			continue;
		}

		instance.Placement = BrushPlacement { root->GetPosition(), root->mRotation };

		PlaceSubviewEntities(instance);
	}
}

bool Blockout::EnterSubview(uint32 instance)
{
	if (mbIsCompiledMap || IsEnteredSubview(instance)) {
		return false;
	}

	const SubviewInstance* entered = FindSubviewInstance(instance);

	if (entered == nullptr || entered->Parent != mActiveSubview || FindSubview(entered->SubviewName) == nullptr) {
		return false;
	}

	BrushPlacement placement;

	if (!GetSubviewPlacement(instance, placement)) {
		return false;
	}

	RemoveSubviewInstance(instance);

	SubviewInstance* found = FindSubviewInstance(instance);

	found->Placement = placement;
	found->bPlaced = true;

	if (mActiveSubview != scNoSubview) {
		mOuterSubviews.push_back(EnteredSubview { mActiveSubview, mActivePlacement });
	}

	mActiveSubview = instance;
	mActivePlacement = placement;

	CreateSubviewBrushes(*found, placement, false, true, nullptr);

	const Subview* subview = FindSubview(found->SubviewName);

	if (subview->Instances.Members.IsInited()) {
		for (const ConfigEntry& entry : subview->Instances.Members) {
			AddNestedSubviewInstance(entry, placement);
		}
	}

	return true;
}

void Blockout::LeaveSubview()
{
	if (!IsEditingSubview()) {
		return;
	}

	const uint32 instance = mActiveSubview;
	const BrushPlacement placement = mActivePlacement;

	CommitSubview(instance, placement);
	RemoveNestedSubviewInstances(instance);

	if (mOuterSubviews.empty()) {
		mActiveSubview = scNoSubview;
	}
	else {
		mActiveSubview = mOuterSubviews.back().Instance;
		mActivePlacement = mOuterSubviews.back().Placement;
		mOuterSubviews.pop_back();
	}

	RemoveSubviewInstance(instance);
	PlaceSubviewInstance(instance, placement);
}

void Blockout::CommitEnteredSubviews()
{
	if (!IsEditingSubview()) {
		return;
	}

	CommitSubview(mActiveSubview, mActivePlacement);

	for (auto outer = mOuterSubviews.rbegin(); outer != mOuterSubviews.rend(); ++outer) {
		CommitSubview(outer->Instance, outer->Placement);
	}
}

void Blockout::CommitSubview(uint32 instance, const BrushPlacement& placement)
{
	const SubviewInstance* entered = FindSubviewInstance(instance);

	if (entered == nullptr) {
		return;
	}

	Subview* subview = FindSubview(entered->SubviewName);

	if (subview == nullptr) {
		return;
	}

	std::vector<Object*> members;
	GetSubviewMembers(instance, members);

	ConfigEntry brushes = ConfigEntry::Struct(subview->Name);
	uint32 member_index = 0;

	for (Object* member : members) {
		Vec3f center;

		if (!GetLocalCenter(member, center)) {
			continue;
		}

		const BrushPlacement local = RemoveFromSubview(BrushPlacement { member->GetPosition(), member->mRotation },
													   center, placement);

		brushes.AddMember(MakeBrushEntry(member, String::Fmt("{}", member_index), local));

		member->SubviewMember = member_index++;
	}

	ConfigEntry instances = ConfigEntry::Struct(scSubviewInstancesEntryName);

	for (const SubviewInstance& child : mSubviewInstances) {
		if (child.Parent != instance) {
			continue;
		}

		BrushPlacement world = child.Placement;

		if (!child.bMissing && !GetSubviewPlacement(child.ID, world)) {
			continue;
		}

		instances.AddMember(
			MakeSubviewInstanceEntry(child.Name, child.SubviewName, RemoveFromSubview(world, Vec3f::sZero, placement)));
	}

	subview->Brushes = std::move(brushes);
	subview->Instances = std::move(instances);
	subview->bDirty = true;

	BuildSubview(*subview);

	RebuildSubviewUsers(subview->Name);
}

void Blockout::RebuildSubviewUsers(const String& subview_name)
{
	std::vector<String> changed { subview_name };
	std::vector<String> path;

	const auto rebuild_users = [&](const auto& self, const String& name) -> void
	{
		path.push_back(name);

		for (Subview& subview : mSubviews) {
			if (std::find(path.begin(), path.end(), subview.Name) != path.end() ||
				!PlacesSubview(subview.Instances, name)) {
				continue;
			}

			BuildSubview(subview);

			if (std::find(changed.begin(), changed.end(), subview.Name) == changed.end()) {
				changed.push_back(subview.Name);
			}

			self(self, subview.Name);
		}

		path.pop_back();
	};

	rebuild_users(rebuild_users, subview_name);

	std::vector<uint32> instances;

	for (const SubviewInstance& instance : mSubviewInstances) {
		if (instance.bPlaced && !IsEnteredSubview(instance.ID) &&
			std::find(changed.begin(), changed.end(), instance.SubviewName) != changed.end()) {
			instances.push_back(instance.ID);
		}
	}

	for (const uint32 instance : instances) {
		BrushPlacement placement;

		if (GetSubviewPlacement(instance, placement)) {
			PlaceSubviewInstance(instance, placement);
		}
	}
}

void Blockout::RemoveNestedSubviewInstances(uint32 parent)
{
	std::vector<uint32> nested;

	for (const SubviewInstance& instance : mSubviewInstances) {
		if (instance.Parent == parent) {
			nested.push_back(instance.ID);
		}
	}

	for (const uint32 instance : nested) {
		RemoveSubviewInstance(instance);
	}

	uint32 kept_count = 0;

	for (uint32 i = 0; i < mSubviewInstances.Size; i++) {
		if (mSubviewInstances[i].Parent == parent) {
			continue;
		}

		if (kept_count != i) {
			mSubviewInstances[kept_count] = std::move(mSubviewInstances[i]);
		}

		kept_count++;
	}

	while (mSubviewInstances.Size > kept_count) {
		mSubviewInstances.RemoveLast();
	}
}

void Blockout::LoadSubviews(ConfigFile& info, const CompiledMap* compiled_map)
{
	const ConfigEntry* instances = info.GetEntry(HashStr32(scSubviewInstancesEntryName));

	if (instances == nullptr) {
		return;
	}

	for (const ConfigEntry& entry : instances->Members) {
		const ConfigEntry* subview_name = entry.GetMember(HashStr32(scSubviewMemberName));

		if (subview_name == nullptr || subview_name->Type != ConfigEntry::ePrimitiveType::String) {
			LogWarning(LC_ASSET, "Subview instance '{}' does not name its subview", entry.Name.Get());
			continue;
		}

		const Subview* subview = LoadSubview(&info, subview_name->Get<const char*>());

		SubviewInstance instance;
		instance.ID = mNextSubviewInstance++;
		instance.Name = entry.Name.Get();
		instance.Placement = ReadBrushPlacement(entry);

		if (subview == nullptr) {
			LogWarning(LC_ASSET, "Subview instance '{}' uses the subview '{}', which can't be read", entry.Name.Get(),
					   subview_name->Get<const char*>());

			instance.SubviewName = subview_name->Get<const char*>();
			instance.bMissing = true;

			mSubviewInstances.Insert(instance);
			continue;
		}

		instance.SubviewName = subview->Name;

		mSubviewInstances.Insert(instance);

		if (compiled_map != nullptr) {
			CreateSubviewBrushes(instance, instance.Placement, false, false, compiled_map);

			SubviewInstance* inst = mSubviewInstances.GetLast();
			if (inst) {
				inst->bPlaced = true;
			}
		}
		else {
			PlaceSubviewInstance(instance.ID, instance.Placement);
		}
	}
}

void Blockout::RenameSubview(const String& previous_name, const String& name)
{
	for (SubviewInstance& instance : mSubviewInstances) {
		if (instance.SubviewName == previous_name) {
			instance.SubviewName = name;
		}
	}

	for (Subview& subview : mSubviews) {
		if (!subview.Instances.Members.IsInited()) {
			continue;
		}

		for (ConfigEntry& instance : subview.Instances.Members) {
			ConfigEntry* member = instance.GetMember(HashStr32(scSubviewMemberName));

			if (member == nullptr || member->Type != ConfigEntry::ePrimitiveType::String ||
				previous_name != String(member->Get<const char*>())) {
				continue;
			}

			member->Set(name);
			subview.bDirty = true;
		}
	}
}

bool Blockout::WriteSubviewFile(Subview& subview)
{
	if (mPrototypePath.IsEmpty()) {
		return false;
	}

	const String path = GetSubviewFilePath(mPrototypePath, subview.Name);

	std::error_code error;
	std::filesystem::create_directories(std::filesystem::path(path.CStr()).parent_path(), error);

	ConfigFile file {};

	if (DoesFileExist(path)) {
		file.Load(path.CStr());

		if (file.HasErrors()) {
			LogError(LC_ASSET, "The subview '{}' could not be saved, its file has errors", path);
			return false;
		}
	}

	const bool has_instances = subview.Instances.Members.IsInited() && subview.Instances.Members.Size() > 0;
	const bool has_brushes = (subview.Brushes.Members.IsInited() && subview.Brushes.Members.Size() > 0) ||
							 !has_instances;

	const auto make_instances = [&subview]
	{
		ConfigEntry instances = ConfigEntry::Struct(scSubviewInstancesEntryName);

		for (const ConfigEntry& instance : subview.Instances.Members) {
			ConfigEntry saved = instance.Clone(instance.Name.Get());
			ConfigEntry* member = saved.GetMember(HashStr32(scSubviewMemberName));

			if (member != nullptr && member->Type == ConfigEntry::ePrimitiveType::String) {
				const String reference = MakeNestedSubviewReference(subview.Name, member->Get<const char*>());
				member->Set(reference);
			}

			instances.AddMember(std::move(saved));
		}

		return instances;
	};

	std::vector<ConfigEntry> entries;

	if (file.GetEntries().IsInited()) {
		for (ConfigEntry& entry : file.GetEntries()) {
			entries.push_back(std::move(entry));
		}

		file.GetEntries().Clear();
	}

	bool wrote_brushes = false;
	bool wrote_instances = false;

	for (ConfigEntry& entry : entries) {
		const Hash32 entry_name = entry.Name.GetHash();

		if (entry_name == HashStr32("all")) {
			if (has_brushes) {
				file.AddEntry(subview.Brushes.Clone("all"));
			}

			wrote_brushes = true;
		}
		else if (entry_name == HashStr32(scSubviewInstancesEntryName)) {
			if (has_instances) {
				file.AddEntry(make_instances());
			}

			wrote_instances = true;
		}
		else {
			file.AddEntry(std::move(entry));
		}
	}

	if (!wrote_brushes && has_brushes) {
		file.AddEntry(subview.Brushes.Clone("all"));
	}

	if (!wrote_instances && has_instances) {
		file.AddEntry(make_instances());
	}

	if (!file.Write(path.CStr())) {
		LogError(LC_ASSET, "The subview could not be written to '{}'", path);
		return false;
	}

	subview.LegacyName = "";
	subview.bHasFile = true;
	subview.bDirty = false;

	return true;
}

void Blockout::SaveSubviews(ConfigFile& info)
{
	std::vector<String> reachable;

	for (const SubviewInstance& instance : mSubviewInstances) {
		if (instance.Parent == scNoSubview && instance.bPlaced &&
			std::find(reachable.begin(), reachable.end(), instance.SubviewName) == reachable.end()) {
			reachable.push_back(instance.SubviewName);
		}
	}

	for (size_t i = 0; i < reachable.size(); i++) {
		const Subview* subview = FindSubview(reachable[i]);

		if (subview == nullptr || !subview->Instances.Members.IsInited()) {
			continue;
		}

		for (const ConfigEntry& instance : subview->Instances.Members) {
			const char* child_name = ReadInstanceSubviewName(instance);

			if (child_name != nullptr &&
				std::find(reachable.begin(), reachable.end(), String(child_name)) == reachable.end()) {
				reachable.push_back(child_name);
			}
		}
	}

	const auto is_reachable = [&reachable](const String& name)
	{ return std::find(reachable.begin(), reachable.end(), name) != reachable.end(); };

	for (Subview& subview : mSubviews) {
		if (!subview.bDirty || subview.bHasFile || !is_reachable(subview.Name) ||
			!DoesFileExist(GetSubviewFilePath(mPrototypePath, subview.Name))) {
			continue;
		}

		const String previous_name = subview.Name;
		const String name = MakeSubviewName(GetSubviewDisplayName(previous_name));

		subview.Name = name;
		RenameSubview(previous_name, name);

		std::replace(reachable.begin(), reachable.end(), previous_name, name);
	}

	for (Subview& subview : mSubviews) {
		if (subview.bDirty && is_reachable(subview.Name)) {
			WriteSubviewFile(subview);
		}
	}

	ConfigEntry instances = ConfigEntry::Struct(scSubviewInstancesEntryName);
	uint32 instance_count = 0;

	for (const SubviewInstance& instance : mSubviewInstances) {
		if (instance.Parent != scNoSubview) {
			continue;
		}

		BrushPlacement placement = instance.Placement;

		if (!instance.bMissing && !GetSubviewPlacement(instance.ID, placement)) {
			continue;
		}

		instances.AddMember(MakeSubviewInstanceEntry(instance.Name, instance.SubviewName, placement));
		instance_count++;
	}

	if (instance_count > 0) {
		info.AddEntry(std::move(instances));
	}
}

void Blockout::ResetToTemplate()
{
	// No world to clear, ignore
	if (pWorld == nullptr) {
		return;
	}

#ifdef FX_IS_EDITOR
	gEditor->ForgetObjects();
#endif

	RemoveBlockoutFromWorld(pWorld);

	mPrototypePath = "";

	MaterialLibraryID lib_base_material = gWorld->pPrototype->mMaterials.FindIDByName("white_tile");
	MaterialID base_material = gWorld->pPrototype->mMaterials.GetMaterial(lib_base_material);

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

	gWorld->Fog = renderer::FogSettings {};

	gWorld->Player.TeleportTo(Vec3f(0.0, 2.0, 0.0));

#ifdef FX_IS_EDITOR
	gEditor->MarkSaved();
#endif
}

ePrototypeLoadStatus Blockout::Load(const String& opened_path)
{
	const bool wants_compiled_map = CompiledMap::IsCompiledMapPath(opened_path);
	const String path = CompiledMap::GetPrototypePath(opened_path);

	ConfigFile info {};
	info.Load(path.CStr());

	if (info.HasErrors()) {
		return ePrototypeLoadStatus::HasErrors;
	}

	ConfigEntry* blocks_entry = info.GetEntry(HashStr32("all"));

	if (blocks_entry == nullptr) {
		LogError("Blockout '{}' could not be loaded or has no 'all' entry", path);
		return ePrototypeLoadStatus::HasErrors;
	}

	const uint32 renamed_count = MakeBrushEntryNamesUnique(*blocks_entry);

	if (renamed_count > 0) {
		LogWarning(LC_ASSET,
				   "{} brushes in '{}' shared a name with another brush and were renamed, save the "
				   "prototype to keep the new names",
				   renamed_count, path);
	}

#ifdef FX_IS_EDITOR
	// Everything is getting reloaded, so the editor can't hold on to any objects
	gEditor->ForgetObjects();
#endif

	ePrototypeLoadStatus final_status = ePrototypeLoadStatus::Success;


	LoadLights(info);
	LoadCamera(info);
	LoadPlayerSpawn(info);
	LoadFog(info);

	// Remove the current blockout from the world
	RemoveBlockoutFromWorld(pWorld);

	mPrototypePath = path;

	CompiledMap compiled_map;
	bool did_load_compiled_map = false;
	if (wants_compiled_map) {
		did_load_compiled_map = LoadCompiledMap(path, info, compiled_map);

		// There was an error loading the compiled map, set the final status to be not compiled
		if (!did_load_compiled_map) {
			final_status = ePrototypeLoadStatus::NotCompiled;
		}
	}

	for (ConfigEntry& entry : blocks_entry->Members) {
		if (did_load_compiled_map && compiled_map.HasBrush(entry.Name.Get())) {
			continue;
		}

		CreateBrush(entry);
	}

	if (did_load_compiled_map) {
		LoadSubviews(info, &compiled_map);
	}
	else {
		LoadSubviews(info, nullptr);
	}


	if (did_load_compiled_map) {
		CreateCompiledWorld(compiled_map);
	}

	const ConfigEntry* models_entry = info.GetEntry(HashStr32("objects"));

	mbHasModelsEntry = (models_entry != nullptr);

	if (mbHasModelsEntry) {
		LoadModels(models_entry, "");
	}
	else {
		LoadLegacyModels();
	}

	RefreshDecals(true);

	renderer::gShadowAtlas->Invalidate();

	// We have overridden the previous prototype, mark it as saved as we are on the unmodified version of a new
	// prototype.
#ifdef FX_IS_EDITOR
	gEditor->MarkSaved();
#endif

	return final_status;
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

void Blockout::Save(const String& save_path)
{
	if (mbIsCompiledMap) {
		LogError(LC_ASSET, "Cannot save to compiled map. Open the prototype file to modify the map.");
		return;
	}

	const String path = CompiledMap::GetPrototypePath(save_path);

	const bool is_moved = std::filesystem::path(path.CStr()).parent_path() !=
						  std::filesystem::path(mPrototypePath.CStr()).parent_path();

	mPrototypePath = path;

	if (is_moved) {
		for (Subview& subview : mSubviews) {
			if (!DoesFileExist(GetSubviewFilePath(path, subview.Name))) {
				subview.bHasFile = false;
				subview.bDirty = true;
			}
		}
	}

	ConfigFile info {};

	SaveLights(info);
	SaveCamera(info);
	SavePlayerSpawn(info);
	SaveFog(info);
	SaveModels(info);

	CommitEnteredSubviews();

	ConfigEntry* all_entry = info.AddEntry("all");

	for (const ObjectID box_id : BlockoutObjects) {
		Object* object = gObjectManager->GetObject(box_id);
		if (object == nullptr || object->SubviewInstance != scNoSubview) {
			continue;
		}

		all_entry->AddMember(
			MakeBrushEntry(object, object->Name.Get(), BrushPlacement { object->mPosition, object->mRotation }));
	}

	SaveSubviews(info);

	if (!info.Write(path.CStr())) {
		LogError(LC_ASSET, "The prototype could not be written to '{}'", path);
		return;
	}

	StoreLastBlockoutPath(path, "Config/Main.conf");

#ifdef FX_IS_EDITOR
	gEditor->MarkSaved();
#endif
}


Blockout::~Blockout() {}


} // namespace fx
