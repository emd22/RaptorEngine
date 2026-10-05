use std::sync::Arc;

use ash::prelude::VkResult;
use ash::vk;

use crate::{
	Allocator, AttachmentInfo, ClearTarget, Device, ImageDesc, ImageFormat, ImageRecord, ImageType,
	TargetAspect,
};

/// How a render stage's target is made and used. The size and format are kept in the target's
/// image.
#[derive(Clone, Copy, Debug)]
pub struct TargetConfig
{
	pub image_type: ImageType,
	pub usage: vk::ImageUsageFlags,
	pub aspect: vk::ImageAspectFlags,
	pub samples: vk::SampleCountFlags,
	pub load_op: vk::AttachmentLoadOp,
	pub store_op: vk::AttachmentStoreOp,
	pub stencil_load_op: vk::AttachmentLoadOp,
	pub stencil_store_op: vk::AttachmentStoreOp,
	pub initial_layout: vk::ImageLayout,
	pub final_layout: vk::ImageLayout,
	/// Only ever used inside of the render pass, so it is not cleared by the stage
	pub render_pass_only: bool,
}

impl TargetConfig
{
	pub fn is_depth(&self) -> bool
	{
		self.aspect == vk::ImageAspectFlags::DEPTH
	}
}

pub struct Target
{
	pub config: TargetConfig,
	pub image: Arc<ImageRecord>,
	reference: bool,
}

impl Target
{
	pub fn is_reference(&self) -> bool
	{
		self.reference
	}
}

/// The targets a render stage draws to, and what is worked out from them.
#[derive(Default)]
pub struct TargetList
{
	targets: Vec<Target>,
	descriptions: Option<Vec<vk::AttachmentDescription>>,
	views: Option<Vec<vk::ImageView>>,
	images_created: bool,
}

impl TargetList
{
	/// Adds a target of `format` and `size`. A target that is made from `reference` uses that image
	/// instead of making its own, so it draws to or reads what another stage made.
	pub fn add(
		&mut self,
		config: TargetConfig,
		format: ImageFormat,
		size: (u32, u32),
		reference: Option<Arc<ImageRecord>>,
	) -> usize
	{
		let is_reference = reference.is_some();
		let image = reference.unwrap_or_else(ImageRecord::new);

		if !is_reference {
			image.set_info(size, format, 0, 1);
		}

		self.targets.push(Target {
			config,
			image,
			reference: is_reference,
		});

		self.descriptions = None;
		self.views = None;

		self.targets.len() - 1
	}

	pub fn len(&self) -> usize
	{
		self.targets.len()
	}

	pub fn is_empty(&self) -> bool
	{
		self.targets.is_empty()
	}

	pub fn get(&self, index: usize) -> Option<&Target>
	{
		self.targets.get(index)
	}

	pub fn iter(&self) -> impl Iterator<Item = &Target>
	{
		self.targets.iter()
	}

	/// The formats of the targets, in order.
	pub fn formats(&self) -> Vec<u16>
	{
		self.targets
			.iter()
			.map(|target| target.image.fields().format)
			.collect()
	}

	/// The formats of the targets that are not depth, which are the ones that blend.
	pub fn color_formats(&self) -> Vec<u16>
	{
		self.targets
			.iter()
			.filter(|target| !target.config.is_depth())
			.map(|target| target.image.fields().format)
			.collect()
	}

	/// Where the `sub_index`th target of `format` is, or -1.
	pub fn find(&self, format: u16, sub_index: i32) -> i32
	{
		crate::find_format_index(&self.formats(), format, sub_index)
	}

	pub fn clear_targets(&self) -> Vec<ClearTarget>
	{
		self.targets
			.iter()
			.map(|target| ClearTarget {
				aspect: match target.config.aspect {
					vk::ImageAspectFlags::COLOR => Some(TargetAspect::Color),
					vk::ImageAspectFlags::DEPTH => Some(TargetAspect::Depth),
					_ => None,
				},
				load_op: target.config.load_op,
				render_pass_only: target.config.render_pass_only,
			})
			.collect()
	}

