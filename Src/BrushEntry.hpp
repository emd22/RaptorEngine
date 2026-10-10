/*
 * File:        BrushEntry.hpp
 * Author:      emd22
 * Created:     10/10/2026
 * Description: Reads brushes from prototype file entries
 */

#pragma once

#include <Brush.hpp>

namespace fx {

class ConfigEntry;

enum class eBrushKind
{
	Geometry,
	/// Collides, never drawn
	NvBrush,
	/// Seals the map for the map compiler, and is otherwise not there
	DBound,
};

/// Members of a blockout entry that set its kind. Brushes saved before the kinds existed used a material of the same
/// name instead, which is still read.
constexpr const char* scNvBrushName = "nvbrush";
constexpr const char* scDBoundName = "dbound";

/// Material that map bounds are drawn with while the prototype is loaded. They are not in a compiled map at all.
constexpr const char* scDBoundMaterialName = "DNU_dbound";

eBrushKind ReadBrushKind(const ConfigEntry& entry);

/**
 * @brief Reads a blockout's brush from either a box (`scale`) or a list of planes (`planes`, with optional face
 * textures in `uvs`)
 */
Brush ReadBrushEntry(ConfigEntry& entry);

} // namespace fx
