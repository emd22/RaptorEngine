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
	void AddObjectFromEntry(const std::string& path, const RxWorldObject& object);
	void AddColliderFromEntry(const std::string& path, const RxWorldCollider& collider);

	void ApplyPropertiesToObject(Object* object, const RxWorldObject& object_entry);

public:
	// ConfigFile InfoFile;
};

} // namespace fx
