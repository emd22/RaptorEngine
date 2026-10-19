#pragma once

#include <Core/String.hpp>
#include <Core/Types.hpp>
#include <Math/Vec3.hpp>

#include <optional>
#include <string>
#include <vector>

namespace fx {

struct MaterialDefinition
{
	static constexpr const char* scExtension = ".mat";

	String Name;
	String SourcePath;

	String DiffusePath;
	String NormalPath;
	String OrmPath;

	float32 Alpha = 1.0f;
	bool bUnlit = false;
	bool bGlass = false;

	std::optional<float32> Metallic;
	std::optional<float32> Roughness;
	std::optional<Vec3f> BaseColor;

	bool IsTransparent() const { return bGlass || Alpha < 0.999f; }
};

std::vector<MaterialDefinition> LoadMaterialDefinitions(const std::string& root);

} // namespace fx
