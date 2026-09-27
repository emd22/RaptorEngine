#include "ScriptInterop.hpp"

#include <Blockout.hpp>
#include <CVar.hpp>
#include <Controls.hpp>
#include <Editor/EditOperation.hpp>
#include <Editor/EditorTool.hpp>
#include <Editor/RaptorEditor.hpp>
#include <Engine.hpp>
#include <Math/SIMDHelper.hpp>
#include <Object/ObjectID.hpp>
#include <Object/ObjectManager.hpp>
#include <Physics/JoltPhysicsBackend.hpp>
#include <Physics/PhysicsManager.hpp>
#include <Renderer/LightProbe.hpp>
#include <World.hpp>

namespace fx::script {

static Object* N_object_get(uint32 id)
{
	ObjectID obj_id(id);
	if (obj_id.IsInvalid()) {
		return nullptr;
	}

	return gObjectManager->GetObject(obj_id);
}

/////////////////////////////////////
// Edit operations
/////////////////////////////////////

using editor::EditOperation;
using editor::EditOperationValue;

static Vec3f N_editor_push_op_vec3(Object* obj, int op_type, FLOAT4 original, FLOAT4 updated, int32 group_size)
{
#ifdef FX_IS_EDITOR
	if (obj == nullptr) {
		return Vec3f::sZero;
	}

	const EditOperationValue result = gEditor->PushEditOperation(EditOperation {
		.Type = static_cast<EditOperation::eType>(op_type),
		.pObject = obj,
		.ValueA = EditOperationValue(Vec3f(original)),
		.ValueB = EditOperationValue(Vec3f(updated)),
		.GroupSize = group_size,
	});

	return result.Position;
#else
	return Vec3f::sZero;
#endif
}

static Object* N_editor_push_op_object(Object* obj, int op_type, Object* original, Object* updated, int32 group_size)
{
#ifdef FX_IS_EDITOR
	if (obj == nullptr) {
		return nullptr;
	}

	const EditOperationValue result = gEditor->PushEditOperation(EditOperation {
		.Type = static_cast<EditOperation::eType>(op_type),
		.pObject = obj,
		.ValueA = EditOperationValue(original),
		.ValueB = EditOperationValue(updated),
		.GroupSize = group_size,
	});

	return result.pObject;
#else
	return nullptr;
#endif
}

static Object* N_editor_op_create_object(FLOAT4 position, int32 group_size)
{
#ifdef FX_IS_EDITOR
	const EditOperationValue result = gEditor->PushEditOperation(EditOperation {
		.Type = EditOperation::eType::Create,
		.pObject = nullptr,
		.ValueA = EditOperationValue(Vec3f(position)),
		.ValueB = EditOperationValue(nullptr),
		.GroupSize = group_size,
	});

	return result.pObject;
#else
	return nullptr;
#endif
}

static Object* N_editor_op_dupe_object(Object* object_to_dupe, FLOAT4 position, int32 group_size)
{
#ifdef FX_IS_EDITOR
	if (object_to_dupe == nullptr) {
		return nullptr;
	}

	const EditOperationValue result = gEditor->PushEditOperation(EditOperation {
		.Type = EditOperation::eType::Dupe,
		.pObject = object_to_dupe,
		.ValueA = EditOperationValue(Vec3f(position)),
		.ValueB = EditOperationValue(nullptr),
		.GroupSize = group_size,
	});

	return result.pObject;
#else
	return nullptr;
#endif
}

static void N_editor_push_op_delete(Object* obj, int32 group_size)
{
#ifdef FX_IS_EDITOR
	if (gEditor->IsSimulationMode()) {
		return;
	}

	gEditor->DeleteObject(obj, group_size);
#endif
}


static void N_object_move_to(Object* obj, FLOAT4 position)
{
	if (obj == nullptr) {
		return;
	}


	obj->SetPosition(Vec3f(position));
}

static void N_object_move_by(Object* obj, FLOAT4 by)
{
	if (obj == nullptr) {
		return;
	}

	obj->MoveBy(Vec3f(by));
}


static FLOAT4 N_object_get_position(Object* obj)
{
	if (obj == nullptr) {
		return simd::LoadFloat4(0.0f);
	}

	return obj->GetPosition().mIntrin;
}

static void N_object_rotate_to(Object* obj, FLOAT4 euler_radians)
{
	if (obj == nullptr) {
		return;
	}

	obj->SetRotation(Quat::FromEulerAngles(Vec3f(euler_radians)));
}

static FLOAT4 N_object_get_rotation(Object* obj)
{
	if (obj == nullptr) {
		return simd::LoadFloat4(0.0f);
	}

	return obj->mRotation.GetEulerAngles().mIntrin;
}

static void N_print_float4(FLOAT4 v) { LogInfo(LC_SCRIPT, "{}", Vec4f(v)); }

static uint32 N_object_get_tags(Object* obj)
{
	if (obj == nullptr) {
		return 0;
	}

	return static_cast<uint32>(obj->Tags);
}

static void N_object_set_tags(Object* obj, uint32 tags)
{
	if (obj == nullptr) {
		return;
	}

	obj->SetTags(static_cast<eObjectTag>(tags));
}


static float32 N_object_direction_scale(Object* obj, FLOAT4 direction)
{
	if (obj == nullptr) {
		return 0.0f;
	}

	// A brush's reach along a face normal is exactly where that face is, which its bounds only give for boxes
	const Brush* brush = gWorld->pBlockout->GetBrush(obj);
	if (brush != nullptr) {
		return brush->GetSupport(Vec3f(direction).Normalize());
	}

	return obj->GetDirectionScale(Vec3f(direction));
}

static FLOAT4 N_object_local_to_world(Object* obj, FLOAT4 point)
{
	if (obj == nullptr) {
		return point;
	}

	const Vec3f p(point);
	const Vec4f world = obj->GetWorldMatrix() * Vec4f(p.X, p.Y, p.Z, 1.0f);

	return Vec3f(world.X, world.Y, world.Z).mIntrin;
}

static FLOAT4 N_object_local_dir_to_world(Object* obj, FLOAT4 direction)
{
	if (obj == nullptr) {
		return direction;
	}

	const Vec3f d(direction);
	const Vec4f world = obj->GetWorldMatrix() * Vec4f(d.X, d.Y, d.Z, 0.0f);
	const Vec3f world_dir(world.X, world.Y, world.Z);

	if (world_dir.IsCloseTo(simd::LoadFloat4(0.0f))) {
		return simd::LoadFloat4(0.0f);
	}

	return world_dir.Normalize().mIntrin;
}

static FLOAT4 N_object_ray_get_face(Object* obj)
{
	if (obj == nullptr) {
		return simd::LoadFloat4(0.0f);
	}

	if (obj->IsProbeVolume()) {
		Vec3f face;

		const float32 distance = obj->RaycastBounds(gWorld->Player.pCamera->Position,
													gWorld->Player.pCamera->GetForwardVector(), face);

		return (distance >= 0.0f) ? face.mIntrin : simd::LoadFloat4(0.0f);
	}

	if (gWorld->pBlockout->GetBrush(obj) != nullptr) {
		Vec3f face_normal;

		if (!gWorld->pBlockout->RaycastFace(obj, gWorld->Player.pCamera->Position,
											gWorld->Player.pCamera->GetForwardVector(), face_normal)) {
			return simd::LoadFloat4(0.0f);
		}

		return face_normal.mIntrin;
	}

	physics::Body* body = gPhysics->GetBody(obj->PhysicsID);
	if (body == nullptr) {
		return simd::LoadFloat4(0.0f);
	}

	return gPhysics->pBackend->RaycastGetFaceOfBox(body->GetBody(), gWorld->Player.pCamera->Position,
												   gWorld->Player.pCamera->GetForwardVector() * 4.0f);
}

static void N_object__select_object_internal(Object* obj, bool is_selected, bool append_selection)
{
#ifdef FX_IS_EDITOR
	if (gEditor->IsSimulationMode()) {
		return;
	}

	// Deselect object
	if (!is_selected || obj == nullptr) {
		gEditor->ClearSelection();
		return;
	}

	// Select an object
	gEditor->SelectObject(obj, append_selection);
#endif
}


static uint32 N_ctrl_mouse_state()
{
	uint32 result = 0;
	if (ControlManager::IsKeyDown(eKey::FX_MOUSE_LEFT)) {
		result |= (1 << 0);
	}
	if (ControlManager::IsKeyDown(eKey::FX_MOUSE_RIGHT)) {
		result |= (1 << 1);
	}

	return result;
}


static FLOAT4 N_camera_position() { return gWorld->GetCurrentCamera()->Position.mIntrin; }
static FLOAT4 N_player_get_position(void*) { return gWorld->Player.Position.mIntrin; }

static void N_player_set_speed_multiplier(void*, float mult) { gWorld->Player.SpeedMultiplier = mult; }
static bool N_player_is_flymode(void*) { return gWorld->Player.IsFlyMode(); }

static FLOAT4 N_player_ray_get_point(void*, float32 range)
{
	physics::RayResult rr = gPhysics->pBackend->Raycast(gWorld->Player.pCamera->Position,
														gWorld->Player.pCamera->Direction * range);

	if (!rr.bHit) {
		return simd::LoadFloat4(0.0f);
	}

	return rr.Point.mIntrin;
}

static FLOAT4 N_player_ray_get_normal(void*, float32 range)
{
	physics::RayResult rr = gPhysics->pBackend->Raycast(gWorld->Player.pCamera->Position,
														gWorld->Player.pCamera->Direction * range);

	if (!rr.bHit) {
		return simd::LoadFloat4(0.0f);
	}

	return rr.Normal.mIntrin;
}

/// Where the crosshair's ray meets the plane through `point` facing `normal`, or `point` if the ray never reaches it
static FLOAT4 N_camera_ray_to_plane(FLOAT4 point, FLOAT4 normal)
{
	const Vec3f origin = gWorld->Player.pCamera->Position;
	const Vec3f direction = gWorld->Player.pCamera->GetForwardVector();
	const Vec3f plane_normal(normal);

	const float32 facing = plane_normal.Dot(direction);
	if (std::abs(facing) < 1e-5f) {
		return point;
	}

	const float32 distance = plane_normal.Dot(Vec3f(point) - origin) / facing;
	if (distance < 0.0f) {
		return point;
	}

	return (origin + direction * distance).mIntrin;
}

static FLOAT4 N_float4_round(FLOAT4 value) { return simd::Round(value); }
static FLOAT4 N_float4_floor(FLOAT4 value) { return simd::Floor(value); }
static FLOAT4 N_float3_abs(FLOAT4 value)
{
#ifdef FX_USE_NEON
	return Neon::SetSigns<1>(value);
#elif FX_USE_AVX
	return SSE::SetSigns<1>(value);
#endif
}


static bool N_is_key_up(uint32 key) { return ControlManager::IsKeyUp(static_cast<eKey>(key)); }
static bool N_is_key_down(uint32 key) { return ControlManager::IsKeyDown(static_cast<eKey>(key)); }
static bool N_is_key_pressed(uint32 key) { return ControlManager::IsKeyPressed(static_cast<eKey>(key)); }

static float N_float_sign(float value) { return MathUtil::GetSign(value); }

static void N_blockout_reload_object(Object* object) { gWorld->pBlockout->RebuildObject(object); }

static void N_blockout_object_scale(Object* object, FLOAT4 face_dir, FLOAT4 magnitude)
{
	gWorld->pBlockout->MoveFace(object, Vec3f(face_dir), Vec3f(magnitude).X);
}

/// The centre of the face facing along `face`, in the object's local space
static FLOAT4 N_blockout_face_center(Object* object, FLOAT4 face)
{
	const Brush* brush = gWorld->pBlockout->GetBrush(object);
	if (brush == nullptr) {
		return simd::LoadFloat4(0.0f);
	}

	const int32 plane_index = brush->FindPlane(Vec3f(face));
	if (plane_index == Brush::scNoPlane) {
		return simd::LoadFloat4(0.0f);
	}

	return brush->GetFaceCenter(static_cast<uint32>(plane_index)).mIntrin;
}

/// Where the crosshair's ray hits a blockout, in world space
static FLOAT4 N_blockout_ray_hit_point(Object* object)
{
	Vec3f face_normal;
	Vec3f point;

	if (object == nullptr ||
		!gWorld->pBlockout->RaycastFace(object, gWorld->Player.pCamera->Position,
										gWorld->Player.pCamera->GetForwardVector(), face_normal, &point)) {
		return simd::LoadFloat4(0.0f);
	}

	return point.mIntrin;
}

static void N_blockout_preview_box(FLOAT4 min, FLOAT4 max)
{
	Vec3f position;
	const Brush brush = gWorld->pBlockout->MakeWorldBox(Vec3f(min), Vec3f(max), position);

	gWorld->pBlockout->ShowPreview(position, Quat::scIdentity, brush);
}

/// Previews the piece that a clip would split off
static void N_blockout_preview_clip(Object* object, FLOAT4 point_a, FLOAT4 point_b, FLOAT4 face_normal)
{
	Brush::PlaneList kept;
	Brush::PlaneList split;
	Vec3f split_position;

	if (object == nullptr || !gWorld->pBlockout->GetClipPieces(object, Vec3f(point_a), Vec3f(point_b),
															   Vec3f(face_normal), kept, split, split_position)) {
		gWorld->pBlockout->HidePreview();
		return;
	}

	gWorld->pBlockout->ShowPreview(split_position, object->mRotation, Brush::FromPlanes(split));
}

static void N_blockout_hide_preview() { gWorld->pBlockout->HidePreview(); }

static Object* N_blockout_create_box(FLOAT4 min, FLOAT4 max)
{
#ifdef FX_IS_EDITOR
	if (gEditor->IsSimulationMode()) {
		return nullptr;
	}

	Vec3f position;
	const Brush brush = gWorld->pBlockout->MakeWorldBox(Vec3f(min), Vec3f(max), position);

	if (!brush.IsValid()) {
		return nullptr;
	}

	EditOperation op {
		.Type = EditOperation::eType::CreateBrush,
		.ValueA = EditOperationValue(Vec3f::sZero),
		.ValueB = EditOperationValue(Vec3f::sZero),
	};

	op.PlanesAfter = brush.Planes;
	op.ObjectSnapshot.Position = position;
	op.ObjectSnapshot.Material = gWorld->pBlockout->GetMaterialForSlot(eCProtoMat::Gray);

	return gEditor->PushEditOperation(op).pObject;
#else
	return nullptr;
#endif
}

/// Splits a blockout in two along a line drawn on one of its faces. Both pieces are kept.
static bool N_blockout_clip(Object* object, FLOAT4 point_a, FLOAT4 point_b, FLOAT4 face_normal)
{
#ifdef FX_IS_EDITOR
	if (object == nullptr || gEditor->IsSimulationMode()) {
		return false;
	}

	const Brush* brush = gWorld->pBlockout->GetBrush(object);
	if (brush == nullptr) {
		return false;
	}

	EditOperation clip_op {
		.Type = EditOperation::eType::BrushEdit,
		.pObject = object,
		.ValueA = EditOperationValue(Vec3f::sZero),
		.ValueB = EditOperationValue(Vec3f::sZero),
		.GroupSize = 2,
	};

	clip_op.PushedObjectID = object->ID;
	clip_op.PlanesBefore = brush->Planes;

	EditOperation split_op {
		.Type = EditOperation::eType::CreateBrush,
		.ValueA = EditOperationValue(Vec3f::sZero),
		.ValueB = EditOperationValue(Vec3f::sZero),
		.GroupSize = 2,
	};

	if (!gWorld->pBlockout->GetClipPieces(object, Vec3f(point_a), Vec3f(point_b), Vec3f(face_normal),
										  clip_op.PlanesAfter, split_op.PlanesAfter,
										  split_op.ObjectSnapshot.Position)) {
		return false;
	}

	split_op.ObjectSnapshot.Rotation = object->mRotation;
	split_op.ObjectSnapshot.Material = gEditor->GetSelection().GetStoredMaterial(object);
	split_op.ObjectSnapshot.bIsProbeVolume = object->IsProbeVolume();

	gEditor->PushEditOperation(clip_op);
	gEditor->PushEditOperation(split_op);

	return true;
#else
	return false;
#endif
}

/// How far away a face can be painted with the Set Material tool
static constexpr float32 scPaintRange = 100.0f;

/**
 * @brief Paints the face under the crosshair (or its whole brush) with a prototype material slot, as an undoable
 * operation. A negative slot paints with the blockout's own material.
 */
static bool N_blockout_paint_material(int32 slot, bool whole_brush)
{
#ifdef FX_IS_EDITOR
	if (gEditor->IsSimulationMode() || slot >= static_cast<int32>(eCProtoMat::Count)) {
		return false;
	}

	Vec3f face_normal;
	Object* object = gWorld->pBlockout->RaycastBlockout(
		gWorld->Player.pCamera->Position, gWorld->Player.pCamera->GetForwardVector() * scPaintRange, face_normal);

	if (object == nullptr) {
		return false;
	}

	const MaterialID material = (slot < 0) ? MaterialID::scNull
										   : gWorld->pBlockout->GetMaterialForSlot(static_cast<eCProtoMat>(slot));

	EditOperation op {
		.Type = EditOperation::eType::BrushEdit,
		.pObject = object,
		.ValueA = EditOperationValue(Vec3f::sZero),
		.ValueB = EditOperationValue(Vec3f::sZero),
	};

	op.PushedObjectID = object->ID;
	op.PlanesBefore = gWorld->pBlockout->GetBrush(object)->Planes;

	if (!gWorld->pBlockout->GetMaterialEdit(object, face_normal, material, whole_brush, op.PlanesAfter)) {
		return false;
	}

	gEditor->PushEditOperation(op);

	return true;
#else
	return false;
#endif
}

static void N_blockout_edit_face_texture(Object* object, FLOAT4 face, uint32 edit, FLOAT4 amount)
{
#ifdef FX_IS_EDITOR
	if (object == nullptr || gEditor->IsSimulationMode()) {
		return;
	}

	const Brush* brush = gWorld->pBlockout->GetBrush(object);
	if (brush == nullptr) {
		return;
	}

	const Vec3f amount_vec(amount);

	EditOperation op {
		.Type = EditOperation::eType::BrushEdit,
		.pObject = object,
		.ValueA = EditOperationValue(Vec3f::sZero),
		.ValueB = EditOperationValue(Vec3f::sZero),
	};

	op.PushedObjectID = object->ID;
	op.PlanesBefore = brush->Planes;

	if (!gWorld->pBlockout->GetFaceTextureEdit(object, Vec3f(face), static_cast<eFaceTextureEdit>(edit),
											   Vec2f(amount_vec.X, amount_vec.Y), op.PlanesAfter)) {
		return;
	}

	gEditor->PushEditOperation(op);
#endif
}

static Object* N_blockout_new_object(FLOAT4 position) { return gWorld->pBlockout->NewObject(Vec3f(position)); }
static Object* N_blockout_dupe_object(Object* object) { return gWorld->pBlockout->DupeObject(object); }
static void N_blockout_destroy_object(Object* object) { gWorld->pBlockout->DestroyObject(object); }


/////////////////////////////////////
// Light probes
/////////////////////////////////////

static void N_probe_volume_mark(Object* obj, bool enabled)
{
	if (obj == nullptr) {
		return;
	}

	obj->SetProbeVolume(enabled);
}

static bool N_probe_volume_is_marked(Object* obj) { return obj != nullptr && obj->IsProbeVolume(); }

static int32 N_probe_rebuild_volumes() { return static_cast<int32>(gProbeManager->RebuildVolumesFromWorld()); }

static int32 N_probe_volume_count() { return static_cast<int32>(gProbeManager->GetVolumeCount()); }
static int32 N_probe_count() { return static_cast<int32>(gProbeManager->GetProbeCount()); }
static bool N_probe_is_baking() { return gProbeManager->IsBaking(); }

static void N_probe_bake()
{
	gProbeManager->RebuildVolumesFromWorld();
	gProbeManager->BeginBake();
}

static bool N_probe_save() { return gProbeManager->SaveProbes(); }

static void N_cvar_set_int(const char* name, int64 value) { gCVars->Set(name, value); }
static void N_cvar_set_float(const char* name, float32 value) { gCVars->Set(name, value); }
static void N_cvar_set_string(const char* name, const char* value) { gCVars->Set(name, value); }

static int64 N_cvar_get_int(const char* name, int64 fallback) { return gCVars->Get(name, fallback); }

static void N_script_error(const char* str) { LogError(LC_SCRIPT, "{}", str); }

static void N_GUI_set_editor_tool(editor::eEditorTool tool)
{
#ifdef FX_IS_EDITOR
	if (tool >= editor::eEditorTool::Count) {
		return;
	}

	gEditor->SetTool(tool);
#endif
}

static void N_tool_state_send(const editor::EditorToolState* state)
{
#ifdef FX_IS_EDITOR
	if (state == nullptr) {
		return;
	}

	gEditor->SubmitToolState(*state);
#endif
}


/////////////////////////////////////
// Predef gather
/////////////////////////////////////


#define PREDEF(name_, fn_)                                                                                             \
	PredefExtern { name_, reinterpret_cast<void*>(fn_) }

static const PredefExtern scAvailableExterns[] = {
	PREDEF("printf", printf),

	/* Controls */
	PREDEF("ctrl_mouse_state", N_ctrl_mouse_state),

	/* Object functions  */
	PREDEF("object_get", N_object_get),

	PREDEF("editor_push_op_vec", N_editor_push_op_vec3),
	PREDEF("editor_push_op_obj", N_editor_push_op_object),
	PREDEF("editor_push_op_delete", N_editor_push_op_delete),
	PREDEF("editor_op_create_object", N_editor_op_create_object),
	PREDEF("editor_op_dupe_object", N_editor_op_dupe_object),

	PREDEF("OBJECT_move_to", N_object_move_to),
	PREDEF("OBJECT_move_by", N_object_move_by),
	PREDEF("OBJECT_get_position", N_object_get_position),
	PREDEF("OBJECT_rotate_to", N_object_rotate_to),
	PREDEF("OBJECT_get_rotation", N_object_get_rotation),
	PREDEF("OBJECT_get_tags", N_object_get_tags),
	PREDEF("OBJECT_set_tags", N_object_set_tags),
	PREDEF("OBJECT_ray_get_face", N_object_ray_get_face),
	PREDEF("OBJECT_local_to_world", N_object_local_to_world),
	PREDEF("OBJECT_local_dir_to_world", N_object_local_dir_to_world),
	PREDEF("OBJECT_direction_scale", N_object_direction_scale),
	PREDEF("OBJECT__select_object_internal", N_object__select_object_internal),

	PREDEF("blockout_reload_object", N_blockout_reload_object),
	PREDEF("blockout_object_scale", N_blockout_object_scale),
	PREDEF("blockout_face_center", N_blockout_face_center),
	PREDEF("blockout_edit_face_texture", N_blockout_edit_face_texture),
	PREDEF("blockout_ray_hit_point", N_blockout_ray_hit_point),
	PREDEF("blockout_preview_box", N_blockout_preview_box),
	PREDEF("blockout_preview_clip", N_blockout_preview_clip),
	PREDEF("blockout_hide_preview", N_blockout_hide_preview),
	PREDEF("blockout_create_box", N_blockout_create_box),
	PREDEF("blockout_clip", N_blockout_clip),
	PREDEF("blockout_paint_material", N_blockout_paint_material),
	PREDEF("camera_ray_to_plane", N_camera_ray_to_plane),
	PREDEF("blockout_new_object", N_blockout_new_object),
	PREDEF("blockout_dupe_object", N_blockout_dupe_object),
	PREDEF("blockout_destroy_object", N_blockout_destroy_object),

	PREDEF("camera_position", N_camera_position),

	PREDEF("PLAYER_get_position", N_player_get_position),
	PREDEF("PLAYER_set_speed_multiplier", N_player_set_speed_multiplier),
	PREDEF("PLAYER_is_flymode", N_player_is_flymode),
	PREDEF("PLAYER_ray_get_point", N_player_ray_get_point),
	PREDEF("PLAYER_ray_get_normal", N_player_ray_get_normal),

	/* Math Util */
	PREDEF("float4_round", N_float4_round),
	PREDEF("float4_floor", N_float4_floor),
	PREDEF("float3_abs", N_float3_abs),
	PREDEF("float_sign", N_float_sign),
	PREDEF("print_float4", N_print_float4),

	/* Controls */
	PREDEF("KEY_is_up", N_is_key_up),
	PREDEF("KEY_is_down", N_is_key_down),
	PREDEF("KEY_is_pressed", N_is_key_pressed),

	/* Light probes */
	PREDEF("probe_volume_mark", N_probe_volume_mark),
	PREDEF("probe_volume_is_marked", N_probe_volume_is_marked),
	PREDEF("probe_rebuild_volumes", N_probe_rebuild_volumes),
	PREDEF("probe_volume_count", N_probe_volume_count),
	PREDEF("probe_count", N_probe_count),
	PREDEF("probe_is_baking", N_probe_is_baking),
	PREDEF("probe_bake", N_probe_bake),
	PREDEF("probe_save", N_probe_save),

	PREDEF("cvar_set_int", N_cvar_set_int),
	PREDEF("cvar_set_float", N_cvar_set_float),
	PREDEF("cvar_set_string", N_cvar_set_string),

	PREDEF("cvar_get_int", N_cvar_get_int),

	PREDEF("script_error", N_script_error),

	PREDEF("GUI_set_editor_tool", N_GUI_set_editor_tool),
	PREDEF("tool_state_send", N_tool_state_send),

}; // namespace fx::script

Slice<const PredefExtern> GetInteropPredefs() { return Slice(scAvailableExterns, std::size(scAvailableExterns)); }


} // namespace fx::script
