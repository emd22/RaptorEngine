use std::collections::HashMap;
use std::path::{Path, PathBuf};
use std::sync::{Arc, Mutex, MutexGuard};

use raptor_core::{log_error, log_info};
use raptor_gfx::{Image, TextureId, Textures, UploadContext, UploadError, UploadSpec, Uploader};
use raptor_gpu::{ImageFormat, ImageType};

use crate::image_decode::{Pixels, decode_file, decode_image};
use crate::ktx::{KtxImage, file_is_ktx, is_ktx, read_ktx};
use crate::model::{Model, ModelOptions, TextureData};
use crate::scheduler::{Backend, LoadStatus, LogLevel, Scheduler, SchedulerConfig};
use crate::ticket::Ticket;

pub type ModelBuilder = Box<dyn FnOnce(Model, &Uploader) + Send>;

#[derive(Clone, Copy, Debug)]
pub struct ImageRequest {
	pub image_type: ImageType,
	pub format: ImageFormat,
	pub is_target: bool,
}

impl ImageRequest {
	pub fn flat(format: ImageFormat) -> Self {
		Self {
			image_type: ImageType::Flat,
			format,
			is_target: false,
		}
	}
}

/// An image that is being loaded, or has been. The image is there at once but only has pixels once
/// the ticket says it is loaded.
#[derive(Clone)]
pub struct ImageHandle {
	pub id: TextureId,
	pub image: Image,
	pub ticket: Arc<Ticket>,
}

impl ImageHandle {
	pub fn is_loaded(&self) -> bool {
		self.ticket.is_loaded()
	}
}

enum Source {
	File(PathBuf),
	Memory(Vec<u8>),
	Chain(TextureData),
}

enum Loaded {
	Ktx(KtxImage),
	Decoded(Pixels),
	Chain,
}

struct ImageJob {
	ticket: Arc<Ticket>,
	image: Image,
	source: Source,
	request: ImageRequest,
	loaded: Option<Loaded>,
}

fn pixel_stride(format: ImageFormat) -> u32 {
	format.pixel_stride()
}

fn lock<T>(mutex: &Mutex<T>) -> MutexGuard<'_, T> {
	mutex
		.lock()
		.unwrap_or_else(|poisoned| poisoned.into_inner())
}

impl ImageJob {
	fn load(&mut self) -> LoadStatus {
		let channels = pixel_stride(self.request.format);

		let loaded = match &self.source {
			Source::File(path) if file_is_ktx(path) => std::fs::read(path)
				.map_err(|error| error.to_string())
				.and_then(|bytes| read_ktx(&bytes).map_err(|error| error.to_string()))
				.map(Loaded::Ktx),
			Source::File(path) => {
				decode_file(path, channels)
					.map(Loaded::Decoded)
					.map_err(|error| {
						format!("Could not load image file at '{}': {error}", path.display())
					})
			}
			Source::Memory(bytes) if is_ktx(bytes) => read_ktx(bytes)
				.map(Loaded::Ktx)
				.map_err(|error| error.to_string()),
			Source::Memory(bytes) => decode_image(bytes, channels)
				.map(Loaded::Decoded)
				.map_err(|error| format!("Could not load image file from memory!: {error}")),
			Source::Chain(texture) => {
				self.image.set_size((texture.width, texture.height));
				self.loaded = Some(Loaded::Chain);

				return LoadStatus::Success;
			}
		};

		match loaded {
			Ok(loaded) => {
				let size = match &loaded {
					Loaded::Ktx(ktx) => (ktx.width, ktx.height),
					Loaded::Decoded(pixels) => (pixels.width, pixels.height),
					Loaded::Chain => (0, 0),
				};

				self.image.set_size(size);
				self.loaded = Some(loaded);

				LoadStatus::Success
			}
			Err(message) => {
				log_error!(Asset; "{message}");

				LoadStatus::Error
			}
		}
	}

