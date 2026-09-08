/*
 * File:        PPMWriter.hpp
 * Author:      emd22
 * Created:     08/09/2026
 * Description: Simple PPM image writer. This is uncompressed, and meant to be used for debugging and direction image
 * output, and provide minimal abstraction compared to the other image writing methods in the engine.
 */

#pragma once

#include <Core/Slice.hpp>
#include <Core/String.hpp>
#include <Math/Vec2.hpp>

namespace fx::asset {

class PPMWriter
{
public:
	PPMWriter() = default;

	/**
	 * @brief Write RGB data out to a PPM image.
	 */
	void WriteRGB(const String& path, const Vec2u& size, const Slice<uint8>& pixel_data);

	~PPMWriter() = default;

private:
};


} // namespace fx::asset
