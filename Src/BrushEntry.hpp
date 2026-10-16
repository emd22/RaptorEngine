/*
 * File:        BrushEntry.hpp
 * Author:      emd22
 * Created:     10/10/2026
 * Description: Reads brushes from prototype file entries
 */

#pragma once

#include <Brush.hpp>
#include <Core/String.hpp>
#include <Math/Quat.hpp>
#include <Math/Vec3.hpp>
#include <Patch.hpp>
#include <functional>

namespace fx {

class ConfigEntry;
class ConfigFile;

enum class eBrushKind
{
	Geometry,
	/// Collides, never drawn
	NvBrush,
	/// Seals the map for the map compiler, and is otherwise not there
	DBound,
	Decal,
};

/// Members of a blockout entry that set its kind. Brushes saved before the kinds existed used a material of the same
/// name instead, which is still read.
constexpr const char* scNvBrushName = "nvbrush";
constexpr const char* scDBoundName = "dbound";
constexpr const char* scDecalName = "decal";

/// Material that map bounds are drawn with while the prototype is loaded. They are not in a compiled map at all.
constexpr const char* scDBoundMaterialName = "DNU_dbound";

eBrushKind ReadBrushKind(const ConfigEntry& entry);

/**
 * @brief Reads a blockout's brush from either a box (`scale`) or a list of planes (`planes`, with optional face
 * textures in `uvs`)
 */
Brush ReadBrushEntry(ConfigEntry& entry);

constexpr const char* scPatchName = "patch";

bool IsPatchEntry(const ConfigEntry& entry);
Patch ReadPatchEntry(ConfigEntry& entry);
void WritePatchEntry(ConfigEntry& entry, const Patch& patch);

/////////////////////////////////////
// Subviews
/////////////////////////////////////

/// Entry of a prototype file that says where each subview is placed in the level. `subviews` held the brushes of
/// the subviews before they had prototype files of their own, and is still read.
constexpr const char* scSubviewsEntryName = "subviews";
constexpr const char* scSubviewInstancesEntryName = "instances";
constexpr const char* scSubviewMemberName = "subview";

/**
 * @brief Where a brush or a subview instance is. A brush turns about the centre of its bounds, so its position is
 * only meaningful together with that centre.
 */
struct BrushPlacement
{
	Vec3f Position = Vec3f::sZero;
	Quat Rotation = Quat::scIdentity;
};

Quat ReadBrushRotation(const ConfigEntry& entry);

/// Takes a brush from its subview's space to the world, for a subview placed at `instance`
BrushPlacement PlaceInSubview(const BrushPlacement& local, const Vec3f brush_center, const BrushPlacement& instance);

/// Takes a brush from the world to the space of a subview placed at `instance`
BrushPlacement RemoveFromSubview(const BrushPlacement& world, const Vec3f brush_center, const BrushPlacement& instance);

/// Works out where a subview is placed from one of its brushes
BrushPlacement FindSubviewPlacement(const BrushPlacement& world, const BrushPlacement& local, const Vec3f brush_center);

String MakeSubviewBrushName(const String& instance_name, uint32 member_index);

/**
 * @brief Renames the brushes of a prototype's `all` entry that share a name with an earlier one, as the engine and
 * the map compiler both tell brushes apart by name. Returns the number of brushes that were renamed.
 */
uint32 MakeBrushEntryNamesUnique(ConfigEntry& blocks_entry);

/**
 * @brief Copies a subview's brush entry into the world for one instance of the subview. Returns an entry with no
 * members if the brush is not valid.
 */
ConfigEntry PlaceSubviewBrushEntry(ConfigEntry& brush_entry, const String& name, const BrushPlacement& instance);

constexpr const char* scSubviewDirectoryName = "subviews";

bool IsEntityBrushEntry(const ConfigEntry& entry);

String GetSubviewDisplayName(const String& subview_name);
String GetSubviewFilePath(const String& prototype_path, const String& subview_name);

bool ReadSubviewBrushes(const ConfigFile& info, const String& prototype_path, const String& subview_name,
						ConfigEntry& out_brushes);

uint64 HashPrototype(const String& prototype_path, const ConfigFile& info);

uint32 CountSubviewBrushes(const ConfigFile& info, const String& prototype_path);

/**
 * @brief Calls `callback` with a world space brush entry for every brush of every subview instance in a prototype
 * file, along with the instance's entry and the index of the brush in its subview
 */
void ForEachSubviewBrush(const ConfigFile& info, const String& prototype_path,
						 const std::function<void(ConfigEntry&, const ConfigEntry&, uint32)>& callback);

} // namespace fx
