#pragma once

#include "ConfigFile.hpp"

#include <World.hpp>

namespace fx {

class WorldFile
{
public:
	WorldFile() = default;

	// void Save(const Scene& scene);

	void Load(const std::string& path, World& scene);
	// void Save(const String& path, const World& world);

private:
	void AddObjectFromEntry(const std::string& path, const ConfigEntry& object, World& scene);
	void AddColliderFromEntry(const std::string& path, const ConfigEntry& collider, World& scene);

	void ApplyPropertiesToObject(Object* object, const ConfigEntry& object_entry);

public:
	// ConfigFile InfoFile;
};

} // namespace fx