	fn upload(&mut self, uploader: &Uploader) -> bool {
		let Some(loaded) = self.loaded.take() else {
			return false;
		};

		let record = self.image.record();
		let request = self.request;

		let result = match (&loaded, &self.source) {
			(Loaded::Ktx(ktx), _) => upload_ktx(uploader, record, ktx, request),
			(Loaded::Decoded(pixels), _) => uploader
				.create_from_data(
					record,
					&UploadSpec {
						image_type: request.image_type,
						size: (pixels.width, pixels.height),
						format: request.format,
						mip_level: 0,
						mip_count: 1,
						data: &pixels.data,
					},
					request.is_target,
				)
				.map_err(UploadError::from),
			(Loaded::Chain, Source::Chain(texture)) => uploader.upload_chain(
				record,
				&UploadSpec {
					image_type: request.image_type,
					size: (texture.width, texture.height),
					format: texture.format,
					mip_level: 0,
					mip_count: texture.mip_count,
					data: &texture.bytes,
				},
			),
			_ => return false,
		};

		if let Err(error) = result {
			log_error!(Asset; "Could not upload an image: {error:?}");
		}

		self.ticket.signal_uploaded();

		true
	}

	fn finish(self, status: LoadStatus) {
		if status == LoadStatus::Success {
			self.ticket.wait_uploaded();
			self.ticket.complete(std::ptr::null_mut(), true);
		} else {
			self.ticket.fail(false);
		}
	}
}

fn upload_ktx(
	uploader: &Uploader,
	record: &raptor_gpu::ImageRecord,
	ktx: &KtxImage,
	request: ImageRequest,
) -> Result<(), UploadError> {
	let format = ImageFormat::from_raw(ktx.format).unwrap_or(request.format);

	if ktx.levels.len() > 1
		&& format.pixel_stride() == 4
		&& let Some((chain, count)) = ktx.chain(0, 0)
	{
		return uploader.upload_chain(
			record,
			&UploadSpec {
				image_type: request.image_type,
				size: (ktx.width, ktx.height),
				format,
				mip_level: 0,
				mip_count: count as u32,
				data: &chain,
			},
		);
	}

	uploader
		.create_from_data(
			record,
			&UploadSpec {
				image_type: request.image_type,
				size: (ktx.width, ktx.height),
				format,
				mip_level: 0,
				mip_count: 1,
				data: ktx.levels.first().map_or(&[][..], Vec::as_slice),
			},
			request.is_target,
		)
		.map_err(UploadError::from)
}

enum ModelSource {
	File(PathBuf),
	Memory(Vec<u8>),
}

struct ModelJob {
	ticket: Arc<Ticket>,
	name: String,
	source: ModelSource,
	options: ModelOptions,
	builder: Option<ModelBuilder>,
	model: Option<Model>,
}

impl ModelJob {
	fn load(&mut self) -> LoadStatus {
		let result = match &self.source {
			ModelSource::File(path) => Model::open(&self.name, path, self.options),
			ModelSource::Memory(bytes) => Model::from_bytes(&self.name, bytes, self.options),
		};

		match result {
			Ok(model) => {
				for message in &model.messages {
					log_error!(Asset; "{message}");
				}

				self.model = Some(model);

				LoadStatus::Success
			}
			Err(error) => {
				log_error!(Asset; "Error loading GLTF file! ({error})");

				LoadStatus::Error
			}
		}
	}

	fn upload(&mut self, uploader: &Uploader) -> bool {
		let (Some(model), Some(builder)) = (self.model.take(), self.builder.take()) else {
			return false;
		};

		builder(model, uploader);

		self.ticket.signal_uploaded();

		true
	}

	fn finish(self, status: LoadStatus) {
		if status == LoadStatus::Success {
			self.ticket.wait_uploaded();
			self.ticket.complete(std::ptr::null_mut(), true);
		} else {
			self.ticket.fail(true);
		}
	}
}

enum Job {
	Image(ImageJob),
	Model(ModelJob),
}

struct GpuBackend {
	upload: Arc<UploadContext>,
}

