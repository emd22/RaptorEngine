#pragma once

#include <Core/Types.hpp>

#include <cmath>

namespace fx {

struct Color
{
public:
	// Colour definitions
	static const Color sNone;
	static const Color sTransparent;

	static const Color sWhite;
	static const Color sBlack;
	static const Color sRed;
	static const Color sBlue;
	static const Color sGreen;

	static constexpr float32 scOneOver255 = 1.0f / 255.0f;

public:
	Color() {}
	Color(uint32 value) : Value(value) {}
	Color(uint32 value, uint8 brightness)
	{
		Value = value;
		A = brightness;
	}

	static FX_FORCE_INLINE Color FromRGBA(uint8 r, uint8 g, uint8 b, uint8 a);
	static FX_FORCE_INLINE Color FromFloats(float32 rgba[4]);

	FX_FORCE_INLINE float32 GetRF() const { return static_cast<float32>(R) * scOneOver255; }
	FX_FORCE_INLINE float32 GetGF() const { return static_cast<float32>(G) * scOneOver255; }
	FX_FORCE_INLINE float32 GetBF() const { return static_cast<float32>(B) * scOneOver255; }
	FX_FORCE_INLINE float32 GetAF() const { return static_cast<float32>(A) * scOneOver255; }

	static FX_FORCE_INLINE float32 SrgbToLinear(float32 value);
	FX_FORCE_INLINE void GetLinearRGB(float32 out_rgb[3]) const;

	FX_FORCE_INLINE uint32 AsUInt() const { return Value; }

public:
	union
	{
		struct
		{
			uint8 R, G, B, A;
		};

		uint32 Value;
	};
};

///////////////////////////
// Definitions
///////////////////////////

FX_FORCE_INLINE Color Color::FromRGBA(uint8 r, uint8 g, uint8 b, uint8 a)
{
	Color colour;
	colour.Value = ((static_cast<uint32>(a) << 24) | (static_cast<uint32>(b) << 16) | (static_cast<uint32>(g) << 8) |
					(static_cast<uint32>(r)));
	return colour;
}

FX_FORCE_INLINE Color Color::FromFloats(float32 rgba[4])
{
	Color colour;
	colour.Value = ((static_cast<uint32>(rgba[3] * 255.0f) << 24) | (static_cast<uint32>(rgba[2] * 255.0f) << 16) |
					(static_cast<uint32>(rgba[1] * 255.0f) << 8) | (static_cast<uint32>(rgba[0] * 255.0f)));
	return colour;
}

FX_FORCE_INLINE float32 Color::SrgbToLinear(float32 value)
{
	return (value <= 0.04045f) ? (value / 12.92f) : std::pow((value + 0.055f) / 1.055f, 2.4f);
}

FX_FORCE_INLINE void Color::GetLinearRGB(float32 out_rgb[3]) const
{
	out_rgb[0] = SrgbToLinear(GetRF());
	out_rgb[1] = SrgbToLinear(GetGF());
	out_rgb[2] = SrgbToLinear(GetBF());
}

enum class eColorComponent
{
	R = (1 << 0),
	G = (1 << 1),
	B = (1 << 2),
	A = (1 << 3),

	RG = R | G,
	RGB = RG | B,
	RGBA = RGB | A,
};

FxEnumFlags(eColorComponent);

} // namespace fx
