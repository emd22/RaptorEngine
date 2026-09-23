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
#include <Math/Quat.hpp>
#include <Math/Vec3.hpp>
#include <Object/ObjectID.hpp>
#include <unordered_map>

namespace fx {
class World;
class ConfigEntry;
class Object;

namespace physics {
enum class eMotionType;
}

enum class eCProtoMat
{
	Gray = 0,
	Orange,
	Blue,
	Tile,

	Count,
};

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
	/// Steps the face's material through the prototype materials, then back to the object's material
	CycleMaterial,
	/// Resets the offset, scale and rotation
	Reset,
};

class Blockout
{
public:
	Blockout();

	void Create(World* world);

	void Load(const String& path);
	void Save(const String& path);

	/**
	 * @brief Moves the face facing along `face_normal` out by `distance` (in by a negative distance), keeping the rest
	 * of the brush where it is. The brush is kept at least scMinBlockoutThickness thick behind the face.
	 */
	bool MoveFace(Object* object, const Vec3f& face_normal, float32 distance);

	/**
	 * @brief Rebuilds a blockout from a set of planes, e.g. to undo an edit
	 */
	bool SetBrushPlanes(Object* object, const Brush::PlaneList& planes);

	/**
	 * @brief Returns the planes of the object's brush with the texture on one face changed, without applying them
	 */
	bool GetFaceTextureEdit(Object* object, const Vec3f& face_normal, eFaceTextureEdit edit, const Vec2f& amount,
							Brush::PlaneList& out_planes);

	/**
	 * @brief Finds the face of a blockout that a world space ray hits
	 * @param out_face_normal The normal of the face that was hit, in the object's local space
	 */
	bool RaycastFace(Object* object, const Vec3f& origin, const Vec3f& direction, Vec3f& out_face_normal);

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
						  const Quat& rotation, const Name& name);

	void DestroyObject(Object* object);

	MaterialID GetMaterialForSlot(eCProtoMat slot) const;

	/**
	 * @brief Returns the prototype material slot a material is in, or -1 if it is not a prototype material
	 */
	int32 GetSlotForMaterial(const MaterialID& material) const;

	/**
	 * @brief The brush that a blockout object is built from, or nullptr if the object is not a blockout.
	 */
	Brush* GetBrush(const Object* object);

	~Blockout();

private:
	ObjectID CreateBrushObject(ConfigEntry& entry);

	/**
	 * @brief Reads a blockout's brush from either a box (`scale`) or a list of planes (`planes`, with optional face
	 * textures in `uvs` and `facemats`)
	 */
	Brush ReadBrushEntry(ConfigEntry& entry) const;
	void WriteBrushEntry(ConfigEntry& entry, const Object* object);

	/**
	 * @brief Builds the object's mesh, bounds and collider from the brush, and takes ownership of the brush.
	 */
	void ApplyBrush(Object* object, Brush&& brush, physics::eMotionType motion_type);

	void RemoveBlockoutFromWorld(World* world);
	void RemoveSingleObjectFromWorld(Object* object);

public:
	FreeArray<ObjectID> BlockoutObjects;
	World* pWorld = nullptr;

	MaterialID SelectionMaterialID = MaterialID::scNull;

	Object* pXFormObject = nullptr;

private:
	MaterialID mWhiteMaterialID = MaterialID::scNull;
	MaterialID mOrangeMaterialID = MaterialID::scNull;
	MaterialID mBlueMaterialID = MaterialID::scNull;
	MaterialID mProtoTileID = MaterialID::scNull;

	/// Keyed by ObjectID::GetID()
	std::unordered_map<uint32, Brush> mBrushes;
};


} // namespace fx