impl Backend for GpuBackend {
	type Job = Job;
	type Resource = ();

	fn load(&self, job: &mut Job) -> LoadStatus {
		match job {
			Job::Image(job) => job.load(),
			Job::Model(job) => job.load(),
		}
	}

	fn begin_upload(&self) {
		if let Err(error) = self.upload.begin_upload() {
			log_error!(Asset; "Could not begin an upload batch: {error:?}");
		}
	}

	fn upload(&self, job: &mut Job) -> bool {
		let uploader = Uploader::new(self.upload.core(), self.upload.cmd(), self.upload.family());

		match job {
			Job::Image(job) => job.upload(&uploader),
			Job::Model(job) => job.upload(&uploader),
		}
	}

	fn end_upload(&self) {
		if let Err(error) = self.upload.end_upload() {
			log_error!(Asset; "Could not submit an upload batch: {error:?}");
		}
	}

	fn finish(&self, job: Job, status: LoadStatus) {
		match job {
			Job::Image(job) => job.finish(status),
			Job::Model(job) => job.finish(status),
		}
	}

	fn frame(&self) -> u32 {
		self.upload.core().elapsed()
	}

	fn wait_for_uploads(&self) {
		let _ = self.upload.wait_for_uploads();
	}

	fn destroy(&self, _resource: ()) {}

	fn log(&self, level: LogLevel, message: &str) {
		match level {
			LogLevel::Info => log_info!(Asset; "{message}"),
			LogLevel::Error => log_error!(Asset; "{message}"),
		}
	}
}

const FLAT_NORMAL_KEY: u32 = u32::MAX;

/// The pixel of a normal map pointing straight out of the surface: (0.5, 0.5, 1.0) decodes to a
/// tangent space normal of (0, 0, 1).
const FLAT_NORMAL_PIXEL: [u8; 4] = [0x80, 0x80, 0xFF, 0xFF];

/// A pixel that leaves a shader multiply alone: every byte at its maximum.
fn neutral_pixel(stride: usize) -> Vec<u8> {
	vec![0xFF; stride]
}

/// Loads images and models on worker threads and puts them on the GPU in batches
pub struct AssetManager {
	scheduler: Scheduler<GpuBackend>,
	upload: Arc<UploadContext>,
	textures: Arc<Textures>,
	null_images: Mutex<HashMap<u32, ImageHandle>>,
}

impl AssetManager {
	pub fn new(upload: Arc<UploadContext>, textures: Arc<Textures>, workers: u32) -> Self {
		Self {
			scheduler: Scheduler::new(
				GpuBackend {
					upload: Arc::clone(&upload),
				},
				SchedulerConfig::new(workers),
			),
			upload,
			textures,
			null_images: Mutex::new(HashMap::new()),
		}
	}

	pub fn upload_context(&self) -> &Arc<UploadContext> {
		&self.upload
	}

	fn new_image(&self) -> ImageHandle {
		let (id, image) = self.textures.new_texture();

		ImageHandle {
			id,
			image,
			ticket: Arc::new(Ticket::default()),
		}
	}

	fn submit_image(&self, source: Source, request: ImageRequest) -> ImageHandle {
		let handle = self.new_image();

		self.scheduler.submit(Job::Image(ImageJob {
			ticket: Arc::clone(&handle.ticket),
			image: handle.image.clone(),
			source,
			request,
			loaded: None,
		}));

		handle
	}

	pub fn load_image(&self, path: impl AsRef<Path>, request: ImageRequest) -> ImageHandle {
		self.submit_image(Source::File(path.as_ref().to_path_buf()), request)
	}

	pub fn load_image_from_memory(&self, data: &[u8], request: ImageRequest) -> ImageHandle {
		self.submit_image(Source::Memory(data.to_vec()), request)
	}

	/// Puts pixels the caller already has on the GPU as an image of mip levels laid end to end
	pub fn upload_image(&self, texture: TextureData) -> ImageHandle {
		let request = ImageRequest::flat(texture.format);

		self.submit_image(Source::Chain(texture), request)
	}

