#pragma once

#include "ConfigFile.hpp"

#include <World.hpp>

namespace fx {

class WorldFile
{
public:
	WorldFile() = default;

	// void Save(const Scene& scene);

	void Load(const std::string& path);
	// void Save(const String& path, const World& world);

private:
	void AddObjectFromEntry(const std::string& path, const ConfigEntry& object);
	void AddColliderFromEntry(const std::string& path, const ConfigEntry& collider);

	void ApplyPropertiesToObject(Object* object, const ConfigEntry& object_entry);

public:
	// ConfigFile InfoFile;
};

} // namespace fx
