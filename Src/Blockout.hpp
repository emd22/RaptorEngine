/*
 * File:        Blockout.hpp
 * Author:      emd22
 * Created:     29/08/2026
 * Description: Blockout
 */

#pragma once

#include <Core/PagedArray.hpp>
#include <Core/StackArray.hpp>
#include <Core/String.hpp>
#include <Material/MaterialID.hpp>
#include <Math/Vec3.hpp>
#include <Object/ObjectID.hpp>

namespace fx {
class World;
class ConfigEntry;
class Object;


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

	~Blockout();

private:
	ObjectID CreateCubeVolume(ConfigEntry& entry);

	void RemoveBlockoutFromWorld(World* world);
	void RemoveSingleObjectFromWorld(Object* object);

public:
	PagedArray<ObjectID> BlockoutObjects;
	World* pWorld = nullptr;

	MaterialID SelectionMaterialID = MaterialID::scNull;

	Object* pXFormObject = nullptr;

private:
	MaterialID mWhiteMaterialID = MaterialID::scNull;
	MaterialID mOrangeMaterialID = MaterialID::scNull;
	MaterialID mBlueMaterialID = MaterialID::scNull;
};


} // namespace fx