	/// Loads a model, and calls `builder` with what it makes while the upload batch is recording, to
	/// make the objects and put their meshes on the GPU. The ticket is loaded once that is done.
	pub fn load_model(
		&self,
		name: &str,
		path: impl AsRef<Path>,
		options: ModelOptions,
		builder: ModelBuilder,
	) -> Arc<Ticket> {
		self.submit_model(
			name,
			ModelSource::File(path.as_ref().to_path_buf()),
			options,
			builder,
		)
	}

	pub fn load_model_from_memory(
		&self,
		name: &str,
		data: &[u8],
		options: ModelOptions,
		builder: ModelBuilder,
	) -> Arc<Ticket> {
		self.submit_model(name, ModelSource::Memory(data.to_vec()), options, builder)
	}

	fn submit_model(
		&self,
		name: &str,
		source: ModelSource,
		options: ModelOptions,
		builder: ModelBuilder,
	) -> Arc<Ticket> {
		let ticket = Arc::new(Ticket::default());

		self.scheduler.submit(Job::Model(ModelJob {
			ticket: Arc::clone(&ticket),
			name: name.to_owned(),
			source,
			options,
			builder: Some(builder),
			model: None,
		}));

		ticket
	}

	fn solid_image(&self, format: ImageFormat, pixel: &[u8]) -> ImageHandle {
		let handle = self.new_image();
		let upload = &self.upload;

		let result = upload.immediate(|cmd| {
			let uploader = Uploader::new(upload.core(), cmd, upload.family());

			let spec = UploadSpec {
				image_type: ImageType::Flat,
				size: (1, 1),
				format,
				mip_level: 0,
				mip_count: 1,
				data: pixel,
			};

			if let Err(error) = uploader.create_from_data(handle.image.record(), &spec, false) {
				log_error!(Asset; "Could not upload a stand-in image: {error:?}");
			}
		});

		if let Err(error) = result {
			log_error!(Asset; "Could not submit a stand-in image: {error:?}");
		}

		handle.image.set_size((1, 1));
		handle.ticket.mark_loaded();

		handle
	}

	fn null_image_for(&self, key: u32, format: ImageFormat, pixel: &[u8]) -> ImageHandle {
		if let Some(existing) = lock(&self.null_images).get(&key) {
			return existing.clone();
		}

		let made = self.solid_image(format, pixel);

		lock(&self.null_images).entry(key).or_insert(made).clone()
	}

	/// A 1x1 image that leaves a shader multiply alone, for materials without a texture
	pub fn null_image(&self, format: ImageFormat) -> ImageHandle {
		let pixel = neutral_pixel(format.pixel_stride() as usize);

		self.null_image_for(format as u32, format, &pixel)
	}

	/// A 1x1 normal map pointing straight out of the surface
	pub fn flat_normal_image(&self) -> ImageHandle {
		self.null_image_for(FLAT_NORMAL_KEY, ImageFormat::Rgba8UNorm, &FLAT_NORMAL_PIXEL)
	}

	/// Stops the threads and lets go of the stand-in images
	pub fn shutdown(&self) {
		log_info!(Asset; "Shutting down asset manager...");

		self.scheduler.stop();
		lock(&self.null_images).clear();
	}

	pub fn queued_jobs(&self) -> usize {
		self.scheduler.queued_jobs()
	}

	pub fn signal(&self) {
		self.scheduler.signal();
	}
}

impl Drop for AssetManager {
	fn drop(&mut self) {
		self.shutdown();
	}
}

#[cfg(test)]
mod tests {
	use super::*;

	#[test]
	fn the_pixel_of_a_stand_in_is_one_pixel_wide() {
		assert_eq!(
			neutral_pixel(ImageFormat::Rgba8UNorm.pixel_stride() as usize).len(),
			4
		);
		assert_eq!(FLAT_NORMAL_PIXEL.len(), 4);
	}
}
