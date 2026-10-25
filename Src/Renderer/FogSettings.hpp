#pragma once

#include <Color.hpp>
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
