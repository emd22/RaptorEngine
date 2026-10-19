#pragma once

#include <Core/Types.hpp>

#include <cstring>
#include <iterator>

namespace fx {

enum class eSpecialMaterial : uint32
{
	Null = 0,
	Caulk,
	NvBrush,
	DBound,

	Count,
	None = Count,
};

constexpr eSpecialMaterial scFirstNamedSpecialMaterial = eSpecialMaterial::Caulk;

constexpr const char* scSpecialMaterialNames[] = {
	"null",
	"caulk",
	"nvbrush",
	"dbound",
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

constexpr bool IsBrushMarkerMaterial(eSpecialMaterial material)
{
	switch (material) {
	case eSpecialMaterial::NvBrush:
	case eSpecialMaterial::DBound:
		return true;
	default:
		return false;
	}
}

} // namespace fx
