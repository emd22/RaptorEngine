/*
 * File:        MeshSection.hpp
 * Author:      emd22
 * Created:     23/09/2026
 * Description: A range of a mesh's indices drawn with its own material
 */

#pragma once

#include <Core/Types.hpp>
#include <Material/MaterialID.hpp>

namespace fx {

/**
 * @brief A range of an object's mesh indices, drawn with its own material.
 */
struct MeshSection
{
	uint32 FirstIndex = 0;
	uint32 IndexCount = 0;

	/// Null draws the section with the object's material.
	MaterialID Material = MaterialID::scNull;
};

} // namespace fx
