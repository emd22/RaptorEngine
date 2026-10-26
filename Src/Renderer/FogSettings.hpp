#pragma once

#include <Color.hpp>
#include <Core/Serialize.hpp>
#include <Core/Types.hpp>

namespace fx::renderer {

struct FogSettings
{
	bool bEnabled = false;

	float32 Density = 0.02f;
	float32 Height = 0.0f;
	float32 Falloff = 0.15f;
	float32 Haze = 0.0f;

	Color Albedo = Color::FromRGBA(243, 243, 243, 255);

	float32 Anisotropy = 0.6f;
	float32 AmbientScale = 1.0f;

	bool operator==(const FogSettings& other) const = default;
};

} // namespace fx::renderer

RS_OBJECT_BEGIN(fx::renderer::FogSettings)
	RS_FIELD_NAMED(bEnabled, "enabled")
	RS_FIELD_NAMED(Density, "density")
	RS_FIELD_NAMED(Height, "height")
	RS_FIELD_NAMED(Falloff, "falloff")
	RS_FIELD_NAMED(Haze, "haze")
	RS_FIELD_NAMED(Albedo, "color")
	RS_FIELD_NAMED(Anisotropy, "anisotropy")
	RS_FIELD_NAMED(AmbientScale, "ambient")
RS_OBJECT_END()
