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
	void AddColliderFromEntry(const std::string& path, const ConfigEntry& collider);

public:
	// ConfigFile InfoFile;
};

} // namespace fx
