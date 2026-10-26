#include "LightEntry.hpp"

#include <Asset/ConfigFile.hpp>

namespace fx {

static Color MakeOpaque(const Color& color) { return Color::FromRGBA(color.R, color.G, color.B, 255); }

SunLightEntry ReadSunLightEntry(const ConfigEntry& entry)
{
	SunLightEntry sun;
	Serializable<SunLightEntry>::LoadFromConfig(entry, sun);

	sun.Color = MakeOpaque(sun.Color);

	return sun;
}

void WriteSunLightEntry(ConfigEntry& entry, const SunLightEntry& sun)
{
	Serializable<SunLightEntry>::SaveToConfig(sun, entry);
}

LightEntry ReadLightEntry(const ConfigEntry& entry)
{
	LightEntry light;
	light.Type = static_cast<ePrxLightType>(
		entry.GetMemberValue<int64>(HashStr32("type"), static_cast<int64>(ePrxLightType::Point)));

	Serializable<LightEntry>::LoadFromConfig(entry, light);

	if (light.Type == ePrxLightType::Spot) {
		Serializable<SpotLightEntry>::LoadFromConfig(entry, light.Spot);
	}

	light.Color = MakeOpaque(light.Color);

	return light;
}

void WriteLightEntry(ConfigEntry& entry, const LightEntry& light)
{
	if (light.Type == ePrxLightType::Spot) {
		entry.AddMember(ConfigEntry::DotReference("type", "$clight.spot"));
	}
	else {
		entry.AddMember(ConfigEntry::DotReference("type", "$clight.point"));
	}

	Serializable<LightEntry>::SaveToConfig(light, entry);

	if (light.Type == ePrxLightType::Spot) {
		Serializable<SpotLightEntry>::SaveToConfig(light.Spot, entry);
	}
}

} // namespace fx
