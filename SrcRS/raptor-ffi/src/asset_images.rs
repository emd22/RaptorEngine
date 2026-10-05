use std::ffi::{CStr, CString, c_char};
use std::path::Path;

use raptor_asset::image_decode::{self, Pixels, SaveFormat};
use raptor_asset::ktx::{self, KtxImage};

use crate::{LogFn, RxLogSink};

const LOG_ERROR: i32 = 3;
const LOG_CATEGORY_ASSET: i32 = 5;

fn report(log: *const RxLogSink, message: &str)
{
	// SAFETY: guaranteed by the caller of the exported functions.
	let Some(sink) = (unsafe { log.as_ref() }) else {
		return;
	};

	let Some(log): Option<LogFn> = sink.log else {
		return;
	};

	let text = CString::new(message.replace('\0', "")).unwrap_or_default();

	// SAFETY: the host promised `log` accepts a pointer and length pair.
	unsafe {
		log(
			sink.user,
			LOG_ERROR,
			LOG_CATEGORY_ASSET,
			text.as_ptr(),
			text.as_bytes().len(),
		);
	}
}

/// # Safety
///
/// `path` must be NUL-terminated.
unsafe fn path_of(path: *const c_char) -> String
{
	// SAFETY: guaranteed by the caller.
	unsafe { CStr::from_ptr(path) }.to_string_lossy().into_owned()
}

pub type RxKtx = KtxImage;

#[repr(C)]
pub struct RxKtxInfo
{
	pub format: u32,
	pub width: u32,
	pub height: u32,
	pub level_count: u32,
}

fn open_ktx(data: &[u8], log: *const RxLogSink, origin: &str) -> *mut RxKtx
{
	match ktx::read_ktx(data) {
		Ok(image) => Box::into_raw(Box::new(image)),
		Err(error @ ktx::KtxError::Invalid(_)) => {
			report(log, &format!("Could not load KTX {origin}: {error}"));
			std::ptr::null_mut()
		}
		Err(error) => {
			report(log, &error.to_string());
			std::ptr::null_mut()
		}
	}
}

/// Reads a KTX file. Returns null, after saying why through `log`, if it is not a KTX texture the
/// engine can use.
///
/// # Safety
///
/// `path` must be NUL-terminated and `log` null or valid for the call.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_ktx_open_file(path: *const c_char, log: *const RxLogSink) -> *mut RxKtx
{
	// SAFETY: guaranteed by the caller.
	let path = unsafe { path_of(path) };

	match std::fs::read(&path) {
		Ok(data) => open_ktx(&data, log, &format!("file at '{path}'")),
		Err(error) => {
			report(log, &format!("Could not load KTX file at '{path}': {error}"));
			std::ptr::null_mut()
		}
	}
}

/// Reads a KTX texture from memory, which is not referred to afterwards.
///
/// # Safety
///
/// `data` must point at `size` readable bytes and `log` be null or valid for the call.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_ktx_open_memory(
	data: *const u8,
	size: usize,
	log: *const RxLogSink,
) -> *mut RxKtx
{
	let data = if data.is_null() {
		&[][..]
	}
	else {
		// SAFETY: guaranteed by the caller.
		unsafe { std::slice::from_raw_parts(data, size) }
	};

	open_ktx(data, log, "from memory")
}

/// # Safety
///
/// `ktx` must be null or come from an open function, and must not be used afterwards.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_ktx_free(ktx: *mut RxKtx)
{
	if !ktx.is_null() {
		// SAFETY: guaranteed by the caller.
		drop(unsafe { Box::from_raw(ktx) });
	}
}

/// # Safety
///
/// `ktx` must be live and `out` writable.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_ktx_info(ktx: *const RxKtx, out: *mut RxKtxInfo)
{
	// SAFETY: guaranteed by the caller.
	let ktx = unsafe { &*ktx };

	// SAFETY: guaranteed by the caller.
	unsafe {
		out.write(RxKtxInfo {
			format: u32::from(ktx.format),
			width: ktx.width,
			height: ktx.height,
			level_count: ktx.levels.len() as u32,
		});
	}
}

/// The pixels of a mip level, level 0 being the largest. Null if there is no such level.
///
/// # Safety
///
/// `ktx` must be live and `size` writable. The pixels last as long as the texture.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_ktx_level(ktx: *const RxKtx, level: u32, size: *mut usize) -> *const u8
{
	// SAFETY: guaranteed by the caller.
	let ktx = unsafe { &*ktx };

	match ktx.levels.get(level as usize) {
		Some(pixels) => {
			// SAFETY: guaranteed by the caller.
			unsafe { size.write(pixels.len()) };
			pixels.as_ptr()
		}
		None => {
			// SAFETY: guaranteed by the caller.
			unsafe { size.write(0) };
			std::ptr::null()
		}
	}
}

