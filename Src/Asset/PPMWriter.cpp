#include "PPMWriter.hpp"

#include <Core/Assert.hpp>
#include <Core/File.hpp>


namespace fx::asset {

void PPMWriter::WriteRGB(const String& path, const Vec2u& size, const Slice<uint8>& pixel_data)
{
	Assert(size.X > 0 && size.Y > 0);
	Assert(pixel_data.pData != nullptr);
	Assert(pixel_data.Size > 0);

	Assert(pixel_data.Size == (size.X * size.Y * 3));

	// Build the file header
	char ppm_header[32];
	snprintf(ppm_header, sizeof(ppm_header), "P6\n%u %u\n255\n", size.X, size.Y);

	// Write the file
	File file(path, File::eModType::Write, File::eDataType::Binary);
	file.Write(ppm_header);
	file.WriteRaw(pixel_data.pData, pixel_data.Size);
	file.Close();

	// And... thats it
}


} // namespace fx::asset
