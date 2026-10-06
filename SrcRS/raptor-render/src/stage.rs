use std::ffi::CString;
use std::sync::{Arc, Mutex, MutexGuard};

use ash::vk::{self, Handle};
use raptor_gfx::Gfx;
use raptor_gpu::{
	FinalViews, ImageFormat, ImageRecord, ImageType, RenderStageRecord, TargetConfig,
};

pub const PRESENT_FORMAT_FALLBACK: ImageFormat = ImageFormat::Bgra8UNorm;

#[derive(Clone)]
pub struct StageTarget {
	pub format: ImageFormat,
	pub size: Option<(u32, u32)>,
	pub image_type: ImageType,
	pub usage: vk::ImageUsageFlags,
	pub aspect: vk::ImageAspectFlags,
	pub load_op: vk::AttachmentLoadOp,
	pub store_op: vk::AttachmentStoreOp,
	pub stencil_load_op: vk::AttachmentLoadOp,
	pub stencil_store_op: vk::AttachmentStoreOp,
	pub initial_layout: vk::ImageLayout,
	pub final_layout: vk::ImageLayout,
	pub reference: Option<Arc<ImageRecord>>,
	pub render_pass_only: bool,
}

impl StageTarget {
	pub fn new(
		format: ImageFormat,
		usage: vk::ImageUsageFlags,
		aspect: vk::ImageAspectFlags,
	) -> Self {
		Self {
			format,
			size: None,
			image_type: ImageType::Flat,
			usage,
			aspect,
			load_op: vk::AttachmentLoadOp::CLEAR,
			store_op: vk::AttachmentStoreOp::STORE,
			stencil_load_op: vk::AttachmentLoadOp::CLEAR,
			stencil_store_op: vk::AttachmentStoreOp::STORE,
			initial_layout: vk::ImageLayout::UNDEFINED,
			final_layout: vk::ImageLayout::SHADER_READ_ONLY_OPTIMAL,
			reference: None,
			render_pass_only: false,
		}
	}

	pub fn color(format: ImageFormat) -> Self {
		Self::new(
			format,
			vk::ImageUsageFlags::COLOR_ATTACHMENT | vk::ImageUsageFlags::SAMPLED,
			vk::ImageAspectFlags::COLOR,
		)
	}

	pub fn depth(format: ImageFormat) -> Self {
		Self::new(
			format,
			vk::ImageUsageFlags::DEPTH_STENCIL_ATTACHMENT | vk::ImageUsageFlags::SAMPLED,
			vk::ImageAspectFlags::DEPTH,
		)
	}

	pub fn with_usage(mut self, usage: vk::ImageUsageFlags) -> Self {
		self.usage = usage;
		self
	}

	fn config(&self) -> TargetConfig {
		TargetConfig {
			image_type: self.image_type,
			usage: self.usage,
			aspect: self.aspect,
			samples: vk::SampleCountFlags::TYPE_1,
			load_op: self.load_op,
			store_op: self.store_op,
			stencil_load_op: self.stencil_load_op,
			stencil_store_op: self.stencil_store_op,
			initial_layout: self.initial_layout,
			final_layout: self.final_layout,
			render_pass_only: self.render_pass_only,
		}
	}
}

struct Inner {
	name: &'static str,
	size: (u32, u32),
	size_divisor: u32,
	record: Box<RenderStageRecord>,
	built: bool,
	final_stage: bool,
}

#[derive(Clone)]
pub struct RenderStage {
	inner: Arc<Mutex<Inner>>,
}

// SAFETY: the stage's Vulkan state is externally synchronised by the renderer, which records from
// one thread, and the mutex serialises access to the bookkeeping.
unsafe impl Send for RenderStage {}
// SAFETY: as above.
unsafe impl Sync for RenderStage {}

fn swapchain_extent(gfx: &Gfx) -> (u32, u32) {
	gfx.swapchain().extent()
}

fn divided(size: (u32, u32), divisor: u32) -> (u32, u32) {
	((size.0 / divisor).max(1), (size.1 / divisor).max(1))
}

impl RenderStage {
	pub fn new(gfx: &Gfx, name: &'static str, size: Option<(u32, u32)>, size_divisor: u32) -> Self {
		Self {
			inner: Arc::new(Mutex::new(Inner {
				name,
				size: size.unwrap_or_else(|| swapchain_extent(gfx)),
				size_divisor,
				record: RenderStageRecord::new(),
				built: false,
				final_stage: false,
			})),
		}
	}

