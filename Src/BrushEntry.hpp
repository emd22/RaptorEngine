/*
 * File:        BrushEntry.hpp
 * Author:      emd22
 * Created:     10/10/2026
 * Description: Reads brushes from prototype file entries
 */

#pragma once

#include <Brush.hpp>
#include <Core/Serialize.hpp>
#include <Core/String.hpp>
#include <Math/FQuat.hpp>
#include <Math/FVec3.hpp>
#include <Patch.hpp>
#include <functional>

namespace fx {

class ConfigEntry;
class ConfigFile;

constexpr const char* scDecalName = "decal";

bool IsDecalEntry(const ConfigEntry& entry);

const char* ReadBrushMaterialName(const ConfigEntry& entry);

Brush ReadBrushEntry(ConfigEntry& entry);

void WriteFaceMaterials(ConfigEntry& entry, const Brush& brush);

bool IsPatchEntry(const ConfigEntry& entry);
Patch ReadPatchEntry(ConfigEntry& entry);
void WritePatchEntry(ConfigEntry& entry, const Patch& patch);

/////////////////////////////////////
// Subviews
/////////////////////////////////////

constexpr const char* scSubviewsEntryName = "subviews";
constexpr const char* scSubviewInstancesEntryName = "instances";
constexpr const char* scSubviewMemberName = "subview";

struct BrushPlacement
{
	FVec3 Position = FVec3::sZero;
	FQuat Rotation = FQuat::scIdentity;
};

BrushPlacement ReadBrushPlacement(const ConfigEntry& entry);
void WriteBrushPlacement(ConfigEntry& entry, const BrushPlacement& placement);

BrushPlacement PlaceInSubview(const BrushPlacement& local, const FVec3 brush_center, const BrushPlacement& instance);
BrushPlacement RemoveFromSubview(const BrushPlacement& world, const FVec3 brush_center, const BrushPlacement& instance);
BrushPlacement FindSubviewPlacement(const BrushPlacement& world, const BrushPlacement& local, const FVec3 brush_center);

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

bool ReadSubviewFile(const ConfigFile& info, const String& prototype_path, const String& subview_name,
					 ConfigEntry& out_brushes, ConfigEntry& out_instances);

String ResolveNestedSubviewName(const String& parent_name, const String& reference);

String MakeNestedSubviewReference(const String& parent_name, const String& subview_name);

const char* ReadInstanceSubviewName(const ConfigEntry& instance);

void AppendPlacedSubviewBrushes(ConfigEntry& out_brushes, ConfigEntry& subview_brushes, const String& instance_name,
								const BrushPlacement& instance);

bool IsBakedModelEntry(const ConfigEntry& model, const ConfigEntry* brushes);
uint64 HashPrototype(const String& prototype_path, const ConfigFile& info);

uint32 CountSubviewBrushes(const ConfigFile& info, const String& prototype_path);

void ForEachSubviewBrush(const ConfigFile& info, const String& prototype_path,
						 const std::function<void(ConfigEntry&, const ConfigEntry&, uint32)>& callback);

} // namespace fx

RS_OBJECT_BEGIN(fx::BrushPlacement)
	RS_FIELD_NAMED(Position, "pos")
	RS_FIELD_NAMED(Rotation, "rotquat")
RS_OBJECT_END()
