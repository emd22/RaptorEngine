#pragma once

#ifdef FX_IS_EDITOR

#include "TopViewPlane.hpp"

#include <Core/Types.hpp>
#include <array>
#include <string>
#include <vector>

namespace fx::editor {

struct ObjectPanelState
{
	bool bHasObject = false;
	bool bIsBlockout = false;
	bool bCanAttachScript = false;
	bool bScriptErrors = false;
	bool bIsTrigger = false;
	bool bHasEnterDirection = false;

	std::string Name;
	std::string ScriptPath;

	float32 EnterDirection[3] = { 0.0f, 0.0f, 0.0f };

	uint32 SelectedCount = 0;
	uint32 Tags = 0;
	uint32 Flags = 0;
	uint32 EditableTags = 0;
	uint32 EditableFlags = 0;

	int32 MaterialSlot = -1;

	bool operator==(const ObjectPanelState& other) const = default;
};

struct MaterialLibraryState
{
	std::vector<std::string> Names;
	std::vector<std::string> DiffusePaths;

	bool operator==(const MaterialLibraryState& other) const = default;
};

struct WorldPanelState
{
	bool bReflectionsEnabled = true;
	bool bReflectionFallback = false;

	int64 DebugMask = 0;
	int64 ReflectionDebugView = 0;

	std::string ReflectionStatus;

	float32 Aperture = 16.0f;
	float32 ShutterTime = 0.01f;
	float32 ISO = 100.0f;
	float32 Compensation = 0.0f;

	bool operator==(const WorldPanelState& other) const = default;
};

struct LightPanelState
{
	bool bHasLight = false;

	std::string Name;

	uint8 ColorR = 255;
	uint8 ColorG = 255;
	uint8 ColorB = 255;

	float32 PositionX = 0.0f;
	float32 PositionY = 0.0f;
	float32 PositionZ = 0.0f;

	float32 Radius = 0.0f;
	float32 Intensity = 0.0f;
	float32 Lumens = 0.0f;
	float32 OuterAngleDegrees = 0.0f;
	float32 InnerAngleDegrees = 0.0f;

	bool operator==(const LightPanelState& other) const = default;
};

enum class eTopViewBrushKind : uint8
{
	Geometry,
	ProbeVolume,
	ReflectionProbe,
	Volume,
};

struct TopViewFace
{
	float32 AU = 0.0f;
	float32 AV = 0.0f;
	float32 BU = 0.0f;
	float32 BV = 0.0f;

	float32 NormalU = 0.0f;
	float32 NormalV = 0.0f;

	bool operator==(const TopViewFace& other) const = default;
};

struct TopViewProjection
{
	float32 PivotU = 0.0f;
	float32 PivotV = 0.0f;
	float32 NearDepth = 0.0f;

	std::vector<float32> Hull;
	std::vector<float32> Edges;
	std::vector<TopViewFace> Faces;

	bool operator==(const TopViewProjection& other) const = default;
};

struct TopViewBrush
{
	uint32 ObjectId = 0;
	eTopViewBrushKind Kind = eTopViewBrushKind::Geometry;

	bool bSelected = false;
	bool bSelectable = true;

	std::array<TopViewProjection, scViewPlaneCount> Views;

	bool operator==(const TopViewBrush& other) const = default;
};

struct TopViewState
{
	bool bActive = false;
	bool bDataMode = false;
	bool bSnapEnabled = true;
	bool bCanCreate = true;
	bool bCanRotate = true;
	bool bCanFace = true;

	float32 MinFaceThickness = 0.1f;

	float32 SnapStep = 0.25f;
	float32 AngleSnapDegrees = 15.0f;

	bool bHasPlayer = false;
	std::array<float32, 3> PlayerPosition = { 0.0f, 0.0f, 0.0f };
	std::array<float32, 3> PlayerDirection = { 0.0f, 0.0f, 1.0f };

	uint32 CommandSerial = 0;

	std::vector<TopViewBrush> Brushes;

	bool operator==(const TopViewState& other) const = default;
};

struct EditorPanelState
{
	ObjectPanelState Object;
	MaterialLibraryState Materials;
	WorldPanelState World;
	LightPanelState Light;
	TopViewState TopView;

	std::string BlockoutPath;

	bool operator==(const EditorPanelState& other) const = default;
};

} // namespace fx::editor

#endif
