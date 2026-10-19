#pragma once

#include <Core/Types.hpp>

namespace fx {

enum class eObjectTag : uint32
{
	None = 0,
	Blockout = (1 << 0),
	/// Locks rotation and transformation for scripts
	LockTransform = (1 << 1),
	/// A blockout brush that marks out a light probe volume instead of level geometry
	ProbeVolume = (1 << 2),
	ReflectionProbe = (1 << 3),
	Spawn = (1 << 4),
	Trigger = (1 << 5),
	Decal = (1 << 8),
	Patch = (1 << 9),
};

FxEnumFlags(eObjectTag);

enum class eObjectFlags : uint16
{
	None = 0,
	ReadyToRender = (1 << 0),
	PhysicsEnabled = (1 << 1),
	ShadowCaster = (1 << 3),
	Unlit = (1 << 4),
	DisableCulling = (1 << 5),
	/// Left out of light probe bakes (capture, capture shadows and probe placement). See Object::IsProbeVisible().
	NotProbeVisible = (1 << 6),
	Hidden = (1 << 7),
	Lightmapped = (1 << 8),
	ShadowOnly = (1 << 9),
};

FxEnumFlags(eObjectFlags);

} // namespace fx
