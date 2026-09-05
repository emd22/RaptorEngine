/*
 * File:        Blockout.hpp
 * Author:      emd22
 * Created:     29/08/2026
 * Description: Blockout
 */

#pragma once

#include <Core/PagedArray.hpp>
#include <Core/String.hpp>
#include <Material/MaterialID.hpp>
#include <Object/ObjectID.hpp>

namespace fx {
class World;
class ConfigEntry;


struct BlockoutBox
{
	ObjectID ID;
};

class Blockout
{
public:
	Blockout();

	void Create(World* world);

	void Load(const String& path);

	~Blockout();

private:
	void CreateCubeVolume(ConfigEntry& entry);

public:
	PagedArray<BlockoutBox> BlockoutObjects;
	World* pWorld = nullptr;

	MaterialID SelectionMaterialID = MaterialID::scNull;

private:
	MaterialID mWhiteMaterialID = MaterialID::scNull;
	MaterialID mOrangeMaterialID = MaterialID::scNull;
};


} // namespace fx
