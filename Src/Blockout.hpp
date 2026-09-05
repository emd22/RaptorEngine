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

	void ReloadSingleObject(Object* object);

	~Blockout();

private:
	void CreateCubeVolume(ConfigEntry& entry);

	void RemoveBlockoutFromWorld(World* world);

public:
	PagedArray<ObjectID> BlockoutObjects;
	World* pWorld = nullptr;

	MaterialID SelectionMaterialID = MaterialID::scNull;

	Object* pXFormObject = nullptr;

private:
	MaterialID mWhiteMaterialID = MaterialID::scNull;
	MaterialID mOrangeMaterialID = MaterialID::scNull;
};


} // namespace fx