	/// Makes the images of the targets at `size`, unless that was done already. A target that was
	/// made from another's image has nothing to make.
	///
	/// # Safety
	///
	/// The device and allocator must be live and belong together, and no images that are being
	/// replaced may be in use.
	pub unsafe fn create_images(
		&mut self,
		device: &Device,
		allocator: &Allocator,
		size: (u32, u32),
	) -> VkResult<()>
	{
		if self.images_created {
			return Ok(());
		}

		for target in &self.targets {
			if target.reference {
				continue;
			}

			target.image.set_size(size);

			let fields = target.image.fields();

			let desc = ImageDesc {
				image_type: target.config.image_type,
				size: (fields.width, fields.height),
				mips: 1,
				format: ImageFormat::from_raw(fields.format).unwrap_or(ImageFormat::None),
				tiling: vk::ImageTiling::OPTIMAL,
				usage: target.config.usage,
				aspect: target.config.aspect,
				is_target: true,
				cube_count: 1,
				initial_layout: vk::ImageLayout::UNDEFINED,
			};

			// SAFETY: guaranteed by the caller.
			unsafe { target.image.create(device, allocator, &desc) }?;
		}

		self.images_created = true;

		Ok(())
	}

	/// Makes the images again, at a new size.
	///
	/// # Safety
	///
	/// As for `create_images`.
	pub unsafe fn recreate_images(
		&mut self,
		device: &Device,
		allocator: &Allocator,
		size: (u32, u32),
	) -> VkResult<()>
	{
		self.images_created = false;
		self.descriptions = None;
		self.views = None;

		// SAFETY: guaranteed by the caller.
		unsafe { self.create_images(device, allocator, size) }
	}

	/// The attachment of each target, in order.
	pub fn descriptions(&mut self) -> &[vk::AttachmentDescription]
	{
		let targets = &self.targets;

		self.descriptions.get_or_insert_with(|| {
			targets
				.iter()
				.map(|target| {
					let format = ImageFormat::from_raw(target.image.fields().format)
						.unwrap_or(ImageFormat::None);

					AttachmentInfo {
						format: format.to_vk(),
						samples: target.config.samples,
						load_op: target.config.load_op,
						store_op: target.config.store_op,
						stencil_load_op: target.config.stencil_load_op,
						stencil_store_op: target.config.stencil_store_op,
						initial_layout: target.config.initial_layout,
						final_layout: target.config.final_layout,
					}
					.description()
				})
				.collect()
		})
	}

	/// The views of the targets whose images are made.
	pub fn views(&mut self) -> &[vk::ImageView]
	{
		use ash::vk::Handle;

		let targets = &self.targets;

		self.views.get_or_insert_with(|| {
			targets
				.iter()
				.filter(|target| target.image.fields().image != 0)
				.map(|target| vk::ImageView::from_raw(target.image.fields().view))
				.collect()
		})
	}

	/// Gives the images back, destroying the ones that were made.
	///
	/// # Safety
	///
	/// The device and allocator must be the ones the images were made with, and nothing may be
	/// using them.
	pub unsafe fn release(&mut self, device: &Device, allocator: &Allocator)
	{
		for target in self.targets.drain(..) {
			// SAFETY: guaranteed by the caller.
			unsafe { ImageRecord::release_arc(target.image, device, allocator) };
		}

		self.descriptions = None;
		self.views = None;
		self.images_created = false;
	}
}

#[cfg(test)]
mod tests
{
	use super::*;

	fn config(aspect: vk::ImageAspectFlags) -> TargetConfig
	{
		TargetConfig {
			image_type: ImageType::Flat,
			usage: vk::ImageUsageFlags::COLOR_ATTACHMENT | vk::ImageUsageFlags::SAMPLED,
			aspect,
			samples: vk::SampleCountFlags::TYPE_1,
			load_op: vk::AttachmentLoadOp::CLEAR,
			store_op: vk::AttachmentStoreOp::STORE,
			stencil_load_op: vk::AttachmentLoadOp::CLEAR,
			stencil_store_op: vk::AttachmentStoreOp::STORE,
			initial_layout: vk::ImageLayout::UNDEFINED,
			final_layout: vk::ImageLayout::SHADER_READ_ONLY_OPTIMAL,
			render_pass_only: false,
		}
	}

