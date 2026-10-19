#pragma once

#include <Core/Types.hpp>

namespace fx {

struct AreaSet
{
	static constexpr uint32 scMaxAreas = 4;
	static constexpr uint32 scMaxAreaIndex = UINT16_MAX;

	uint16 Areas[scMaxAreas] = {};
	uint8 Count = 0;
	bool bEverywhere = true;
	bool bFixed = false;

	static AreaSet MakeEmpty()
	{
		AreaSet set;
		set.bEverywhere = false;
		return set;
	}

	bool Contains(uint32 area) const
	{
		if (bEverywhere) {
			return true;
		}

		for (uint32 i = 0; i < Count; i++) {
			if (Areas[i] == area) {
				return true;
			}
		}

		return false;
	}

	void Add(uint32 area)
	{
		if (bEverywhere || Contains(area)) {
			return;
		}

		if (Count == scMaxAreas || area > scMaxAreaIndex) {
			bEverywhere = true;
			Count = 0;
			return;
		}

		Areas[Count++] = static_cast<uint16>(area);
	}
};

} // namespace fx
