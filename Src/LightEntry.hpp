/*
 * File:        LightEntry.hpp
 * Author:      emd22
 * Created:     25/10/2026
 * Description: Reads and writes the lights of prototype file entries
 */

#pragma once

#include <Color.hpp>
#include <Core/Serialize.hpp>
#include <Core/Types.hpp>
#include <Math/Vec3.hpp>

namespace fx {

class ConfigEntry;

/// Values of the CLight constants in Config/Internal/Constants.conf
enum class ePrxLightType : int64
{
	Point = 0,
	Spot = 1,
};

struct SunLightEntry
{
	bool bEnabled = true;
	Vec3f Position = Vec3f::sZero;
	struct Color Color = Color::FromRGBA(255, 255, 255, 255);
	float32 Intensity = 100000.0f;
};

struct SpotLightEntry
{
	Vec3f Direction = Vec3f::sZero;
	float32 InnerAngle = 20.0f;
	float32 OuterAngle = 30.0f;
	bool bCastShadows = true;
};

struct LightEntry
{
	ePrxLightType Type = ePrxLightType::Point;
	Vec3f Position = Vec3f::sZero;
	struct Color Color = Color::FromRGBA(255, 255, 255, 255);
	float32 Intensity = 100000.0f;
	float32 Radius = 5.0f;
	SpotLightEntry Spot;
};

SunLightEntry ReadSunLightEntry(const ConfigEntry& entry);
void WriteSunLightEntry(ConfigEntry& entry, const SunLightEntry& sun);

LightEntry ReadLightEntry(const ConfigEntry& entry);
void WriteLightEntry(ConfigEntry& entry, const LightEntry& light);

} // namespace fx

RS_OBJECT_BEGIN(fx::SunLightEntry)
	RS_FIELD_NAMED(bEnabled, "enabled")
	RS_FIELD_NAMED(Position, "pos")
	RS_FIELD_NAMED(Color, "color")
	RS_FIELD_NAMED(Intensity, "intensity")
RS_OBJECT_END()

RS_OBJECT_BEGIN(fx::SpotLightEntry)
	RS_FIELD_NAMED(Direction, "dir")
	RS_FIELD_NAMED(InnerAngle, "inner")
	RS_FIELD_NAMED(OuterAngle, "outer")
	RS_FIELD_NAMED(bCastShadows, "shadows")
RS_OBJECT_END()

RS_OBJECT_BEGIN(fx::LightEntry)
	RS_FIELD_NAMED(Position, "pos")
	RS_FIELD_NAMED(Color, "color")
	RS_FIELD_NAMED(Intensity, "intensity")
	RS_FIELD_NAMED(Radius, "radius")
RS_OBJECT_END()
