#pragma once

#include <Core/Types.hpp>
#include <cstring>
#include <iterator>
#include <string>

namespace fx {

enum class eSpecialMaterial : uint32
{
	Null = 0,
	NonVis,
	NvBrush,
	DBound,
	Portal,

	Count,
	None = Count,
};

constexpr eSpecialMaterial scFirstNamedSpecialMaterial = eSpecialMaterial::NonVis;

constexpr const char* scSpecialMaterialNames[] = {
	"null", "nonvis", "nvbrush", "dbound", "portal",
};

static_assert(std::size(scSpecialMaterialNames) == static_cast<size_t>(eSpecialMaterial::Count));

constexpr const char* GetSpecialMaterialName(eSpecialMaterial material)
{
	if (material >= eSpecialMaterial::Count) {
		return "";
	}

	return scSpecialMaterialNames[static_cast<uint32>(material)];
}

inline eSpecialMaterial FindSpecialMaterial(const char* name)
{
	if (name == nullptr) {
		return eSpecialMaterial::None;
	}

	for (uint32 i = static_cast<uint32>(scFirstNamedSpecialMaterial); i < static_cast<uint32>(eSpecialMaterial::Count);
		 i++) {
		if (std::strcmp(name, scSpecialMaterialNames[i]) == 0) {
			return static_cast<eSpecialMaterial>(i);
		}
	}

	return eSpecialMaterial::None;
}

constexpr const char* scSystemMaterialPrefix = "SYS_";
constexpr size_t scSystemMaterialPrefixLength = std::char_traits<char>::length(scSystemMaterialPrefix);

inline bool IsSystemMaterialName(const char* name)
{
	return name != nullptr && std::strncmp(name, scSystemMaterialPrefix, scSystemMaterialPrefixLength) == 0;
}

inline eSpecialMaterial FindSystemMaterial(const char* name)
{
	if (!IsSystemMaterialName(name)) {
		return eSpecialMaterial::None;
	}

	return FindSpecialMaterial(name + scSystemMaterialPrefixLength);
}

constexpr bool IsBrushMarkerMaterial(eSpecialMaterial material)
{
	switch (material) {
	case eSpecialMaterial::NvBrush:
	case eSpecialMaterial::DBound:
	case eSpecialMaterial::Portal:
		return true;
	default:
		return false;
	}
}

} // namespace fx
