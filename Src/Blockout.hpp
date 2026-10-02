/*
 * File:        Blockout.hpp
 * Author:      emd22
 * Created:     29/08/2026
 * Description: Blockout
 */

#pragma once

#include <Brush.hpp>
#include <Core/FreeArray.hpp>
#include <Core/Name.hpp>
#include <Core/StackArray.hpp>
#include <Core/String.hpp>
#include <Material/MaterialID.hpp>
#include <Material/MaterialLibrary.hpp>
#include <Math/Quat.hpp>
#include <Math/Vec3.hpp>
#include <Object/ObjectID.hpp>
#include <unordered_map>

namespace fx {
class World;
class ConfigFile;
class ConfigEntry;
class Object;

namespace physics {
enum class eMotionType;
}

/**
 * @brief A change to the texture on one face of a brush
 */
enum class eFaceTextureEdit : uint32
{
	/// Adds the amount's X and Y to the offset
	Shift = 0,
	/// Multiplies the scale by the amount's X and Y
	Scale,
	/// Adds the amount's X to the rotation, in degrees
	Rotate,
	/// Resets the offset, scale and rotation
	Reset,
};

class Blockout
{
public:
	Blockout();

	void Create(World* world);

	bool Load(const String& path);
	void Save(const String& path);

	/**
	 * @brief Moves the face facing along `face_normal` out by `distance` (in by a negative distance), keeping the rest
	 * of the brush where it is. The brush is kept at least scMinBlockoutThickness thick behind the face.
	 */
	bool MoveFace(Object* object, const Vec3f& face_normal, float32 distance);

	/**
	 * @brief Rebuilds a blockout from a set of planes, e.g. to undo an edit. Anything the planes have in common with
	 * the old brush stays where it is, even though the object may have to move for that.
	 */
	bool SetBrushPlanes(Object* object, const Brush::PlaneList& planes);

	/**
	 * @brief Works out how a blockout splits along the plane through `point_a` and `point_b` that is perpendicular to
	 * the face they were drawn on. Everything is in world space.
	 * @param out_kept The blockout's brush after the split
	 * @param out_split The piece that splits off, for a new blockout at `out_split_position`
	 */
	bool GetClipPieces(Object* object, const Vec3f& point_a, const Vec3f& point_b, const Vec3f& face_normal,
					   Brush::PlaneList& out_kept, Brush::PlaneList& out_split, Vec3f& out_split_position);

	/**
	 * @brief Returns the brush for a new blockout filling `min` to `max` in world space, with its textures lined up
	 * with the world grid
	 * @param out_position Where the blockout goes
	 */
	Brush MakeWorldBox(const Vec3f& min, const Vec3f& max, Vec3f& out_position) const;

	/**
	 * @brief Shows a see-through preview of a brush on an object with the given transform
	 */
	void ShowPreview(const Vec3f& position, const Quat& rotation, const Brush& brush);
	void HidePreview();

	/**
	 * @brief Returns the planes of the object's brush with the texture on one face changed, without applying them
	 */
	bool GetFaceTextureEdit(Object* object, const Vec3f& face_normal, eFaceTextureEdit edit, const Vec2f& amount,
							Brush::PlaneList& out_planes);

	/**
	 * @brief Finds the nearest blockout that a world space ray hits
	 * @param direction The direction of the ray, with the length of how far it reaches
	 * @param out_face_normal The normal of the face that was hit, in the blockout's local space
	 */
	Object* RaycastBlockout(const Vec3f& origin, const Vec3f& direction, Vec3f& out_face_normal);

	/**
	 * @brief Finds the face of a blockout that a world space ray hits
	 * @param out_face_normal The normal of the face that was hit, in the object's local space
	 * @param out_point Where the face was hit, in world space
	 */
	bool RaycastFace(Object* object, const Vec3f& origin, const Vec3f& direction, Vec3f& out_face_normal,
					 Vec3f* out_point = nullptr);

	void ReloadSingleObject(Object* object);

	void RebuildObject(Object* object);

	/**
	 * @brief Creates a new blockout object
	 */
	Object* NewObject(const Vec3f& position);

	Object* DupeObject(Object* object);

	/**
	 * @brief Recreates a destroyed blockout object from a snapshot (undo of Delete).
	 * Falls back to a unique name if the original name is taken.
	 */
	Object* RestoreObject(const Vec3f& position, const Brush::PlaneList& planes, MaterialID material,
						  const Quat& rotation, const Name& name, bool is_dynamic = false);

	bool IsDynamic(const Object* object) const;

	void DestroyObject(Object* object);

	const MaterialLibrary& GetMaterialLibrary() const { return mMaterials; }

	MaterialID GetMaterialForID(int32 id) const;
	int32 GetIDForMaterial(const MaterialID& material) const;
	MaterialID GetDefaultMaterial() const;

	/**
	 * @brief The brush that a blockout object is built from, or nullptr if the object is not a blockout.
	 */
	Brush* GetBrush(const Object* object);

	~Blockout();

private:
	ObjectID CreateBrushObject(ConfigEntry& entry);

	/**
	 * @brief Loads the sun and the point/spot lights from the blockout's `sun`/`lights` entries
	 */
	void LoadLights(ConfigFile& info);
	void SaveLights(ConfigFile& info);
	void LoadCamera(ConfigFile& info);
	void SaveCamera(ConfigFile& info);
	void AddOrUpdateLightFromEntry(const ConfigEntry& light_entry);

	/**
	 * @brief Reads a blockout's brush from either a box (`scale`) or a list of planes (`planes`, with optional face
	 * textures in `uvs`)
	 */
	Brush ReadBrushEntry(ConfigEntry& entry) const;
	void WriteBrushEntry(ConfigEntry& entry, const Object* object);

	/**
	 * @brief Builds the object's mesh, bounds and collider from the brush, and takes ownership of the brush.
	 */
	void ApplyBrush(Object* object, Brush&& brush, physics::eMotionType motion_type);

	/**
	 * @brief Applies a brush that replaces the object's current one, moving the object so the parts they share stay
	 * where they are
	 */
	void ApplyBrushInPlace(Object* object, Brush&& brush);

	void RemoveBlockoutFromWorld(World* world);
	void RemoveSingleObjectFromWorld(Object* object);

public:
	FreeArray<ObjectID> BlockoutObjects;
	World* pWorld = nullptr;

	MaterialID SelectionMaterialID = MaterialID::scNull;

	Object* pXFormObject = nullptr;

	/// Shows what the Create and Clip tools will make
	Object* pPreviewObject = nullptr;

private:
	MaterialLibrary mMaterials;
	std::unordered_map<uint32, Brush> mBrushes;

	Brush::PlaneList mPreviewPlanes;
};


} // namespace fx