	fn sample() -> TargetList
	{
		let mut list = TargetList::default();

		list.add(
			config(vk::ImageAspectFlags::COLOR),
			ImageFormat::Rgba16Float,
			(64, 32),
			None,
		);
		list.add(
			config(vk::ImageAspectFlags::DEPTH),
			ImageFormat::D32Float,
			(64, 32),
			None,
		);
		list.add(
			config(vk::ImageAspectFlags::COLOR),
			ImageFormat::R8UNorm,
			(64, 32),
			None,
		);

		list
	}

	#[test]
	fn a_new_target_holds_its_size_and_format_until_its_image_is_made()
	{
		let list = sample();
		let fields = list.get(0).unwrap().image.fields();

		assert_eq!((fields.width, fields.height), (64, 32));
		assert_eq!(fields.format, ImageFormat::Rgba16Float as u16);
		assert_eq!(fields.image, 0);
	}

	#[test]
	fn formats_are_listed_in_order_and_colors_leave_out_depth()
	{
		let list = sample();

		assert_eq!(
			list.formats(),
			[
				ImageFormat::Rgba16Float as u16,
				ImageFormat::D32Float as u16,
				ImageFormat::R8UNorm as u16
			]
		);
		assert_eq!(
			list.color_formats(),
			[ImageFormat::Rgba16Float as u16, ImageFormat::R8UNorm as u16]
		);
	}

	#[test]
	fn a_target_is_found_by_format_and_by_how_many_of_it_come_before()
	{
		let mut list = sample();

		list.add(
			config(vk::ImageAspectFlags::COLOR),
			ImageFormat::Rgba16Float,
			(64, 32),
			None,
		);

		assert_eq!(list.find(ImageFormat::D32Float as u16, 0), 1);
		assert_eq!(list.find(ImageFormat::Rgba16Float as u16, 0), 0);
		assert_eq!(list.find(ImageFormat::Rgba16Float as u16, 1), 3);
		assert_eq!(list.find(ImageFormat::Rgba16Float as u16, 2), -1);
		assert_eq!(list.find(ImageFormat::Bgra8Srgb as u16, 0), -1);
	}

	#[test]
	fn a_reference_target_shares_the_image_it_refers_to()
	{
		let mut list = sample();
		let source = list.get(1).unwrap().image.clone();

		let index = list.add(
			config(vk::ImageAspectFlags::DEPTH),
			ImageFormat::D32Float,
			(1, 1),
			Some(source.clone()),
		);

		assert!(list.get(index).unwrap().is_reference());
		assert!(Arc::ptr_eq(&list.get(index).unwrap().image, &source));

		let fields = source.fields();
		assert_eq!((fields.width, fields.height), (64, 32));
	}

	#[test]
	fn descriptions_follow_each_target_and_are_kept_until_the_list_changes()
	{
		let mut list = sample();

		let descriptions = list.descriptions().to_vec();

		assert_eq!(descriptions.len(), 3);
		assert_eq!(descriptions[0].format, vk::Format::R16G16B16A16_SFLOAT);
		assert_eq!(descriptions[1].format, vk::Format::D32_SFLOAT);
		assert_eq!(
			descriptions[2].final_layout,
			vk::ImageLayout::SHADER_READ_ONLY_OPTIMAL
		);

		list.add(
			config(vk::ImageAspectFlags::COLOR),
			ImageFormat::R8UNorm,
			(1, 1),
			None,
		);

		assert_eq!(list.descriptions().len(), 4);
	}

	#[test]
	fn the_clear_targets_carry_the_aspect_and_load_op()
	{
		let mut list = sample();

		list.add(
			TargetConfig {
				render_pass_only: true,
				load_op: vk::AttachmentLoadOp::LOAD,
				..config(vk::ImageAspectFlags::DEPTH)
			},
			ImageFormat::D32Float,
			(1, 1),
			None,
		);

		let targets = list.clear_targets();

		assert_eq!(targets[0].aspect, Some(TargetAspect::Color));
		assert_eq!(targets[1].aspect, Some(TargetAspect::Depth));
		assert!(targets[3].render_pass_only);
		assert_eq!(targets[3].load_op, vk::AttachmentLoadOp::LOAD);
	}

	#[test]
	fn views_leave_out_targets_whose_image_is_not_made()
	{
		let mut list = sample();

		assert!(list.views().is_empty());
	}
}
