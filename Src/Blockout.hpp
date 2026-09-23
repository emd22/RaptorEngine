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

class Blockout
{
public:
	Blockout();

	void Create(World* world);

	void Load(const String& path);
	void Save(const String& path);

	void ScaleInDirection(Object* object, const Vec3f& face_dir, const Vec3f& magnitude);
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
	 * @brief The brush that a blockout object is built from, or nullptr if the object is not a blockout.
	 */
	Brush* GetBrush(const Object* object);

	~Blockout();

private:
	ObjectID CreateBrushObject(ConfigEntry& entry);

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
