#pragma once

#ifdef FX_IS_EDITOR

#include "EditorTool.hpp"
#include "TopViewPlane.hpp"

#include <Color.hpp>
#include <Core/DynArray.hpp>
#include <Core/String.hpp>
#include <Core/Types.hpp>
#include <Math/Vec2.hpp>
#include <Math/Vec3.hpp>
#include <Object/ObjectFlags.hpp>

namespace fx::editor {

struct ObjectPanelState
{
	bool bHasObject = false;
	bool bIsBlockout = false;
	bool bCanAttachScript = false;
	bool bScriptErrors = false;
	bool bIsTrigger = false;
	bool bHasEnterDirection = false;

	String Name;
	String ScriptPath;
	/// The subview that the selected object was placed with
	String SubviewName;
	uint32 SubviewBrushCount = 0;
	/// The subview being edited
	String ActiveSubview;

	Vec3f EnterDirection = Vec3f::sZero;

	uint32 SelectedCount = 0;
	eObjectTag Tags = eObjectTag::None;
	eObjectFlags Flags = eObjectFlags::None;
	eObjectTag EditableTags = eObjectTag::None;
	eObjectFlags EditableFlags = eObjectFlags::None;

	int32 MaterialSlot = -1;

	bool operator==(const ObjectPanelState& other) const = default;
};

struct MaterialLibraryState
{
	DynArray<String> Names;
	DynArray<String> DiffusePaths;

	MaterialLibraryState() = default;
	MaterialLibraryState(MaterialLibraryState&& other) = default;
	MaterialLibraryState(const MaterialLibraryState& other) { (*this) = other; }

	MaterialLibraryState& operator=(MaterialLibraryState&& other) = default;

	MaterialLibraryState& operator=(const MaterialLibraryState& other)
	{
		if (this == &other) {
			return *this;
		}

		Names = other.Names.Clone();
		DiffusePaths = other.DiffusePaths.Clone();

		return *this;
	}

	bool operator==(const MaterialLibraryState& other) const = default;
};

struct WorldPanelState
{
	bool bReflectionsEnabled = true;
	bool bReflectionFallback = false;

	int64 DebugMask = 0;
	int64 ReflectionDebugView = 0;

	String ReflectionStatus;

	float32 Aperture = 16.0f;
	float32 ShutterTime = 0.01f;
	float32 ISO = 100.0f;
	float32 Compensation = 0.0f;

	bool operator==(const WorldPanelState& other) const = default;
};

struct LightPanelState
{
	bool bHasLight = false;

	String Name;

	struct Color Color = Color::sWhite;
	Vec3f Position = Vec3f::sZero;

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
	NvBrush,
	DBound,
	Decal,
};

struct TopViewEdge
{
	Vec2f A = Vec2f::sZero;
	Vec2f B = Vec2f::sZero;

	bool operator==(const TopViewEdge& other) const = default;
};

struct TopViewFace
{
	Vec2f A = Vec2f::sZero;
	Vec2f B = Vec2f::sZero;
	Vec2f Normal = Vec2f::sZero;

	bool operator==(const TopViewFace& other) const = default;
};

struct TopViewProjection
{
	Vec2f Pivot = Vec2f::sZero;
	float32 NearDepth = 0.0f;

	DynArray<Vec2f> Hull;
	DynArray<TopViewEdge> Edges;
	DynArray<TopViewFace> Faces;

	TopViewProjection() = default;
	TopViewProjection(TopViewProjection&& other) = default;
	TopViewProjection(const TopViewProjection& other) { (*this) = other; }

	TopViewProjection& operator=(TopViewProjection&& other) = default;

	TopViewProjection& operator=(const TopViewProjection& other)
	{
		if (this == &other) {
			return *this;
		}

		Pivot = other.Pivot;
		NearDepth = other.NearDepth;

		Hull = other.Hull.Clone();
		Edges = other.Edges.Clone();
		Faces = other.Faces.Clone();

		return *this;
	}

	bool operator==(const TopViewProjection& other) const = default;
};

struct TopViewBrush
{
	ObjectID ObjectId = ObjectID::scNull;
	eTopViewBrushKind Kind = eTopViewBrushKind::Geometry;

	bool bSelected = false;
	bool bSelectable = true;

	TopViewProjection Views[scViewPlaneCount];

	bool operator==(const TopViewBrush& other) const = default;
};

enum class eTopViewMarkerKind : uint8
{
	Light,
	Spawn,
};

struct TopViewMarker
{
	eTopViewMarkerKind Kind = eTopViewMarkerKind::Light;
	uint32 LightId = 0;

	Vec3f Position = Vec3f::sZero;
	Vec3f AimOrigin = Vec3f::sZero;
	Vec3f Direction = Vec3f(0.0f, 0.0f, 1.0f);

	Vec3f BoundsMin = Vec3f::sZero;
	Vec3f BoundsMax = Vec3f::sZero;

	bool bSelected = false;
	bool bSelectable = false;

	bool operator==(const TopViewMarker& other) const = default;
};

struct TopViewState
{
	bool bActive = false;
	bool bDataMode = false;
	bool bSimulating = false;
	bool bSnapEnabled = true;

	uint32 AvailableTools = 0;

	float32 MinFaceThickness = 0.1f;

	float32 SnapStep = 0.25f;
	float32 AngleSnapDegrees = 15.0f;

	bool bHasPlayer = false;
	Vec3f PlayerPosition = Vec3f::sZero;
	Vec3f PlayerDirection = Vec3f(0.0f, 0.0f, 1.0f);

	uint32 CommandSerial = 0;

	/// How many brushes are hidden from the 2D views
	uint32 HiddenCount = 0;

	DynArray<TopViewBrush> Brushes;
	DynArray<TopViewMarker> Markers;

	TopViewState() = default;
	TopViewState(TopViewState&& other) = default;
	TopViewState(const TopViewState& other) { (*this) = other; }

	TopViewState& operator=(TopViewState&& other) = default;

	TopViewState& operator=(const TopViewState& other)
	{
		if (this == &other) {
			return *this;
		}

		bActive = other.bActive;
		bDataMode = other.bDataMode;
		bSimulating = other.bSimulating;
		bSnapEnabled = other.bSnapEnabled;
		AvailableTools = other.AvailableTools;

		MinFaceThickness = other.MinFaceThickness;

		SnapStep = other.SnapStep;
		AngleSnapDegrees = other.AngleSnapDegrees;

		bHasPlayer = other.bHasPlayer;
		PlayerPosition = other.PlayerPosition;
		PlayerDirection = other.PlayerDirection;

		CommandSerial = other.CommandSerial;
		HiddenCount = other.HiddenCount;

		Brushes = other.Brushes.Clone();
		Markers = other.Markers.Clone();

		return *this;
	}

	bool operator==(const TopViewState& other) const = default;
};

struct EditorPanelState
{
	ObjectPanelState Object;
	MaterialLibraryState Materials;
	WorldPanelState World;
	LightPanelState Light;
	TopViewState TopView;

	String BlockoutPath;

	/// The subview being edited, empty while the level itself is
	String ActiveSubview;
	/// Every subview of the prototype, one per line
	String SubviewNames;

	bool bUnsavedChanges = false;

	bool operator==(const EditorPanelState& other) const = default;
};

} // namespace fx::editor

#endif
