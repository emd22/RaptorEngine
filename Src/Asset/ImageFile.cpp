#include "ImageFile.hpp"

#include <Util/RustInterop.hpp>
#include <raptor_ffi.h>

namespace fx {

bool SaveImageFile(eImageSaveFormat format, const Slice<const uint8>& data, const Vec2u& size, const String& path,
				   eImageSaveFlags flags)
{
	static const RxLogSink scImageLog = { .user = nullptr, .log = RustInterop::Log };

	const uint32 rust_format = (format == eImageSaveFormat::Jpeg) ? RX_IMAGE_SAVE_JPEG : RX_IMAGE_SAVE_PNG;
	const bool flip_y = (flags & eImageSaveFlags::FlipY) != 0;

	return rx_image_save(path.CStr(), rust_format, data.pData, data.Size, size.GetX(), size.GetY(), flip_y ? 1 : 0,
						 &scImageLog) != 0;
}

} // namespace fx