	fn lock(&self) -> MutexGuard<'_, Inner> {
		self.inner
			.lock()
			.unwrap_or_else(|poisoned| poisoned.into_inner())
	}

	pub fn name(&self) -> &'static str {
		self.lock().name
	}

	pub fn is_built(&self) -> bool {
		self.lock().built
	}

	pub fn size_divisor(&self) -> u32 {
		self.lock().size_divisor
	}

	pub fn record_ptr(&self) -> *mut RenderStageRecord {
		std::ptr::from_ref::<RenderStageRecord>(&self.lock().record).cast_mut()
	}

	pub fn render_pass(&self) -> vk::RenderPass {
		vk::RenderPass::from_raw(self.lock().record.fields.render_pass)
	}

	pub fn extent(&self) -> (u32, u32) {
		let inner = self.lock();

		(inner.record.fields.width, inner.record.fields.height)
	}

	pub fn mark_final(&self) {
		let mut inner = self.lock();

		assert!(!inner.built, "cannot mark a stage final once it was built");

		inner.final_stage = true;
	}

	pub fn add_target(&self, target: StageTarget) {
		let mut inner = self.lock();

		let size = target
			.size
			.unwrap_or_else(|| divided(inner.size, inner.size_divisor));

		inner
			.record
			.add_target(target.config(), target.format, size, target.reference);
	}

	pub fn target_index(&self, format: ImageFormat, sub_index: i32) -> Option<usize> {
		let index = self.lock().record.targets().find(format as u16, sub_index);

		usize::try_from(index).ok()
	}

	pub fn target_image(&self, format: ImageFormat) -> Option<Arc<ImageRecord>> {
		self.target_at(self.target_index(format, 0)?)
	}

	pub fn target_at(&self, index: usize) -> Option<Arc<ImageRecord>> {
		self.lock()
			.record
			.targets()
			.get(index)
			.map(|target| target.image.clone())
	}

	pub fn target_count(&self) -> usize {
		self.lock().record.targets().len()
	}

	pub fn build(&self, gfx: &Gfx) {
		if self.lock().built {
			return;
		}

		if self.lock().final_stage {
			let mut target = StageTarget::color(gfx.swapchain().format());
			target.size = Some(swapchain_extent(gfx));
			target.load_op = vk::AttachmentLoadOp::DONT_CARE;
			target.store_op = vk::AttachmentStoreOp::STORE;
			target.initial_layout = vk::ImageLayout::UNDEFINED;
			target.final_layout = vk::ImageLayout::PRESENT_SRC_KHR;

			self.add_target(target);
		}

		let mut inner = self.lock();

		let size = divided(inner.size, inner.size_divisor);

		inner.build_inner(gfx, size, false);
		inner.built = true;
	}

	pub fn rebuild(&self, gfx: &Gfx, size: Option<(u32, u32)>) {
		let mut inner = self.lock();

		inner.size = size.unwrap_or_else(|| swapchain_extent(gfx));
		inner.built = false;

		let size = divided(inner.size, inner.size_divisor);

		inner.build_inner(gfx, size, true);
		inner.built = true;
	}

	pub fn begin(&self, gfx: &Gfx, cmd: vk::CommandBuffer) {
		let fields = self.lock().record.fields;

		let area = vk::Rect2D {
			offset: vk::Offset2D {
				x: fields.offset_x as i32,
				y: fields.offset_y as i32,
			},
			extent: vk::Extent2D {
				width: fields.width,
				height: fields.height,
			},
		};

		self.begin_area(gfx, cmd, area, area);
	}

	pub fn begin_area(
		&self,
		gfx: &Gfx,
		cmd: vk::CommandBuffer,
		render_area: vk::Rect2D,
		draw_area: vk::Rect2D,
	) {
		let inner = self.lock();

		assert!(inner.built, "the render stage {} was not built", inner.name);

		let image_index = if inner.final_stage {
			gfx.image_index()
		} else {
			0
		};

		// SAFETY: the command buffer is recording outside of a render pass and the stage is built.
		unsafe {
			inner
				.record
				.begin(gfx.device(), cmd, image_index, render_area, draw_area)
		};
	}

	pub fn release(&self, gfx: &Gfx) {
		let record = std::mem::replace(&mut self.lock().record, RenderStageRecord::new());

		self.lock().built = false;

		gfx.wait_idle();

		// SAFETY: the device is idle and the stage was made with this device and allocator.
		unsafe { record.destroy(gfx.device(), gfx.allocator()) };
	}

	pub fn end(&self, gfx: &Gfx, cmd: vk::CommandBuffer) {
		// SAFETY: the command buffer is inside this stage's render pass.
		unsafe { self.lock().record.end(gfx.device(), cmd) };
	}
}

impl Inner {
	fn build_inner(&mut self, gfx: &Gfx, size: (u32, u32), recreate: bool) {
		assert!(size.0 > 0 && size.1 > 0);

		let swapchain = gfx.swapchain();
		let extent = swapchain.extent();

		let views: Vec<vk::ImageView> = if self.final_stage {
			swapchain
				.images()
				.iter()
				.map(|image| image.view())
				.collect()
		} else {
			Vec::new()
		};

		let final_views = self.final_stage.then_some(FinalViews {
			views: &views,
			size: extent,
		});

		// SAFETY: the device and allocator belong together, and the stage's old resources are not
		// in use, as the caller waits for the device before rebuilding.
		let result = unsafe {
			self.record
				.build(gfx.device(), gfx.allocator(), size, final_views, recreate)
		};

		if let Err(error) = result {
			raptor_core::log_fatal!(Render; "Failed to build the render stage {}: {error:?}", self.name);
			panic!("Failed to build the render stage {}: {error:?}", self.name);
		}

		if let Ok(name) = CString::new(self.name) {
			gfx.device().set_object_name(
				vk::ObjectType::RENDER_PASS,
				self.record.fields.render_pass,
				&name,
			);
		}
	}
}
