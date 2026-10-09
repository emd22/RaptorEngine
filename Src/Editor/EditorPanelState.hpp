#pragma once

#ifdef FX_IS_EDITOR

#include <Core/Types.hpp>
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

struct EditorPanelState
{
	ObjectPanelState Object;
	MaterialLibraryState Materials;
	WorldPanelState World;
	LightPanelState Light;

	std::string BlockoutPath;

	bool operator==(const EditorPanelState& other) const = default;
};

} // namespace fx::editor

#endif