pub type RxDecodedImage = Pixels;

#[repr(C)]
pub struct RxDecodedInfo
{
	pub width: u32,
	pub height: u32,
	pub channels: u32,
	pub size: usize,
}

/// Decodes an image file to `channels` 8-bit channels. Null if it could not be.
///
/// # Safety
///
/// `path` must be NUL-terminated.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_image_decode_file(path: *const c_char, channels: u32) -> *mut RxDecodedImage
{
	// SAFETY: guaranteed by the caller.
	let path = unsafe { path_of(path) };

	image_decode::decode_file(Path::new(&path), channels)
		.map_or(std::ptr::null_mut(), |pixels| Box::into_raw(Box::new(pixels)))
}

/// Decodes an image in memory to `channels` 8-bit channels. Null if it could not be.
///
/// # Safety
///
/// `data` must point at `size` readable bytes.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_image_decode_memory(
	data: *const u8,
	size: usize,
	channels: u32,
) -> *mut RxDecodedImage
{
	if data.is_null() {
		return std::ptr::null_mut();
	}

	// SAFETY: guaranteed by the caller.
	let data = unsafe { std::slice::from_raw_parts(data, size) };

	image_decode::decode_image(data, channels)
		.map_or(std::ptr::null_mut(), |pixels| Box::into_raw(Box::new(pixels)))
}

/// # Safety
///
/// `image` must be null or come from a decode function, and must not be used afterwards.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_image_decoded_free(image: *mut RxDecodedImage)
{
	if !image.is_null() {
		// SAFETY: guaranteed by the caller.
		drop(unsafe { Box::from_raw(image) });
	}
}

/// # Safety
///
/// `image` must be live and `out` writable.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_image_decoded_info(image: *const RxDecodedImage, out: *mut RxDecodedInfo)
{
	// SAFETY: guaranteed by the caller.
	let image = unsafe { &*image };

	// SAFETY: guaranteed by the caller.
	unsafe {
		out.write(RxDecodedInfo {
			width: image.width,
			height: image.height,
			channels: image.channels,
			size: image.data.len(),
		});
	}
}

/// # Safety
///
/// `image` must be live. The pixels last as long as the image.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_image_decoded_data(image: *const RxDecodedImage) -> *const u8
{
	// SAFETY: guaranteed by the caller.
	unsafe { &*image }.data.as_ptr()
}

/// Reads the size of an image in memory without decoding it. Returns 0 if it is not an image.
///
/// # Safety
///
/// `data` must point at `size` readable bytes, and `width` and `height` be writable.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_image_probe_memory(
	data: *const u8,
	size: usize,
	width: *mut u32,
	height: *mut u32,
) -> u8
{
	if data.is_null() {
		return 0;
	}

	// SAFETY: guaranteed by the caller.
	let data = unsafe { std::slice::from_raw_parts(data, size) };

	match image_decode::image_size(data) {
		Some((w, h)) => {
			// SAFETY: guaranteed by the caller.
			unsafe {
				width.write(w);
				height.write(h);
			}
			1
		}
		None => 0,
	}
}

pub const RX_IMAGE_SAVE_JPEG: u32 = 0;
pub const RX_IMAGE_SAVE_PNG: u32 = 1;

/// Writes RGBA pixels to a file as a JPEG or a PNG. Returns 0, after saying why through `log`, if it
/// could not be written.
///
/// # Safety
///
/// `path` must be NUL-terminated, `rgba` point at `size` readable bytes and `log` be null or valid.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_image_save(
	path: *const c_char,
	format: u32,
	rgba: *const u8,
	size: usize,
	width: u32,
	height: u32,
	flip_y: u8,
	log: *const RxLogSink,
) -> u8
{
	// SAFETY: guaranteed by the caller.
	let path = unsafe { path_of(path) };

	let pixels = if rgba.is_null() {
		&[][..]
	}
	else {
		// SAFETY: guaranteed by the caller.
		unsafe { std::slice::from_raw_parts(rgba, size) }
	};

	let format = if format == RX_IMAGE_SAVE_JPEG { SaveFormat::Jpeg } else { SaveFormat::Png };

	match image_decode::save_rgba(Path::new(&path), format, pixels, width, height, flip_y != 0) {
		Ok(()) => 1,
		Err(error) => {
			report(log, &format!("Could not save the image '{path}': {error}"));
			0
		}
	}
}

